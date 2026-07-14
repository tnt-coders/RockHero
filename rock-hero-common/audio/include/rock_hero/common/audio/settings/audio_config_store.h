/*!
\file audio_config_store.h
\brief JUCE-backed per-app audio-config store over one properties file.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief Stores one application's audio configuration in a per-user JUCE properties file.

The store is instantiated per app over that app's own file: the application name partitions the
file (see audio_config_identity.h) and processLock is always null because each file has exactly one
writer. Opening a store ReadOnly disables saving, so a stray write can never mutate the file; this
lets one app read another app's file without risk of clobbering it.
*/
class AudioConfigStore final : public IAudioConfigStore
{
public:
    /*! \brief Whether the store may write its backing file. */
    enum class Access : std::uint8_t
    {
        /*! \brief The store reads and writes its own file. */
        ReadWrite,

        /*! \brief The store reads only; every setter is a no-op that reports CouldNotSave. */
        ReadOnly,
    };

    /*!
    \brief Opens the store at the standard per-user location for an application name.
    \param application_name Application name that partitions the audio-config file.
    \param access Whether the store may write its backing file.
    */
    AudioConfigStore(std::string_view application_name, Access access);

    /*!
    \brief Opens the store at an explicit native path so lifecycle behavior can be tested.
    \param settings_file Audio-config file path used for persisted state.
    \param access Whether the store may write its backing file.
    */
    AudioConfigStore(const std::filesystem::path& settings_file, Access access);

    /*! \brief Copying is disabled because juce::PropertiesFile is stateful file IO. */
    AudioConfigStore(const AudioConfigStore&) = delete;

    /*! \brief Copy assignment is disabled because juce::PropertiesFile is stateful file IO. */
    AudioConfigStore& operator=(const AudioConfigStore&) = delete;

    /*! \brief Moving is disabled because the store owns runtime file state. */
    AudioConfigStore(AudioConfigStore&&) = delete;

    /*! \brief Move assignment is disabled because the store owns runtime file state. */
    AudioConfigStore& operator=(AudioConfigStore&&) = delete;

    /*! \brief Destroys the store. */
    ~AudioConfigStore() override = default;

    /*!
    \brief Reads the active device route stored by a previous successful device apply.
    \return Stored route, or empty when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] std::optional<ActiveDeviceRoute> activeDeviceRoute() const override;

    /*!
    \brief Stores or clears the active device route.
    \param route Route to restore on next launch, or empty to clear the stored route.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> setActiveDeviceRoute(
        std::optional<ActiveDeviceRoute> route) override;

    /*!
    \brief Reads app-local input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed store failure.
    */
    [[nodiscard]] std::expected<std::optional<InputCalibrationState>, AudioConfigError>
    inputCalibrationFor(const InputDeviceIdentity& identity) const override;

    /*!
    \brief Stores or replaces app-local input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> saveInputCalibration(
        InputCalibrationState calibration_state) override;

    /*!
    \brief Removes app-local input calibration for one physical input route.
    \param identity Physical input route to remove.
    \return Empty success, or a typed store failure.
    */
    [[nodiscard]] std::expected<void, AudioConfigError> removeInputCalibration(
        const InputDeviceIdentity& identity) override;

private:
    /*! \brief True when the store was opened read-only and must reject every write. */
    bool m_read_only;

    /*! \brief JUCE properties file backing this application's audio config. */
    juce::PropertiesFile m_properties;
};

} // namespace rock_hero::common::audio
