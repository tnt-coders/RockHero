// MenuBarButton paints a direct action with the same visual language as menu-bar items.

#include "menu_bar_button.h"

#include "text_metrics.h"

namespace rock_hero::editor::ui
{

namespace
{

// Matches the highlight rect drawn by the menu-bar item path in MenuLookAndFeel.
void paintMenuStyleHighlight(
    juce::Graphics& graphics, juce::Rectangle<int> bounds, bool highlighted)
{
    if (!highlighted)
    {
        return;
    }

    graphics.setColour(juce::Colours::grey);
    graphics.fillRect(bounds.reduced(2, 2));
}

} // namespace

// Creates a JUCE button with no visible label until the owner supplies one. The empty internal
// name is intentional; owners identify the component via Component::setComponentID.
MenuBarButton::MenuBarButton()
    : juce::Button{juce::String{}}
{}

// Adapts the menu-bar action vocabulary to JUCE's button text storage.
void MenuBarButton::setText(const juce::String& label_text)
{
    setButtonText(label_text);
}

// Returns the label text through the menu-bar action vocabulary used by EditorView tests.
const juce::String& MenuBarButton::getText() const noexcept
{
    return getButtonText();
}

// Calculates the width EditorView reserves for the right-aligned menu-bar action.
int MenuBarButton::preferredWidthForHeight(int height) const
{
    constexpr int horizontal_padding{16};
    const float font_height = static_cast<float>(height > 0 ? height : 1) * 0.7f;
    return textWidth(juce::Font{juce::FontOptions{font_height}}, getButtonText()) +
           horizontal_padding;
}

// Renders the button as menu-bar chrome while preserving juce::Button behavior.
void MenuBarButton::paintButton(
    juce::Graphics& graphics, bool should_draw_button_as_highlighted,
    bool should_draw_button_as_down)
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    paintMenuStyleHighlight(
        graphics, bounds, should_draw_button_as_highlighted || should_draw_button_as_down);

    graphics.setColour(isEnabled() ? juce::Colours::white : juce::Colours::white.withAlpha(0.5f));
    // Mirrors the height-based font sizing JUCE uses for its default menu-bar font, so the label
    // matches the menu titles as long as both share the strip height.
    graphics.setFont(juce::Font{juce::FontOptions{static_cast<float>(getHeight()) * 0.7f}});
    graphics.drawFittedText(getButtonText(), bounds.reduced(4, 0), juce::Justification::centred, 1);
}

} // namespace rock_hero::editor::ui
