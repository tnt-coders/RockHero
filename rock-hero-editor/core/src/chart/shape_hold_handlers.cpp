#include "chart/chart_edits.h"
#include "chart/chart_selection.h"
#include "controller/editor_controller_impl.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

// The verbs on a span or a none hold. Ctrl+H asks what the selection's extent becomes and the hold
// picker's answer writes it; Delete writes the choice one level below a selected span or none hold
// (deleteSelectedMarker); the edge keys move a held span's or a none hold's edges, or convert a
// derived span. Every write carves into the chart's holds (carvedShapeHolds, which merges the none
// holds that meet) and ends at the hold gate (finalizeShapeHoldPlan), so a result the normalizer
// would rewrite is refused.

namespace
{

// The slots of the notes struck inside [start, end): what a refused hold verb flashes.
[[nodiscard]] std::vector<ChartSlotKey> notesStruckWithin(
    const std::vector<common::core::ChartNote>& notes, const common::core::GridPosition& start,
    const common::core::GridPosition& end)
{
    std::vector<ChartSlotKey> struck;
    for (auto note = std::ranges::lower_bound(
             notes, start, std::ranges::less{}, &common::core::ChartNote::position);
         note != notes.end() && note->position < end;
         ++note)
    {
        struck.push_back(chartSlotKeyOf(*note));
    }
    return struck;
}

// The selection naming the hold of `holds` standing at `place`: a none hold as itself, an unnamed
// hold as the span it states, which starts where it does, and with no hold there the span the
// notes derive starting at `place`. A verb lands on what its carve left there, so a none hold
// merged into a neighbour is selected whole.
[[nodiscard]] MarkerSelection selectionOfHoldAt(
    const std::vector<common::core::ShapeHold>& holds, const common::core::GridPosition& place)
{
    const auto standing =
        std::ranges::find_if(holds, [&place](const common::core::ShapeHold& hold) {
            return !(place < hold.start) && place < hold.end;
        });
    if (standing != holds.end() && std::holds_alternative<common::core::NoneGrip>(standing->grip))
    {
        return NoneHoldSelection{.start = standing->start};
    }
    return SpanSelection{.start = standing != holds.end() ? standing->start : place};
}

// One answer to what an extent becomes: the grip it states, the verb it is written under (its undo
// label and its refusals' log label), and the words the hold picker shows for it.
struct HoldChoiceRow
{
    std::optional<common::core::HoldGrip> grip;
    MarkerVerb verb{};
    std::string_view label;
    std::string_view tooltip;
};

// THE closed set of what an extent can become, in the order the picker shows it: a shape, none,
// and no hold at all. The picker's rows and the verb a choice is written under both read it.
[[nodiscard]] std::array<HoldChoiceRow, 3> holdChoiceRows()
{
    return {{
        {
            .grip = common::core::UnnamedGrip{},
            .verb = MarkerVerb::HoldShape,
            .label = "Shape",
            .tooltip = "Hold one shape over the whole range",
        },
        {
            .grip = common::core::NoneGrip{},
            .verb = MarkerVerb::AddNoneHold,
            .label = "None",
            .tooltip = "Hide arpeggios here; chords still stand",
        },
        {
            .grip = std::nullopt,
            .verb = MarkerVerb::ClearHolds,
            .label = "Auto",
            .tooltip = "Clear holds here and let the notes decide the spans",
        },
    }};
}

// The verb the choice stating `grip` is written under. Every grip is one of the rows, so the
// search always finds it.
[[nodiscard]] MarkerVerb holdChoiceVerb(const std::optional<common::core::HoldGrip>& grip)
{
    const std::array<HoldChoiceRow, 3> rows = holdChoiceRows();
    return std::ranges::find(rows, grip, &HoldChoiceRow::grip)->verb;
}

} // namespace

// Read through the row the selection names: a span's index is its index in the notes layer, a none
// hold's its index among the hatched none holds, which carry its stated extent.
std::optional<common::core::ShapeHold> EditorController::Impl::selectedSpanRowHold() const
{
    const std::optional<SelectedMarker> marker = selectedMarker();
    if (!marker.has_value() || !marker->index.has_value())
    {
        return std::nullopt;
    }
    const std::size_t index = *marker->index;
    if (marker->row == MarkerRow::Span)
    {
        return shapeHoldOverSpan(
            chartResolutions().shapes.at(index),
            session().song().tempo_map,
            common::core::UnnamedGrip{});
    }
    if (marker->row == MarkerRow::NoneHold)
    {
        const common::core::NoneHoldViewState& hold = noneHoldMarks().at(index);
        return common::core::ShapeHold{
            .start = hold.start, .end = hold.end, .grip = common::core::NoneGrip{}
        };
    }
    return std::nullopt;
}

