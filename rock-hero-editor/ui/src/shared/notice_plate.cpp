#include "shared/notice_plate.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"

#include <algorithm>

namespace rock_hero::editor::ui
{

namespace
{

// Padding around the text, and the plate's surface-coloured fill.
constexpr int g_notice_padding_x{16};
constexpr int g_notice_padding_y{10};
constexpr float g_notice_plate_alpha{0.9f};
constexpr float g_notice_plate_radius{4.0f};

} // namespace

// Sizes the plate to one line of the notice, capped at the area's width; a notice wider than that
// elides rather than wraps, so the plate keeps its shape.
void paintNoticePlate(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    const juce::Font font{juce::FontOptions{16.0f, juce::Font::bold}};
    const int plate_width =
        std::min(textWidth(font, text) + (2 * g_notice_padding_x), area.getWidth());
    const juce::Rectangle<int> plate = area.withSizeKeepingCentre(
        plate_width, juce::roundToInt(font.getHeight()) + (2 * g_notice_padding_y));

    g.setColour(editorTheme().panel_header.withAlpha(g_notice_plate_alpha));
    g.fillRoundedRectangle(plate.toFloat(), g_notice_plate_radius);
    g.setColour(editorTheme().primary_text);
    g.setFont(font);
    g.drawText(text, plate.reduced(g_notice_padding_x, 0), juce::Justification::centred, true);
}

} // namespace rock_hero::editor::ui
