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

// The span rows' verbs: Ctrl+H states a hold over the selection. Every verb carves the hold it
// authors into the chart's holds (carvedShapeHolds) and ends at the hold gate
// (finalizeShapeHoldPlan), so a result the normalizer would rewrite is refused.

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

// Found by the selection's start among the hand-span row's markers, whose index names the span.
std::optional<std::size_t> EditorController::Impl::selectedHandSpanShape() const
{
    const auto* const selected = std::get_if<HandSpanSelection>(&m_selection);
    if (selected == nullptr)
    {
        return std::nullopt;
    }
    const std::optional<std::size_t> index = markerIndex(MarkerRow::HandSpan, selected->start);
    if (!index.has_value())
    {
        return std::nullopt;
    }
    return derivedSpanShapes().at(*index);
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
    if (const common::core::ShapeHold* const hold = selectedShapeHold(); hold != nullptr)
    {
        return unnamed(hold->start, hold->end);
    }
    if (const std::optional<std::size_t> shape = selectedHandSpanShape(); shape.has_value())
    {
        return shapeHoldOverSpan(
            chartResolutions().shapes.at(*shape), tempo_map, common::core::UnnamedGrip{});
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

} // namespace rock_hero::editor::core
