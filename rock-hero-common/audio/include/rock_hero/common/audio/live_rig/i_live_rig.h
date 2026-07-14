/*!
\file i_live_rig.h
\brief Tracktion-free live guitar rig port.
*/

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <rock_hero/common/audio/live_rig/live_rig_error.h>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/plugin/plugin_chain_snapshot.h>
#include <rock_hero/common/audio/plugin/plugin_instance_state.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Message-thread request to capture the loaded live rig into song files.

Capture rewrites every loaded tone branch's document at that branch's own reference: any branch
can drift from its file (undo restores plugin state by instance id, plugin windows stay open
across audibility switches), so persisting only the audible branch loses edits. The editor-owned
layout vectors below describe the audible chain only; other branches persist their retained
load-time layout.
*/
struct [[nodiscard]] LiveRigCaptureRequest
{
    /*! \brief Native song workspace directory that owns package-relative tone files. */
    std::filesystem::path song_directory;

    /*! \brief Canonical package arrangement ID whose rig is being captured. */
    std::string arrangement_id;

    /*!
    \brief Opaque editor-owned visual block per user plugin, in chain order.

    The audio layer writes these through to the tone document without interpreting them; the editor
    owns their meaning and validity. Index i is the block for the i-th user plugin in the captured
    chain. Entries beyond the supplied size default to the gapless block equal to the chain
    position, so an empty vector persists a gapless layout.
    */
    std::vector<std::size_t> block_indices;

    /*!
    \brief Opaque editor-owned display type override tokens per user plugin, in chain order.

    The audio layer writes these through to the tone document without interpreting them; the editor
    owns their meaning and validity. Index i is the override for the i-th user plugin in the
    captured chain. Entries beyond the supplied size default to empty, meaning no override.
    */
    std::vector<std::string> display_type_overrides;

    /*!
    \brief Opaque editor-owned durable plugin ids per user plugin, in chain order.

    The audio layer writes these through to the tone document without interpreting them; the editor
    mints them and keys arrangement-scoped automation on them. Index i is the id for the i-th user
    plugin in the captured chain. Entries beyond the supplied size default to empty.
    */
    std::vector<std::string> stable_ids;
};

/*! \brief Result of writing the loaded live rig into song tone files. */
struct [[nodiscard]] LiveRigSnapshot
{
    /*! \brief Captured audible chain state for the editor signal-chain panel. */
    std::vector<PluginChainEntry> plugins{};

    /*! \brief Captured output gain after the signal chain. */
    Gain output_gain{};
};

/*! \brief Progress reported while restoring plugins into the live rig. */
struct [[nodiscard]] LiveRigLoadProgress
{
    /*! \brief Number of plugins fully restored into the live rig so far. */
    std::size_t completed_plugins{};

    /*! \brief Total plugin count expected for the current tone document. */
    std::size_t total_plugins{};

    /*! \brief Zero-based index of the active plugin name within the current tone document. */
    std::size_t active_plugin_index{};

    /*! \brief User-facing name of the plugin currently being restored, when known. */
    std::string active_plugin_name;
};

/*! \brief Callback used by live rig restore to report determinate plugin progress. */
using LiveRigLoadProgressCallback = std::function<void(const LiveRigLoadProgress&)>;

/*!
\brief Callback used by live rig restore to yield to the message loop between plugin steps.

The implementation should arrange for `next` to run after the message loop has had a chance to
process pending paints; this is what makes per-plugin progress updates actually visible to the
user. The editor wires this to a paint-fence helper that waits for the busy overlay to repaint
before resuming work. When unset, the engine falls back to a plain async post that does not
guarantee a paint cycle between steps.
*/
using LiveRigLoadYieldCallback = std::function<void(std::function<void()> next)>;

/*! \brief Message-thread request to restore an arrangement's tone set into the live rig. */
struct [[nodiscard]] LiveRigLoadRequest
{
    /*! \brief Native song workspace directory that owns package-relative tone files. */
    std::filesystem::path song_directory;

    /*!
    \brief Package-relative tone document paths to preload, deduplicated, in schedule order.

    Every referenced tone loads into its own always-processing branch of the multi-tone rig so
    switching between them never rebuilds plugins. An empty list clears the rig.
    */
    std::vector<std::string> tone_document_refs;

