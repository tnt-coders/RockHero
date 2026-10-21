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
\brief Computes the pixel column the one-pixel transport cursor draws in for a timeline position.

The time-to-x mapping clamped to the visible range, resolved to its column by
core::timelineColumnForX, which holds the visible end inside the last column. Chosen in canvas
space, so a surface narrower or wider than the canvas (the pinned ruler) draws the same column by
subtracting its scroll offset.

\param position Current transport position.
\param visible_timeline Visible timeline range.
\param width Drawing width in pixels.
\return The cursor's column, in [0, width - 1], or empty when no cursor can be mapped.
*/
[[nodiscard]] std::optional<int> cursorColumnForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept;

/*!
\brief Invalidates only the narrow full-height strips around the cursor column it leaves and the
one it enters, so cursor motion stays cheap.
\param component Full-height timeline component to invalidate.
\param previous_column Column the cursor was drawn in, if any.
\param next_column Column it now draws in, if any.
*/
void repaintCursorColumns(
    juce::Component& component, std::optional<int> previous_column, std::optional<int> next_column);

/*!
\brief Draws the one-pixel transport cursor column shared by every timeline view.

The column (from cursorColumnForTimelinePosition, offset into this component's space) draws
nothing where it falls outside the component: it is scrolled past its edge. The column runs from
top to the component's bottom edge; views whose cursor must not cover header rows pass their
header height as top.

\param g Graphics context of the component being painted.
\param component Timeline component the cursor is drawn over.
\param column The cursor's column in this component's space, if a cursor is currently mappable.
\param top Local y where the cursor column starts.
\param color Cursor color: playback_cursor while playing, paused_cursor for the paused mark.
\return The column the cursor was drawn in, so adornments (the ruler's flag) center on the drawn
pixel; empty when nothing was drawn.
*/
std::optional<int> drawTimelineCursor(
    juce::Graphics& g, const juce::Component& component, std::optional<int> column, int top,
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
