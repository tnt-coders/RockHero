/*!
\file gameplay_session_error.h
\brief Typed errors reported by the gameplay session orchestration boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Stable failure reasons for gameplay session operations. */
enum class GameplaySessionErrorCode : std::uint8_t
{
    /*! \brief The session's private scratch workspace could not be created. */
    WorkspaceUnavailable,

    /*! \brief The song package could not be extracted or parsed. */
    PackageUnreadable,

    /*! \brief The requested arrangement id is not in the loaded song. */
    ArrangementNotFound,

    /*! \brief The audio boundary rejected the loaded song during preparation. */
    PreparationFailed,

    /*! \brief The audio boundary could not activate the selected arrangement. */
    ActivationFailed,

    /*! \brief The live rig failed to preload the arrangement's tones. */
    RigLoadFailed,

    /*!
    \brief The song's tones reference plugins that are not installed (21-Q1: refuse to start).

    The message lists every missing plugin so the player can install them all in one pass.
    */
    MissingPlugins,

    /*! \brief The tone timeline could not bake the switch schedule. */
    ToneTimelineFailed,

    /*! \brief The requested operation is not legal in the session's current stage. */
    OperationUnavailable,
};

/*! \brief Recoverable gameplay session failure with a stable code and displayable detail. */
struct [[nodiscard]] GameplaySessionError
{
    /*! \brief Stable error code used by callers for branching. */
    GameplaySessionErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit GameplaySessionError(GameplaySessionErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    GameplaySessionError(GameplaySessionErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::game::core
