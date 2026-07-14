/*!
\file null_game_settings.h
\brief No-op game settings fake for tests that do not exercise persistence.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/game/core/settings/i_game_settings.h>
#include <string>

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
};

} // namespace rock_hero::game::core::testing
