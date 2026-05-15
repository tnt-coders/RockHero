#include "audio_device_settings_dialog.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_dialog_width{520};
constexpr int g_dialog_height{460};
constexpr int g_content_inset{8};
constexpr int g_row_height{28};
constexpr int g_row_gap{8};
constexpr int g_close_button_width{96};

// Dialog content that hosts the JUCE device selector and a direct-monitoring toggle.
class AudioDeviceSettingsContent final : public juce::Component
{
public:
    AudioDeviceSettingsContent(
        juce::AudioDeviceManager& device_manager, bool direct_monitoring_enabled,
        AudioDeviceSettingsDialog::MonitoringChangedFunction monitoring_changed)
        : m_selector(
              device_manager,
              /* minInputChannels */ 0,
              /* maxInputChannels */ 1,
              /* minOutputChannels */ 2,
              /* maxOutputChannels */ 2,
              /* showMidiInputOptions */ false,
              /* showMidiOutputSelector */ false,
              /* showChannelsAsStereoPairs */ true,
              /* hideAdvancedOptionsWithButton */ false)
        , m_monitoring_changed(std::move(monitoring_changed))
    {
        setComponentID("audio_device_settings_dialog");

        m_monitor_toggle.setComponentID("audio_settings_monitor_toggle");
        m_monitor_toggle.setButtonText("Direct Monitor");
        m_monitor_toggle.setToggleState(direct_monitoring_enabled, juce::dontSendNotification);
        m_monitor_toggle.onClick = [this] {
            if (m_monitoring_changed)
            {
                m_monitoring_changed(m_monitor_toggle.getToggleState());
            }
        };

        m_close_button.setComponentID("audio_settings_close_button");
        m_close_button.setButtonText("Close");
        m_close_button.onClick = [this] {
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            {
                dialog->exitModalState(0);
            }
        };

        addAndMakeVisible(m_selector);
        addAndMakeVisible(m_monitor_toggle);
        addAndMakeVisible(m_close_button);
        setSize(g_dialog_width, g_dialog_height);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(g_content_inset);
        auto button_row = area.removeFromBottom(g_row_height);
        m_close_button.setBounds(button_row.removeFromRight(g_close_button_width));
        area.removeFromBottom(g_row_gap);
        auto monitor_row = area.removeFromBottom(g_row_height);
        m_monitor_toggle.setBounds(monitor_row);
        area.removeFromBottom(g_row_gap);
        m_selector.setBounds(area);
    }

private:
    juce::AudioDeviceSelectorComponent m_selector;
    juce::ToggleButton m_monitor_toggle;
    juce::TextButton m_close_button;
    AudioDeviceSettingsDialog::MonitoringChangedFunction m_monitoring_changed;
};

} // namespace

// Launches the audio-device settings dialog centered on the requesting component.
void AudioDeviceSettingsDialog::show(
    juce::AudioDeviceManager& device_manager, bool direct_monitoring_enabled,
    juce::Component& anchor, MonitoringChangedFunction monitoring_changed)
{
    auto content = std::make_unique<AudioDeviceSettingsContent>(
        device_manager, direct_monitoring_enabled, std::move(monitoring_changed));

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio Device Settings";
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.16F);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = &anchor;
    options.content.setOwned(content.release());
    options.launchAsync();
}

} // namespace rock_hero::editor::ui
