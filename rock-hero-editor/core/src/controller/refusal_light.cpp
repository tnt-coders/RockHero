#include "controller/refusal_light.h"

#include <algorithm>
#include <rock_hero/common/core/shared/overloaded.h>
#include <utility>

namespace rock_hero::editor::core
{

// A linear scan: a refusal lights a handful of targets, and the set keeps the order they arrived
// in. A predicate rather than a value find, which under clang the MSVC STL may send down a
// vectorized path that cannot take the element.
void RefusalLight::add(RefusalTarget target)
{
    if (std::ranges::none_of(
            targets, [&target](const RefusalTarget& held) { return held == target; }))
    {
        targets.push_back(std::move(target));
    }
}

// The union, each of the other light's targets added through the one membership test.
void RefusalLight::add(const RefusalLight& light)
{
    for (const RefusalTarget& target : light.targets)
    {
        add(target);
    }
}

bool RefusalLight::empty() const noexcept
{
    return targets.empty();
}

// Every selected object, notes then keyframes as the selection publishes them. The selection holds
// each object once, so its keys are already a set and skip the membership test, which a whole-song
// selection would otherwise pay per key.
RefusalLight lightOf(const ChartSelection& selection)
{
    RefusalLight light;
    const std::vector<ChartSelectionKey> keys = selection.keys();
    light.targets.reserve(keys.size());
    for (const ChartSelectionKey& key : keys)
    {
        std::visit([&light](const auto& object) { light.targets.emplace_back(object); }, key);
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
