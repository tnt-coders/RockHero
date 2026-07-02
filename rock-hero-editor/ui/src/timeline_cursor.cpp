#include "timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/editor/core/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline_geometry.h>

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

// Converts either overlay or ruler clicks through the same placement path. The click column first
// becomes a timeline position, so snapping happens in musical time and the resulting seek is the
// exact grid-line time instead of a value quantized to the pixel grid.
std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x,
    TimelineCursorPlacementMode mode)
{
    const std::optional<common::core::TimePosition> click_time =
        core::timelinePositionForX(timeline_x, visible_timeline, timeline_width);
    if (!click_time.has_value() || mode == TimelineCursorPlacementMode::Free)
    {
        return click_time;
    }

    return core::nearestTempoGridTime(tempo_map, grid_spacing_beats, *click_time);
}

} // namespace rock_hero::editor::ui
