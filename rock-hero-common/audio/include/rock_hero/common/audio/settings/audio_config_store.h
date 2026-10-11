/*!
\file audio_config_store.h
\brief JUCE-backed audio-config store over the one file both products share.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Stores the user's audio configuration in the settings file both products share.

Both the editor and the game may run at once, so every operation goes through the shared file:
reads open it fresh rather than trusting an in-memory copy, and every write holds its lock across
the read-modify-write, so neither product clobbers a key the other just wrote.
*/
class AudioConfigStore final : public IAudioConfigStore
{
public:
    /*!
    \brief Stores the audio configuration in the shared file.
    \param settings_file The file both products share; must outlive the store.
    */
    explicit AudioConfigStore(common::core::SharedSettingsFile& settings_file);

    /*!
    \brief Reads the active device route stored by a previous device change.
    \return The stored restore payload, never an empty string; absent when none is stored.
    */
    [[nodiscard]] std::optional<std::string> activeDeviceRoute() const override;

    /*!
    \brief Stores or clears the active device route.
    \param route Restore payload for the next launch; empty or an empty string clears it.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> setActiveDeviceRoute(
        std::optional<std::string> route) override;

    /*!
    \brief Reads input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed store failure.
    */
    [[nodiscard]] std::expected<std::optional<InputCalibrationState>, AudioConfigError>
    inputCalibrationFor(const InputDeviceIdentity& identity) const override;

    /*!
    \brief Stores or replaces input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> saveInputCalibration(
        InputCalibrationState calibration_state) override;

private:
    // The file both products share; the composition root owns it.
    common::core::SharedSettingsFile& m_settings_file;
};

} // namespace rock_hero::common::audio
