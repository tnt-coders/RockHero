/*!
\file logger_error.h
\brief Typed errors reported by the project logging facade.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable logging backend startup failure reasons. */
enum class LoggerErrorCode : std::uint8_t
{
    /*! \brief The Quill backend worker could not be started. */
    BackendStartFailed,

    /*! \brief A configured log file sink could not be opened. */
    FileSinkOpenFailed,

    /*! \brief Logging was already shut down and cannot restart in this process. */
    AlreadyShutDown,
};

/*!
\brief Logging facade error with a stable code and diagnostic message.

The code is the stable contract callers branch on. The message carries runtime context such as the
failing path or the framework exception text for UI display or logs.
*/
struct [[nodiscard]] LoggerError
{
    /*! \brief Stable reason for the logging startup failure. */
    LoggerErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates a logging error with the default message for the code.
    \param error_code Stable logging failure code.
    */
    explicit LoggerError(LoggerErrorCode error_code);

    /*!
    \brief Creates a logging error with explicit diagnostic text.
    \param error_code Stable logging failure code.
    \param message_text Human-readable diagnostic text.
    */
    LoggerError(LoggerErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::core
