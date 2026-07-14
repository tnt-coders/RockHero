/*!
\file audio_device_settings_window.h
\brief Private editor UI window hosting Rock Hero audio device settings.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

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
    \brief Dispatches an audio-device operation behind the host editor's busy overlay paint fence.

    Empty by default; when supplied, OK and Cancel on the settings dialog hide the window, hand
    the operation and post-clear continuations to the dispatcher (which schedules them through the
    editor's busy overlay). Used for any blocking JUCE device-manager work the dialog drives (the
    apply and the cancel both reopen the audio device, which blocks the message thread). Empty
    leaves the dialog in the synchronous fallback behavior so it can stand alone in tests or in
    composition contexts that lack a busy overlay.
    */
    using Dispatcher =
        std::function<void(std::function<void()> work, std::function<void()> after_cleared)>;
    /*! \brief Called when the settings window reaches a final close path. */
    using ClosedCallback = std::function<void()>;

    /*!
    \brief Called when the "use game audio settings" toggle changes.

    Receives the requested toggle value plus the dialog's applying presentation (empty on the
    cancel-time restore). The composition layer forwards both to the editor controller, which
    brackets a genuine blocking device re-open with the presentation -- hiding the dialog like the
    OK/Cancel apply path -- and never invokes it for an instant same-device flip.
    */
    using GameAudioSettingsChangedCallback =
        std::function<void(bool enabled, std::function<void(bool)> set_applying)>;

    /*! \brief Resolved "use game audio settings" toggle and game-availability state at open. */
    struct GameAudioSettings final
    {
        /*! \brief True when the toggle is on and the panel opens read-only. */
        bool use_game_settings{false};

        /*! \brief True when a calibrated game audio configuration exists to reflect. */
        bool game_source_available{false};
    };

    /*!
    \brief Opens the modal window around the top-level component that owns the launcher.
    \param audio_devices Audio-device configuration backend; must outlive the window.
    \param anchor Launcher component used to find the owning editor window.
    \param dispatcher Optional operation hook supplied by the editor composition layer; receives
           device-manager work plus a post-clear continuation.
    \param closed_callback Called when the window reaches a final close path.
    \param game_settings Resolved "use game audio settings" toggle/availability state at open.
    \param on_game_settings_changed Called when the user changes the toggle inside the window.
    \return The opened window. The caller owns it and should clear it from the close callback.
    */
    [[nodiscard]] static std::unique_ptr<juce::DocumentWindow> show(
        common::audio::IAudioDeviceConfiguration& audio_devices, juce::Component& anchor,
        Dispatcher dispatcher = {}, ClosedCallback closed_callback = {},
        GameAudioSettings game_settings = {},
        GameAudioSettingsChangedCallback on_game_settings_changed = {});

private:
    AudioDeviceSettingsWindow() = default;
};

} // namespace rock_hero::editor::ui
