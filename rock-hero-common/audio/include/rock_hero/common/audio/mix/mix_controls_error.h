/*!
\file mix_controls_error.h
\brief Typed errors returned by the mix-controls boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for mix-control operations. */
enum class MixControlsErrorCode : std::uint8_t
{
    /*! \brief The edit-wide master gain stage is unavailable. */
    MasterUnavailable,

    /*! \brief The backing audio track's gain stage is unavailable. */
    BackingTrackUnavailable,
};

/*! \brief Recoverable mix-control failure with a stable code and displayable detail. */
struct [[nodiscard]] MixControlsError
{
    /*! \brief Stable error code used by callers for branching. */
    MixControlsErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit MixControlsError(MixControlsErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    MixControlsError(MixControlsErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
