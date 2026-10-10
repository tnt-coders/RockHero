#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "shared/ratio_text.h"
#include "shared/refusal_log.h"
#include "timeline/meter_edits.h"
#include "timeline/song_timeline_snapshot.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/time_signature_text.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The prompt's text read back as a meter: two positive whole numbers around one slash, kept as
// typed. Whether the meter is one a song may hold (a power-of-two denominator the tick lattice
// carries) is the tempo-map rules' question, asked when the edit commits.
[[nodiscard]] std::optional<common::core::TimeSignature> parseTimeSignatureText(
    const std::string_view text)
{
    const std::optional<TypedRatio> ratio = parseTypedRatio(text);
    if (!ratio.has_value())
    {
        return std::nullopt;
    }
    return common::core::TimeSignature{
        .numerator = ratio->numerator, .denominator = ratio->denominator
    };
}

// The stored change starting a reign, found by the reign's first measure: its index in the
// authored list, or nothing where the reign is the derived short bar, which stores no change.
[[nodiscard]] std::optional<std::size_t> storedChangeIndex(
    const common::core::TempoMap& tempo_map, const int first_measure)
{
    const std::vector<common::core::TimeSignatureChange>& meters = tempo_map.timeSignatures();
    const auto stored = std::ranges::find(
        meters,
        tempo_map.wholeNotePositionAt(common::core::measureDownbeat(first_measure)),
        &common::core::TimeSignatureChange::place);
    if (stored == meters.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(meters.begin(), stored));
}

// Shows which measure numbers a meter verb changed (decision 5): the ruler re-derives them from the
// map, and the flash lights them from the first measure whose downbeat moved. Asked after every
// meter commit, landed or not: a refused or unchanged commit leaves the map as it was, and a
// restate that keeps every measure's length moves no downbeat, so neither flashes.
void flashRenumberedMeasures(
    IEditorView* const view, const common::core::TempoMap& before,
    const common::core::TempoMap& after)
{
    if (view == nullptr)
    {
        return;
    }
    if (const std::optional<int> first = firstRenumberedMeasure(before, after); first.has_value())
    {
        view->flashRenumberedMeasures(*first);
    }
}

} // namespace

// The meter chord authors at the cursor and never reads the selection: the BEAT the cursor is in,
// read at the tick and floored to the beat, since a meter starts a bar and a bar is whole beats —
// so the chord never leaves the meter lattice, and the bar it cuts is always a meter. What it does
// there is the one rule timeSignatureTargetAt states; a reign starting there is stated, which is
// what makes the double press work: the first inserts and selects, and the commit keeps the cursor
// on its time, so the second finds that reign starting on the same beat.
TimeSignatureChordTarget EditorController::Impl::timeSignatureChordTarget() const
{
    const std::optional<common::core::GridPosition> position =
        cursorPosition(common::core::g_tick_quantum_note_value);
    if (!position.has_value())
    {
        return {};
    }
    return timeSignatureTargetAt(session().song().tempo_map, common::core::beatStart(*position));
}

// The selected reign's meter, authored or derived; a selection naming a reign that is gone answers
// nothing.
std::optional<SetTimeSignatureTarget> EditorController::Impl::selectedTimeSignatureTarget() const
{
    const auto* const selection = std::get_if<TimeSignatureSelection>(&m_selection);
    if (selection == nullptr)
    {
        return std::nullopt;
    }
    const std::vector<common::core::MeterReign> reigns = session().song().tempo_map.reigns();
    const auto reign =
        std::ranges::find(reigns, selection->measure, &common::core::MeterReign::first_measure);
    if (reign == reigns.end())
    {
        return std::nullopt;
    }
    return SetTimeSignatureTarget{
        .position = common::core::measureDownbeat(reign->first_measure),
        .meter_text = common::core::timeSignatureText(reign->meter),
    };
}

