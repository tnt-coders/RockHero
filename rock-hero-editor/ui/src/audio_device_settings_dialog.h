/*!
\file audio_device_settings_dialog.h
\brief Private editor UI dialog hosting Rock Hero audio device settings.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce
{
class AudioDeviceManager;
}

namespace rock_hero::editor::ui
{

/*!
\brief Opens the audio-device settings dialog.

The dialog hosts the Rock Hero audio settings component against the supplied device manager and
adds cancelable preview semantics around hardware route changes.
*/
class AudioDeviceSettingsDialog final
{
public:
    /*!
    \brief Opens the modal dialog around the top-level component that owns the launcher.
    \param device_manager Device manager hosted by the settings component; must outlive the dialog.
    \param anchor Launcher component used to find the owning editor window.
    */
    static void show(juce::AudioDeviceManager& device_manager, juce::Component& anchor);

private:
    AudioDeviceSettingsDialog() = default;
};

} // namespace rock_hero::editor::ui
