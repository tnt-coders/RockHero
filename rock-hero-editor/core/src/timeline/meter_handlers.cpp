#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "shared/ratio_text.h"
#include "timeline/meter_edits.h"
#include "timeline/song_timeline_snapshot.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
#include <rock_hero/editor/core/timeline/time_signature_text.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The labels the four time-signature verbs record their undo entries under and log their refusals
// under, whichever gesture drives them.
constexpr std::string_view g_add_time_signature_label{"Add Time Signature"};
constexpr std::string_view g_set_time_signature_label{"Set Time Signature"};
constexpr std::string_view g_delete_time_signature_label{"Delete Time Signature"};
constexpr std::string_view g_move_time_signature_label{"Move Time Signature"};

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

// The timeline re-addressed under a new meter list, as the snapshot a verb commits, or nothing
// once the transform's refusal is logged under the verb's label.
[[nodiscard]] std::optional<SongTimelineSnapshot> readdressedSnapshot(
    const common::core::SongTimeline& timeline, const std::span<const PlacedMeter> meters,
    const std::string_view label)
{
    std::expected<common::core::SongTimeline, ReaddressError> readdressed =
        readdressUnderMeters(timeline, meters);
    if (!readdressed.has_value())
    {
        logMarkerRefusal(label, readdressed.error().message);
        return std::nullopt;
    }
    return SongTimelineSnapshot{.timeline = std::move(*readdressed)};
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
    const common::core::TimeSignatureChange& change =
        session().song().tempo_map.timeSignatures()[*standing];
    return SetTimeSignatureTarget{
        .measure = change.measure,
        .meter_text = timeSignatureText(change.meter),
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
        logMarkerRefusal(g_set_time_signature_label, "not a time signature: " + text);
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
    const std::string_view label =
        standing.has_value() ? g_set_time_signature_label : g_add_time_signature_label;
    SongTimelineSnapshot before = SongTimelineSnapshot::capture(session());
    const common::core::TempoMap tempo_map = before.timeline.tempo_map;
    std::vector<PlacedMeter> meters = placedMetersOf(tempo_map);
    if (standing.has_value())
    {
        meters[*standing].meter = action.meter;
    }
    else
    {
        if (!common::core::markerCanStartAt(downbeat, tempo_map))
        {
            logMarkerRefusal(label, "a time signature cannot start at or past the end of the song");
            return;
        }
        const common::core::Fraction place = tempo_map.wholeNotePositionAt(downbeat);
        meters.insert(
            std::ranges::find_if(
                meters, [&place](const PlacedMeter& placed) { return placed.whole_note >= place; }),
            PlacedMeter{.whole_note = place, .meter = action.meter});
    }
    std::optional<SongTimelineSnapshot> after = readdressedSnapshot(before.timeline, meters, label);
    if (!after.has_value())
    {
        return;
    }
    commitGridModel(
        std::move(before),
        std::move(*after),
        std::string{label},
        MarkerRow::TimeSignature,
        downbeat,
        keptPlace);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Deletes the selected time signature (the Delete-key dispatch for the meter alternative): the
// meter before it governs its measures from then on, every object keeping its time. The measure-1
// meter is refused; every song keeps one.
void EditorController::Impl::deleteSelectedTimeSignature(const TimeSignatureSelection& selection)
{
    if (selection.measure == 1)
    {
        logMarkerRefusal(
            g_delete_time_signature_label, "the time signature at measure 1 cannot be deleted");
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
    std::vector<PlacedMeter> meters = placedMetersOf(tempo_map);
    meters.erase(std::next(meters.begin(), static_cast<std::ptrdiff_t>(*standing)));
    std::optional<SongTimelineSnapshot> after =
        readdressedSnapshot(before.timeline, meters, g_delete_time_signature_label);
    if (!after.has_value())
    {
        return;
    }
    commitGridModel(
        std::move(before),
        std::move(*after),
        std::string{g_delete_time_signature_label},
        MarkerRow::TimeSignature,
        std::nullopt,
        keptPlace);
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

// Moves the selected time signature one measure (65-Q2): its measure number steps by one in the new
// list. Measures before it are not its own, so measure m-1 or m+1 in the result is one measure of
// the meter BEFORE it from its old downbeat, which is where it is placed on the whole-note axis.
// The measure-1 meter, a step onto or past a neighbouring meter, and a step to or past the end of
// the song are refused. A held run is ONE undo entry, each press re-addressed from the run's
// origin: a re-address that snapped a section or re-seated the terminal is not undone by the step
// back, so only the origin keeps a run that walks back exact.
void EditorController::Impl::moveSelectedTimeSignature(
    const TimeSignatureSelection& selection, const ChartStepDirection direction)
{
    if (selection.measure == 1)
    {
        logMarkerRefusal(
            g_move_time_signature_label, "the time signature at measure 1 cannot move");
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
    std::vector<PlacedMeter> meters = placedMetersOf(tempo_map);
    const common::core::Fraction step = meters[index - 1].meter.measureWholeNotes();
    const common::core::Fraction place =
        later ? meters[index].whole_note + step : meters[index].whole_note - step;
    if (place <= meters[index - 1].whole_note ||
        (index + 1 < meters.size() && place >= meters[index + 1].whole_note))
    {
        logMarkerRefusal(
            g_move_time_signature_label, "a time signature cannot step onto or past its neighbour");
        return;
    }
    // The run's origin is what the press re-addresses, so its terminal is the end the step must
    // stay before; a terminal the run has since re-seated later does not count.
    const common::core::SongTimeline& base = runBase(before).timeline;
    if (place >=
        base.tempo_map.wholeNotePositionAt(common::core::terminalGridPosition(base.tempo_map)))
    {
        logMarkerRefusal(
            g_move_time_signature_label,
            "a time signature cannot step to or past the end of the song");
        return;
    }
    meters[index].whole_note = place;

    std::optional<SongTimelineSnapshot> moved =
        readdressedSnapshot(base, meters, g_move_time_signature_label);
    if (!moved.has_value())
    {
        return;
    }
    if (commitMarkerMove(
            std::move(before),
            std::move(*moved),
            std::string{g_move_time_signature_label},
            TimeSignatureSelection{.measure = target}))
    {
        followMovedMarker(common::core::measureDownbeat(target));
    }
    flashRenumberedMeasures(m_view, tempo_map, session().song().tempo_map);
}

} // namespace rock_hero::editor::core
