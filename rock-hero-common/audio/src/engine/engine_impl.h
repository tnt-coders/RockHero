/*!
\file engine_impl.h
\brief Private declaration of Engine::Impl shared by the engine's per-port source files.

Private to rock_hero_common_audio: this header lives under src/, which is on no consumer include
path, and Impl is a private nested type of Engine, so access control makes any outside use
ill-formed even if the header text were reachable. Keep this header a declaration surface: the
only member bodies allowed here are the template method and the nested RAII guard, which the
language requires to be visible in-class; everything else stays declared-only with definitions in
the engine translation units.
*/

#pragma once

#include "clock/atomic_playback_clock.h"
#include "live_rig/tone_document.h"
#include "shared/meter_reader.h"
#include "tracktion/monitoring_mode_transition.h"
#include "tracktion/multi_tone_rack.h"
#include "tracktion/plugin_window.h"
#include "tracktion/tracktion_instrument_wave_device_mapping.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <string_view>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

class LiveRigGainPlugin;
class PluginParameterDirtyTracker;
class PluginDirtyStateTracker;

/*!
\brief Records recoverable instrument-route failures to the log.
\param message Failure detail for the log line.
*/
void logInstrumentMonitoringFailure(const juce::String& message);

/*!
\brief Maps monitoring rebuild failures into plugin-host mutation errors.
\param error Live-input failure reported by the monitoring rebuild.
\return Equivalent plugin-host error for the mutation caller.
*/
[[nodiscard]] PluginHostError pluginHostErrorFromLiveInputError(const LiveInputError& error);

// Per-call state for an in-flight async live rig load. Lives on the heap inside Engine::Impl so
// MessageManager::callAsync continuations can resume the work between plugins without each lambda
// having to carry the full state along.
struct LiveRigLoadOperation
{
    // One tone document being restored into its own rack branch.
    struct ToneLoad
    {
        // Package-relative tone document reference identifying the branch.
        std::string tone_document_ref;

        // Parsed tone document chain, already validated against package-relative path rules.
        std::vector<PluginRecord> chain;

        // Display names precomputed once so progress messages stay stable across resume points.
        std::vector<std::string> display_names;

        // Output gain from the parsed tone document, applied when this tone becomes audible.
        Gain output_gain;

        // Plugins restored so far, held free-floating until the rack assembles them.
        std::vector<tracktion::Plugin::Ptr> loaded_plugins;
    };

    // Original request, kept for the song directory, audible tone, and the progress callback.
    LiveRigLoadRequest request;

    // Every tone to preload, in request order.
    std::vector<ToneLoad> tones;

    // Position of the next plugin to load on the upcoming step.
    std::size_t next_tone{0};
    std::size_t next_plugin{0};

    // Plugins whose backend load failed because they are not installed, as "Name (tone ref)"
    // display strings. The load keeps scanning so the finalize step can refuse ONCE with the
    // complete list (gameplay policy 21-Q1(A)) instead of aborting at the first miss.
    std::vector<std::string> missing_plugin_names;

    // Flattened progress counters across all tones.
    std::size_t total_plugins{0};
    std::size_t completed_plugins{0};

    // Callback fired exactly once when the load finishes or fails.
    LiveRigLoadResultCallback on_result;
};

struct PluginChainMutationFailure
{
    PluginHostError error;
    std::string_view reroute_context;
};

// Private Tracktion/JUCE adapter state hidden behind Engine's public pimpl boundary.
struct Engine::Impl : public juce::ChangeListener, public juce::ValueTree::Listener
{
private:
    friend class Engine;

    // Tracktion runtime root that owns device and plugin infrastructure.
    std::unique_ptr<tracktion::Engine> m_engine;

    // Two-track edit used for backing playback and instrument monitoring.
    std::unique_ptr<tracktion::Edit> m_edit;

    // Stable ID for the Tracktion track that owns arrangement backing clips.
    tracktion::EditItemID m_backing_track_id;

    // Stable ID for the Tracktion track that owns instrument input and future plugin FX.
    tracktion::EditItemID m_instrument_track_id;

