#include "live_rig_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default live rig messages so public errors stay consistent.
[[nodiscard]] std::string defaultLiveRigErrorMessage(LiveRigErrorCode code)
{
    switch (code)
    {
        case LiveRigErrorCode::MessageThreadRequired:
        {
            return "Live rig operation must run on the message thread";
        }
        case LiveRigErrorCode::InvalidRequest:
        {
            return "Live rig request is invalid";
        }
        case LiveRigErrorCode::TrackMissing:
        {
            return "Live rig instrument track is not available";
        }
        case LiveRigErrorCode::InvalidToneDocument:
        {
            return "Tone document is invalid";
        }
        case LiveRigErrorCode::MissingToneDocument:
        {
            return "Tone document does not exist";
        }
        case LiveRigErrorCode::MissingPluginState:
        {
            return "Tone plugin state does not exist";
        }
        case LiveRigErrorCode::CouldNotCreateDirectory:
        {
            return "Could not create tone directory";
        }
        case LiveRigErrorCode::CouldNotWriteToneDocument:
        {
            return "Could not write tone document";
        }
        case LiveRigErrorCode::CouldNotWritePluginState:
        {
            return "Could not write tone plugin state";
        }
        case LiveRigErrorCode::CouldNotReadToneDocument:
        {
            return "Could not read tone document";
        }
        case LiveRigErrorCode::CouldNotReadPluginState:
        {
            return "Could not read tone plugin state";
        }
        case LiveRigErrorCode::UnsupportedPlugin:
        {
            return "Tone chain contains an unsupported plugin";
        }
        case LiveRigErrorCode::PluginNotFound:
        {
            return "Tone plugin was not found";
        }
        case LiveRigErrorCode::PluginScanFailed:
        {
            return "Tone plugin scan failed";
        }
        case LiveRigErrorCode::PluginRestoreFailed:
        {
            return "Could not restore tone plugin";
        }
    }

    return "Live rig operation failed";
}

} // namespace

LiveRigError::LiveRigError(LiveRigErrorCode error_code)
    : LiveRigError(error_code, defaultLiveRigErrorMessage(error_code))
{}

LiveRigError::LiveRigError(LiveRigErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
