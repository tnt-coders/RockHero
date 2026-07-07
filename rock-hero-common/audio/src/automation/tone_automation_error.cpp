#include "rock_hero/common/audio/automation/tone_automation_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default tone automation messages so public errors stay consistent.
[[nodiscard]] std::string defaultToneAutomationErrorMessage(ToneAutomationErrorCode code)
{
    switch (code)
    {
        case ToneAutomationErrorCode::MessageThreadRequired:
        {
            return "Tone automation operation must run on the message thread";
        }
        case ToneAutomationErrorCode::ToneNotLoaded:
        {
            return "Tone is not loaded in the live rig";
        }
        case ToneAutomationErrorCode::PluginInstanceNotFound:
        {
            return "Plugin instance was not found in the tone chain";
        }
        case ToneAutomationErrorCode::ParameterNotFound:
        {
            return "Automatable parameter was not found on the plugin";
        }
    }

    return "Tone automation operation failed";
}

} // namespace

ToneAutomationError::ToneAutomationError(ToneAutomationErrorCode error_code)
    : ToneAutomationError(error_code, defaultToneAutomationErrorMessage(error_code))
{}

ToneAutomationError::ToneAutomationError(
    ToneAutomationErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
