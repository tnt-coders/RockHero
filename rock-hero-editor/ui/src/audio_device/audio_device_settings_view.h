/*!
\file audio_device_settings_view.h
\brief Private editor UI view for Rock Hero audio hardware routing.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/audio_device/audio_device_settings_view_state.h>
#include <rock_hero/editor/core/audio_device/i_audio_device_settings_controller.h>
#include <rock_hero/editor/core/audio_device/i_audio_device_settings_view.h>

namespace rock_hero::editor::ui
{

/*!
\brief Presents editor audio-device settings state and emits controller intents.

The view owns only JUCE controls and layout. The staged route transaction, device capability
queries, apply rollback, and native-close cancellation backstop live outside the component.
*/
class AudioDeviceSettingsView final : public juce::Component, public core::IAudioDeviceSettingsView
{
public:
    /*! \brief Host callback notified when asynchronous apply starts or finishes. */
    using ApplyingCallback = std::function<void(bool)>;

    /*! \brief Host callback used when the controller requests that the window close. */
    using CloseCallback = std::function<void()>;

    /*!
    \brief Creates the audio settings view around an editor settings controller.
    \param controller Controller that receives all user intents emitted by this view.
    \param applying_callback Optional host callback for applying presentation changes.
    \param close_callback Optional host callback for real window closure.
    */
    explicit AudioDeviceSettingsView(
        core::IAudioDeviceSettingsController& controller, ApplyingCallback applying_callback = {},
        CloseCallback close_callback = {});

    /*! \brief Uses default destruction; no backend listener is owned by the view. */
    ~AudioDeviceSettingsView() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    AudioDeviceSettingsView(const AudioDeviceSettingsView&) = delete;

    /*! \brief Copy assignment is disabled because component ownership is not copyable. */
    AudioDeviceSettingsView& operator=(const AudioDeviceSettingsView&) = delete;

    /*! \brief Moving is disabled because child registrations are not movable. */
    AudioDeviceSettingsView(AudioDeviceSettingsView&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    AudioDeviceSettingsView& operator=(AudioDeviceSettingsView&&) = delete;

    /*!
    \brief Returns the default window width for the current control set.
    \return Preferred window width in pixels.
    */
    [[nodiscard]] static int preferredWidth() noexcept;

    /*!
    \brief Returns the preferred window height for the currently visible controls.
    \return Preferred content height in pixels.
    */
    [[nodiscard]] int preferredContentHeight() const noexcept;

    /*!
    \brief Returns the minimum usable window width.
    \return Minimum window width in pixels.
    */
    [[nodiscard]] static int minimumWidth() noexcept;

    /*!
    \brief Returns the maximum useful window width.
    \return Maximum window width in pixels.
    */
    [[nodiscard]] static int maximumWidth() noexcept;

    /*!
    \brief Returns the maximum useful window height.
    \return Maximum window height in pixels.
    */
    [[nodiscard]] static int maximumHeight() noexcept;

    /*!
    \brief Applies controller-derived state to the JUCE controls.
    \param state State to render.
    */
    void setState(const core::AudioDeviceSettingsViewState& state) override;

    /*! \brief Requests modal shutdown from the host DialogWindow. */
    void requestClose() override;

    /*!
    \brief Disables or restores the settings controls during async apply.
    \param applying True while an apply is in progress.
    */
    void setApplying(bool applying) override;

    /*! \brief Lays out the routing controls and window action buttons. */
    void resized() override;

private:
    // Sets labels, button text, component IDs, and static presentation properties.
    void configureControls();

    // Populates every control from the current view state.
    void applyStateToControls();

    // Resizes the view and host window to match the current form rows.
    void syncWindowHeightToContent();

    // Closes the containing DialogWindow, if the view is currently hosted by one.
    void closeWindow();

    // Controller that owns settings workflow policy.
    core::IAudioDeviceSettingsController& m_controller;

    // Last controller-derived view state rendered by this component.
    core::AudioDeviceSettingsViewState m_state{};

    // True while OK has dispatched a blocking device apply through the editor busy overlay.
    bool m_applying{false};

    // Host callback that hides or reshows the window while async apply is active.
    ApplyingCallback m_applying_callback;

    // Host callback that owns final window disposal.
    CloseCallback m_close_callback;

    juce::Label m_device_type_label;
    juce::ComboBox m_device_type_combo;
    juce::Label m_device_label;
    juce::ComboBox m_device_combo;
    juce::Label m_input_device_label;
    juce::ComboBox m_input_device_combo;
    juce::Label m_output_device_label;
    juce::ComboBox m_output_device_combo;
    juce::Label m_input_channel_label;
    juce::ComboBox m_input_channel_combo;
    juce::Label m_output_pair_label;
    juce::ComboBox m_output_pair_combo;
    juce::Label m_sample_rate_label;
    juce::ComboBox m_sample_rate_combo;
    juce::Label m_buffer_size_label;
    juce::ComboBox m_buffer_size_combo;
    juce::Label m_error_label;
    juce::TextButton m_control_panel_button;
    juce::TextButton m_ok_button;
    juce::TextButton m_cancel_button;
};

} // namespace rock_hero::editor::ui
