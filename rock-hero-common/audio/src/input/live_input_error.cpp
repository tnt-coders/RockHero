#include "input/live_input_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default live input messages so public errors stay consistent.
[[nodiscard]] std::string defaultLiveInputErrorMessage(LiveInputErrorCode code)
{
    switch (code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return "Live input operation must run on the message thread";
        }
        case LiveInputErrorCode::InputRouteUnavailable:
        {
            return "Live input route is unavailable";
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return "Live input instrument track is not available";
        }
        case LiveInputErrorCode::CouldNotSetInputGain:
        {
            return "Could not apply input calibration gain";
        }
        case LiveInputErrorCode::CouldNotSetMonitoring:
        {
            return "Could not change live input monitoring";
        }
    }

    return "Live input operation failed";
}

} // namespace

LiveInputError::LiveInputError(LiveInputErrorCode error_code)
    : LiveInputError(error_code, defaultLiveInputErrorMessage(error_code))
{}

LiveInputError::LiveInputError(LiveInputErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
