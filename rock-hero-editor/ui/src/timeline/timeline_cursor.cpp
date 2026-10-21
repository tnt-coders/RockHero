#include "timeline_cursor.h"

#include <initializer_list>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>

namespace rock_hero::editor::ui
{

// The cursor is a one-pixel point mark, so its column is chosen here, in canvas space, through
// the one point-mark column rule: every surface drawing it (the overlay, the content's paused
// column, the pinned ruler) then reads the same column whatever its own width.
std::optional<int> cursorColumnForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept
{
    // Read through has_value rather than optional::transform, which libc++ routes through the
    // throwing value() and so cannot be called under this function's noexcept.
    const std::optional<float> x = core::timelineXForPosition(
        position, visible_timeline, width, core::TimelinePositionClamping::ClampToVisibleRange);
    if (!x.has_value())
    {
        return std::nullopt;
    }
    return core::timelineColumnForX(*x, width);
}

// Two strips rather than one spanning both, so a cursor that jumps repaints only where it was and
// where it lands; the repaint manager merges them where they touch. A few pixels of margin either
// side of the column, which Component::repaint holds to the component.
void repaintCursorColumns(
    juce::Component& component, const std::optional<int> previous_column,
    const std::optional<int> next_column)
{
    constexpr int margin = 3;
    for (const std::optional<int>& column : {previous_column, next_column})
    {
        if (column.has_value())
        {
            component.repaint(*column - margin, 0, (2 * margin) + 1, component.getHeight());
        }
    }
}

// Fills the shared cursor column so every timeline view draws the transport cursor identically.
// The column is never clamped here: cursorColumnForTimelinePosition already held the end inside
// the canvas, so a column outside this component is one scrolled past its edge.
std::optional<int> drawTimelineCursor(
    juce::Graphics& g, const juce::Component& component, const std::optional<int> column,
    const int top, const juce::Colour color)
{
    if (!column.has_value() || *column < 0 || *column >= component.getWidth())
    {
        return std::nullopt;
    }
    g.setColour(color);
    g.fillRect(*column, top, 1, component.getHeight() - top);
    return column;
}

// Maps a content x to the nearest placement-grid line's exact musical position.
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
