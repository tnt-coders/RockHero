#include "audio_device_settings_window.h"

#include "audio_device_settings_view.h"

#include <memory>
#include <rock_hero/common/audio/audio_device_settings.h>
#include <rock_hero/editor/core/audio_device_settings_controller.h>

namespace rock_hero::editor::ui
{

namespace
{

// Owns the shared settings service, editor controller, and passive view for one modal window.
class AudioDeviceSettingsWindowContent final : public juce::Component
{
public:
    explicit AudioDeviceSettingsWindowContent(
        common::audio::IAudioDeviceConfiguration& audio_devices)
        : m_settings(audio_devices)
        , m_controller(m_settings)
        , m_view(m_controller)
    {
        m_controller.attachView(m_view);
        addAndMakeVisible(m_view);
        setSize(AudioDeviceSettingsView::preferredWidth(), m_view.preferredContentHeight());
    }

    // Keeps the settings view filling the dialog content area.
    void resized() override
    {
        m_view.setBounds(getLocalBounds());
    }

    // Returns the preferred height currently derived by the rendered settings view.
    [[nodiscard]] int preferredContentHeight() const noexcept
    {
        return m_view.preferredContentHeight();
    }

private:
    // Shared backend that owns the staged route transaction.
    common::audio::AudioDeviceSettings m_settings;

    // Editor-specific controller that maps settings state into view state.
    core::AudioDeviceSettingsController m_controller;

    // Passive JUCE controls rendered inside this window content.
    AudioDeviceSettingsView m_view;
};

} // namespace

// Launches the audio settings window centered on the editor window that owns the launcher.
void AudioDeviceSettingsWindow::show(
    common::audio::IAudioDeviceConfiguration& audio_devices, juce::Component& anchor)
{
    auto content = std::make_unique<AudioDeviceSettingsWindowContent>(audio_devices);
    const int content_height = content->preferredContentHeight();
    juce::Component* const centering_component = anchor.getTopLevelComponent();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Device Settings";
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.16F);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.useBottomRightCornerResizer = true;
    options.componentToCentreAround =
        centering_component != nullptr ? centering_component : &anchor;
    options.content.setOwned(content.release());
    auto* window = options.launchAsync();
    window->setResizeLimits(
        AudioDeviceSettingsView::minimumWidth(),
        content_height,
        AudioDeviceSettingsView::maximumWidth(),
        AudioDeviceSettingsView::maximumHeight());
}

} // namespace rock_hero::editor::ui
