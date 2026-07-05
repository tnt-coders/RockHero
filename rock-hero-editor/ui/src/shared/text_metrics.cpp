#include "text_metrics.h"

#include <cmath>

namespace rock_hero::editor::ui
{

// Lays the text out once and takes the glyph bounding box, replacing JUCE's deprecated
// Font::getStringWidth path.
int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

} // namespace rock_hero::editor::ui