// THE KEYFRAME COMMIT LAW's meter row. A time signature restating its reign changes no measure, so
// the history and the file never hold it (writtenModel); it is authoring state the editor holds
// while its chip is selected — which is what lets Alt+arrows walk a repeat across its
// predecessor's downbeats without the chip vanishing under the key, and the meter chord show a
// restated meter as a chip to retype — and no longer. Every such change but the focused one goes
// here with NO history entry, because no entry ever held it. Dropping one joins two reigns of one
// meter at a barline, so no place moves and nothing is re-addressed.
bool EditorController::Impl::dissolveRedundantMeters(const TimeSignatureSelection* const focused)
{
    if (m_undo_history.hasPendingTransition())
    {
        return false;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::optional<common::core::Fraction> focused_place =
        focused == nullptr ? std::nullopt
                           : std::optional{tempo_map.wholeNotePositionAt(
                                 common::core::measureDownbeat(focused->measure))};
    const std::vector<common::core::MeterReign> reigns = tempo_map.reigns();
    std::vector<common::core::TimeSignatureChange> settled;
    std::ranges::remove_copy_if(
        tempo_map.timeSignatures(),
        std::back_inserter(settled),
        [&reigns, &focused_place](const common::core::TimeSignatureChange& change) {
            return change.place != focused_place &&
                   common::core::isRedundantTimeSignature(reigns, change);
        });
    if (settled.size() == tempo_map.timeSignatures().size())
    {
        return false;
    }
    m_session.setTempoMap(common::core::TempoMap{std::move(settled), tempo_map.anchors()});
    syncTempoDerivedAudio();
    updateView();
    return true;
}

// The prompt hands back raw text, so the parse is the core's: text that is not one meter changes
// nothing and records nothing, exactly as a refused commit does. Whether the meter is one a song
// may hold is the tempo-map rules' question, asked at the commit.
void EditorController::Impl::onTimeSignatureSetRequested(
    const common::core::GridPosition position, const std::string& text)
{
    const std::optional<common::core::TimeSignature> meter = parseTimeSignatureText(text);
    if (!meter.has_value())
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::SetTimeSignature), "not a time signature: " + text);
        return;
    }
    runAction(EditorAction::SetTimeSignature{.position = position, .meter = *meter});
}

// Puts a meter at a position: the change stored there is restated; otherwise one is inserted at
// its place — which AUTHORS the derived short bar where one starts there, and starts a new reign
// at a beat where none does, cutting the measure short. Every other meter keeps its place in time,
// so the list is stated on the whole-note axis and the measures after the change renumber. Like
// every insert and restate, it leaves the reign SELECTED under the landed map, so Enter, Delete
// and Alt+arrows act on it next.
void EditorController::Impl::performActionImpl(const EditorAction::SetTimeSignature& action)
{
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    // The one meter rule names the verb: stating a reign that starts here, authored or derived,
    // is the Set; a beat where none starts is the Add; nowhere a marker may start is a refusal.
    const TimeSignatureChordTarget verb = timeSignatureTargetAt(tempo_map, action.position);
    if (std::holds_alternative<std::monostate>(verb))
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::AddTimeSignature),
            "a time signature cannot start at or past the end of the song");
        return;
    }
    const std::string label = markerVerbLabel(
        std::holds_alternative<SetTimeSignatureTarget>(verb) ? MarkerVerb::SetTimeSignature
                                                             : MarkerVerb::AddTimeSignature);
    const common::core::Fraction place = tempo_map.wholeNotePositionAt(action.position);
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    if (const auto stored =
            std::ranges::find(meters, place, &common::core::TimeSignatureChange::place);
        stored != meters.end())
    {
        stored->meter = action.meter;
    }
    else
    {
        meters.insert(
            std::ranges::find_if(
                meters,
                [&place](const common::core::TimeSignatureChange& placed) {
                    return placed.place >= place;
                }),
            common::core::TimeSignatureChange{.place = place, .meter = action.meter});
    }
    // The re-address refuses nothing; a reign left shorter than one of its measures is the
    // tempo-map rules' refusal, asked at the commit.
    SongTimelineSnapshot after{.timeline = readdressUnderMeters(before.timeline, meters)};
    // Where the meter stands under the landed map: a meter put mid-measure opens the bar AFTER the
    // one it cut, so its downbeat is not the beat it was typed at.
    const common::core::GridPosition landed =
        after.timeline.tempo_map.gridPositionAtWholeNote(place);
    commitGridModel(
        std::move(before),
        std::move(after),
        label,
        MarkerRow::TimeSignature,
        landed,
        keptPlace,
        EntryFocus::FirstChange);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Rationale lives on the declaration in editor_controller_impl.h.
void EditorController::Impl::refuseTimeSignatureVerb(
    const TimeSignatureSelection& selection, const MarkerVerb verb, const std::string& reason)
{
    m_refusal_log.markerEdit(markerVerbLabel(verb), reason);
    if (m_view != nullptr)
    {
        m_view->flashTimeSignatureRefusal(selection.measure);
    }
}

