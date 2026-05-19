#include "plugin_host_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default plugin-host messages so public errors stay consistent.
[[nodiscard]] std::string defaultPluginHostErrorMessage(PluginHostErrorCode code)
{
    switch (code)
    {
        case PluginHostErrorCode::MissingPluginFile:
        {
            return "Plugin file does not exist";
        }
        case PluginHostErrorCode::NoCompatiblePlugin:
        {
            return "No compatible plugin was found";
        }
        case PluginHostErrorCode::PluginScanFailed:
        {
            return "Plugin scan failed";
        }
        case PluginHostErrorCode::PluginNotFound:
        {
            return "Plugin candidate was not found";
        }
        case PluginHostErrorCode::PluginInstanceNotFound:
        {
            return "Plugin instance was not found";
        }
        case PluginHostErrorCode::MessageThreadRequired:
        {
            return "Plugin-host operation must run on the message thread";
        }
        case PluginHostErrorCode::TrackMissing:
        {
            return "Plugin host track is not available";
        }
        case PluginHostErrorCode::PluginCreationFailed:
        {
            return "Could not create plugin";
        }
        case PluginHostErrorCode::PluginLoadFailed:
        {
            return "Plugin failed to load";
        }
        case PluginHostErrorCode::PluginInsertionFailed:
        {
            return "Could not append plugin to the hosted chain";
        }
        case PluginHostErrorCode::PluginWindowUnavailable:
        {
            return "Could not open plugin editor window";
        }
    }

    return "Plugin-host operation failed";
}

} // namespace

PluginHostError::PluginHostError(PluginHostErrorCode error_code)
    : PluginHostError(error_code, defaultPluginHostErrorMessage(error_code))
{}

PluginHostError::PluginHostError(PluginHostErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
