/*!
\file i_plugin_host.h
\brief Tracktion-free plugin-host port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <rock_hero/common/audio/plugin/plugin_catalog_scan_progress.h>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/plugin/plugin_chain_snapshot.h>
#include <rock_hero/common/audio/plugin/plugin_host_error.h>
#include <rock_hero/common/audio/plugin/plugin_instance_state.h>
#include <rock_hero/common/core/cancellation_token.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned description of one loadable plugin candidate.

The ID is opaque to callers. It is stable enough for catalog selection and display state, but
callers should not parse it or depend on Tracktion/JUCE formatting.
*/
struct [[nodiscard]] PluginCandidate
{
    /*! \brief Opaque candidate ID used for catalog selection and backend lookup. */
    std::string id;

    /*! \brief Plugin display name from scanner metadata or filesystem discovery. */
    std::string name;

    /*! \brief Plugin manufacturer reported by the scanner when available. */
    std::string manufacturer;

    /*! \brief Backend plugin format name, currently expected to be VST3 for authored tones. */
    std::string format_name;

    /*! \brief Plugin category/type metadata reported by the scanner when available. */
    std::string category;

    /*! \brief File or bundle path that produced this plugin candidate. */
    std::filesystem::path file_path;

    /*!
    \brief Compares two plugin candidates by their stored values.
    \param lhs Left-hand plugin candidate.
    \param rhs Right-hand plugin candidate.
    \return True when both plugin candidates store equal values.
    */
    friend bool operator==(const PluginCandidate& lhs, const PluginCandidate& rhs) = default;
};

/*! \brief Result returned after inserting a plugin candidate into the hosted chain. */
struct [[nodiscard]] PluginInsertResult
{
    /*! \brief Authoritative chain snapshot after the insertion succeeded. */
    PluginChainSnapshot snapshot;

    /*! \brief Runtime instance ID assigned to the inserted plugin. */
    std::string inserted_instance_id;

    /*!
    \brief Compares two insert results by their stored values.
    \param lhs Left-hand insert result.
    \param rhs Right-hand insert result.
    \return True when both results store equal values.
    */
    friend bool operator==(const PluginInsertResult& lhs, const PluginInsertResult& rhs) = default;
};

/*! \brief Full before/after state for one settled plugin-wide state edit. */
struct [[nodiscard]] PluginStateEdit
{
    /*! \brief Runtime plugin instance whose opaque state changed. */
    std::string instance_id;

    /*! \brief Full opaque chunk captured before the settled edit. */
    PluginInstanceState before;

    /*! \brief Full opaque chunk captured after the settled edit. */
    PluginInstanceState after;

    /*! \brief Display-only label hint for the plugin or state change. */
    std::string label_hint;

    /*!
    \brief Compares two plugin-state edits by their stored values.
    \param lhs Left-hand plugin-state edit.
    \param rhs Right-hand plugin-state edit.
    \return True when both edits store equal values.
    */
    friend bool operator==(const PluginStateEdit& lhs, const PluginStateEdit& rhs) = default;
};

/*! \brief Observer callbacks for pending and completed user plugin edits. */
struct PluginEditObserver
{
    /*! \brief Called when aggregate pending plugin edit state changes. */
    std::function<void(bool)> pending_changed;
};

/*! \brief Observer callbacks for completed plugin-wide state edits. */
struct PluginStateEditObserver
{
    /*! \brief Called when a settled plugin-wide state edit yields a full memento pair. */
    std::function<void(PluginStateEdit)> edit_completed;
};

/*! \brief Host-window shortcut callbacks forwarded to the owning application workflow. */
struct PluginWindowCommandObserver
{
    /*! \brief Called when a hosted plugin editor window receives the Undo shortcut. */
    std::function<void()> undo_requested;

    /*! \brief Called when a hosted plugin editor window receives the Redo shortcut. */
    std::function<void()> redo_requested;

