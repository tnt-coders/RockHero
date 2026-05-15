/*!
\file audio_device_settings_dialog.h
\brief Private editor UI dialog hosting JUCE's stock audio device selector.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace juce
{
class AudioDeviceManager;
}

namespace rock_hero::editor::ui
{

/*!
\brief Opens the audio-device settings dialog.

The dialog hosts juce::AudioDeviceSelectorComponent against the supplied device manager and adds
a direct-monitoring toggle. Device changes commit immediately through the device manager; the
monitoring callback fires whenever the toggle is changed.
*/
class AudioDeviceSettingsDialog final
{
public:
    /*! \brief Callback invoked when the user toggles direct monitoring. */
    using MonitoringChangedFunction = std::function<void(bool monitoring_enabled)>;

    /*!
    \brief Opens the modal dialog around the requesting component.
    \param device_manager Device manager hosted by the stock selector; must outlive the dialog.
    \param direct_monitoring_enabled Initial state of the direct-monitoring toggle.
    \param anchor Component used to center the dialog.
    \param monitoring_changed Callback invoked when the user toggles direct monitoring.
    */
    static void show(
        juce::AudioDeviceManager& device_manager, bool direct_monitoring_enabled,
        juce::Component& anchor, MonitoringChangedFunction monitoring_changed);

private:
    AudioDeviceSettingsDialog() = default;
};

} // namespace rock_hero::editor::ui
