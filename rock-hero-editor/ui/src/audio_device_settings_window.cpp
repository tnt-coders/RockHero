#include "audio_device_settings_window.h"

#include "audio_device_settings_view.h"

#include <memory>

namespace rock_hero::editor::ui
{

// Launches the audio settings window centered on the editor window that owns the launcher.
void AudioDeviceSettingsWindow::show(
    juce::AudioDeviceManager& device_manager, juce::Component& anchor)
{
    auto view = std::make_unique<AudioDeviceSettingsView>(device_manager);
    const int content_height = view->preferredContentHeight();
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
    options.content.setOwned(view.release());
    auto* window = options.launchAsync();
    window->setResizeLimits(
        AudioDeviceSettingsView::minimumWidth(),
        content_height,
        AudioDeviceSettingsView::maximumWidth(),
        AudioDeviceSettingsView::maximumHeight());
}

} // namespace rock_hero::editor::ui
