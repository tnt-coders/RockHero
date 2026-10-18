#include "chart/chart_edits.h"
#include "chart/chart_selection.h"
#include "controller/editor_controller_impl.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
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

// The span rows' verbs: Ctrl+H states a hold over the selection, the edge keys move a hold's or a
// derived span's edges, and Delete removes a hold or denies a derived arpeggio with a none hold
// over it. Every verb carves the hold it authors into the chart's holds (carvedShapeHolds) and ends
// at the hold gate (finalizeShapeHoldPlan), so a result the normalizer would rewrite is refused.

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

} // namespace

const common::core::ShapeHold* EditorController::Impl::selectedShapeHold() const
{
    const auto* const selected = std::get_if<ShapeHoldSelection>(&m_selection);
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (selected == nullptr || arrangement == nullptr || !arrangement->chart.has_value())
    {
        return nullptr;
    }
    const std::vector<common::core::ShapeHold>& holds = arrangement->chart->shape_holds;
    const auto found = std::ranges::find_if(holds, [selected](const common::core::ShapeHold& hold) {
        return hold.start == selected->start;
    });
    return found == holds.end() ? nullptr : &*found;
}

// Found by the selection's start among the derived-span row's markers, whose index names the span.
std::optional<std::size_t> EditorController::Impl::selectedDerivedSpanShape() const
{
    const auto* const selected = std::get_if<DerivedSpanSelection>(&m_selection);
    if (selected == nullptr)
    {
        return std::nullopt;
    }
    const std::optional<std::size_t> index = markerIndex(MarkerRow::DerivedSpan, selected->start);
    if (!index.has_value())
    {
        return std::nullopt;
    }
    return derivedSpanShapes().at(*index);
}

std::optional<common::core::ShapeHold> EditorController::Impl::selectedSpanRowHold() const
{
    if (const common::core::ShapeHold* const hold = selectedShapeHold(); hold != nullptr)
    {
        return *hold;
    }
    if (const std::optional<std::size_t> shape = selectedDerivedSpanShape(); shape.has_value())
    {
        return shapeHoldOverSpan(
            chartResolutions().shapes.at(*shape),
            session().song().tempo_map,
            common::core::UnnamedGrip{});
    }
    return std::nullopt;
}

// One target per selection kind, so the precedence is the selection's own: exactly one exists.
std::optional<common::core::ShapeHold> EditorController::Impl::holdShapeTarget() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const auto unnamed = [](const common::core::GridPosition& start,
                            const common::core::GridPosition& end) {
        return common::core::ShapeHold{
            .start = start, .end = end, .grip = common::core::UnnamedGrip{}
        };
    };
    if (const std::optional<common::core::ShapeHold> held = selectedSpanRowHold(); held.has_value())
    {
        return unnamed(held->start, held->end);
    }
    if (const TimeSelection* const range = selectedTimeSelection(); range != nullptr)
    {
        return unnamed(range->start(), range->end());
    }
    // A selected keyframe names its note's ring, so the hold covers that note.
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const std::vector<std::size_t> touched = slotIndicesForKeys(
        notes, notesTouchedBy(chartSelection().notes(), chartSelection().keyframes()));
    if (touched.empty())
    {
        return std::nullopt;
    }
    // Slot order puts the earliest onset first; the last ring's end is the latest of them all.
    common::core::GridPosition end = notes[touched.front()].position;
    for (const std::size_t index : touched)
    {
        end = std::max(
            end,
            common::core::advanceGridPositionByWholeNotes(
                tempo_map, notes[index].position, notes[index].sustain));
    }
    return unnamed(notes[touched.front()].position, end);
}

void EditorController::Impl::refuseShapeHoldVerb(
    const MarkerVerb verb, const std::string_view reason, const common::core::ShapeHold& target)
{
    m_refusal_log.markerEdit(markerVerbLabel(verb), reason);
    if (const common::core::Arrangement* const arrangement = session().currentArrangement();
        arrangement != nullptr && arrangement->chart.has_value())
    {
        flashChartElements(
            notesStruckWithin(arrangement->chart->notes, target.start, target.end), {});
    }
}

