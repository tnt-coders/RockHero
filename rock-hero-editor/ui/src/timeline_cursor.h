/*!
\file timeline_cursor.h
\brief Shared repaint helper for the timeline playback cursor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

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

} // namespace rock_hero::editor::ui
