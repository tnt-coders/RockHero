#include "shared/mode_toggle_button.h"

#include "shared/editor_theme.h"

namespace rock_hero::editor::ui
{

namespace
{

// The on-state ring: the editor's 1 px selection stroke, its corners rounded as the class states.
constexpr float g_ring_thickness{1.0f};

// The whole button's opacity while disabled, ring and glyph alike.
constexpr float g_disabled_alpha{0.4f};

} // namespace

// The images go in for the disabled states too, so JUCE's own disabled dimming of the image never
// runs: the whole button dims once, in enablementChanged.
ModeToggleButton::ModeToggleButton(
    const juce::String& name, std::unique_ptr<juce::Drawable> on_icon,
    std::unique_ptr<juce::Drawable> off_icon)
    : juce::DrawableButton(name, juce::DrawableButton::ImageFitted)
{
    setImages(
        off_icon.get(),
        nullptr,
        nullptr,
        off_icon.get(),
        on_icon.get(),
        nullptr,
        nullptr,
        on_icon.get());
    setClickingTogglesState(false);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

// No background fill in either state: the ring alone says "on". Stroked half a pixel in, so the
// whole 1 px ring lands inside the bounds.
void ModeToggleButton::paintButton(
    juce::Graphics& g, const bool /*should_draw_button_as_highlighted*/,
    const bool /*should_draw_button_as_down*/)
{
    if (!getToggleState())
    {
        return;
    }
    g.setColour(editorTheme().accent);
    g.drawRoundedRectangle(
        getLocalBounds().toFloat().reduced(g_ring_thickness * 0.5f),
        g_ring_corner_radius,
        g_ring_thickness);
}

// Dims the ring and glyph together while the button is disabled.
void ModeToggleButton::enablementChanged()
{
    juce::DrawableButton::enablementChanged();
    setAlpha(isEnabled() ? 1.0f : g_disabled_alpha);
}

} // namespace rock_hero::editor::ui
