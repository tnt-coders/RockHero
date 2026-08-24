/*!
\file timeline_cursor.h
\brief Shared helpers for drawing and repainting the timeline transport cursor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>

namespace rock_hero::editor::ui
{

/*!
\brief Computes a cursor x coordinate for a timeline position and visible range.

\param position Current transport position.
\param visible_timeline Visible timeline range.
\param width Drawing width in pixels.
\return Subpixel x coordinate in [0, width - 1], or empty when no cursor can be mapped.
*/
[[nodiscard]] std::optional<float> cursorXForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept;

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
\brief Draws the one-pixel transport cursor column shared by every timeline view.

The subpixel cursor x rounds and clamps into the component's column range so the cursor stays
visible at both edges, matching the shared clamping in cursorXForTimelinePosition consumers. The
column runs from top to the component's bottom edge; views whose cursor must not cover header
rows pass their header height as top.

\param g Graphics context of the component being painted.
\param component Timeline component the cursor is drawn over.
\param cursor_x Subpixel cursor x, if a cursor is currently mappable.
\param top Local y where the cursor column starts.
\param color Cursor color: playback_cursor while playing, paused_cursor for the paused mark.
\return The pixel column the cursor was drawn in, so adornments (the ruler's flag) can center
on the exact drawn pixel; empty when nothing was drawn.
*/
std::optional<int> drawTimelineCursor(
    juce::Graphics& g, const juce::Component& component, std::optional<float> cursor_x, int top,
    juce::Colour color);

/*!
\brief Resolves a timeline-content x to the exact musical position a placement gesture should use.

Shared by every placement gesture (automation points and tone-region boundaries) so they snap
identically. The position is the quantum lattice's own exact rational address, so any grid value
round-trips — including odd fractions like 1/13 that no fixed fine grid divides.

\param tempo_map Song tempo map supplying the snap grid.
\param placement_quantum Note value positions quantize to (\ref core::placementQuantumNoteValue).
\param visible_timeline Timeline range represented by the full content width.
\param width Full content width in pixels.
\param content_x X coordinate in timeline-content coordinates.
\return Exact musical position, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<common::core::GridPosition> musicalGridPositionForX(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum,
    common::core::TimeRange visible_timeline, int width, float content_x);

} // namespace rock_hero::editor::ui
