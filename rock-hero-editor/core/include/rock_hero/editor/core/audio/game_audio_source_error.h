/*!
\file game_audio_source_error.h
\brief Typed error for a declined switch to the game's audio configuration.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Stable reasons the game's audio configuration cannot be used by the editor. */
enum class GameAudioSourceErrorCode : std::uint8_t
{
    /*! \brief The game's audio-config file is missing, unreadable, or stores no device route. */
    NotConfigured,

    /*! \brief The game stored a device route but no input calibration exists for that route. */
    Uncalibrated,
};

/*!
\brief Recoverable failure to adopt the game's audio configuration.

The default per-code messages are the canonical user-facing copy for every surface that reports
this failure (the startup popup and the device-settings checkbox dialog), so the same reason always
reads the same way.
*/
struct [[nodiscard]] GameAudioSourceError
{
    /*! \brief Stable error code used by callers for branching. */
    GameAudioSourceErrorCode code{};

    /*! \brief Canonical user-facing text suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the canonical message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit GameAudioSourceError(GameAudioSourceErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    GameAudioSourceError(GameAudioSourceErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::editor::core
