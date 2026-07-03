/*!
\file text_metrics.h
\brief Shared text-measurement helper for editor widgets.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Measures the pixel width of one line of text in a font.

JUCE's direct Font string-width helpers are deprecated, so widgets measure through a
juce::GlyphArrangement layout instead; the width rounds up to whole pixels so reserved label
space never truncates the final glyph.

\param font Font the text will be drawn with.
\param text Single line of text to measure.
\return Text width in pixels, rounded up.
*/
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text);

} // namespace rock_hero::editor::ui
