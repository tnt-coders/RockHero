#include "timeline_cursor.h"

#include "shared/editor_theme.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>

namespace rock_hero::editor::ui
{

// Converts a timeline position to a bounded subpixel coordinate for the cursor overlay.
std::optional<float> cursorXForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept
{
    return core::timelineXForPosition(
        position, visible_timeline, width, core::TimelinePositionClamping::ClampToVisibleRange);
}

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

// Rounds, clamps, and fills the shared cursor column so every timeline view draws the transport
// cursor identically.
std::optional<int> drawTimelineCursor(
    juce::Graphics& g, const juce::Component& component, std::optional<float> cursor_x, int top,
    juce::Colour color)
{
    if (!cursor_x.has_value() || component.getWidth() <= 0)
    {
        return std::nullopt;
    }

    const int cursor_column =
        std::clamp(static_cast<int>(std::round(*cursor_x)), 0, component.getWidth() - 1);
    g.setColour(color);
    g.fillRect(cursor_column, top, 1, component.getHeight() - top);
    return cursor_column;
}

std::optional<common::core::GridPosition> musicalGridPositionForX(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum,
    common::core::TimeRange visible_timeline, int width, float content_x)
{
    const std::optional<common::core::TimePosition> clicked =
        core::timelinePositionForX(content_x, visible_timeline, width);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }

    // Placements store the lattice line's own exact musical address, so any grid value — including
    // odd fractions like 1/13 that no fixed fine grid divides — round-trips exactly.
    return core::nearestTempoGridPosition(tempo_map, placement_quantum, *clicked);
}

} // namespace rock_hero::editor::ui
