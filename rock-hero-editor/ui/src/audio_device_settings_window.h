/*!
\file audio_device_settings_window.h
\brief Private editor UI window hosting Rock Hero audio device settings.
*/

#pragma once

#include <functional>
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
    \brief Defers an apply continuation behind the host editor's busy overlay paint fence.

    Empty by default; when supplied, OK on the settings dialog hides the window, hands the apply
    continuation to the dispatcher (which schedules it through the editor's busy overlay), and the
    dispatcher invokes the continuation after the overlay paints. Empty leaves the window in the
    synchronous fallback behavior so the dialog can stand alone in tests or in composition contexts
    that lack a busy overlay.
    */
    using ApplyDispatcher = std::function<void(std::function<void()>)>;

    /*!
    \brief Opens the modal window around the top-level component that owns the launcher.
    \param audio_devices Audio-device configuration backend; must outlive the window.
    \param anchor Launcher component used to find the owning editor window.
    \param apply_dispatcher Optional async-apply hook supplied by the editor composition layer.
    */
    static void show(
        common::audio::IAudioDeviceConfiguration& audio_devices, juce::Component& anchor,
        ApplyDispatcher apply_dispatcher = {});

private:
    AudioDeviceSettingsWindow() = default;
};

} // namespace rock_hero::editor::ui
