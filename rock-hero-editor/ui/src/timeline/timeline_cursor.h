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
\brief Maps mouse modifiers to the cursor placement mode shared by every timeline click site.

Ctrl keeps the exact click point; an unmodified click snaps to the tempo grid.

\param mods Modifier state of the mouse event.
\return Placement mode for timelineCursorPlacementTime.
*/
[[nodiscard]] core::TimelineCursorPlacementMode placementModeFor(const juce::ModifierKeys& mods);

/*!
\brief Resolves a timeline-content x to the exact musical position a placement gesture should use.

Shared by every grid-snapping placement gesture (automation points and tone-region boundaries) so
they snap identically. An unmodified gesture snaps to the tempo grid's own exact rational address
(so any grid value round-trips), while Ctrl bypasses the visible grid and quantizes to a 1/960-beat
fine grid, keeping the stored position an exact rational far finer than audible resolution.

\param tempo_map Song tempo map supplying the snap grid.
\param grid_note_value Grid step as a fraction of a whole note, shared with grid rendering.
\param visible_timeline Timeline range represented by the full content width.
\param width Full content width in pixels.
\param content_x X coordinate in timeline-content coordinates.
\param mods Modifier state of the gesture (Ctrl bypasses the visible grid).
\return Exact musical position, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<common::core::GridPosition> musicalGridPositionForX(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int width, float content_x,
    const juce::ModifierKeys& mods);

/*!
\brief Reports whether two optional caret-mask y-ranges are exactly equal.

The caret-bearing views push this range (the paused cursor's cut-out span) to the track viewport,
which gates redundant refreshes on it and folds it into its ruler-cursor memo. Empty compares equal
only to empty; two present ranges compare by exact bounds. Hand-written rather than relying on
juce::Range's operator== so the exact float comparison stays -Wfloat-equal clean (is_eq expresses it
warning-free), matching the project's other exact-float compares.

\param lhs First optional caret-mask range.
\param rhs Second optional caret-mask range.
\return True when both are empty, or both present with exactly equal start and end.
*/
[[nodiscard]] bool sameCaretMask(
    const std::optional<juce::Range<float>>& lhs, const std::optional<juce::Range<float>>& rhs);

} // namespace rock_hero::editor::ui