    /*!
    \brief Tone document reference that should be audible after the load.

    Must be one of tone_document_refs. This is the tone the editor's selection points at; the
    signal-chain panel binds to the same tone.
    */
    std::string audible_tone_ref;

    /*! \brief Optional callback invoked as plugin restore progress changes. */
    LiveRigLoadProgressCallback progress_callback;

    /*!
    \brief Optional callback used to yield to the message loop between plugin steps.

    When set, the engine calls this between each cooperative step and waits for the supplied
    continuation to be invoked before running the next step. Wire this to a paint-fence helper so
    pending paints actually run between steps; otherwise the engine falls back to plain async
    posts and per-step progress updates may not be visible.
    */
    LiveRigLoadYieldCallback yield_callback;
};

/*! \brief Identity of one loaded tone-chain plugin: runtime instance id plus durable stable id. */
struct [[nodiscard]] LoadedTonePluginIdentity
{
    /*! \brief Runtime plugin instance id (matches plugin chain snapshot instance ids). */
    std::string instance_id;

    /*! \brief Editor-minted durable plugin id from the tone document; empty when not yet minted. */
    std::string stable_id;
};

/*! \brief Identities of one loaded tone's chain plugins, in chain order. */
struct [[nodiscard]] LoadedToneChainIdentities
{
    /*! \brief Tone document reference identifying the loaded branch. */
    std::string tone_document_ref;

    /*! \brief Chain plugin identities in playback order. */
    std::vector<LoadedTonePluginIdentity> plugins;

    /*!
    \brief Sum of the chain plugins' self-reported latencies, in seconds, at load completion.

    With plugin-delay compensation off (the product stance), this is the real monitoring
    latency the player hears while this tone is audible. Gameplay stays silent about it
    (21-Q2); the consumer is the editor's authoring-time export warning, so a charter cannot
    ship an unintentionally high-latency tone. Accuracy depends on each plugin's own
    getLatencySeconds() report.
    */
    double summed_reported_latency_seconds{0.0};
};

/*! \brief Result of loading an arrangement's tone set into the live rig. */
struct [[nodiscard]] LiveRigLoadResult
{
    /*! \brief The audible tone's restored chain state for the editor signal-chain panel. */
    std::vector<PluginChainEntry> plugins{};

    /*! \brief The audible tone's restored output gain. */
    Gain output_gain{};

    /*!
    \brief Plugin identities for every loaded tone, in load order.

    Filled only by loadLiveRig() completions, whose identities come fresh from the parsed tone
    documents; setAudibleTone() results leave this empty because retained per-branch metadata can
    go positionally stale after chain mutations. The editor merges these into its runtime
    instance-to-stable-id association at load completion and maintains it itself afterwards.
    */
    std::vector<LoadedToneChainIdentities> tone_chains{};
};

/*!
\brief Callback invoked on the message thread once an async live rig load has fully finished.

Fires exactly once per loadLiveRig() call, after every plugin in the chain has been restored or
after the operation fails. Per-plugin updates during the load are delivered through
LiveRigLoadProgressCallback instead.
*/
using LiveRigLoadResultCallback =
    std::function<void(std::expected<LiveRigLoadResult, LiveRigError>)>;

/*!
\brief Message-thread request to export the audible tone's rig to a standalone tone file.

The exported file carries the rig only: plugin chain, full plugin state, and output gain. It
never carries automation, catalog identity, or durable plugin ids — the tone-file container
normalizes those away on write regardless of the live chain's state.
*/
struct [[nodiscard]] ToneFileExportRequest
{
    /*! \brief Absolute output path for the tone file; parent directories are created. */
    std::filesystem::path tone_file_path;

    /*!
    \brief Opaque editor-owned visual block per audible-chain plugin, in chain order.

    Written through to the exported document without interpretation, exactly like
    LiveRigCaptureRequest::block_indices. Entries beyond the supplied size default to the gapless
    block equal to the chain position.
    */
    std::vector<std::size_t> block_indices;

