/*!
\file game_settings.h
\brief JUCE PropertiesFile-backed implementation of the app-local game settings port.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/game/core/settings/i_game_settings.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Persists game settings to per-user XML, mirroring the editor's settings adapter.

The file lands in the standard per-user application data folder under the shared "Rock Hero"
directory, named by gameApplicationName(), stored as XML, and saved on every set so a crash
never loses an acknowledged write. Key names stay private to this implementation.
*/
class GameSettings final : public IGameSettings
{
public:
    /*! \brief Opens (or lazily creates) the per-user game settings file. */
    GameSettings();

    /*!
    \brief Opens game settings at an explicit native path (test isolation).
    \param settings_file Settings file path used for persisted game state.
    */
    explicit GameSettings(const std::filesystem::path& settings_file);

    /*! \brief Copying is disabled because juce::PropertiesFile is stateful file IO. */
    GameSettings(const GameSettings&) = delete;

    /*! \brief Copy assignment is disabled because juce::PropertiesFile is stateful file IO. */
    GameSettings& operator=(const GameSettings&) = delete;

    /*! \brief Moving is disabled because juce::PropertiesFile is stateful file IO. */
    GameSettings(GameSettings&&) = delete;

    /*! \brief Move assignment is disabled because juce::PropertiesFile is stateful file IO. */
    GameSettings& operator=(GameSettings&&) = delete;

    /*! \brief Saves any unsaved values on destruction (best-effort). */
    ~GameSettings() override = default;

    /*!
    \brief Returns the implicit profile's stable id, generating and persisting it on first use.
    \return The stable profile id, or a typed failure when a fresh id could not be persisted.
    */
    [[nodiscard]] std::expected<std::string, GameSettingsError> getOrCreateProfileId() override;

    /*!
    \brief Reads the implicit profile's display name.
    \return Stored display name, or the "Player" default when the user never set one.
    */
    [[nodiscard]] std::string profileDisplayName() const override;

    /*!
    \brief Stores the implicit profile's display name.
    \param display_name Name to show on results and future leaderboards; must not be empty.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setProfileDisplayName(
        const std::string& display_name) override;

    /*!
    \brief Reads whether first-run onboarding has completed.
    \return Stored flag, or empty when the user has never finished onboarding.
    */
    [[nodiscard]] std::optional<bool> firstRunCompleted() const override;

    /*!
    \brief Stores the first-run onboarding completion flag.
    \param completed True once onboarding has finished.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setFirstRunCompleted(
        bool completed) override;

    /*!
    \brief Reads the user-added custom song directories.
    \return The custom song directories in user order; empty when none are stored.
    */
    [[nodiscard]] std::vector<std::filesystem::path> customScanRoots() const override;

    /*!
    \brief Stores the user-added custom song directories, replacing any previous set.
    \param roots Custom song directories to persist.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setCustomScanRoots(
        std::span<const std::filesystem::path> roots) override;

private:
    // Backing JUCE properties store; message-thread use only, save-on-set.
    mutable juce::PropertiesFile m_properties;
};

} // namespace rock_hero::game::core