    // Stable IDs for structural live-rig plugins around the external plugin chain. These are
    // hidden from PluginChainEntry snapshots and from the removable plugin rows in the editor.
    tracktion::EditItemID m_input_gain_plugin_id;
    tracktion::EditItemID m_input_meter_plugin_id;
    tracktion::EditItemID m_output_gain_plugin_id;
    tracktion::EditItemID m_output_meter_plugin_id;

    // Structural master-output meter, living on the edit master plugin list rather than the
    // instrument track. It rides a stable measurer (unlike the churning EditPlaybackContext), so the
    // UI meter read never re-registers a client onto a measurer that is mid-rebuild.
    tracktion::EditItemID m_master_meter_plugin_id;

    // Meter readers registered with Tracktion measurers on demand by audioMeterSnapshot().
    mutable MeterReader m_input_meter_reader;
    mutable MeterReader m_output_meter_reader;
    mutable MeterReader m_master_meter_reader;
    mutable MeterReader m_raw_input_meter_reader;

    // RockHero-owned wait-free storage backing the IPlaybackClock port. Message-thread transport
    // operations publish boundary values through publishClockBoundary(); consumer threads only
    // ever read.
    AtomicPlaybackClock m_playback_clock;

    // Message-thread republisher that refreshes audible playback time into the clock while the
    // transport plays. Created on first boundary publish; started/stopped by boundary publishes
    // so every play/pause/stop/load path keeps its lifecycle consistent. Reset explicitly in
    // ~Engine before the edit dies because its tick dereferences m_edit.
    std::unique_ptr<juce::Timer> m_clock_republish_timer;

    // Duration of the loaded audio, used to clamp seeks and detect end-of-file.
    double m_loaded_length_seconds{0.0};

    // Port-level playback speed factor. Only 1.0 is storable until practice-speed support
    // (docs/roadmap/28-practice-mode.md) implements real time-stretch over the proxy-off backing
    // clip; the port rejects every other value with a typed error.
    double m_playback_speed{1.0};

    // Last coarse state used to suppress duplicate listener notifications.
    TransportState m_last_notified_transport_state{};

    // Explicit gate for processed live guitar monitoring. Calibration owns this gate.
    bool m_live_input_monitoring_enabled{false};

    // Explicit gate for unprocessed calibration monitoring through the backing track.
    bool m_calibration_input_monitoring_enabled{false};

    // Message-thread listener list for the project-owned ITransport listener surface.
    juce::ListenerList<ITransport::Listener> m_transport_listeners;

    // Message-thread listener list for audio-device configuration changes.
    juce::ListenerList<IAudioDeviceConfiguration::Listener> m_audio_device_listeners;

    // Observer installed by editor-core for pending user plugin edit state.
    PluginEditObserver m_plugin_edit_observer;

    // Observer installed by editor-core for completed plugin-wide state edits.
    PluginStateEditObserver m_plugin_state_edit_observer;

    // Observer installed by editor-core for Undo/Redo shortcuts from plugin editor windows.
    PluginWindowCommandObserver m_plugin_window_command_observer;

    // Per-external-plugin parameter observers for the user-visible live-rig chain.
    std::vector<std::unique_ptr<PluginParameterDirtyTracker>> m_plugin_parameter_dirty_trackers;

    // Per-external-plugin dirty-state transaction observers.
    std::vector<std::unique_ptr<PluginDirtyStateTracker>> m_plugin_state_trackers;

    // Defers undo capture during host-owned restore/load paths without hiding dirty callbacks.
    bool m_plugin_undo_capture_deferred{false};

    // Last aggregate pending state reported to the editor observer.
    bool m_plugin_edit_pending_notified{false};

    // Coalesces JUCE audio-device callbacks so Tracktion route repair runs after callback unwinds.
    bool m_audio_device_configuration_refresh_pending{false};

    // Whether the saved device route's hardware was attached at the last configuration refresh.
    // enforceNoFallbackDevicePolicy() re-applies the saved route only on an absent -> present
    // transition of this flag, so a replug reopens the user's device while deliberate closed
    // states (a staging settings edit) and failed reopen attempts never enter a retry loop.
    bool m_saved_device_present{false};

    // Alive token captured by deferred MessageManager::callAsync lambdas so they can detect
    // Engine destruction before re-entering Impl state.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // In-flight async live rig load operation, when one is running. Reset between loads so the
    // result callback may safely start a new load.
    std::unique_ptr<LiveRigLoadOperation> m_load_op;

