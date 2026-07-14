/*!
\file i_game_settings.h
\brief Framework-light persistence contract for app-local game settings.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/settings/game_settings_error.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Stores game settings and the implicit v1 player profile outside song packages.

The game's one per-user persistence seam: every game plan that needs a persisted value adds a
typed getter/setter pair here in its own phase (reads return `std::optional`, writes return
`std::expected`), keeping key names and storage format private to the implementation. Reserved
key names already promised by other plans: `mixMasterDb`, `mixBackingDb`, `mixMonitorDb`
(plan 21 Phase 4's session-local mix values; 21-Q3: global scope). v1 ships the implicit
profile: a stable generated profile id stamped on every persisted record, a display name, and
the first-run flag plan 26's onboarding consumes.
*/
class IGameSettings
{
public:
    /*! \brief Destroys the game-settings interface. */
    virtual ~IGameSettings() = default;

    /*!
    \brief Returns the implicit profile's stable id, generating and persisting it on first use.

    The id is a canonical UUID minted once per user; every persisted score record carries it so
    multi-profile support later is additive, never a migration.

    \return The stable profile id, or a typed failure when a fresh id could not be persisted.
    */
    [[nodiscard]] virtual std::expected<std::string, GameSettingsError> getOrCreateProfileId() = 0;

    /*!
    \brief Reads the implicit profile's display name.
    \return Stored display name, or the "Player" default when the user never set one.
    */
    [[nodiscard]] virtual std::string profileDisplayName() const = 0;

    /*!
    \brief Stores the implicit profile's display name.
    \param display_name Name to show on results and future leaderboards; must not be empty.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, GameSettingsError> setProfileDisplayName(
        const std::string& display_name) = 0;

    /*!
    \brief Reads whether first-run onboarding has completed.
    \return Stored flag, or empty when the user has never finished (or started) onboarding.
    */
    [[nodiscard]] virtual std::optional<bool> firstRunCompleted() const = 0;

    /*!
    \brief Stores the first-run onboarding completion flag.
    \param completed True once onboarding has finished; plan 26 consumes this at startup.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, GameSettingsError> setFirstRunCompleted(
        bool completed) = 0;

    /*!
    \brief Reads the user-added custom song directories.

    Extra library scan roots beyond the always-present per-user default Songs folder;
    resolveLibraryScanRoots composes the effective roots as the default folder followed by these.
    Empty until the user adds a folder. Persisted under the reserved key `customScanRoots`.

    \return The custom song directories in the order the user added them.
    */
    [[nodiscard]] virtual std::vector<std::filesystem::path> customScanRoots() const = 0;

    /*!
    \brief Stores the user-added custom song directories, replacing any previous set.
    \param roots Custom song directories to persist.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, GameSettingsError> setCustomScanRoots(
        std::span<const std::filesystem::path> roots) = 0;

    /*!
    \brief Reads the game's player-slot-to-route audio configuration.

    The game-private player-to-route mapping (plan 32 Phase 1), distinct from the shared
    common::audio::AudioConfigStore that holds device state, gain calibration, and latency offsets
    keyed by route. A corrupt or absent stored value reads as an empty config, mirroring the
    rebuild-on-doubt tolerance the custom scan roots use so a broken value never crashes startup.
    Persisted under the reserved key `gameAudioConfig`.

    \return The persisted game audio config, or an empty config when none is stored.
    */
    [[nodiscard]] virtual GameAudioConfig gameAudioConfig() const = 0;

    /*!
    \brief Stores the game's player-slot-to-route audio configuration, replacing any previous set.
    \param config Player-slot-to-route bindings to persist (v1: a single slot-0 entry).
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, GameSettingsError> setGameAudioConfig(
        const GameAudioConfig& config) = 0;

protected:
    /*! \brief Creates the game-settings interface. */
    IGameSettings() = default;

    /*! \brief Copies the game-settings interface. */
    IGameSettings(const IGameSettings&) = default;

    /*! \brief Moves the game-settings interface. */
    IGameSettings(IGameSettings&&) = default;

    /*!
    \brief Assigns the game-settings interface from another instance.
    \return Reference to this game-settings interface.
    */
    IGameSettings& operator=(const IGameSettings&) = default;

    /*!
    \brief Move-assigns the game-settings interface from another instance.
    \return Reference to this game-settings interface.
    */
    IGameSettings& operator=(IGameSettings&&) = default;
};

} // namespace rock_hero::game::core
