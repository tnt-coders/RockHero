/*!
\file null_game_settings.h
\brief No-op game settings fake for tests that do not exercise persistence.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/settings/i_game_settings.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::game::core::testing
{

/*!
\brief Accepts every write and reports defaults, persisting nothing.

Use when a component requires the settings port but the test does not care about persisted
values; tests that assert persistence behavior use GameSettings over a temp file instead.
*/
class NullGameSettings final : public IGameSettings
{
public:
    /*!
    \brief Returns a fixed profile id without persisting anything.
    \return A stable placeholder id.
    */
    [[nodiscard]] std::expected<std::string, GameSettingsError> getOrCreateProfileId() override
    {
        return std::string{"00000000-0000-0000-0000-000000000000"};
    }

    /*!
    \brief Reports the default display name.
    \return Always "Player".
    */
    [[nodiscard]] std::string profileDisplayName() const override
    {
        return "Player";
    }

    /*!
    \brief Accepts the display name without storing it.
    \param display_name Ignored.
    \return Always success.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setProfileDisplayName(
        const std::string& /*display_name*/) override
    {
        return {};
    }

    /*!
    \brief Reports that onboarding state was never stored.
    \return Always empty.
    */
    [[nodiscard]] std::optional<bool> firstRunCompleted() const override
    {
        return std::nullopt;
    }

    /*!
    \brief Accepts the flag without storing it.
    \param completed Ignored.
    \return Always success.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setFirstRunCompleted(
        bool /*completed*/) override
    {
        return {};
    }

    /*!
    \brief Reports no custom song directories.
    \return Always empty.
    */
    [[nodiscard]] std::vector<std::filesystem::path> customScanRoots() const override
    {
        return {};
    }

    /*!
    \brief Accepts the custom song directories without storing them.
    \param roots Ignored.
    \return Always success.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setCustomScanRoots(
        std::span<const std::filesystem::path> /*roots*/) override
    {
        return {};
    }

    /*!
    \brief Reports an empty game audio config.
    \return Always empty.
    */
    [[nodiscard]] GameAudioConfig gameAudioConfig() const override
    {
        return {};
    }

    /*!
    \brief Accepts the game audio config without storing it.
    \param config Ignored.
    \return Always success.
    */
    [[nodiscard]] std::expected<void, GameSettingsError> setGameAudioConfig(
        const GameAudioConfig& /*config*/) override
    {
        return {};
    }
};

} // namespace rock_hero::game::core::testing