    // Multi-tone rack holding every preloaded tone as an always-processing branch, plus the
    // track-hosted instance placed between the structural gain/meter slots.
    std::optional<ToneRack> m_tone_rack;
    tracktion::EditItemID m_tone_rack_instance_id;

    // Tone currently audible and bound to the signal-chain panel.
    std::string m_audible_tone_ref;

    // Per-branch output gains (aligned with m_tone_rack->branches) so switching the audible tone
    // restores that tone's authored output level on the structural output gain stage.
    std::vector<Gain> m_branch_output_gains;

    // Editor-owned panel layout per branch, captured at load and refreshed on capture. Chain
    // mutations can outrun it; readers fall back to a gapless layout past its size. The stable ids
    // are only trusted at load completion (LiveRigLoadResult.tone_chains), where they are fresh
    // from the parsed documents and cannot be positionally stale yet.
    struct BranchDisplayMetadata
    {
        std::vector<std::size_t> block_indices;
        std::vector<std::string> display_type_overrides;
        std::vector<std::string> stable_ids;
    };
    std::vector<BranchDisplayMetadata> m_branch_display_metadata;

    // Builds the audible tone's chain-and-gain result for load completion and audible switches.
    [[nodiscard]] LiveRigLoadResult audibleToneResult() const;

    // Returns the branch the audible tone plays through, or null when no rig is loaded.
    [[nodiscard]] ToneRackBranch* audibleToneBranch();
    [[nodiscard]] const ToneRackBranch* audibleToneBranch() const;

    // Returns the audible tone's branch index within the rack, when a rig is loaded.
    [[nodiscard]] std::optional<std::size_t> audibleBranchIndex() const;

    // Switches branch gains and the structural output gain to the given loaded tone.
    [[nodiscard]] std::expected<void, LiveRigError> applyAudibleTone(
        const std::string& tone_document_ref);

    // Tears down the multi-tone rack state (instance removal is the track-plugin sweep's job).
    void resetToneRackState();

    // Starts the next plugin step: completes the load if all plugins are restored, otherwise
    // reports "Loading X" progress and yields before the heavy plugin construction.
    void beginNextPluginStep();

    // Performs the heavy plugin construction for the current step, reports "Loaded X" progress,
    // and yields before beginning the next plugin step.
    void executePluginStep();

    // Assembles the multi-tone rack from the loaded chains, places its instance on the track,
    // applies the audible tone, and delivers the audible tone's chain to the caller.
    void finalizeLiveRigLoad();

    // Hands the supplied continuation to the request's yield callback when one is provided so a
    // paint cycle can run between cooperative steps; falls back to a plain async post otherwise.
    void yieldThenContinue(std::function<void()> next);

    // Aborts the in-flight live rig load with the supplied error, clearing the half-built chain
    // and rebuilding the instrument monitoring graph before invoking the completion callback.
    void abortLiveRigLoad(LiveRigError error);

    // Derives the current coarse transport state directly from Tracktion state.
    [[nodiscard]] TransportState currentTransportState() const noexcept;

    // Publishes a message-thread boundary value into the playback clock: the given position, a
    // fresh steady-clock capture stamp, and the current coarse playing flag. Also manages the
    // playback republish timer so it runs exactly while the transport plays.
    void publishClockBoundary(common::core::TimePosition position);

    // Republishes the current audible playback time into the clock with a fresh capture stamp.
    // Same lifetime-safe read Engine::position() performs; called by the republish timer at
    // render-adjacent cadence while playing.
    void publishAudibleTimeNow();

    // Creates the edit and gives its two audio tracks explicit product roles.
    void createEdit();

    // Derives coarse transport state from Tracktion and notifies listeners when it changes.
    void updateTransportState();

    // Mirrors Tracktion transport and audio-device broadcasts into the project-owned surfaces.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Schedules device-change repair outside JUCE's AudioDeviceManager callback stack.
    void scheduleAudioDeviceConfigurationRefresh();

    // Repairs Tracktion's device cache and notifies editor listeners after JUCE has changed routes.
    void handleAudioDeviceConfigurationRefresh();

