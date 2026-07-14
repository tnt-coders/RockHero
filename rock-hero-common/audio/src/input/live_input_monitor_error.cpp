#include "input/live_input_monitor_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default monitor-error messages so public errors stay consistent.
[[nodiscard]] std::string defaultLiveInputMonitorErrorMessage(LiveInputMonitorErrorCode code)
{
    switch (code)
    {
        case LiveInputMonitorErrorCode::InvalidRequest:
        {
            return "Live input monitoring request is not valid in the current state";
        }
        case LiveInputMonitorErrorCode::BackendRejected:
        {
            return "Live input backend rejected the monitoring operation";
        }
        case LiveInputMonitorErrorCode::CalibrationStoreUnavailable:
        {
            return "Input calibration store could not be read or written";
        }
    }

    return "Live input monitoring operation failed";
}

} // namespace

LiveInputMonitorError::LiveInputMonitorError(LiveInputMonitorErrorCode error_code)
    : LiveInputMonitorError(error_code, defaultLiveInputMonitorErrorMessage(error_code))
{}

LiveInputMonitorError::LiveInputMonitorError(
    LiveInputMonitorErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
