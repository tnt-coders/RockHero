#include "shared/logger_error.h"

#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultLoggerErrorMessage(LoggerErrorCode code)
{
    switch (code)
    {
        case LoggerErrorCode::BackendStartFailed:
        {
            return "Logging backend could not be started.";
        }
        case LoggerErrorCode::FileSinkOpenFailed:
        {
            return "Log file sink could not be opened.";
        }
        case LoggerErrorCode::AlreadyShutDown:
        {
            return "Logging backend cannot be restarted after shutdown.";
        }
    }

    return "Logging operation failed.";
}

} // namespace

LoggerError::LoggerError(LoggerErrorCode error_code)
    : LoggerError(error_code, defaultLoggerErrorMessage(error_code))
{}

LoggerError::LoggerError(LoggerErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::core
