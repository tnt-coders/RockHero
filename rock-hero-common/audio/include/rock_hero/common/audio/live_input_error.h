/*!
\file live_input_error.h
\brief Typed errors returned by the live input boundary.
*/

#pragma once

#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for live input calibration and monitoring operations. */
enum class LiveInputErrorCode
{
    /*! \brief A live input operation was invoked from the wrong thread. */
    MessageThreadRequired,

    /*! \brief The active input route is missing or not valid for calibration. */
    InputRouteUnavailable,

    /*! \brief The target instrument track was not available in the backend edit. */
    TrackMissing,

    /*! \brief The backend could not create or find the structural input gain stage. */
    CouldNotSetInputGain,

    /*! \brief The backend could not enable or disable live input monitoring. */
    CouldNotSetMonitoring,
};

/*! \brief Recoverable live input failure with a stable code and displayable detail. */
struct [[nodiscard]] LiveInputError
{
    /*! \brief Stable error code used by callers for branching. */
    LiveInputErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*! \brief Creates an error with the default message for its code. */
    explicit LiveInputError(LiveInputErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    LiveInputError(LiveInputErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