    // Undoes JUCE's disconnect fallback (closing the substitute device) and re-applies the saved
    // route when its device is replugged, so the open device is only ever the user's choice.
    void enforceNoFallbackDevicePolicy();

    // Tracktion publishes playhead movement through the transport ValueTree. The coarse state
    // surface ignores ordinary movement, but this hook still detects automatic end-of-file stops.
    void valueTreePropertyChanged(
        juce::ValueTree& /*tree*/, const juce::Identifier& property) override;

    // Keeps externally requested positions inside the current loaded file duration.
    [[nodiscard]] double clampToLoadedRange(double seconds) const noexcept;

    // Returns the Tracktion audio track that owns backing arrangement clips.
    [[nodiscard]] tracktion::AudioTrack* backingTrack() const;

    // Returns the Tracktion audio track that receives the selected instrument input.
    [[nodiscard]] tracktion::AudioTrack* instrumentTrack() const;

    // Looks up a previously scanned plugin candidate without exposing JUCE descriptions publicly.
    [[nodiscard]] std::unique_ptr<juce::PluginDescription> findKnownPlugin(
        const std::string& plugin_id) const;

    // Scans one selected plugin file through Tracktion's JUCE-backed known-plugin list. This is
    // used by lazy browser adds and message-thread live-rig restore; callers must keep it off the
    // realtime audio thread and avoid concurrent access to Tracktion's known-plugin list.
    [[nodiscard]] std::expected<std::vector<PluginCandidate>, PluginHostError>
    scanPluginFileForCandidates(const std::filesystem::path& plugin_path);

    [[nodiscard]] juce::AudioPluginFormat* vst3PluginFormat() const;

    [[nodiscard]] juce::File pluginScanDeadMansPedalFile() const;

    [[nodiscard]] static juce::FileSearchPath pluginSearchPathFromRoots(
        const std::vector<std::filesystem::path>& roots);

    [[nodiscard]] static std::filesystem::path pluginPathFromIdentifier(
        const juce::String& file_or_identifier);

    [[nodiscard]] std::expected<juce::StringArray, PluginHostError> scanVst3SearchPath(
        juce::FileSearchPath search_path,
        const PluginCatalogScanProgressCallback& progress_callback,
        const common::core::CancellationToken& cancel = {});

    // Exposes Tracktion's in-memory known-plugin list without triggering filesystem scans.
    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalog() const;

    [[nodiscard]] std::vector<PluginCandidate> knownPluginCatalogForScannedFiles(
        const juce::StringArray& scanned_files) const;

    // Inserts a selected plugin candidate into the instrument track's user-visible chain.
    [[nodiscard]] std::expected<PluginInsertResult, PluginHostError> insertPluginCandidateToTrack(
        const PluginCandidate& plugin_candidate, std::size_t chain_index);

    // Builds the authoritative user-visible plugin chain snapshot from Tracktion state.
    [[nodiscard]] PluginChainSnapshot pluginChainSnapshot() const;

    // Counts user-visible plugins while optionally ignoring a plugin being moved.
    [[nodiscard]] std::size_t userVisiblePluginCount(
        const tracktion::Plugin* ignored_plugin = nullptr) const;

    // Finds one plugin's current user-visible index, excluding structural live-rig plugins.
    [[nodiscard]] std::optional<std::size_t> userVisiblePluginIndexOf(
        const tracktion::Plugin* target_plugin) const;

    // Checks the current known-plugin list for a plugin matching persisted identity.
    [[nodiscard]] bool hasKnownPluginForIdentity(const PluginIdentity& identity) const;

    // Ensures a persisted plugin can be resolved before creating Tracktion state for it.
    [[nodiscard]] std::expected<void, LiveRigError> ensureKnownPluginForIdentity(
        const PluginIdentity& identity);

    // Finds a loaded instrument-chain plugin by the opaque instance ID returned to callers.
    [[nodiscard]] tracktion::Plugin* findInstrumentPluginInstance(
        const std::string& instance_id) const;

    // Reports whether the given plugin is one of the structural live-rig plugins managed here.
    [[nodiscard]] bool isStructuralLiveRigPlugin(const tracktion::Plugin* plugin) const;

