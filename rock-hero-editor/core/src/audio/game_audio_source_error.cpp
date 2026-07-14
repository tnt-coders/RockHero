#include "audio/game_audio_source_error.h"

#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Centralises the canonical user-facing copy so every surface reports the same reason identically.
[[nodiscard]] std::string defaultGameAudioSourceErrorMessage(GameAudioSourceErrorCode code)
{
    switch (code)
    {
        case GameAudioSourceErrorCode::NotConfigured:
        {
            return "No game audio settings were found. Set up audio in the game first, or use the "
                   "editor's own audio settings.";
        }
        case GameAudioSourceErrorCode::Uncalibrated:
        {
            return "Game audio settings cannot be used until input calibration has been completed "
                   "in the game.";
        }
    }

    return "Game audio settings cannot be used.";
}

} // namespace

GameAudioSourceError::GameAudioSourceError(GameAudioSourceErrorCode error_code)
    : GameAudioSourceError(error_code, defaultGameAudioSourceErrorMessage(error_code))
{}

GameAudioSourceError::GameAudioSourceError(
    GameAudioSourceErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::editor::core