// Of the marks under the press, the one the walk up from the strings meets first (g_span_rows), so
// a press over a chord inside a none hold selects the hold. Seeks nothing, as a chip press does.
void EditorController::Impl::performActionImpl(const EditorAction::SelectSpanRowMark& action)
{
    for (const MarkerRow row : std::views::reverse(g_span_rows))
    {
        const std::optional<std::size_t>& index =
            row == MarkerRow::NoneHold ? action.marks.none_hold : action.marks.span;
        if (index.has_value())
        {
            selectMarkerAt(row, *index);
            return;
        }
    }
}

// One extent per selection kind, so the precedence is the selection's own: exactly one exists.
std::optional<ShapeHoldExtent> EditorController::Impl::selectedHoldExtent() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    if (const std::optional<common::core::ShapeHold> held = selectedSpanRowHold(); held.has_value())
    {
        return ShapeHoldExtent{.start = held->start, .end = held->end};
    }
    if (const TimeSelection* const range = selectedTimeSelection(); range != nullptr)
    {
        return ShapeHoldExtent{.start = range->start(), .end = range->end()};
    }
    // A selected keyframe names its note's ring, so the extent covers that note.
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const std::vector<std::size_t> touched = slotIndicesForKeys(
        notes, notesTouchedBy(chartSelection().notes(), chartSelection().keyframes()));
    if (touched.empty())
    {
        return std::nullopt;
    }
    // Slot order puts the earliest onset first; the extent runs to the latest of the rings' ends.
    const auto ring_end = [&notes, &tempo_map](const std::size_t index) {
        return common::core::advanceGridPositionByWholeNotes(
            tempo_map, notes[index].position, notes[index].sustain);
    };
    return ShapeHoldExtent{
        .start = notes[touched.front()].position,
        .end = std::ranges::max(touched | std::views::transform(ring_end)),
    };
}

void EditorController::Impl::refuseShapeHoldVerb(
    const MarkerVerb verb, const std::string_view reason, const ShapeHoldExtent& target)
{
    m_refusal_log.markerEdit(markerVerbLabel(verb), reason);
    if (const common::core::Arrangement* const arrangement = session().currentArrangement();
        arrangement != nullptr && arrangement->chart.has_value())
    {
        flashChartElements(
            notesStruckWithin(arrangement->chart->notes, target.start, target.end), {});
    }
}

// The choice lands on the hold the carve leaves at the extent's start, merged or not: a none hold
// as itself, an unnamed hold as the span it states, and where Auto left none, the span the notes
// derive there, which selects nothing where they derive none (landMarkerSelection).
void EditorController::Impl::commitHoldChoice(
    const MarkerVerb verb, const ShapeHoldExtent& extent,
    const std::optional<common::core::HoldGrip>& grip)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const std::vector<common::core::ShapeHold> holds =
        carvedShapeHolds(chart.shape_holds, extent, grip);
    std::expected<JudgedChartPlan, ChartPlanRefusal> plan =
        finalizeShapeHoldPlan(chart, session().song().tempo_map, holds, markerVerbLabel(verb));
    if (!plan.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&plan.error()))
        {
            refuseShapeHoldVerb(verb, invalid->reason, extent);
        }
        return;
    }
    static_cast<void>(applyChartEditPlan(std::move(*plan), selectionOfHoldAt(holds, extent.start)));
}

