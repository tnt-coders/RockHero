/*!
\file timeline_cursor.h
\brief Shared helpers for drawing and repainting the timeline transport cursor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/core/tempo_grid_geometry.h>

namespace rock_hero::editor::ui
{

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
\brief Draws the one-pixel white transport cursor column shared by every timeline view.

The subpixel cursor x rounds and clamps into the component's column range so the cursor stays
visible at both edges, matching the shared clamping in cursorXForTimelinePosition consumers. The
column runs from top to the component's bottom edge; views whose cursor must not cover header
rows pass their header height as top.

\param g Graphics context of the component being painted.
\param component Timeline component the cursor is drawn over.
\param cursor_x Subpixel cursor x, if a cursor is currently mappable.
\param top Local y where the cursor column starts.
*/
void drawTimelineCursor(
    juce::Graphics& g, const juce::Component& component, std::optional<float> cursor_x, int top);

/*!
\brief Maps mouse modifiers to the cursor placement mode shared by every timeline click site.

Ctrl keeps the exact click point; an unmodified click snaps to the tempo grid.

\param mods Modifier state of the mouse event.
\return Placement mode for timelineCursorPlacementTime.
*/
[[nodiscard]] core::TimelineCursorPlacementMode placementModeFor(const juce::ModifierKeys& mods);

} // namespace rock_hero::editor::ui
