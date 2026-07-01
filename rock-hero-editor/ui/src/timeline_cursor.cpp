#include "timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/editor/core/tempo_grid_geometry.h>

namespace rock_hero::editor::ui
{

// Invalidates the union of old/new subpixel cursor strips, including antialias padding.
void repaintCursorStrip(
    juce::Component& component, std::optional<float> previous_cursor_x,
    std::optional<float> next_cursor_x)
{
    if ((!previous_cursor_x.has_value() && !next_cursor_x.has_value()) ||
        component.getWidth() <= 0 || component.getHeight() <= 0)
    {
        return;
    }

    float left_x = 0.0f;
    float right_x = 0.0f;
    if (previous_cursor_x.has_value() && next_cursor_x.has_value())
    {
        left_x = std::min(*previous_cursor_x, *next_cursor_x);
        right_x = std::max(*previous_cursor_x, *next_cursor_x);
    }
    else
    {
        const float cursor_x = previous_cursor_x.has_value() ? *previous_cursor_x : *next_cursor_x;
        left_x = cursor_x;
        right_x = cursor_x;
    }

    constexpr int padding = 3;
    const int left = std::max(0, static_cast<int>(std::floor(left_x)) - padding);
    const int right =
        std::min(component.getWidth(), static_cast<int>(std::ceil(right_x)) + padding + 1);
    component.repaint(left, 0, right - left, component.getHeight());
}

// Converts either overlay or ruler clicks through the same placement path. Both modes resolve the
// click to an integer pixel column and normalize by the column count, matching the 1px cursor that
// renders at a single rounded column; snapping only shifts that column to the nearest grid line.
std::optional<double> normalizedTimelineCursorPlacementX(
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline,
    int timeline_width, float timeline_x, TimelineCursorPlacementMode mode)
{
    if (timeline_width <= 0)
    {
        return std::nullopt;
    }

    const int max_column = timeline_width - 1;
    if (max_column <= 0)
    {
        return 0.0;
    }

    int column = std::clamp(static_cast<int>(std::round(timeline_x)), 0, max_column);
    if (mode == TimelineCursorPlacementMode::SnapToGrid)
    {
        if (const std::optional<int> snapped_x =
                core::nearestTempoGridLineX(tempo_map, visible_timeline, timeline_width, column))
        {
            column = *snapped_x;
        }
    }

    return static_cast<double>(column) / static_cast<double>(max_column);
}

} // namespace rock_hero::editor::ui
