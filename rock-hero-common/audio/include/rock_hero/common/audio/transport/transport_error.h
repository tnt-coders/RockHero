/*!
\file transport_error.h
\brief Typed errors returned by the transport control boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for transport control operations. */
enum class TransportErrorCode : std::uint8_t
{
    /*! \brief The requested playback speed is not supported by the current implementation. */
    SpeedNotSupported,

    /*! \brief The requested loop region is shorter than the supported minimum duration. */
    LoopRegionTooShort,
};

/*! \brief Recoverable transport control failure with a stable code and displayable detail. */
struct [[nodiscard]] TransportError
{
    /*! \brief Stable error code used by callers for branching. */
    TransportErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit TransportError(TransportErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    TransportError(TransportErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
