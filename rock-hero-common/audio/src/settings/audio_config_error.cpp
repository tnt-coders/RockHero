#include "settings/audio_config_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default store messages so public errors stay consistent.
[[nodiscard]] std::string defaultAudioConfigErrorMessage(AudioConfigErrorCode code)
{
    switch (code)
    {
        case AudioConfigErrorCode::InvalidSettingValue:
        {
            return "Audio configuration value is not valid.";
        }
        case AudioConfigErrorCode::InvalidInputCalibrationHistory:
        {
            return "Saved input calibration settings are invalid.";
        }
        case AudioConfigErrorCode::CouldNotSave:
        {
            return "Could not save audio configuration.";
        }
    }

    return "Audio configuration operation failed.";
}

} // namespace

AudioConfigError::AudioConfigError(AudioConfigErrorCode error_code)
    : AudioConfigError(error_code, defaultAudioConfigErrorMessage(error_code))
{}

AudioConfigError::AudioConfigError(AudioConfigErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
