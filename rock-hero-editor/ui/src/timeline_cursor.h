/*!
\file timeline_cursor.h
\brief Shared helpers for timeline cursor repainting and placement.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>

namespace rock_hero::editor::ui
{

// Controls whether cursor placement snaps to the visible beat grid or preserves the click point.
enum class TimelineCursorPlacementMode
{
    SnapToGrid,
    Free,
};

/*!
\brief Invalidates only the strip spanning the old and new cursor positions.

Repaints a narrow full-height band over a timeline component instead of the whole component, so
cursor motion stays cheap. The band includes a few pixels of padding for the cursor line's
antialiasing. Passing two positions invalidates their union; passing one invalidates that single
strip.

\param component Full-height timeline component to invalidate.
\param previous_cursor_x Last drawn subpixel cursor x, if any.
\param next_cursor_x New subpixel cursor x, if any.
*/
void repaintCursorStrip(
    juce::Component& component, std::optional<float> previous_cursor_x,
    std::optional<float> next_cursor_x);

/*!
\brief Converts a timeline-content x coordinate into normalized controller seek intent.

\param tempo_map Song tempo map supplying the snap grid.
\param visible_timeline Timeline range represented by the full timeline width.
\param timeline_width Full timeline content width in pixels.
\param timeline_x X coordinate in timeline-content coordinates.
\param mode Whether placement should snap to the grid or stay at the click point.
\return Normalized seek value, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<double> normalizedTimelineCursorPlacementX(
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline,
    int timeline_width, float timeline_x, TimelineCursorPlacementMode mode);

} // namespace rock_hero::editor::ui
