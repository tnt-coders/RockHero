/*!
\file audio_device_settings.h
\brief Shared audio-device settings workflow for Rock Hero applications.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Stable failure codes for audio-device settings operations. */
enum class AudioDeviceSettingsErrorCode : std::uint8_t
{
    /*! \brief No audio system is available to configure. */
    NoAudioSystem,

    /*! \brief No usable device is available for the selected audio system. */
    NoDevice,

    /*! \brief Applying the staged route failed. */
    ApplyFailed,

    /*! \brief Restoring the route captured before the settings edit failed. */
    RestoreFailed,

    /*! \brief The selected route has no backend control panel to open. */
    ControlPanelUnavailable,
};

/*!
\brief Typed error value returned by audio-device settings operations.
*/
struct [[nodiscard]] AudioDeviceSettingsError
{
    /*! \brief Stable error code for program behavior. */
    AudioDeviceSettingsErrorCode code{};

    /*! \brief User-facing or diagnostic error message. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit AudioDeviceSettingsError(AudioDeviceSettingsErrorCode error_code);

    /*!
    \brief Creates an error with operation-specific detail.
    \param error_code Stable error code used by callers for branching.
    \param message_text User-facing or diagnostic error message.
    */
    AudioDeviceSettingsError(AudioDeviceSettingsErrorCode error_code, std::string message_text);
};

/*! \brief A user-selectable stereo output route made from two hardware output channels. */
struct StereoOutputPair
{
    /*! \brief Zero-based left output channel index. */
    int left_channel{};

    /*! \brief Zero-based right output channel index. */
    int right_channel{};

    /*! \brief Display text for the stereo output pair. */
    std::string label;
};

/*! \brief Shared staged audio-device settings state independent of any product UI. */
struct AudioDeviceSettingsState
{
    /*! \brief Audio systems available in Rock Hero's preferred default order. */
    std::vector<std::string> audio_systems{};

    /*! \brief Selected audio-system choice ID, or zero when none is selected. */
    int selected_audio_system_id{};

    /*! \brief True when the selected audio system exposes separate input/output device lists. */
    bool uses_separate_input_output_devices{};

    /*! \brief Combined device names for audio systems that use one route selector. */
    std::vector<std::string> devices{};

    /*! \brief Selected combined-device choice ID, or zero when none is selected. */
    int selected_device_id{};

    /*! \brief Input device names for audio systems that expose separate input devices. */
    std::vector<std::string> input_devices{};

    /*! \brief Selected input-device choice ID, or zero when none is selected. */
    int selected_input_device_id{};

    /*! \brief Output device names for audio systems that expose separate output devices. */
    std::vector<std::string> output_devices{};

    /*! \brief Selected output-device choice ID, or zero when none is selected. */
    int selected_output_device_id{};

    /*! \brief Mono input channel names available on the staged route. */
    std::vector<std::string> input_channels{};

    /*! \brief Selected input-channel choice ID, or zero when none is selected. */
    int selected_input_channel_id{};

    /*! \brief Stereo output pairs available on the staged route. */
    std::vector<StereoOutputPair> stereo_output_pairs{};

    /*! \brief Selected stereo-output-pair choice ID, or zero when none is selected. */
    int selected_stereo_output_pair_id{};

    /*! \brief Sample rates available on the staged route. */
    std::vector<double> sample_rates{};

    /*! \brief Selected sample-rate choice ID, or zero when none is selected. */
    int selected_sample_rate_id{};

    /*! \brief Buffer sizes available on the staged route. */
    std::vector<int> buffer_sizes{};

    /*! \brief Selected buffer-size choice ID, or zero when none is selected. */
    int selected_buffer_size_id{};

    /*! \brief True when the staged route's audio backend exposes a control panel. */
    bool control_panel_enabled{};

    /*! \brief Last operation error to display, or empty when no error is active. */
    std::string error_message{};
};

