/*!
\file i_audio_config_store.h
\brief Persistence contract for the audio device route and input calibration.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Stores the user's audio configuration outside project packages and tone documents.

The editor and the game share one configuration: the same hardware, and the same calibrated
level for the same guitar, in both products. The store holds the active device route (the
opaque JUCE audio-device restore payload) and a route-keyed input-calibration set.
Fallible operations return std::expected so a corrupt persisted history surfaces as a typed failure
rather than silent absence; bare std::optional is used only where absence is the sole non-error
outcome.
*/
class IAudioConfigStore
{
public:
    /*! \brief Destroys the audio-config store interface. */
    virtual ~IAudioConfigStore() = default;

    /*!
    \brief Reads the active device route stored by a previous device change.
    \return The stored restore payload, never an empty string; absent when none is stored.
    */
    [[nodiscard]] virtual std::optional<std::string> activeDeviceRoute() const = 0;

    /*!
    \brief Stores or clears the active device route.
    \param route Restore payload for the next launch; empty or an empty string clears it.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioConfigError> setActiveDeviceRoute(
        std::optional<std::string> route) = 0;

    /*!
    \brief Reads input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed store failure.
    */
    [[nodiscard]] virtual std::expected<std::optional<InputCalibrationState>, AudioConfigError>
    inputCalibrationFor(const InputDeviceIdentity& identity) const = 0;

    /*!
    \brief Stores or replaces input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioConfigError> saveInputCalibration(
        InputCalibrationState calibration_state) = 0;

    /*!
    \brief Removes input calibration for one physical input route.
    \param identity Physical input route to remove.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] virtual std::expected<void, AudioConfigError> removeInputCalibration(
        const InputDeviceIdentity& identity) = 0;

protected:
    /*! \brief Creates the audio-config store interface. */
    IAudioConfigStore() = default;

    /*! \brief Copies the audio-config store interface. */
    IAudioConfigStore(const IAudioConfigStore&) = default;

    /*! \brief Moves the audio-config store interface. */
    IAudioConfigStore(IAudioConfigStore&&) = default;

    /*!
    \brief Assigns the audio-config store interface from another interface.
    \return Reference to this audio-config store interface.
    */
    IAudioConfigStore& operator=(const IAudioConfigStore&) = default;

    /*!
    \brief Move-assigns the audio-config store interface.
    \return Reference to this audio-config store interface.
    */
    IAudioConfigStore& operator=(IAudioConfigStore&&) = default;
};

} // namespace rock_hero::common::audio