    /*!
    \brief Opaque editor-owned display type overrides per audible-chain plugin, in chain order.

    Written through to the exported document without interpretation. Entries beyond the supplied
    size default to empty, meaning no override.
    */
    std::vector<std::string> display_type_overrides;
};

/*!
\brief Opaque whole-chain memento of the audible tone used by editor undo.

Each plugin state is a full serialized plugin tree that keeps its runtime instance id, so a
restore recreates the prior chain with the prior instance ids and editor-side bindings stay
valid across an undo. Editor-owned layout (blocks, display overrides, durable ids) is not part
of this memento — the editor's undo entry carries it, matching the plugin-remove precedent.
*/
struct [[nodiscard]] AudibleToneState
{
    /*! \brief Full per-plugin state mementos in chain order, instance ids preserved. */
    std::vector<PluginInstanceState> plugin_states;

    /*! \brief Output gain applied after the chain. */
    Gain output_gain{};
};

/*! \brief Message-thread request to replace the audible tone's chain from a tone file. */
struct [[nodiscard]] ToneFileReplaceRequest
{
    /*! \brief Absolute path of the tone file to read. */
    std::filesystem::path tone_file_path;

    /*! \brief Optional callback invoked as plugin restore progress changes. */
    LiveRigLoadProgressCallback progress_callback;

    /*!
    \brief Optional callback used to yield to the message loop between plugin steps.

    Same contract as LiveRigLoadRequest::yield_callback: wire a paint fence so per-plugin
    progress actually paints; unset falls back to plain async posts.
    */
    LiveRigLoadYieldCallback yield_callback;
};

/*!
\brief Project-owned facade for the currently loaded playable guitar rig.

All methods are message-thread operations. Implementations may stop transport, scan plugin files,
load plugin state, and rebuild backend playback graphs; callers must never invoke this interface
from the real-time audio callback.
*/
class ILiveRig
{
public:
    /*! \brief Destroys the live rig interface. */
    virtual ~ILiveRig() = default;

    /*!
    \brief Captures the audible tone's chain into package-relative song files.

    Only the audible tone is user-editable (the panel binds to it), so non-audible tones cannot
    drift from their tone documents and never need re-capturing.

    \param request Song workspace and arrangement identity for the capture.
    \return Written tone document reference and display chain, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<LiveRigSnapshot, LiveRigError> captureActiveRig(
        const LiveRigCaptureRequest& request) = 0;

    /*!
    \brief Writes a fresh empty tone document into the song workspace.

    Creates a tone whose chain is empty and whose output gain is unity, and returns its
    package-relative reference. The tone is not loaded into the rig by this call: the caller stores
    the reference on the arrangement and reloads the rig (loadLiveRig) to give the tone its own
    branch. Minting eagerly (before the reference is stored) is required because loadLiveRig fails
    on a reference whose document file does not yet exist.

    \param song_directory Native song workspace directory that owns package-relative tone files.
    \return The new tone's package-relative document reference, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<std::string, LiveRigError> mintEmptyTone(
        const std::filesystem::path& song_directory) = 0;

    /*!
    \brief Appends an empty (passthrough) branch for a tone to the currently loaded rig.

    The fast path for creating a new tone: the branch is one internal gain plugin plus wiring, so
    the rig's existing plugins are never torn down or re-instantiated and playback never stops.
    Adding a reference whose branch is already loaded succeeds without changes. Fails when no rig
    is loaded; the caller falls back to a full loadLiveRig.

    \param tone_document_ref Tone the new branch represents (its document must already exist).
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> addEmptyToneBranch(
        const std::string& tone_document_ref) = 0;

    /*!
    \brief Loads a package-relative tone document into the active live rig chain.

    The operation runs cooperatively on the message thread: each plugin is restored in its own
    message-loop turn so the message loop can service paints and input between plugins. The
    completion callback fires on the message thread with the restored chain or a typed failure.
    Tone documents with more than g_max_signal_chain_plugins user plugins fail before plugin
    instantiation. For an empty tone document reference the completion fires immediately with an
    empty result.

    \param request Song workspace, tone document reference, and optional progress callback.
    \param completion Callback invoked once the operation finishes or fails.
    */
    virtual void loadLiveRig(LiveRigLoadRequest request, LiveRigLoadResultCallback completion) = 0;