    // Finalizes a committed plugin removal after rollback is no longer possible.
    void commitPluginRemoval(tracktion::Plugin& plugin) const;

    // Reports whether any tracked plugin currently holds an unsettled user edit.
    [[nodiscard]] bool hasPendingPluginEdits() const;

    // Sends aggregate pending-state changes to editor-core.
    void notifyPluginEditPendingStateChanged();

    [[nodiscard]] bool shouldDeferPluginUndoCapture() const;

    void clearPluginUndoCaptureDeferral();

    // Arms plugin-undo capture deferral for a rig operation that will rebuild the monitoring
    // graph, so the rebuild's parameter churn is not recorded as undo entries.
    void beginPluginUndoCaptureDeferral();

    // Ends an active capture deferral and reinstalls the plugin edit observers. Call it once a rig
    // operation has finished rebuilding the monitoring graph. The rebuild's asynchronous
    // post-rebuild re-announce carries no parameter gesture, so the observers' intent gate folds it
    // and no timing window is needed here.
    void endPluginUndoCaptureDeferral();

    // RAII guard for synchronous rig capture operations that rebuild the monitoring graph.
    // While alive it defers plugin-undo capture so the rebuild's parameter re-announce is not
    // recorded as undo entries; on scope exit it clears the deferral and reinstalls the plugin
    // edit observers.
    class [[nodiscard]] ScopedPluginUndoCaptureDeferral
    {
    public:
        explicit ScopedPluginUndoCaptureDeferral(Impl& impl)
            : m_impl(impl)
        {
            m_impl.beginPluginUndoCaptureDeferral();
        }

        ~ScopedPluginUndoCaptureDeferral()
        {
            // Reinstalling the observers logs through Quill, which can throw when its queue is
            // saturated; a destructor must not let that escape and nothing can safely log here.
            try
            {
                m_impl.endPluginUndoCaptureDeferral();
            }
            catch (...)
            {
                m_impl.clearPluginUndoCaptureDeferral();
            }
        }

        ScopedPluginUndoCaptureDeferral(const ScopedPluginUndoCaptureDeferral&) = delete;
        ScopedPluginUndoCaptureDeferral& operator=(const ScopedPluginUndoCaptureDeferral&) = delete;
        ScopedPluginUndoCaptureDeferral(ScopedPluginUndoCaptureDeferral&&) = delete;
        ScopedPluginUndoCaptureDeferral& operator=(ScopedPluginUndoCaptureDeferral&&) = delete;

    private:
        Impl& m_impl;
    };

    // Emits a completed plugin-wide state edit to editor-core unless the engine is restoring state.
    void emitPluginStateEdit(PluginStateEdit edit);

    // Routes hosted plugin-window shortcuts to the current app-level command observer.
    void dispatchPluginWindowCommand(PluginWindowCommand command);

    // Detaches all Tracktion plugin edit listeners and reports any aggregate pending transition.
    void clearPluginEditObservers();

    struct KnownPluginBaseline
    {
        std::string instance_id;
        PluginInstanceState state;
    };

    // Rebuilds plugin edit listeners for user-visible external plugins only.
    void refreshPluginEditObservers(
        std::optional<KnownPluginBaseline> known_baseline = std::nullopt);

    // Retargets dirty tracking after an in-place state restore without re-reading live plugin
    // state. The restored memento is already the correct baseline, and forcing a capture here can
    // block playback on plugins that serialize slowly.
    void refreshRestoredPluginEditObserver(
        const std::string& instance_id, PluginInstanceState restored_state);

    // Settles all pending plugin edits synchronously.
    //
    // Invariant: this re-enters editor-core synchronously through the completed-edit observer.
    // That is safe only because render (updateView) never synchronously mutates the plugin chain;
    // a chain mutation here would call clearPluginEditObservers()/refreshPluginEditObservers() and
    // invalidate this loop's iterator. Any render-triggered chain change must be enqueued for a
    // later dispatch, not applied under this loop.
    void flushPendingPluginEdits();

