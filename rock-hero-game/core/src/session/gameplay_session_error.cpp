#include "rock_hero/game/core/session/gameplay_session_error.h"

#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Centralises default gameplay session messages so public errors stay consistent.
[[nodiscard]] std::string defaultGameplaySessionErrorMessage(GameplaySessionErrorCode code)
{
    switch (code)
    {
        case GameplaySessionErrorCode::WorkspaceUnavailable:
        {
            return "Session workspace could not be created";
        }
        case GameplaySessionErrorCode::PackageUnreadable:
        {
            return "Song package could not be read";
        }
        case GameplaySessionErrorCode::ArrangementNotFound:
        {
            return "Requested arrangement is not in the song";
        }
        case GameplaySessionErrorCode::PreparationFailed:
        {
            return "Song audio preparation failed";
        }
        case GameplaySessionErrorCode::ActivationFailed:
        {
            return "Arrangement could not be activated for playback";
        }
        case GameplaySessionErrorCode::RigLoadFailed:
        {
            return "Tone rig failed to load";
        }
        case GameplaySessionErrorCode::MissingPlugins:
        {
            return "Song tones reference plugins that are not installed";
        }
        case GameplaySessionErrorCode::ToneTimelineFailed:
        {
            return "Tone switch schedule could not be prepared";
        }
        case GameplaySessionErrorCode::OperationUnavailable:
        {
            return "Operation is not available in the current session stage";
        }
    }

    return "Gameplay session operation failed";
}

} // namespace

GameplaySessionError::GameplaySessionError(GameplaySessionErrorCode error_code)
    : GameplaySessionError(error_code, defaultGameplaySessionErrorMessage(error_code))
{}

GameplaySessionError::GameplaySessionError(
    GameplaySessionErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::game::core
