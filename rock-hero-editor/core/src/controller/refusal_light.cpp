#include "controller/refusal_light.h"

#include <rock_hero/common/core/shared/overloaded.h>
#include <utility>

namespace rock_hero::editor::core
{

// Adds one target to light.
void RefusalLight::add(RefusalTarget target)
{
    targets.push_back(std::move(target));
}

// Merges every target another light holds, after this light's own.
void RefusalLight::add(const RefusalLight& light)
{
    targets.insert(targets.end(), light.targets.begin(), light.targets.end());
}

// Every selected object, notes then keyframes as the selection publishes them.
RefusalLight lightOf(const ChartSelection& selection)
{
    RefusalLight light;
    for (const ChartSelectionKey& key : selection.keys())
    {
        std::visit([&light](const auto& object) { light.add(object); }, key);
    }
    return light;
}

// The one marker, as the target of its own kind.
RefusalLight lightOf(const MarkerSelection& selection)
{
    RefusalLight light;
    std::visit([&light](const auto& marker) { light.add(marker); }, selection);
    return light;
}

// The marker kinds share one case, which cannot compile for a kind that is not a marker, so a new
// selection kind must say what it lights.
RefusalLight selectionLight(const EditorSelection& selection)
{
    return std::visit(
        common::core::Overloaded{
            [](const std::monostate&) { return RefusalLight{}; },
            [](const ChartSelection& chart) { return lightOf(chart); },
            [](const AutomationPointSelection&) { return RefusalLight{}; },
            [](const AddAutomationLaneRowSelection&) { return RefusalLight{}; },
            [](const TimeSelection&) { return RefusalLight{}; },
            [](const auto& marker) { return lightOf(MarkerSelection{marker}); },
        },
        selection);
}

// One case per grip, so a new grip kind cannot compile without saying which mark it lights.
RefusalTarget holdTarget(const common::core::ShapeHold& hold)
{
    return std::visit(
        common::core::Overloaded{
            [&hold](const common::core::UnnamedGrip&) -> RefusalTarget {
                return SpanSelection{.start = hold.start};
            },
            [&hold](const common::core::NoneGrip&) -> RefusalTarget {
                return NoneHoldSelection{.start = hold.start};
            },
        },
        hold.grip);
}

} // namespace rock_hero::editor::core