    /*!
    \brief Clears the active live rig chain.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> clearLiveRig() = 0;

    /*!
    \brief Switches which preloaded tone is audible (and bound to the signal-chain panel).

    All tones stay loaded and processing; only branch gains move, through short click-free ramps.
    This is the selection-driven switch path; scheduled playback switching is baked separately.

    \param tone_document_ref One of the tone references supplied to the last loadLiveRig call.
    \return The now-audible tone's chain and output gain for panel rebinding, or a typed failure
            when the tone is not loaded.
    */
    [[nodiscard]] virtual std::expected<LiveRigLoadResult, LiveRigError> setAudibleTone(
        const std::string& tone_document_ref) = 0;

    /*!
    \brief Exports the audible tone's rig to a standalone tone file.

    A pure read of the live chain: no graph mutation, no transport stop, so saving a tone never
    interrupts monitoring. Works for any audible branch, including the placeholder branch of a
    tone-less rig, because the exported payload is the chain itself rather than a catalog tone.

    \param request Output path and editor-owned audible-chain layout.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> exportAudibleTone(
        const ToneFileExportRequest& request) = 0;

    /*!
    \brief Captures the audible tone's chain into an in-memory whole-chain memento.

    A pure read used by editor undo before a destructive chain replacement; instance ids are
    preserved inside the per-plugin states so restoreAudibleToneState() can rebuild the exact
    prior chain.

    \return Whole-chain memento, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<AudibleToneState, LiveRigError>
    captureAudibleToneState() = 0;

    /*!
    \brief Replaces the audible tone's chain with the contents of a standalone tone file.

    Transactional: the file is fully read and validated, and every plugin is resolved and
    instantiated, before the existing chain is touched — a failure (corrupt file, missing
    plugins) leaves the previous chain intact. Missing plugins are collected across the whole
    chain and refused once with the complete list, matching loadLiveRig's policy. Runs
    cooperatively on the message thread like loadLiveRig; the completion fires exactly once with
    the new audible chain or a typed failure. Only the audible branch changes; other loaded
    tones are untouched.

    \param request Tone file path and optional progress/yield callbacks.
    \param completion Callback invoked once the operation finishes or fails.
    */
    virtual void replaceAudibleToneFromFile(
        ToneFileReplaceRequest request, LiveRigLoadResultCallback completion) = 0;

    /*!
    \brief Restores the audible tone's chain from a memento captured by captureAudibleToneState().

    The undo-side counterpart of replaceAudibleToneFromFile(): synchronous, because editor undo
    entries apply synchronously (behind the plugin-loading busy presentation when they
    instantiate plugins). Transactional like the file replace — every plugin instantiates before
    the existing chain is touched — and restored plugins keep the instance ids recorded in the
    memento so editor-side bindings stay valid across an undo. The engine resets the restored
    chain's retained panel layout to defaults; the editor's undo entry owns the authoritative
    layout and reapplies it.

    \param state Whole-chain memento captured by captureAudibleToneState().
    \return The restored audible chain for panel rebinding, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<LiveRigLoadResult, LiveRigError> restoreAudibleToneState(
        const AudibleToneState& state) = 0;

    /*!
    \brief Reads the current output gain applied after the signal chain.
    \return Current output gain, or the default when no structural gain plugin exists.
    */
    [[nodiscard]] virtual Gain outputGain() const = 0;

    /*!
    \brief Sets the output gain applied after the signal chain.
    \param gain Desired output gain; clamped to the accepted range.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> setOutputGain(Gain gain) = 0;

protected:
    /*! \brief Creates the live rig interface. */
    ILiveRig() = default;

    /*! \brief Copies the live rig interface. */
    ILiveRig(const ILiveRig&) = default;

    /*! \brief Moves the live rig interface. */
    ILiveRig(ILiveRig&&) = default;

    /*!
    \brief Assigns the live rig interface from another interface.
    \return Reference to this live rig interface.
    */
    ILiveRig& operator=(const ILiveRig&) = default;

    /*!
    \brief Move-assigns the live rig interface from another interface.
    \return Reference to this live rig interface.
    */
    ILiveRig& operator=(ILiveRig&&) = default;
};

} // namespace rock_hero::common::audio
