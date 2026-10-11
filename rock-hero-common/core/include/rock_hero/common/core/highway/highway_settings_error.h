/*!
\file highway_settings_error.h
\brief Typed errors returned by the highway settings store.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable failure reasons for highway settings operations. */
enum class HighwaySettingsErrorCode : std::uint8_t
{
    /*! \brief The shared settings file could not be locked or written. */
    CouldNotSave,
};

/*! \brief Recoverable highway settings failure with a stable code and displayable detail. */
struct [[nodiscard]] HighwaySettingsError
{
    /*! \brief Stable error code used by callers for branching. */
    HighwaySettingsErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    HighwaySettingsError(HighwaySettingsErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::core
