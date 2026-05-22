/*!
\file audio_device_settings_window.h
\brief Private editor UI window hosting Rock Hero audio device settings.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace juce
{
class Component;
}

namespace rock_hero::common::audio
{
class IAudioDeviceConfiguration;
}

namespace rock_hero::editor::ui
{

/*!
\brief Opens the audio-device settings window.

The window hosts the Rock Hero audio settings view against the supplied audio-device configuration
port.
*/
class AudioDeviceSettingsWindow final
{
public:
    /*!
    \brief Opens the modal window around the top-level component that owns the launcher.
    \param audio_devices Audio-device configuration backend; must outlive the window.
    \param anchor Launcher component used to find the owning editor window.
    */
    static void show(
        common::audio::IAudioDeviceConfiguration& audio_devices, juce::Component& anchor);

private:
    AudioDeviceSettingsWindow() = default;
};

} // namespace rock_hero::editor::ui
