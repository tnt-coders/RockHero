/*!
\file i_audio_device_configuration.h
\brief Project-owned audio hardware configuration port.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_configuration_error.h>
#include <rock_hero/common/audio/device/audio_device_status.h>
#include <rock_hero/common/audio/device/device_restore_outcome.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <string>

namespace juce
{
class AudioDeviceManager;
}

namespace rock_hero::common::audio
{

/*!
\brief Shared notification boundary for the app's audio hardware configuration.

The port exposes the underlying juce::AudioDeviceManager so product settings UI can present and
apply hardware route choices, and broadcasts change notifications so app code can persist and
re-derive state when the hardware route changes.
*/
class IAudioDeviceConfiguration
{
public:
    /*! \brief Listener notified on the message thread when the audio route changes. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener. */
        virtual ~Listener() = default;

        /*! \brief Called after the underlying audio device manager state changes. */
        virtual void onAudioDeviceConfigurationChanged() = 0;

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

    /*! \brief Destroys the audio-device configuration interface. */
    virtual ~IAudioDeviceConfiguration() = default;

    /*!
    \brief Returns the JUCE audio device manager backing this port.
    \return Reference to the active device manager owned by the audio backend.
    */
    [[nodiscard]] virtual juce::AudioDeviceManager& deviceManager() noexcept = 0;

    /*!
    \brief Applies an opaque serialized audio-device route on the message thread (no fallback).
    \param serialized_state State string previously returned by serializedDeviceState().
    \return Which designed outcome the applied route produced: DeviceRestoreOutcome::Opened when the
            saved device opened, or DeviceRestoreOutcome::DeviceUnavailable when the saved device is
            absent or could not be opened, so the device was left closed with the saved choice
            retained (the no-fallback policy never substitutes a different device). A typed failure
            is returned only when the route could not be applied at all (unparseable state, wrong
            thread).
    \note Must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<DeviceRestoreOutcome, AudioDeviceConfigurationError>
    restoreSerializedDeviceState(const std::string& serialized_state) = 0;

    /*!
    \brief Captures the current audio-device route as an opaque serialized state string.
    \return Serialized state, or empty when no state can be captured.
    \note Must be called on the message thread.
    */
    [[nodiscard]] virtual std::optional<std::string> serializedDeviceState() const = 0;

    /*!
    \brief Reports whether a serialized route already matches the open audio device.

    Lets callers skip a redundant restoreSerializedDeviceState(): when the target route is already
    live, the restore's device re-open and monitoring-graph rebuild are pure cost. The comparison
    reconstructs the device-setup fields JUCE ties for equality and requires a device to be open, so
    it can never report a match while the live route actually differs.

    \param serialized_state State string previously returned by serializedDeviceState().
    \return True when a device is open and its setup equals the serialized route's setup.
    \note Must be called on the message thread.
    */
    [[nodiscard]] virtual bool deviceStateMatchesActive(
        const std::string& serialized_state) const = 0;

    /*!
    \brief Returns a project-owned snapshot of the current audio-device route.
    \return Current device status, or a closed status when no device is open.
    */
    [[nodiscard]] virtual AudioDeviceStatus currentDeviceStatus() const = 0;

    /*!
    \brief Returns the active physical input route identity used by input calibration.
    \return Current one-channel input identity, or empty when no valid mono input route is active.
    */
    [[nodiscard]] virtual std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const = 0;

    /*!
    \brief Marks whether a settings edit is staging with the device deliberately closed.

    While active, the backend's no-fallback policy must not auto-reopen the saved route: the
    staging close is intentional, and a reopen would grab the device under the open settings
    window. The settings workflow holds this for the lifetime of one edit; it is a plain flag, not
    a counter, because the product opens at most one settings edit at a time.

    \param active True while a settings edit holds the device closed for staging.
    */
    virtual void setRouteStagingActive(bool active) = 0;

    /*!
    \brief Registers a listener notified after audio device configuration changes.
    \param listener Listener that should be notified until it is removed.
    */
    virtual void addListener(Listener& listener) = 0;

    /*!
    \brief Removes a previously registered listener.
    \param listener Listener previously registered with addListener().
    */
    virtual void removeListener(Listener& listener) = 0;

protected:
    /*! \brief Creates the audio-device configuration interface. */
    IAudioDeviceConfiguration() = default;

    /*! \brief Copies the audio-device configuration interface. */
    IAudioDeviceConfiguration(const IAudioDeviceConfiguration&) = default;

    /*! \brief Moves the audio-device configuration interface. */
    IAudioDeviceConfiguration(IAudioDeviceConfiguration&&) = default;

    /*!
    \brief Assigns the audio-device configuration interface from another interface.
    \return Reference to this audio-device configuration interface.
    */
    IAudioDeviceConfiguration& operator=(const IAudioDeviceConfiguration&) = default;

    /*!
    \brief Move-assigns the audio-device configuration interface from another interface.
    \return Reference to this audio-device configuration interface.
    */
    IAudioDeviceConfiguration& operator=(IAudioDeviceConfiguration&&) = default;
};

} // namespace rock_hero::common::audio
