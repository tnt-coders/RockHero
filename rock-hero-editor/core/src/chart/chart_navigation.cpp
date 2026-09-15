#include "chart/chart_navigation.h"

#include <algorithm>
#include <ranges>
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

// The sections are sorted and lie strictly inside the chart, so the bounds need no merging into
// the list: the end is the stop after the last section and the start the stop before the first.
// A section standing on the chart start is that same stop, so nothing lies before it.
std::optional<common::core::GridPosition> adjacentSectionStop(
    const std::vector<common::core::SongSection>& sections,
    const common::core::GridPosition& chart_end, const common::core::GridPosition& reference,
    const bool later)
{
    if (later)
    {
        for (const common::core::SongSection& section : sections)
        {
            if (reference < section.position)
            {
                return section.position;
            }
        }
        if (reference < chart_end)
        {
            return chart_end;
        }
        return std::nullopt;
    }
    for (const common::core::SongSection& section : std::views::reverse(sections))
    {
        if (section.position < reference)
        {
            return section.position;
        }
    }
    if (chartStartPosition() < reference)
    {
        return chartStartPosition();
    }
    return std::nullopt;
}

} // namespace rock_hero::editor::core