    /*! \brief Called when a hosted plugin editor window receives the Play/Pause shortcut. */
    std::function<void()> play_pause_requested;
};

/*!
\brief Project-owned facade for plugin discovery and chain mutation.

Plugin catalog discovery may run on a non-realtime worker thread because it can traverse user
plugin folders. Chain mutation and plugin-window methods are message-thread operations because
implementations may mutate or inspect backend playback/UI graph state. Callers must never invoke
this interface from the real-time audio callback.

Mutating methods have an unchanged-on-error contract for ordinary PluginHostError failures. If a
method returns std::unexpected, the implementation either performed no side effects or repaired
all side effects back to the exact pre-call state before returning.
*/
class IPluginHost
{
public:
    /*! \brief Destroys the plugin-host interface. */
    virtual ~IPluginHost() = default;

    /*!
    \brief Refreshes the host's default plugin catalog locations.

    Implementations own the platform- and host-specific default locations for the plugin formats
    they support. Callers should use this for a user-initiated full catalog refresh instead of
    resolving platform search paths themselves. Implementations may cache scan results in their
    backend-native plugin catalog.

    \param progress_callback Optional callback for countable metadata-scan progress.
    \param cancel Cooperative cancellation handle; the scan stops at the next candidate boundary
    when cancellation is requested, keeping candidates already discovered.
    \return Success after the refresh, or a typed failure when scanning cannot proceed.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> scanPluginCatalog(
        PluginCatalogScanProgressCallback progress_callback = {},
        const common::core::CancellationToken& cancel = {}) = 0;

    /*!
    \brief Discovers plugin files or directories as candidates.

    Implementations may recurse through supplied directories and should return an empty list when
    no compatible candidate paths are found. Implementations may cache successful discoveries in
    their backend-native plugin catalog.

    \param roots Files or directories to inspect for plugin candidates.
    \param progress_callback Optional callback for countable metadata-scan progress.
    \return Discovered plugin candidates, or a typed failure when scanning itself cannot proceed.
    \note This method may be called from a non-realtime worker thread.
    */
    [[nodiscard]] virtual std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginLocations(
        const std::vector<std::filesystem::path>& roots,
        PluginCatalogScanProgressCallback progress_callback = {}) = 0;

    /*!
    \brief Returns plugin candidates already known to the host without scanning plugin binaries.

    This is a lightweight catalog read for UI presentation. It must not traverse plugin folders,
    launch plugin scanner processes, or execute third-party plugin code.

    \return Known plugin candidates currently available to add to the hosted chain.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::vector<PluginCandidate> knownPluginCatalog() const = 0;

    /*!
    \brief Inserts a previously discovered plugin candidate into the hosted chain.

    The chain index is in the user-visible chain, excluding hidden structural gain and meter
    plugins. Passing the current plugin count appends. The implementation stops and rebuilds
    backend playback graph state as needed. Insertion fails once the chain already contains
    g_max_signal_chain_plugins user plugins.

    \param plugin_candidate Candidate returned by knownPluginCatalog() or a scan method.
    \param chain_index User-visible insertion index in [0, plugin_count] before the chain is full.
    \return Authoritative post-mutation chain snapshot plus the inserted runtime ID, or failure.
    */
    [[nodiscard]] virtual std::expected<PluginInsertResult, PluginHostError> insertPlugin(
        const PluginCandidate& plugin_candidate, std::size_t chain_index) = 0;

