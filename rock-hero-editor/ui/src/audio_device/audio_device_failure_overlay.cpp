#include "audio_device_failure_overlay.h"

#include <juce_graphics/juce_graphics.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Dim layer alpha behind the centered surface, matching the busy overlay so both blocking states
// share one visual language.
constexpr float g_dim_alpha = 0.55F;

// Centered surface dimensions in pixels. Sized for a single wrapped message line plus the button
// row; the overlay shrinks the surface when EditorView is narrow.
constexpr int g_surface_width = 460;
constexpr int g_surface_height = 130;
constexpr int g_surface_padding = 16;
constexpr int g_surface_corner_radius = 8;

constexpr int g_button_row_height = 28;
constexpr int g_button_message_gap = 14;
constexpr int g_retry_button_width = 96;
constexpr int g_open_settings_button_width = 160;
constexpr int g_button_gap = 12;

} // namespace

// Configures the overlay as a hidden, non-intercepting front-most child. Visibility flips when
// setPrompt() receives a value.
AudioDeviceFailureOverlay::AudioDeviceFailureOverlay()
{
    setVisible(false);
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);

    m_message_label.setJustificationType(juce::Justification::centred);
    m_message_label.setColour(juce::Label::textColourId, juce::Colours::white);
    m_message_label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_message_label);

    m_retry_button.setComponentID("audio_device_failure_retry_button");
    m_retry_button.setButtonText("Retry");
    m_retry_button.onClick = [this] {
        if (m_retry_callback)
        {
            m_retry_callback();
        }
    };
    addAndMakeVisible(m_retry_button);

    m_open_settings_button.setComponentID("audio_device_failure_open_settings_button");
    m_open_settings_button.setButtonText("Open Audio Settings");
    m_open_settings_button.onClick = [this] {
        if (m_open_settings_callback)
        {
            m_open_settings_callback();
        }
    };
    addAndMakeVisible(m_open_settings_button);
}

// Drives visibility and reason text from the controller-supplied prompt. When the overlay becomes
// newly visible it grabs keyboard focus so EditorView shortcut handlers stop receiving keypresses;
// when it hides it releases interception so the editor returns to its normal input state.
void AudioDeviceFailureOverlay::setPrompt(
    const std::optional<core::AudioDeviceFailurePrompt>& prompt)
{
    const bool should_be_visible = prompt.has_value();
    const bool was_visible = isVisible();

    if (should_be_visible)
    {
        m_message_label.setText(
            juce::String{"There was an error opening the audio hardware: "} +
                juce::String::fromUTF8(prompt->message.c_str()),
            juce::dontSendNotification);
        resized();
    }

    if (should_be_visible == was_visible)
    {
        return;
    }

    setVisible(should_be_visible);
    if (should_be_visible)
    {
        toFront(true);
        grabKeyboardFocus();
    }
}

// Stores the owner callback fired by the Retry button.
void AudioDeviceFailureOverlay::setRetryCallback(std::function<void()> callback)
{
    m_retry_callback = std::move(callback);
}

// Stores the owner callback fired by the Open Audio Settings button.
void AudioDeviceFailureOverlay::setOpenSettingsCallback(std::function<void()> callback)
{
    m_open_settings_callback = std::move(callback);
}

// Paints the dim layer behind the editor content and the rounded failure surface centered on
// top. Child widgets paint themselves above this background.
void AudioDeviceFailureOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(g_dim_alpha));

    const juce::Rectangle<int> bounds = getLocalBounds();
    const int surface_width = juce::jmin(g_surface_width, bounds.getWidth() - g_surface_padding);
    const int surface_height = juce::jmin(g_surface_height, bounds.getHeight() - g_surface_padding);
    const juce::Rectangle<int> surface =
        bounds.withSizeKeepingCentre(surface_width, surface_height);

    g.setColour(juce::Colour::fromRGB(40, 40, 40));
    g.fillRoundedRectangle(surface.toFloat(), static_cast<float>(g_surface_corner_radius));
}

// Lays out the centered surface contents: the wrapped reason line fills the space above the two
// choice buttons along the bottom.
void AudioDeviceFailureOverlay::resized()
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const int surface_width = juce::jmin(g_surface_width, bounds.getWidth() - g_surface_padding);
    const int surface_height = juce::jmin(g_surface_height, bounds.getHeight() - g_surface_padding);
    juce::Rectangle<int> surface = bounds.withSizeKeepingCentre(surface_width, surface_height);
    surface = surface.reduced(g_surface_padding);

    juce::Rectangle<int> button_row = surface.removeFromBottom(g_button_row_height);
    const int buttons_width = g_retry_button_width + g_button_gap + g_open_settings_button_width;
    button_row = button_row.withSizeKeepingCentre(
        juce::jmin(buttons_width, button_row.getWidth()), g_button_row_height);
    m_retry_button.setBounds(button_row.removeFromLeft(g_retry_button_width));
    button_row.removeFromLeft(juce::jmin(g_button_gap, button_row.getWidth()));
    m_open_settings_button.setBounds(button_row);
    surface.removeFromBottom(g_button_message_gap);

    m_message_label.setBounds(surface);
}

// Swallows every key so editor shortcuts cannot reach EditorView while the overlay is visible,
// mapping the two choice shortcuts onto their buttons first.
bool AudioDeviceFailureOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress{juce::KeyPress::returnKey} && m_retry_callback)
    {
        m_retry_callback();
    }
    else if (key == juce::KeyPress{juce::KeyPress::escapeKey} && m_open_settings_callback)
    {
        m_open_settings_callback();
    }

    return true;
}

} // namespace rock_hero::editor::ui
