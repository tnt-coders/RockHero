/*!
\file tone_automation_error.h
\brief Typed errors returned by the tone parameter automation boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for tone parameter automation queries and edits. */
enum class ToneAutomationErrorCode : std::uint8_t
{
    /*! \brief A tone automation operation was invoked from the wrong thread. */
    MessageThreadRequired,

    /*! \brief The referenced tone is not currently loaded as a rack branch. */
    ToneNotLoaded,

    /*! \brief The referenced plugin instance is not in the tone's chain. */
    PluginInstanceNotFound,

    /*! \brief The referenced parameter id does not resolve on the plugin. */
    ParameterNotFound,
};

/*! \brief Recoverable tone automation failure with a stable code and displayable detail. */
struct [[nodiscard]] ToneAutomationError
{
    /*! \brief Stable error code used by callers for branching. */
    ToneAutomationErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit ToneAutomationError(ToneAutomationErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    ToneAutomationError(ToneAutomationErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
