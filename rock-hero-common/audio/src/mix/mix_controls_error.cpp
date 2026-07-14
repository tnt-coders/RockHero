#include "rock_hero/common/audio/mix/mix_controls_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default mix-control messages so public errors stay consistent.
[[nodiscard]] std::string defaultMixControlsErrorMessage(MixControlsErrorCode code)
{
    switch (code)
    {
        case MixControlsErrorCode::MasterUnavailable:
        {
            return "Master gain stage is unavailable";
        }
        case MixControlsErrorCode::BackingTrackUnavailable:
        {
            return "Backing track gain stage is unavailable";
        }
    }

    return "Mix control operation failed";
}

} // namespace

MixControlsError::MixControlsError(MixControlsErrorCode error_code)
    : MixControlsError(error_code, defaultMixControlsErrorMessage(error_code))
{}

MixControlsError::MixControlsError(MixControlsErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
