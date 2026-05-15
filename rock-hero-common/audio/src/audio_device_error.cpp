#include "audio_device_error.h"

#include <string>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultAudioDeviceErrorMessage(AudioDeviceErrorCode code)
{
    switch (code)
    {
        case AudioDeviceErrorCode::AsioUnavailable:
        {
            return "ASIO input is not available.";
        }
        case AudioDeviceErrorCode::AsioDeviceNotFound:
        {
            return "Selected ASIO device was not found.";
        }
        case AudioDeviceErrorCode::AsioInputChannelUnavailable:
        {
            return "Selected ASIO input channel is not available.";
        }
        case AudioDeviceErrorCode::AudioDeviceOpenFailed:
        {
            return "Could not open ASIO device for live monitoring.";
        }
        case AudioDeviceErrorCode::LiveInputRoutingFailed:
        {
            return "Could not route live guitar input to the output.";
        }
    }

    return "Audio device operation failed.";
}

} // namespace

AudioDeviceError::AudioDeviceError(AudioDeviceErrorCode error_code)
    : AudioDeviceError(error_code, defaultAudioDeviceErrorMessage(error_code))
{}

AudioDeviceError::AudioDeviceError(AudioDeviceErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
