/*!
\file audio_config_store.h
\brief JUCE-backed audio-config store over the one file both products share.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Stores the user's audio configuration in the per-user JUCE properties file both products
share.

Both the editor and the game may run at once, so every operation opens the file fresh rather
than trusting an in-memory copy, and every write holds an inter-process lock across its
read-modify-write: neither product clobbers a key the other just wrote.
*/
class AudioConfigStore final : public IAudioConfigStore
{
public:
    /*! \brief Opens the shared audio-config file at its standard per-user location. */
    AudioConfigStore();

    /*!
    \brief Opens the store at an explicit native path so lifecycle behavior can be tested.
    \param settings_file Audio-config file path used for persisted state.
    */
    explicit AudioConfigStore(const std::filesystem::path& settings_file);

    /*! \brief Copying is disabled because the store owns an inter-process lock. */
    AudioConfigStore(const AudioConfigStore&) = delete;

    /*! \brief Copy assignment is disabled because the store owns an inter-process lock. */
    AudioConfigStore& operator=(const AudioConfigStore&) = delete;

    /*! \brief Moving is disabled because the store's options point at its own lock. */
    AudioConfigStore(AudioConfigStore&&) = delete;

    /*! \brief Move assignment is disabled because the store's options point at its own lock. */
    AudioConfigStore& operator=(AudioConfigStore&&) = delete;

    /*! \brief Destroys the store. */
    ~AudioConfigStore() override = default;

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
    /*!
    \brief Opens the store at a resolved file; both public constructors delegate here.
    \param file Audio-config file used for persisted state.
    */
    explicit AudioConfigStore(juce::File file);

    /*!
    \brief Serializes access to the file across both products. Mutable because a read takes it
    too: JUCE's reload() locks through m_options.
    */
    mutable juce::InterProcessLock m_lock;

    /*! \brief Properties-file options naming m_lock as the file's process lock. */
    juce::PropertiesFile::Options m_options;

    /*! \brief The audio-config file both products read and write. */
    juce::File m_file;
};

} // namespace rock_hero::common::audio
