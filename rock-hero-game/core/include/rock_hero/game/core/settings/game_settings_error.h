/*!
\file game_settings_error.h
\brief Typed errors returned by app-local game settings persistence.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Stable failure reasons for app-local game settings operations. */
enum class GameSettingsErrorCode : std::uint8_t
{
    /*! \brief A settings value was not valid for persistence or lookup. */
    InvalidSettingValue,

    /*! \brief The settings file could not be saved. */
    CouldNotSave,
};

/*! \brief Recoverable game settings failure with a stable code and displayable detail. */
struct [[nodiscard]] GameSettingsError
{
    /*! \brief Stable error code used by callers for branching. */
    GameSettingsErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit GameSettingsError(GameSettingsErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    GameSettingsError(GameSettingsErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::game::core