    /*!
    \brief Moves a loaded plugin instance to a new user-visible chain index.

    The destination index describes the final plugin index after the move. Moving to the current
    index is a no-op that still returns the current authoritative snapshot.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \param destination_index Final user-visible chain index for the instance.
    \return Authoritative post-mutation chain snapshot, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
        const std::string& instance_id, std::size_t destination_index) = 0;

    /*!
    \brief Removes a loaded plugin instance from the hosted chain.

    The first implementation removes from the linear Tracktion plugin list owned by the
    instrument track. It stops and rebuilds backend playback graph state as needed.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Authoritative post-mutation chain snapshot, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
        const std::string& instance_id) = 0;

    /*!
    \brief Captures a full opaque state chunk for a loaded plugin instance.

    The returned bytes are owned by the backend contract. Editor-core may store and replay them,
    but must not inspect them or depend on their format.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Captured plugin state, or a typed failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<PluginInstanceState, PluginHostError> capturePluginState(
        const std::string& instance_id) = 0;

    /*!
    \brief Recreates a captured plugin state under its encoded runtime instance ID.

    The chain index is in the user-visible chain, excluding hidden structural gain and meter
    plugins. The original instance must already be absent from the hosted chain. Implementations
    preserve the runtime ID encoded in the state and fail if they cannot prove that identity.

    \param state Opaque plugin state previously captured from this boundary.
    \param chain_index User-visible insertion index in [0, plugin_count] before the chain is full.
    \return Authoritative post-recreate snapshot, or failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError>
    recreatePluginStatePreservingId(const PluginInstanceState& state, std::size_t chain_index) = 0;

    /*!
    \brief Restores a full opaque state chunk onto an existing plugin instance.

    Full state restore is used for user plugin edits so plugin-owned opaque state such as preset
    labels, dirty flags, and loaded file references stays consistent across undo and redo.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \param state Opaque plugin state previously captured from this boundary.
    \return Empty success, or a typed failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> setPluginState(
        const std::string& instance_id, const PluginInstanceState& state) = 0;

    /*!
    \brief Flushes pending user plugin edits into completed before/after values.

    Implementations synchronously settle eligible user plugin edits, refresh their internal
    baseline, and notify observers if aggregate pending state changes.

    \note This method must be called on the message thread.
    */
    virtual void flushPendingPluginEdits() = 0;

    /*!
    \brief Reports whether any user plugin edit is waiting to settle or flush.
    \return True while a plugin edit is pending.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual bool hasPendingPluginEdits() const = 0;

    /*!
    \brief Installs callbacks for pending user plugin edit notifications.
    \param observer Callback set replacing any previous observer.
    \note This method must be called on the message thread.
    */
    virtual void setPluginEditObserver(PluginEditObserver observer) = 0;

    /*!
    \brief Installs callbacks for completed plugin-wide state edit notifications.
    \param observer Callback set replacing any previous observer.
    \note This method must be called on the message thread.
    */
    virtual void setPluginStateEditObserver(PluginStateEditObserver observer) = 0;

    /*!
    \brief Installs callbacks for Undo/Redo shortcuts received by hosted plugin editor windows.

    The plugin host only forwards window-level shortcuts; the owning application remains
    responsible for deciding whether a command is available and how pending edits become undo
    history.

    \param observer Callback set replacing any previous observer.
    \note This method must be called on the message thread.
    */
    virtual void setPluginWindowCommandObserver(PluginWindowCommandObserver observer) = 0;

    /*!
    \brief Opens the hosted editor window for a loaded plugin instance.

    The first implementation asks Tracktion to show the plugin's native/editor component and bring
    it to the front if it is already open.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \return Empty success, or a typed failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> openPluginWindow(
        const std::string& instance_id) = 0;

protected:
    /*! \brief Creates the plugin-host interface. */
    IPluginHost() = default;

    /*! \brief Copies the plugin-host interface. */
    IPluginHost(const IPluginHost&) = default;

    /*! \brief Moves the plugin-host interface. */
    IPluginHost(IPluginHost&&) = default;

    /*!
    \brief Assigns the plugin-host interface from another interface.
    \return Reference to this plugin-host interface.
    */
    IPluginHost& operator=(const IPluginHost&) = default;

    /*!
    \brief Move-assigns the plugin-host interface from another interface.
    \return Reference to this plugin-host interface.
    */
    IPluginHost& operator=(IPluginHost&&) = default;
};

} // namespace rock_hero::common::audio