/*!
\brief Shared audio-device settings workflow boundary.
*/
class IAudioDeviceSettings
{
public:
    /*! \brief Listener notified when the settings backend changes outside direct caller control. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener. */
        virtual ~Listener() = default;

        /*! \brief Called after the settings state should be re-read. */
        virtual void onAudioDeviceSettingsChanged() = 0;

    protected:
        /*! \brief Creates the listener. */
        Listener() = default;

        /*! \brief Copies the listener. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener.
        \return Reference to this listener.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener.
        \return Reference to this listener.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Destroys the settings boundary. */
    virtual ~IAudioDeviceSettings() = default;

    /*!
    \brief Returns the current staged settings snapshot.
    \return Current staged settings state.
    */
    [[nodiscard]] virtual AudioDeviceSettingsState state() const = 0;

    /*!
    \brief Selects an audio system by one-based choice ID.
    \param choice_id One-based audio-system choice ID, or zero to clear selection.
    */
    virtual void selectAudioSystem(int choice_id) = 0;

    /*!
    \brief Selects a combined input/output device by one-based choice ID.
    \param choice_id One-based combined-device choice ID, or zero to clear selection.
    */
    virtual void selectDevice(int choice_id) = 0;

    /*!
    \brief Selects an input device by one-based choice ID.
    \param choice_id One-based input-device choice ID, or zero to clear selection.
    */
    virtual void selectInputDevice(int choice_id) = 0;

    /*!
    \brief Selects an output device by one-based choice ID.
    \param choice_id One-based output-device choice ID, or zero to clear selection.
    */
    virtual void selectOutputDevice(int choice_id) = 0;

    /*!
    \brief Selects a mono input channel by one-based choice ID.
    \param choice_id One-based input-channel choice ID, or zero to clear selection.
    */
    virtual void selectInputChannel(int choice_id) = 0;

    /*!
    \brief Selects a stereo output pair by one-based choice ID.
    \param choice_id One-based stereo-output-pair choice ID, or zero to clear selection.
    */
    virtual void selectStereoOutputPair(int choice_id) = 0;

    /*!
    \brief Selects a sample rate by one-based choice ID.
    \param choice_id One-based sample-rate choice ID, or zero to clear selection.
    */
    virtual void selectSampleRate(int choice_id) = 0;

    /*!
    \brief Selects a buffer size by one-based choice ID.
    \param choice_id One-based buffer-size choice ID, or zero to clear selection.
    */
    virtual void selectBufferSize(int choice_id) = 0;

    /*!
    \brief Applies the staged route to the active audio backend.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioDeviceSettingsError> apply() = 0;

    /*!
    \brief Restores the active audio device to the route that was open when this object was made.

    The settings dialog closes the active device when it opens so the user can edit hardware
    routing without holding the device. Cancel reopens the captured route only when the device
    was actually open before editing began.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioDeviceSettingsError> cancel() = 0;

    /*!
    \brief Opens the backend control panel for the staged route.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioDeviceSettingsError> openControlPanel() = 0;

    /*!
    \brief Registers a listener notified after external settings changes.
    \param listener Listener that should be notified until it is removed.
    */
    virtual void addListener(Listener& listener) = 0;

    /*!
    \brief Removes a previously registered listener.
    \param listener Listener previously registered with addListener().
    */
    virtual void removeListener(Listener& listener) = 0;

protected:
    /*! \brief Creates the settings boundary. */
    IAudioDeviceSettings() = default;

    /*! \brief Copies the settings boundary. */
    IAudioDeviceSettings(const IAudioDeviceSettings&) = default;

    /*! \brief Moves the settings boundary. */
    IAudioDeviceSettings(IAudioDeviceSettings&&) = default;

    /*!
    \brief Assigns the settings boundary.
    \return Reference to this settings boundary.
    */
    IAudioDeviceSettings& operator=(const IAudioDeviceSettings&) = default;

