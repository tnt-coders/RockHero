#include "menu_look_and_feel.h"

#include "shared/editor_theme.h"

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
    // silently. The underline spans the letter's ADVANCE CELL, bearings included, exactly as
    // Windows draws a menu mnemonic (ruled 2026-09-12 over an ink-spanning rule): this font's
    // cell sits left of the strokes by the bearings' difference (0.6 px under F, 0.3 under E,
    // 0 under V at the bar's size), a lean Windows' own menu font does not show because its F and
    // E carry symmetric bearings. Thickness and the one-row gap below the baseline are what both
    // fonts' own underline metrics round to at these sizes. Whole pixels on one whole row, so the
    // rule is crisp rather than smeared over two columns.
    const juce::PositionedGlyph& letter = title.getGlyph(0);
    const int underline_left = juce::roundToInt(letter.getLeft());
    g.fillRect(
        underline_left,
        juce::roundToInt(letter.getBaselineY()) + 1,
        juce::roundToInt(letter.getRight()) - underline_left,
        1);
}

} // namespace rock_hero::editor::ui
