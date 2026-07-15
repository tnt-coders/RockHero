/*!
\file audio_device_failure_overlay.h
\brief Editor-wide blocking overlay shown while no audio device is open.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Translucent blocking overlay shown while the editor has no open audio device.

Shares the busy overlay's presentation language — a dim layer over the whole editor with a
centered surface — but is its own standing state, not a busy operation: the editor cannot
function without an audio device, so all interaction is blocked until the user resolves the
failure through the surfaced Retry / Open Audio Settings choices. Because it is an ordinary child
component rather than a JUCE modal, the main window's own close controls keep working and the
user can always exit the editor normally.

Visibility is driven entirely by EditorViewState::audio_device_failure_prompt: the overlay
appears when a prompt is staged, live-updates its text when the prompt changes, and retracts the
moment a device opens.
*/
class AudioDeviceFailureOverlay final : public juce::Component
{
public:
    /*! \brief Creates the overlay in a hidden, non-intercepting state. */
    AudioDeviceFailureOverlay();

    /*! \brief Releases child widgets owned by the overlay. */
    ~AudioDeviceFailureOverlay() override = default;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    AudioDeviceFailureOverlay(const AudioDeviceFailureOverlay&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    AudioDeviceFailureOverlay& operator=(const AudioDeviceFailureOverlay&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    AudioDeviceFailureOverlay(AudioDeviceFailureOverlay&&) = delete;

    /*! \brief Move assignment is disabled because child component registrations are not movable. */
    AudioDeviceFailureOverlay& operator=(AudioDeviceFailureOverlay&&) = delete;

    /*!
    \brief Applies the latest failure prompt.

    With a prompt staged the overlay becomes visible, renders the device headline and reason, and
    grabs keyboard focus so editor shortcuts stop reaching the EditorView component tree; with an
    empty prompt it hides and stops intercepting input. The view rendering this overlay must keep
    it in front of the editor content, with only the busy overlay above it (a Retry reopen runs
    behind the busy presentation).

    \param prompt Latest controller-derived failure prompt, or empty when no failure stands.
    */
    void setPrompt(const std::optional<core::AudioDeviceFailurePrompt>& prompt);

    /*!
    \brief Installs the callback fired by the Retry button (Return triggers it too).
    \param callback Callback notified from the button's onClick handler.
    */
    void setRetryCallback(std::function<void()> callback);

    /*!
    \brief Installs the callback fired by the Open Audio Settings button (Escape triggers it too).
    \param callback Callback notified from the button's onClick handler.
    */
    void setOpenSettingsCallback(std::function<void()> callback);

    /*!
    \brief Paints the dim layer and centered failure surface.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the centered surface: reason text above the two choice buttons. */
    void resized() override;

    /*!
    \brief Consumes keyboard input while visible so editor shortcuts cannot reach EditorView.

    Return maps to Retry and Escape to Open Audio Settings, mirroring the choice buttons; every
    other key is swallowed.

    \param key Key press delivered by JUCE.
    \return Always true; the overlay swallows every key while visible.
    */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // Reason line: "There was an error opening the audio hardware: <reason>".
    juce::Label m_message_label;

    // Re-applies the saved route through the controller's Retry decision.
    juce::TextButton m_retry_button;

    // Hands the failure to the audio device settings window.
    juce::TextButton m_open_settings_button;

    // Owner callbacks kept free of workflow policy; the overlay only reports the chosen action.
    std::function<void()> m_retry_callback;
    std::function<void()> m_open_settings_callback;
};

} // namespace rock_hero::editor::ui