// Deletes the selected time signature (the Delete-key dispatch for the meter alternative): the
// meter before it governs its measures from then on, every object keeping its time. Refused on the
// measure-1 meter, which every song keeps, and on the derived short bar, which stores nothing to
// delete.
void EditorController::Impl::deleteSelectedTimeSignature(const TimeSignatureSelection& selection)
{
    if (selection.measure == 1)
    {
        refuseTimeSignatureVerb(
            selection,
            MarkerVerb::DeleteTimeSignature,
            "the time signature at measure 1 cannot be deleted");
        return;
    }
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    const std::optional<std::size_t> stored = storedChangeIndex(tempo_map, selection.measure);
    if (!stored.has_value())
    {
        refuseTimeSignatureVerb(
            selection,
            MarkerVerb::DeleteTimeSignature,
            "the measure " + std::to_string(selection.measure) +
                " meter is derived, so nothing is stored there");
        return;
    }
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    meters.erase(std::next(meters.begin(), static_cast<std::ptrdiff_t>(*stored)));
    SongTimelineSnapshot after{.timeline = readdressUnderMeters(before.timeline, meters)};
    commitGridModel(
        std::move(before),
        std::move(after),
        markerVerbLabel(MarkerVerb::DeleteTimeSignature),
        MarkerRow::TimeSignature,
        std::nullopt,
        keptPlace,
        EntryFocus::FirstChange);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Moves the selected time signature one BEAT of the meter before it on the whole-note axis, where
// every other meter and the terminal keep their place: the bar it leaves or enters is cut short
// and becomes the derived short bar. Refused, with the chip glowing: on the measure-1 meter, on
// the derived short bar (nothing is stored to move), onto or past a neighbouring meter, to or past
// the end of the song, and — by the tempo-map rules at the commit — where a reign would be left
// shorter than one of its measures (ReignTooShort). A held run is ONE undo entry; every press
// keeps every place exactly, so a step back is the step's exact inverse and a run walked back
// lands where it began.
void EditorController::Impl::moveSelectedTimeSignature(
    const TimeSignatureSelection& selection, const ChartStepDirection direction)
{
    const MarkerVerb verb = MarkerVerb::MoveTimeSignature;
    if (selection.measure == 1)
    {
        refuseTimeSignatureVerb(selection, verb, "the time signature at measure 1 cannot move");
        return;
    }
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    const std::optional<std::size_t> stored = storedChangeIndex(tempo_map, selection.measure);
    if (!stored.has_value())
    {
        refuseTimeSignatureVerb(
            selection,
            verb,
            "the measure " + std::to_string(selection.measure) +
                " meter is derived, so nothing is stored there");
        return;
    }
    const std::size_t index = *stored;
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    const common::core::Fraction step{1, meters[index - 1].meter.denominator};
    const common::core::Fraction place = direction == ChartStepDirection::Right
                                             ? meters[index].place + step
                                             : meters[index].place - step;
    // The re-address needs the list in order, so the neighbour check stands here, ahead of it.
    if (place <= meters[index - 1].place ||
        (index + 1 < meters.size() && place >= meters[index + 1].place))
    {
        refuseTimeSignatureVerb(
            selection, verb, "a time signature cannot step onto or past its neighbour");
        return;
    }
    if (place >= tempo_map.wholeNotePositionAt(common::core::terminalGridPosition(tempo_map)))
    {
        refuseTimeSignatureVerb(
            selection, verb, "a time signature cannot step to or past the end of the song");
        return;
    }
    meters[index].place = place;

    SongTimelineSnapshot moved{.timeline = readdressUnderMeters(before.timeline, meters)};
    // The reign the moved meter opens under the new grid: the first full bar after the cut.
    const int landed = moved.timeline.tempo_map.gridPositionAtWholeNote(place).measure;
    if (!commitMarkerMove(
            std::move(before),
            std::move(moved),
            markerVerbLabel(verb),
            TimeSignatureSelection{.measure = landed}))
    {
        if (m_view != nullptr)
        {
            m_view->flashTimeSignatureRefusal(selection.measure);
        }
        return;
    }
    followMovedMarker(common::core::measureDownbeat(landed));
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

} // namespace rock_hero::editor::core
