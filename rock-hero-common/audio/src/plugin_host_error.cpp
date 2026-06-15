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
            return "Could not insert plugin into the hosted chain";
        }
        case PluginHostErrorCode::InvalidChainIndex:
        {
            return "Plugin chain index is out of range";
        }
        case PluginHostErrorCode::PluginChainLimitExceeded:
        {
            return "Signal chain plugin limit was exceeded";
        }
        case PluginHostErrorCode::PluginMoveFailed:
        {
            return "Could not move plugin in the hosted chain";
        }
        case PluginHostErrorCode::PluginRemovalFailed:
        {
            return "Could not remove plugin from the hosted chain";
        }
        case PluginHostErrorCode::MonitoringRouteFailed:
        {
            return "Could not restore monitoring after changing the hosted chain";
        }
        case PluginHostErrorCode::PluginWindowUnavailable:
        {
            return "Could not open plugin editor window";
        }
        case PluginHostErrorCode::PluginStateCaptureFailed:
        {
            return "Could not capture plugin state";
        }
        case PluginHostErrorCode::PluginStateRestoreFailed:
        {
            return "Could not restore plugin state";
        }
        case PluginHostErrorCode::RollbackContractViolation:
        {
            return "Plugin-host mutation could not prove success or rollback";
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
