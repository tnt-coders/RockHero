#include "timeline_cursor.h"

#include "shared/editor_theme.h"

#include <algorithm>
#include <cmath>
#include <compare>
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

// One mapping for every timeline click site, so the snap-bypass modifier cannot drift between
// the ruler and the content overlay.
core::TimelineCursorPlacementMode placementModeFor(const juce::ModifierKeys& mods)
{
    return mods.isCtrlDown() ? core::TimelineCursorPlacementMode::Free
                             : core::TimelineCursorPlacementMode::SnapToGrid;
}

std::optional<common::core::GridPosition> musicalGridPositionForX(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int width, float content_x,
    const juce::ModifierKeys& mods)
{
    const std::optional<common::core::TimePosition> clicked = core::timelineCursorPlacementTime(
        tempo_map,
        grid_note_value,
        visible_timeline,
        width,
        content_x,
        core::TimelineCursorPlacementMode::Free);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }

    // Snapped placements store the grid line's own exact musical address, so any grid value —
    // including odd fractions like 1/13 that no fixed fine grid divides — round-trips exactly.
    if (placementModeFor(mods) == core::TimelineCursorPlacementMode::SnapToGrid)
    {
        return core::nearestTempoGridPosition(tempo_map, grid_note_value, *clicked);
    }

    // Ctrl-free placements quantize the fractional beat to the shared 1/960 fine grid.
    return core::fineGridPositionForBeat(
        tempo_map, tempo_map.beatPositionAtSeconds(clicked->seconds));
}

// Exact equality expressed through is_eq so -Wfloat-equal builds (CI's GCC/Clang/clang-cl) stay
// clean; juce::Range's own operator== would compare the floats directly and trip it.
bool sameCaretMask(
    const std::optional<juce::Range<float>>& lhs, const std::optional<juce::Range<float>>& rhs)
{
    if (lhs.has_value() != rhs.has_value())
    {
        return false;
    }
    if (!lhs.has_value())
    {
        return true;
    }
    return std::is_eq(lhs->getStart() <=> rhs->getStart()) &&
           std::is_eq(lhs->getEnd() <=> rhs->getEnd());
}

} // namespace rock_hero::editor::ui