// Ctrl+H: the unnamed hold over what the selection covers, carved into the chart's holds and
// judged by the hold gate, left selected. A bare caret covers nothing and is refused; a hold that
// is already exactly there changes nothing.
void EditorController::Impl::performActionImpl(EditorAction::HoldShape /*action*/)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const std::string label = markerVerbLabel(MarkerVerb::HoldShape);
    const std::optional<common::core::ShapeHold> target = holdShapeTarget();
    if (!target.has_value())
    {
        m_refusal_log.markerEdit(label, "nothing is selected to hold a shape over");
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    std::expected<JudgedChartPlan, ChartPlanRefusal> plan = finalizeShapeHoldPlan(
        chart, session().song().tempo_map, carvedShapeHolds(chart.shape_holds, *target), label);
    if (!plan.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&plan.error()))
        {
            refuseShapeHoldVerb(MarkerVerb::HoldShape, invalid->reason, *target);
        }
        return;
    }
    static_cast<void>(applyChartEditPlan(
        std::move(*plan), ChartEditLanding{ShapeHoldSelection{.start = target->start}}));
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
        // A hold and a span are both named by their start, which is the held hold's.
        const MarkerSelection origin =
            std::holds_alternative<ShapeHoldSelection>(m_selection)
                ? MarkerSelection{ShapeHoldSelection{.start = held->start}}
                : MarkerSelection{DerivedSpanSelection{.start = held->start}};
        run = ChartShapeHoldEdgeGesture{
            .edge = edge,
            .base = *held,
            .origin = origin,
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
            gesture.base);
        return;
    }
    gesture.value = *stepped;
    // A run standing where it began is no edit (planMoveShapeHoldEdge) and lands on what it began
    // from; any other lands on the hold it moved, under that hold's start.
    const bool home =
        gesture.value == (edge == ShapeHoldEdge::Start ? gesture.base.start : gesture.base.end);
    const MarkerSelection landed =
        home ? gesture.origin
             : MarkerSelection{ShapeHoldSelection{
                   .start = edge == ShapeHoldEdge::Start ? gesture.value : gesture.base.start
               }};
    const std::string label = markerVerbLabel(verb);
    const std::expected<bool, ChartPlanInvalid> moved = commitChartGestureStep(
        continues,
        [&tempo_map, &gesture, &label](const common::core::Chart& pre_gesture) {
            return planMoveShapeHoldEdge(
                pre_gesture, tempo_map, gesture.base, gesture.edge, gesture.value, label);
        },
        gesture,
        [&landed](const common::core::Chart& /*pre_gesture*/) -> ChartEditLanding {
            return landed;
        });
    if (!moved.has_value())
    {
        refuseShapeHoldVerb(verb, moved.error().reason, gesture.base);
    }
}

// The holds without it, judged like any hold verb's: removing a hold moves the walls its neighbours
// may have closed at. Nothing is left selected here; the delete's walk back along the row selects.
void EditorController::Impl::deleteSelectedShapeHold(const common::core::ShapeHold& hold)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    std::vector<common::core::ShapeHold> remaining = chart.shape_holds;
    std::erase_if(
        remaining, [&hold](const common::core::ShapeHold& standing) { return standing == hold; });
    std::expected<JudgedChartPlan, ChartPlanRefusal> plan = finalizeShapeHoldPlan(
        chart, session().song().tempo_map, remaining, markerVerbLabel(MarkerVerb::DeleteHold));
    if (!plan.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&plan.error()))
        {
            refuseShapeHoldVerb(MarkerVerb::DeleteHold, invalid->reason, hold);
        }
        return;
    }
    static_cast<void>(
        applyChartEditPlan(std::move(*plan), ChartEditLanding{std::vector<ChartSelectionKey>{}}));
}

// A none hold over a chord box would change nothing, since a none hold keeps the boxes struck
// inside it, so a box is refused rather than given a hold that states nothing anyone can see.
void EditorController::Impl::deleteSelectedDerivedSpan()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::optional<std::size_t> shape = selectedDerivedSpanShape();
    if (arrangement == nullptr || !arrangement->chart.has_value() || !shape.has_value())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::ChartResolutions& resolved = chartResolutions();
    const common::core::ShapeHold denial = shapeHoldOverSpan(
        resolved.shapes.at(*shape), session().song().tempo_map, common::core::NoneGrip{});
    if (!resolved.arrivals.at(*shape))
    {
        refuseShapeHoldVerb(
            MarkerVerb::DeleteArpeggio, "a chord box is no arpeggio to delete", denial);
        return;
    }
    std::expected<JudgedChartPlan, ChartPlanRefusal> plan = finalizeShapeHoldPlan(
        chart,
        session().song().tempo_map,
        carvedShapeHolds(chart.shape_holds, denial),
        markerVerbLabel(MarkerVerb::DeleteArpeggio));
    if (!plan.has_value())
    {
        if (const auto* const invalid = std::get_if<ChartPlanInvalid>(&plan.error()))
        {
            refuseShapeHoldVerb(MarkerVerb::DeleteArpeggio, invalid->reason, denial);
        }
        return;
    }
    static_cast<void>(applyChartEditPlan(
        std::move(*plan), ChartEditLanding{ShapeHoldSelection{.start = denial.start}}));
}

} // namespace rock_hero::editor::core