    // Keeps user-plugin mutation, monitoring re-route, and failure routing in one path.
    template <typename Mutate, typename Rollback>
    [[nodiscard]] std::expected<void, PluginHostError> mutateAndReroutePluginChain(
        const Mutate& mutate, const Rollback& rollback, std::string_view route_rollback_context)
    {
        const bool was_playing = m_edit != nullptr && m_edit->getTransport().isPlaying();
        RH_LOG_INFO("audio.engine", "Plugin-chain mutation started playing_before={}", was_playing);
        clearPluginEditObservers();

        auto mutation_result = mutate();
        if (!mutation_result.has_value())
        {
            PluginChainMutationFailure failure = std::move(mutation_result.error());
            rebuildInstrumentMonitoringGraphBestEffort(failure.reroute_context);
            refreshPluginEditObservers();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin-chain mutation failed playing_after={}",
                m_edit != nullptr && m_edit->getTransport().isPlaying());
            return std::unexpected{std::move(failure.error)};
        }

        auto route_result = rebuildInstrumentMonitoringGraph();
        if (route_result.has_value())
        {
            refreshPluginEditObservers();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin-chain mutation completed playing_after={}",
                m_edit != nullptr && m_edit->getTransport().isPlaying());
            return {};
        }

        rollback();
        rebuildInstrumentMonitoringGraphBestEffort(route_rollback_context);
        refreshPluginEditObservers();
        RH_LOG_INFO(
            "audio.engine",
            "Plugin-chain mutation rolled back playing_after={}",
            m_edit != nullptr && m_edit->getTransport().isPlaying());
        return std::unexpected{pluginHostErrorFromLiveInputError(route_result.error())};
    }

    // Finds a structural live-rig gain plugin by its stored EditItemID, or null if absent.
    [[nodiscard]] LiveRigGainPlugin* findStructuralGainPlugin(
        tracktion::EditItemID plugin_id) const;

    // Finds a structural LevelMeterPlugin by its stored EditItemID within a plugin list, or null.
    [[nodiscard]] static tracktion::LevelMeterPlugin* findLevelMeter(
        tracktion::PluginList& list, tracktion::EditItemID plugin_id);

    // Finds the input/output structural LevelMeterPlugin on the instrument track, or null.
    [[nodiscard]] tracktion::LevelMeterPlugin* findStructuralMeterPlugin(
        tracktion::EditItemID plugin_id) const;

    // Finds the master-output structural LevelMeterPlugin on the edit master plugin list, or null.
    [[nodiscard]] tracktion::LevelMeterPlugin* findStructuralMasterMeterPlugin(
        tracktion::EditItemID plugin_id) const;

    // Creates a hidden live-rig gain plugin on the instrument track at the given index.
    [[nodiscard]] std::expected<LiveRigGainPlugin*, LiveRigError> createLiveRigGainPlugin(
        int insert_index);

    // Creates and inserts a hidden structural LevelMeterPlugin at a slot in the given plugin list.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError> createLevelMeter(
        tracktion::PluginList& list, int insert_index);

    // Creates the input/output LevelMeterPlugin on the instrument track at the given slot.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError> createLevelMeterPlugin(
        int insert_index);

    // Creates the master-output LevelMeterPlugin at the end of the edit master plugin list. Unlike
    // EditPlaybackContext::masterLevels, this measurer is not torn down when a plugin reconfigure
    // rebuilds the playback graph, so the UI meter read stays a no-op re-attach.
    [[nodiscard]] std::expected<tracktion::LevelMeterPlugin*, LiveRigError>
    createMasterLevelMeterPlugin();

    // Attaches a meter reader to a level meter's measurer, or detaches it when the meter is absent.
    static void attachMeterReader(MeterReader& reader, tracktion::LevelMeterPlugin* meter);

    // Detaches a meter reader and clears the retained peak window on its plugin's measurer.
    static void detachAndClearMeter(MeterReader& reader, tracktion::LevelMeterPlugin* meter);

    // Validates that the hidden live-rig plugins exist at their fixed measurement points.
    [[nodiscard]] std::expected<void, LiveRigError> validateStructuralLiveRigPlugins() const;

    // Creates the fixed hidden gain and meter plugins around the user-visible live-rig chain.
    [[nodiscard]] std::expected<void, LiveRigError> createStructuralLiveRigPlugins();

    // Removes only user-visible plugins, preserving fixed structural gain and meter anchors.
    [[nodiscard]] std::expected<void, LiveRigError> clearUserLiveRigPlugins();

    // Clears live-rig meter windows retained by structural meter plugins across project changes.
    void clearRetainedLiveRigMeterState();

    // Resets project-owned live-rig structural state while preserving device input calibration.
    [[nodiscard]] std::expected<void, LiveRigError> resetLiveRigProjectState();

    // Reads the dB value from a structural live-rig gain plugin, returning default if absent.
    [[nodiscard]] Gain readGainFromPlugin(tracktion::EditItemID plugin_id) const;

    // Applies a gain value to a structural live-rig gain plugin.
    [[nodiscard]] std::expected<void, LiveRigError> applyGainToPlugin(
        tracktion::EditItemID plugin_id, Gain gain);

    // Detects the moment Tracktion playback has reached or passed the loaded audio duration.
    [[nodiscard]] bool loadedAudioEndReached(double position_seconds) const;

    // Tracktion's stop(false, false) halts playback but leaves the playhead where it is.
    void stopTracktionPlayback();

    // Pauses Rock Hero playback without resetting the transport position.
    void pauseTransport();

    // Stops Tracktion and tears down the active playback graph for graph mutation or shutdown.
    void stopTransportAndReleaseContext();

    // Removes live input assignments from Rock Hero's monitoring target tracks.
    void clearInstrumentInputAssignments();

    // Removes stale monitoring assignments from Tracktion's active input instances.
    void detachInstrumentMonitoringRoute();

    // Detaches any previous route before surfacing why the new route cannot be armed.
    [[nodiscard]] LiveInputError failInstrumentMonitoringRoute(const juce::String& reason);

    // Finds the generated Tracktion wave input that corresponds to the selected JUCE mono input.
    [[nodiscard]] tracktion::WaveInputDevice* findInstrumentWaveInput(
        const InstrumentWaveDescription& description) const;

    // Finds the current mono instrument input device for raw route metering.
    [[nodiscard]] tracktion::WaveInputDevice* currentInstrumentWaveInput() const;

    // Binds the selected app-local mono input to the active Tracktion monitoring target.
    std::expected<void, LiveInputError> applyInstrumentMonitoringRoute();

    // Applies Rock Hero Stop-button semantics: halt playback and reset to timeline start.
    void stopTransport();

    // Disengages transport looping AND zeroes the stored backend loop points. The two must move
    // together: loop state persists in the edit's TRANSPORT tree, so a flag-only reset would let
    // a stale region resurrect on a later engage or leak into another arrangement. Shared by
    // clearLoopRegion() and arrangement activation.
    void disengageLoop();

    // Restores the instrument monitoring context after plugin-list graph mutation or failed
    // insertion.
    std::expected<void, LiveInputError> rebuildInstrumentMonitoringGraph();

    // Cleanup and rollback paths cannot replace their primary failure with monitoring cleanup
    // detail, so route failures are logged through this named best-effort helper.
    void rebuildInstrumentMonitoringGraphBestEffort(std::string_view context);

    // Centralizes the shared "adopt the requested monitoring flags, reroute, and roll back to off
    // on route failure" path for the two monitoring toggles, which were otherwise identical apart
    // from the channel and the rollback context. The mutual-exclusion and no-input-device policy
    // lives in the pure, device-free monitoringFlagsForRequest(); this method owns only the side
    // effects. Keeps the existing synchronous LiveInputError contract; callers supply device
    // availability because that check lives on Engine, not Impl.
    [[nodiscard]] std::expected<void, LiveInputError> setMonitoringChannelEnabled(
        MonitorChannel channel, bool enabled, bool input_device_available,
        std::string_view rollback_context);

    // Connects meter readers to their structural measurers and returns one display snapshot. All
    // three meters ride stable structural LevelMeterPlugins (the master deliberately does not use the
    // churning EditPlaybackContext::masterLevels), so each attach() is a no-op once registered and the
    // read never re-registers a client onto a measurer a plugin reconfigure is mid-rebuild.
    [[nodiscard]] AudioMeterSnapshot audioMeterSnapshot() const;

    // Reads the hardware input meter before the live-rig monitoring gate.
    [[nodiscard]] AudioMeterLevel rawInputMeterLevel() const;
};

} // namespace rock_hero::common::audio
