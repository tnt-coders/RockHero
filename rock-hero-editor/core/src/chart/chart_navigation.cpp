#include "chart/chart_navigation.h"

#include <algorithm>
#include <iterator>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>

namespace rock_hero::editor::core
{

CaretTimeBounds caretTimeBounds(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    return CaretTimeBounds{
        .seconds = secondsAtGridPosition(tempo_map, position),
        .measure_start_seconds = tempo_map.secondsAtNote(position.measure, 1, {}),
        .measure_end_seconds = tempo_map.secondsAtNote(position.measure + 1, 1, {}),
    };
}

common::core::GridPosition measureJumpPosition(
    const common::core::GridPosition& from, const bool later) noexcept
{
    const bool at_measure_start = from.beat == 1 && from.offset.numerator == 0;
    const int target_measure = later              ? from.measure + 1
                               : at_measure_start ? std::max(1, from.measure - 1)
                                                  : from.measure;
    return common::core::GridPosition{.measure = target_measure, .beat = 1, .offset = {}};
}

// One binary search either way; the strictness is what makes "previous" from inside a span land on
// that span's own start rather than skipping it.
std::optional<common::core::GridPosition> adjacentPosition(
    const std::vector<common::core::GridPosition>& positions,
    const common::core::GridPosition& reference, const bool later)
{
    if (later)
    {
        const auto next = std::ranges::upper_bound(positions, reference);
        return next != positions.end() ? std::optional{*next} : std::nullopt;
    }
    const auto at_or_after = std::ranges::lower_bound(positions, reference);
    return at_or_after != positions.begin() ? std::optional{*std::prev(at_or_after)} : std::nullopt;
}

// The stops are one sorted set: the chart start, the section starts (sorted and strictly inside
// the chart, so they need no merging) and the chart end. A section standing on the chart start is
// that same stop, so the set holds it once and nothing lies before it.
std::optional<common::core::GridPosition> adjacentSectionStop(
    const std::vector<common::core::SongSection>& sections,
    const common::core::GridPosition& chart_end, const common::core::GridPosition& reference,
    const bool later)
{
    std::vector<common::core::GridPosition> stops;
    stops.reserve(sections.size() + 2);
    stops.push_back(chartStartPosition());
    for (const common::core::SongSection& section : sections)
    {
        if (section.position != stops.back())
        {
            stops.push_back(section.position);
        }
    }
    stops.push_back(chart_end);
    return adjacentPosition(stops, reference, later);
}

} // namespace rock_hero::editor::core
