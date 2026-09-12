#include "menu_look_and_feel.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"

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
    g.setFont(font);
    const juce::Rectangle<int> text_bounds = bounds.reduced(4, 0);
    g.drawFittedText(item_text, text_bounds, juce::Justification::centred, 1);

    if (!m_access_keys_visible || item_text.isEmpty())
    {
        return;
    }

    // The access letter is the title's FIRST character, which is what the keybind registry's
    // Alt+F / Alt+E / Alt+V chords match by hand; test_editor_view_state.cpp locks the menu names
    // (lines 111-114) and those chords (lines 476-478) together, so the two cannot drift apart
    // silently. drawFittedText centres the single line in text_bounds, so it starts half its width
    // left of the centre and its baseline sits one ascent below the top of that centred line box.
    const float text_left = static_cast<float>(text_bounds.getCentreX()) -
                            static_cast<float>(textWidth(font, item_text)) * 0.5f;
    const float baseline =
        static_cast<float>(text_bounds.getCentreY()) - (font.getHeight() * 0.5f) + font.getAscent();
    g.fillRect(
        text_left,
        std::round(baseline) + 1.0f,
        static_cast<float>(textWidth(font, item_text.substring(0, 1))),
        1.0f);
}

} // namespace rock_hero::editor::ui
