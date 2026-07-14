#include "rock_hero/game/core/settings/game_settings_error.h"

#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Centralises default game settings messages so public errors stay consistent.
[[nodiscard]] std::string defaultGameSettingsErrorMessage(GameSettingsErrorCode code)
{
    switch (code)
    {
        case GameSettingsErrorCode::InvalidSettingValue:
        {
            return "Setting value is not valid";
        }
        case GameSettingsErrorCode::CouldNotSave:
        {
            return "Game settings file could not be saved";
        }
    }

    return "Game settings operation failed";
}

} // namespace

GameSettingsError::GameSettingsError(GameSettingsErrorCode error_code)
    : GameSettingsError(error_code, defaultGameSettingsErrorMessage(error_code))
{}

GameSettingsError::GameSettingsError(GameSettingsErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::game::core