// Ctrl+H: what the selection's extent becomes. Every choice is planned as the carve it writes, and
// one that leaves the holds as they stand changes nothing, so it is not offered; counting the
// choices any other way would be a second model of the same question. At most one of the three
// can change nothing — the extent already one unnamed hold, already inside a none hold, or touched
// by no hold — so the picker always has at least two rows and always asks. A bare caret covers
// nothing and is refused.
void EditorController::Impl::performActionImpl(EditorAction::ChooseHold /*action*/)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const std::optional<ShapeHoldExtent> extent = selectedHoldExtent();
    if (!extent.has_value())
    {
        m_refusal_log.markerEdit(
            markerVerbLabel(MarkerVerb::ChooseHold), "nothing is selected to hold");
        return;
    }
    // Bound above the loop, which the optional-access check cannot see through.
    const ShapeHoldExtent& range = *extent;
    const std::vector<common::core::ShapeHold>& standing = arrangement->chart->shape_holds;
    ChartHoldPicker picker;
    for (const HoldChoiceRow& row : holdChoiceRows())
    {
        if (carvedShapeHolds(standing, range, row.grip) != standing)
        {
            picker.choices.push_back(
                ChartHoldChoice{
                    .grip = row.grip,
                    .label = std::string{row.label},
                    .tooltip = std::string{row.tooltip},
                });
        }
    }
    requestChartHoldPicker(std::move(picker));
}

// The row chosen in the hold picker, applied at once over the extent the selection still covers.
void EditorController::Impl::performActionImpl(const EditorAction::SetHold& action)
{
    if (const std::optional<ShapeHoldExtent> extent = selectedHoldExtent(); extent.has_value())
    {
        commitHoldChoice(holdChoiceVerb(action.grip), *extent, action.grip);
    }
}

// A press continues the live run of the same edge, whose start state the gesture authority replays
// from; any other press starts one from the selection as it stands. The edge steps from where the
// run has moved it, by the notes, never by the grid, and the hold stays selected under its start.
void EditorController::Impl::stepShapeHoldEdge(const ShapeHoldEdge edge, const bool later)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const MarkerVerb verb =
        edge == ShapeHoldEdge::Start ? MarkerVerb::MoveHoldStart : MarkerVerb::MoveHoldEnd;

    // Copied out of the window, which the commit below reassigns.
    const ChartVerbWindowVerb* const live_verb = liveChartGestureVerb();
    const auto* const live =
        live_verb != nullptr ? std::get_if<ChartShapeHoldEdgeGesture>(live_verb) : nullptr;
    const bool continues = live != nullptr && live->edge == edge;
    std::optional<ChartShapeHoldEdgeGesture> run;
    if (continues)
    {
        run = *live;
    }
    else if (
        const std::optional<common::core::ShapeHold> held = selectedSpanRowHold(); held.has_value()
    )
    {
        run = ChartShapeHoldEdgeGesture{
            .edge = edge,
            .base = *held,
            .value = edge == ShapeHoldEdge::Start ? held->start : held->end,
        };
    }
    if (!run.has_value())
    {
        return;
    }
    ChartShapeHoldEdgeGesture gesture = *run;
    const std::optional<common::core::GridPosition> stepped =
        steppedShapeHoldEdge(chart.notes, tempo_map, edge, gesture.value, later);
    if (!stepped.has_value())
    {
        refuseShapeHoldVerb(
            verb,
            edge == ShapeHoldEdge::Start ? "no note onset to step the start to"
                                         : "no note boundary to step the end to",
            ShapeHoldExtent{.start = gesture.base.start, .end = gesture.base.end});
        return;
    }
    gesture.value = *stepped;
    const common::core::GridPosition landed =
        edge == ShapeHoldEdge::Start ? gesture.value : gesture.base.start;
    const std::string label = markerVerbLabel(verb);
    const std::expected<bool, ChartPlanInvalid> moved = commitChartGestureStep(
        continues,
        [&tempo_map, &gesture, &label](const common::core::Chart& pre_gesture) {
            return planMoveShapeHoldEdge(
                pre_gesture, tempo_map, gesture.base, gesture.edge, gesture.value, label);
        },
        gesture,
        // The hold the run leaves standing at its moved start, read off the same holds the plan
        // judges, so a none hold the run merged into a neighbour lands selected whole.
        [&landed, &gesture](const common::core::Chart& pre_gesture) -> ChartEditLanding {
            const std::expected<std::vector<common::core::ShapeHold>, ChartPlanRefusal> holds =
                movedShapeHolds(pre_gesture.shape_holds, gesture.base, gesture.edge, gesture.value);
            return selectionOfHoldAt(holds.value_or(pre_gesture.shape_holds), landed);
        });
    if (!moved.has_value())
    {
        refuseShapeHoldVerb(
            verb,
            moved.error().reason,
            ShapeHoldExtent{.start = gesture.base.start, .end = gesture.base.end});
    }
}

} // namespace rock_hero::editor::core
