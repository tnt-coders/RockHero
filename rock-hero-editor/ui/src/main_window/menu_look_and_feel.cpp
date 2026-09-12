#include "menu_look_and_feel.h"

#include "shared/editor_theme.h"

#include <cmath>

namespace rock_hero::editor::ui
{

// Holds the strip so the access-key setter can invalidate it.
MenuLookAndFeel::MenuLookAndFeel(juce::MenuBarComponent& menu_bar)
    : m_menu_bar{menu_bar}
{}

// Mirrors the tab lane's reveal setter: the held-Alt sampler pushes this every frame, so repaint
// only on an actual change.
void MenuLookAndFeel::setAccessKeysVisible(bool visible)
{
    if (visible == m_access_keys_visible)
    {
        return;
    }

    m_access_keys_visible = visible;
    m_menu_bar.repaint();
}

// Matches the editor background without the default JUCE top/bottom border lines.
void MenuLookAndFeel::drawMenuBarBackground(
    juce::Graphics& g, int /*width*/, int /*height*/, bool /*is_mouse_over_bar*/,
    juce::MenuBarComponent& /*menu_bar*/)
{
    g.fillAll(editorTheme().window_background);
}

// Keeps the menu item readable on the flat strip and uses a simple hover fill.
void MenuLookAndFeel::drawMenuBarItem(
    juce::Graphics& g, int width, int height, int item_index, const juce::String& item_text,
    bool is_mouse_over_item, bool is_menu_open, bool /*is_mouse_over_bar*/,
    juce::MenuBarComponent& menu_bar)
{
    const juce::Rectangle<int> bounds{0, 0, width, height};
    if (is_menu_open || is_mouse_over_item)
    {
        g.setColour(juce::Colours::grey);
        g.fillRect(bounds.reduced(2, 2));
    }

    g.setColour(menu_bar.isEnabled() ? juce::Colours::white : juce::Colours::white.withAlpha(0.5f));
    const juce::Font font = getMenuBarFont(menu_bar, item_index, item_text);
    // The layout drawFittedText would build, held here so the access letter's cell is read off
    // the glyph actually drawn. Recomputing the start from integer widths and an integer centre
    // sat the underline up to three quarters of a pixel left of the letter: JUCE centres on the
    // float advance sum, the integer width is that sum ceiled, and an odd-width box truncates its
    // centre.
    const juce::Rectangle<float> text_bounds = bounds.reduced(4, 0).toFloat();
    juce::GlyphArrangement title;
    title.addFittedText(
        font,
        item_text,
        text_bounds.getX(),
        text_bounds.getY(),
        text_bounds.getWidth(),
        text_bounds.getHeight(),
        juce::Justification::centred,
        1);
    title.draw(g);

    if (!m_access_keys_visible || item_text.isEmpty())
    {
        return;
    }

    // The access letter is the title's FIRST character, which is what the keybind registry's
    // Alt+F / Alt+E / Alt+V chords match by hand; test_editor_view_state.cpp locks the menu names
    // (lines 111-114) and those chords (lines 476-478) together, so the two cannot drift apart
    // silently. The underline spans the letter's INK, not its advance cell Windows underlines: the
    // eye centres the rule on the strokes it sees, and this font's cell sits left of them by the
    // bearings' difference (measured 0.6 px under F, 0.3 under E, 0 under V at the bar's size),
    // which read as a rule leaning left. Windows only looks centred because its menu font's F and
    // E carry symmetric bearings. Thickness and the one-row gap below the baseline are what both
    // fonts' own underline metrics round to at these sizes. The rule covers every pixel column
    // the outline touches — rounding each edge to the nearest pixel instead left one title's rule
    // half a pixel off its ink — and sits on one whole row, so it is crisp rather than smeared
    // over two columns.
    const juce::PositionedGlyph& letter = title.getGlyph(0);
    juce::Path outline;
    letter.createPath(outline);
    const juce::Rectangle<float> ink = outline.getBounds();
    const int underline_left = static_cast<int>(std::floor(ink.getX()));
    g.fillRect(
        underline_left,
        juce::roundToInt(letter.getBaselineY()) + 1,
        static_cast<int>(std::ceil(ink.getRight())) - underline_left,
        1);
}

} // namespace rock_hero::editor::ui
