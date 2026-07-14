/*!
\file live_input_monitor_error.h
\brief Typed errors returned by the shared live-input monitoring service.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*!
\brief Coarse failure reasons for LiveInputMonitor operations.

The service exposes operation-level codes and preserves the underlying live-input or store
diagnostic in \ref LiveInputMonitorError::message. The internal route-unavailable-versus-other
rollback distinction is not surfaced here; it stays inside the service, branching on the raw
LiveInputError code the live-input port returns.
*/
enum class LiveInputMonitorErrorCode : std::uint8_t
{
    /*! \brief The operation was not valid in the current route or session state. */
    InvalidRequest,

    /*! \brief A live-input port setter rejected the operation. */
    BackendRejected,

    /*! \brief A calibration-store read or write failed. */
    CalibrationStoreUnavailable,
};

/*! \brief Recoverable live-input monitoring failure with a stable code and displayable detail. */
struct [[nodiscard]] LiveInputMonitorError
{
    /*! \brief Stable error code used by callers for branching. */
    LiveInputMonitorErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit LiveInputMonitorError(LiveInputMonitorErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    LiveInputMonitorError(LiveInputMonitorErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