    /*!
    \brief Move-assigns the settings boundary.
    \return Reference to this settings boundary.
    */
    IAudioDeviceSettings& operator=(IAudioDeviceSettings&&) = default;
};

/*!
\brief JUCE-backed shared audio-device settings workflow.

Construction captures the current route, closes the active audio device when one is open, and
builds staged settings from that captured route. apply() opens the staged setup; cancel()
reopens the captured previous setup only when there was an open device to restore. Destruction
without an explicit cancel() also attempts that restore so a native window close does not leave
an originally-open backend silent.
*/
class AudioDeviceSettings final : public IAudioDeviceSettings
{
public:
    /*!
    \brief Creates a settings edit around an existing audio-device configuration port.
    \param audio_devices Audio-device configuration backend; must outlive this object.
    */
    explicit AudioDeviceSettings(IAudioDeviceConfiguration& audio_devices);

    /*!
    \brief Releases listener registration and restores the captured route when needed.
    */
    ~AudioDeviceSettings() override;

    /*! \brief Copying is disabled because the settings object owns listener registration. */
    AudioDeviceSettings(const AudioDeviceSettings&) = delete;

    /*! \brief Copy assignment is disabled because listener ownership is fixed. */
    AudioDeviceSettings& operator=(const AudioDeviceSettings&) = delete;

    /*! \brief Moving is disabled because listener identity must remain stable. */
    AudioDeviceSettings(AudioDeviceSettings&&) = delete;

    /*! \brief Move assignment is disabled because listener identity must remain stable. */
    AudioDeviceSettings& operator=(AudioDeviceSettings&&) = delete;

    /*!
    \brief Returns the current staged settings snapshot.
    \return Current staged settings state.
    */
    [[nodiscard]] AudioDeviceSettingsState state() const override;

    /*!
    \brief Selects an audio system by one-based choice ID.
    \param choice_id One-based audio-system choice ID, or zero to clear selection.
    */
    void selectAudioSystem(int choice_id) override;

    /*!
    \brief Selects a combined input/output device by one-based choice ID.
    \param choice_id One-based combined-device choice ID, or zero to clear selection.
    */
    void selectDevice(int choice_id) override;

    /*!
    \brief Selects an input device by one-based choice ID.
    \param choice_id One-based input-device choice ID, or zero to clear selection.
    */
    void selectInputDevice(int choice_id) override;

    /*!
    \brief Selects an output device by one-based choice ID.
    \param choice_id One-based output-device choice ID, or zero to clear selection.
    */
    void selectOutputDevice(int choice_id) override;

    /*!
    \brief Selects a mono input channel by one-based choice ID.
    \param choice_id One-based input-channel choice ID, or zero to clear selection.
    */
    void selectInputChannel(int choice_id) override;

    /*!
    \brief Selects a stereo output pair by one-based choice ID.
    \param choice_id One-based stereo-output-pair choice ID, or zero to clear selection.
    */
    void selectStereoOutputPair(int choice_id) override;

    /*!
    \brief Selects a sample rate by one-based choice ID.
    \param choice_id One-based sample-rate choice ID, or zero to clear selection.
    */
    void selectSampleRate(int choice_id) override;

    /*!
    \brief Selects a buffer size by one-based choice ID.
    \param choice_id One-based buffer-size choice ID, or zero to clear selection.
    */
    void selectBufferSize(int choice_id) override;

    /*!
    \brief Applies the staged route to the active audio backend.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> apply() override;

    /*!
    \brief Restores the captured route when this settings edit is canceled.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> cancel() override;

    /*!
    \brief Opens the backend control panel for the staged route.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, AudioDeviceSettingsError> openControlPanel() override;

    /*!
    \brief Registers a listener notified after external settings changes.
    \param listener Listener that should be notified until it is removed.
    */
    void addListener(Listener& listener) override;

    /*!
    \brief Removes a previously registered listener.
    \param listener Listener previously registered with addListener().
    */
    void removeListener(Listener& listener) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::common::audio
