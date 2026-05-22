/*!
\file audio_device_settings_window.h
\brief Private editor UI window hosting Rock Hero audio device settings.
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
\brief Opens the audio-device settings window.

The window hosts the Rock Hero audio settings view against the supplied device manager and adds
cancelable preview semantics around hardware route changes.
*/
class AudioDeviceSettingsWindow final
{
public:
    /*!
    \brief Opens the modal window around the top-level component that owns the launcher.
    \param device_manager Device manager hosted by the settings view; must outlive the window.
    \param anchor Launcher component used to find the owning editor window.
    */
    static void show(juce::AudioDeviceManager& device_manager, juce::Component& anchor);

private:
    AudioDeviceSettingsWindow() = default;
};

} // namespace rock_hero::editor::ui
