#include "menu_look_and_feel.h"

#include "shell/editor_colors.h"

namespace rock_hero::editor::ui
{

// Matches the editor background without the default JUCE top/bottom border lines.
void MenuLookAndFeel::drawMenuBarBackground(
    juce::Graphics& g, int /*width*/, int /*height*/, bool /*is_mouse_over_bar*/,
    juce::MenuBarComponent& /*menu_bar*/)
{
    g.fillAll(g_editor_background_color);
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
    g.setFont(getMenuBarFont(menu_bar, item_index, item_text));
    g.drawFittedText(item_text, bounds.reduced(4, 0), juce::Justification::centred, 1);
}

} // namespace rock_hero::editor::ui
