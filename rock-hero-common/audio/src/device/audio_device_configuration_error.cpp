#include "device/audio_device_configuration_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default configuration messages so public errors stay consistent.
[[nodiscard]] std::string defaultAudioDeviceConfigurationErrorMessage(
    AudioDeviceConfigurationErrorCode code)
{
    switch (code)
    {
        case AudioDeviceConfigurationErrorCode::MessageThreadRequired:
        {
            return "Audio device configuration must run on the message thread.";
        }
        case AudioDeviceConfigurationErrorCode::InvalidSerializedState:
        {
            return "Stored audio device state is not valid XML.";
        }
        case AudioDeviceConfigurationErrorCode::RestoreFailed:
        {
            return "Could not restore stored audio device state.";
        }
    }

    return "Audio device configuration failed.";
}

} // namespace

AudioDeviceConfigurationError::AudioDeviceConfigurationError(
    AudioDeviceConfigurationErrorCode error_code)
    : AudioDeviceConfigurationError(
          error_code, defaultAudioDeviceConfigurationErrorMessage(error_code))
{}

AudioDeviceConfigurationError::AudioDeviceConfigurationError(
    AudioDeviceConfigurationErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
