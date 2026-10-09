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
#include <rock_hero/common/core/chart/chart_rules.h>
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

// The meter chord authors at the cursor and never reads the selection: the measure the cursor is
// IN, read at the tick, since a meter only ever starts on a downbeat. What it does there is the one
// rule timeSignatureTargetAt states; a meter starting there is restated, which is what makes the
// double press work: the first inserts and selects, and the commit keeps the cursor on its time, so
// the second finds that meter on the same downbeat.
TimeSignatureChordTarget EditorController::Impl::timeSignatureChordTarget() const
{
    const std::optional<common::core::GridPosition> position =
        cursorPosition(common::core::g_tick_quantum_note_value);
    if (!position.has_value())
    {
        return {};
    }
    return timeSignatureTargetAt(session().song().tempo_map, position->measure);
}

// A selection naming a time signature that is gone answers nothing.
std::optional<SetTimeSignatureTarget> EditorController::Impl::selectedTimeSignatureTarget() const
{
    const std::optional<SelectedMarker> selected = selectedMarker();
    if (!selected.has_value() || selected->row != MarkerRow::TimeSignature)
    {
        return std::nullopt;
    }
    const std::optional<std::size_t> standing = selected->index;
    if (!standing.has_value())
    {
        return std::nullopt;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const common::core::TimeSignatureChange& change = tempo_map.timeSignatures()[*standing];
    return SetTimeSignatureTarget{
        .measure = tempo_map.gridPositionAtWholeNote(change.place).measure,
        .meter_text = common::core::timeSignatureText(change.meter),
    };
}

// The prompt hands back raw text, so the parse is the core's: text that is not one meter changes
// nothing and records nothing, exactly as a refused commit does. Whether the meter is one a song
// may hold is the tempo-map rules' question, asked at the commit.
void EditorController::Impl::onTimeSignatureSetRequested(const int measure, const std::string& text)
{
    const std::optional<common::core::TimeSignature> meter = parseTimeSignatureText(text);
    if (!meter.has_value())
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::SetTimeSignature), "not a time signature: " + text);
        return;
    }
    runAction(EditorAction::SetTimeSignature{.measure = measure, .meter = *meter});
}

// Puts a meter on a measure's downbeat: the time signature standing there is restated, else one is
// inserted. Either way every other meter keeps its place in time, so the list is stated on the
// whole-note axis and the measures after the change renumber. Like every insert and restate, it
// leaves the time signature SELECTED, so Enter, Delete and Alt+arrows act on it next.
void EditorController::Impl::performActionImpl(const EditorAction::SetTimeSignature& action)
{
    const common::core::GridPosition downbeat = common::core::measureDownbeat(action.measure);
    const std::optional<std::size_t> standing = markerIndex(MarkerRow::TimeSignature, downbeat);
    const std::string label = markerVerbLabel(
        standing.has_value() ? MarkerVerb::SetTimeSignature : MarkerVerb::AddTimeSignature);
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    if (standing.has_value())
    {
        meters[*standing].meter = action.meter;
    }
    else
    {
        if (!common::core::markerCanStartAt(downbeat, tempo_map))
        {
            m_refusal_log.markerEdit(
                label, "a time signature cannot start at or past the end of the song");
            return;
        }
        const common::core::Fraction place = tempo_map.wholeNotePositionAt(downbeat);
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
    commitGridModel(
        std::move(before), std::move(after), label, MarkerRow::TimeSignature, downbeat, keptPlace);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Deletes the selected time signature (the Delete-key dispatch for the meter alternative): the
// meter before it governs its measures from then on, every object keeping its time. The measure-1
// meter is refused; every song keeps one.
void EditorController::Impl::deleteSelectedTimeSignature(const TimeSignatureSelection& selection)
{
    if (selection.measure == 1)
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::DeleteTimeSignature),
            "the time signature at measure 1 cannot be deleted");
        return;
    }
    const std::optional<std::size_t> standing =
        markerIndex(MarkerRow::TimeSignature, common::core::measureDownbeat(selection.measure));
    if (!standing.has_value())
    {
        // The selection went stale; deleting nothing is the answer.
        return;
    }
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    meters.erase(std::next(meters.begin(), static_cast<std::ptrdiff_t>(*standing)));
    SongTimelineSnapshot after{.timeline = readdressUnderMeters(before.timeline, meters)};
    commitGridModel(
        std::move(before),
        std::move(after),
        markerVerbLabel(MarkerVerb::DeleteTimeSignature),
        MarkerRow::TimeSignature,
        std::nullopt,
        keptPlace);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Moves the selected time signature one measure (65-Q2): its measure number steps by one in the new
// list. Measures before it are not its own, so measure m-1 or m+1 in the result is one measure of
// the meter BEFORE it from its old downbeat, which is where it is placed on the whole-note axis.
// The measure-1 meter, a step onto or past a neighbouring meter, and a step to or past the end of
// the song are refused. A held run is ONE undo entry; every press keeps every place exactly, so a
// step back is the step's exact inverse and a run walked back lands where it began.
void EditorController::Impl::moveSelectedTimeSignature(
    const TimeSignatureSelection& selection, const ChartStepDirection direction)
{
    if (selection.measure == 1)
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::MoveTimeSignature),
            "the time signature at measure 1 cannot move");
        return;
    }
    const std::optional<std::size_t> standing =
        markerIndex(MarkerRow::TimeSignature, common::core::measureDownbeat(selection.measure));
    if (!standing.has_value())
    {
        // The selection went stale; moving nothing is the answer.
        return;
    }
    const std::size_t index = *standing;
    const bool later = direction == ChartStepDirection::Right;
    const int target = selection.measure + (later ? 1 : -1);

    // Placed on the whole-note axis, where every other meter and the terminal keep their place, so
    // the neighbour and end refusals are plain comparisons there.
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    std::vector<common::core::TimeSignatureChange> meters = tempo_map.timeSignatures();
    const common::core::Fraction step = meters[index - 1].meter.measureWholeNotes();
    const common::core::Fraction place =
        later ? meters[index].place + step : meters[index].place - step;
    if (place <= meters[index - 1].place ||
        (index + 1 < meters.size() && place >= meters[index + 1].place))
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::MoveTimeSignature),
            "a time signature cannot step onto or past its neighbour");
        return;
    }
    if (place >= tempo_map.wholeNotePositionAt(common::core::terminalGridPosition(tempo_map)))
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::MoveTimeSignature),
            "a time signature cannot step to or past the end of the song");
        return;
    }
    meters[index].place = place;

    SongTimelineSnapshot moved{.timeline = readdressUnderMeters(before.timeline, meters)};
    if (commitMarkerMove(
            std::move(before),
            std::move(moved),
            markerVerbLabel(MarkerVerb::MoveTimeSignature),
            TimeSignatureSelection{.measure = target}))
    {
        followMovedMarker(common::core::measureDownbeat(target));
    }
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

} // namespace rock_hero::editor::core
