/*!
\file i_plugin_host.h
\brief Tracktion-free plugin-host port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <rock_hero/common/audio/plugin_catalog_scan_progress.h>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/audio/plugin_chain_snapshot.h>
#include <rock_hero/common/audio/plugin_host_error.h>
#include <rock_hero/common/audio/plugin_instance_state.h>
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

/*! \brief Full before/after state for one settled user plugin-parameter edit. */
struct [[nodiscard]] PluginParameterEdit
{
    /*! \brief Runtime plugin instance whose state changed. */
    std::string instance_id;

    /*! \brief Full opaque chunk captured before the settled edit. */
    PluginInstanceState before;

    /*! \brief Full opaque chunk captured after the settled edit. */
    PluginInstanceState after;

    /*! \brief Display-only changed parameter name hint; never used for restore identity. */
    std::string label_hint;

    /*!
    \brief Compares two parameter edits by their stored values.
    \param lhs Left-hand parameter edit.
    \param rhs Right-hand parameter edit.
    \return True when both edits store equal values.
    */
    friend bool operator==(const PluginParameterEdit& lhs, const PluginParameterEdit& rhs) =
        default;
};

/*! \brief Observer callbacks for pending and completed user plugin-parameter edits. */
struct PluginParameterEditObserver
{
    /*! \brief Called when aggregate pending plugin-parameter edit state changes. */
    std::function<void(bool)> pending_changed;

    /*! \brief Called when a settled edit yields a full before/after memento pair. */
    std::function<void(PluginParameterEdit)> edit_completed;
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
    max_signal_chain_plugins user plugins.

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

    This is the parameter-undo path. Implementations must drive the live processor state setter,
    not merely mutate a serialized tree that would leave the running plugin unchanged.

    \param instance_id Opaque instance ID returned in a plugin chain snapshot.
    \param state Opaque plugin state previously captured from this boundary.
    \return Empty success, or a typed failure.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual std::expected<void, PluginHostError> setPluginState(
        const std::string& instance_id, const PluginInstanceState& state) = 0;

    /*!
    \brief Flushes pending user plugin-parameter edits into completed before/after chunks.

    Implementations synchronously settle eligible discrete edits, drop uncertain continuous edits,
    refresh their internal baseline, and notify the observer if aggregate pending state changes.

    \note This method must be called on the message thread.
    */
    virtual void flushPendingPluginParameterEdits() = 0;

    /*!
    \brief Reports whether any user plugin-parameter edit is waiting to settle or flush.
    \return True while a plugin-parameter edit is pending.
    \note This method must be called on the message thread.
    */
    [[nodiscard]] virtual bool hasPendingPluginParameterEdits() const = 0;

    /*!
    \brief Installs callbacks for pending and completed user plugin-parameter edit notifications.
    \param observer Callback set replacing any previous observer.
    \note This method must be called on the message thread.
    */
    virtual void setPluginParameterEditObserver(PluginParameterEditObserver observer) = 0;

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
