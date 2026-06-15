/*!
\file plugin_host_error.h
\brief Typed errors returned by the plugin-host boundary.
*/

#pragma once

#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for plugin discovery and chain mutation. */
enum class PluginHostErrorCode
{
    /*! \brief The requested plugin file path is empty or does not exist. */
    MissingPluginFile,

    /*! \brief Plugin scanning completed without finding a compatible plugin candidate. */
    NoCompatiblePlugin,

    /*! \brief Plugin scanning failed before candidates could be inspected. */
    PluginScanFailed,

    /*! \brief The requested plugin candidate ID is not present in the known plugin list. */
    PluginNotFound,

    /*! \brief The requested loaded plugin instance is not present in the hosted chain. */
    PluginInstanceNotFound,

    /*! \brief A plugin-host operation was invoked from the wrong thread. */
    MessageThreadRequired,

    /*! \brief The target track was not available in the backend edit. */
    TrackMissing,

    /*! \brief The backend could not create a plugin object for the selected candidate. */
    PluginCreationFailed,

    /*! \brief The backend created the plugin but the plugin reported a load failure. */
    PluginLoadFailed,

    /*! \brief The backend could not append the plugin to the hosted chain. */
    PluginInsertionFailed,

    /*! \brief The requested user-visible chain index is outside the current chain range. */
    InvalidChainIndex,

    /*! \brief The hosted chain already contains the maximum supported user plugin count. */
    PluginChainLimitExceeded,

    /*! \brief The backend could not reorder an existing plugin in the hosted chain. */
    PluginMoveFailed,

    /*! \brief The backend could not remove a plugin from the hosted chain. */
    PluginRemovalFailed,

    /*! \brief The backend could not restore monitoring after a hosted-chain mutation. */
    MonitoringRouteFailed,

    /*! \brief The plugin editor window could not be created or shown. */
    PluginWindowUnavailable,

    /*! \brief The backend could not capture a full opaque state chunk for a plugin instance. */
    PluginStateCaptureFailed,

    /*! \brief The backend could not restore a full opaque state chunk to a plugin instance. */
    PluginStateRestoreFailed,

    /*! \brief The backend could prove neither success nor rollback after a mutation failure. */
    RollbackContractViolation,
};

/*! \brief Recoverable plugin-host failure with a stable code and displayable detail. */
struct [[nodiscard]] PluginHostError
{
    /*! \brief Stable error code used by callers for branching. */
    PluginHostErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit PluginHostError(PluginHostErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    PluginHostError(PluginHostErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
