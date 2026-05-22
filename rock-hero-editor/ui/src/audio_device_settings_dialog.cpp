#include "audio_device_settings_dialog.h"

#include "audio_device_settings_component.h"

#include <memory>

namespace rock_hero::editor::ui
{

// Launches the audio settings dialog centered on the editor window that owns the launcher.
void AudioDeviceSettingsDialog::show(
    juce::AudioDeviceManager& device_manager, juce::Component& anchor)
{
    auto content = std::make_unique<AudioDeviceSettingsComponent>(device_manager);
    const int content_height = content->preferredContentHeight();
    juce::Component* const dialog_owner = anchor.getTopLevelComponent();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Device Settings";
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.16F);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.useBottomRightCornerResizer = true;
    options.componentToCentreAround = dialog_owner != nullptr ? dialog_owner : &anchor;
    options.content.setOwned(content.release());
    auto* window = options.launchAsync();
    window->setResizeLimits(
        AudioDeviceSettingsComponent::minimumWidth(),
        content_height,
        AudioDeviceSettingsComponent::maximumWidth(),
        AudioDeviceSettingsComponent::maximumHeight());
}

} // namespace rock_hero::editor::ui
