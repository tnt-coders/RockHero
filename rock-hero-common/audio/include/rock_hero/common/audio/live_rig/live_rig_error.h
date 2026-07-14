/*!
\file live_rig_error.h
\brief Typed errors returned by the live rig boundary.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons for live rig capture and restore. */
enum class LiveRigErrorCode : std::uint8_t
{
    /*! \brief A live rig operation was invoked from the wrong thread. */
    MessageThreadRequired,

    /*! \brief The request did not contain enough valid path or arrangement data. */
    InvalidRequest,

    /*! \brief The target instrument track was not available in the backend edit. */
    TrackMissing,

    /*! \brief A tone document path was unsafe or malformed. */
    InvalidToneDocument,

    /*! \brief The referenced tone document does not exist. */
    MissingToneDocument,

    /*! \brief A referenced Tracktion plugin-state sidecar does not exist. */
    MissingPluginState,

    /*! \brief A directory needed for tone persistence could not be created. */
    CouldNotCreateDirectory,

    /*! \brief The tone document could not be written. */
    CouldNotWriteToneDocument,

    /*! \brief A Tracktion plugin-state sidecar could not be written. */
    CouldNotWritePluginState,

    /*! \brief A tone document could not be read or parsed. */
    CouldNotReadToneDocument,

    /*! \brief A Tracktion plugin-state sidecar could not be read or parsed. */
    CouldNotReadPluginState,

    /*! \brief A plugin in the current runtime chain cannot be persisted yet. */
    UnsupportedPlugin,

    /*! \brief The plugin referenced by the tone document could not be found. */
    PluginNotFound,

    /*! \brief Plugin scanning failed while trying to resolve a persisted plugin. */
    PluginScanFailed,

    /*! \brief The backend could not restore a plugin from the persisted tone state. */
    PluginRestoreFailed,

    /*!
    \brief One or more tone plugins are not installed on this machine.

    The message lists every missing plugin across every tone — the load scans to completion
    before refusing, so a player can install them all in one pass. Gameplay policy 21-Q1(A):
    a song whose tones reference uninstalled plugins refuses to start.
    */
    MissingPlugins,

    /*! \brief The tone chain contains more user plugins than this version supports. */
    PluginChainLimitExceeded,

    /*! \brief The backend could not restore monitoring after a live-rig mutation. */
    MonitoringRouteFailed,
};

/*! \brief Recoverable live rig failure with a stable code and displayable detail. */
struct [[nodiscard]] LiveRigError
{
    /*! \brief Stable error code used by callers for branching. */
    LiveRigErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit LiveRigError(LiveRigErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    LiveRigError(LiveRigErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::audio
