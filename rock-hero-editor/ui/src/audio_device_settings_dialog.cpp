#include "audio_device_settings_dialog.h"

#include "audio_device_settings_component.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

// Launches the custom audio-device settings dialog centered on the requesting component.
void AudioDeviceSettingsDialog::show(
    juce::AudioDeviceManager& device_manager, bool direct_monitoring_enabled,
    juce::Component& anchor, MonitoringChangedFunction monitoring_changed)
{
    auto content = std::make_unique<AudioDeviceSettingsComponent>(
        device_manager, direct_monitoring_enabled, std::move(monitoring_changed));

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Device Settings";
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.16F);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.useBottomRightCornerResizer = true;
    options.componentToCentreAround = &anchor;
    options.content.setOwned(content.release());
    auto* window = options.launchAsync();
    window->setResizeLimits(
        AudioDeviceSettingsComponent::minimumWidth(),
        AudioDeviceSettingsComponent::minimumHeight(),
        AudioDeviceSettingsComponent::maximumWidth(),
        AudioDeviceSettingsComponent::maximumHeight());
}

} // namespace rock_hero::editor::ui
