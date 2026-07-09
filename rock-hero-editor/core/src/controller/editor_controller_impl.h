/*!
\file editor_controller_impl.h
\brief Private declaration of EditorController::Impl shared by its per-feature source files.

Private to rock_hero_editor_core: this header lives under src/, which is on no consumer include
path, and Impl is a private nested type of EditorController, so access control makes any outside
use ill-formed even if the header text were reachable. It exists so the controller's member
function definitions can be distributed across per-feature translation units while the state
stays declared exactly once. Keep this header a declaration surface: no logic, no shared helper
definitions, no state added just to make a translation-unit split work.
*/

#pragma once

#include "busy/busy_operation_workflow.h"
#include "deferred_project_action_state.h"
#include "editor_action.h"
#include "editor_action_availability.h"
#include "editor_undo_history.h"
#include "input_calibration/input_calibration_workflow.h"
#include "project/project_io.h"
#include "signal_chain/plugin_catalog_workflow.h"
#include "signal_chain/signal_chain_workflow.h"
#include "tone/tone_automation_projection.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/controller/editor_controller.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_view.h>
#include <rock_hero/editor/core/project/project.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Distinguishes live slider audition from a value that should enter undo history. */
enum class OutputGainChangeIntent : std::uint8_t
{
    /*! \brief Drag-scoped audition value; applied live but never recorded. */
    Preview,

    /*! \brief Final value; applied and recorded as an undo entry. */
    Commit,
};

/*! \brief Outcome of undoing a just-completed insert whose undo entry could not be prepared. */
enum class InsertUndoPreparationRollbackStatus : std::uint8_t
{
    /*! \brief The inserted plugin was removed again; the session stays trusted. */
    RolledBack,

    /*! \brief The rollback failed but the chain state is still known. */
    Failed,

    /*! \brief The rollback left the backend unproven; the session must fault. */
    RollbackContractViolation,
};

// Carries the rollback outcome and detail text for final user-facing/reporting decisions.
struct InsertUndoPreparationRollbackResult
{
    InsertUndoPreparationRollbackStatus status{InsertUndoPreparationRollbackStatus::RolledBack};
    std::string detail;
};

// Owns every implementation detail that does not need to be part of the public controller type.
struct EditorController::Impl final : private common::audio::ITransport::Listener,
                                      private common::audio::IAudioDeviceConfiguration::Listener
{
    struct OpenTaskState;
    struct ImportTaskState;
    struct InsertSelectedPluginTaskState;
    struct PluginCatalogTaskState;
    struct ProjectWriteTaskState;
    struct ProjectLoadLiveRigStage;

    Impl(
        common::audio::ITransport& transport, common::audio::ISongAudio& song_audio,
        common::audio::IAudioDeviceConfiguration& audio_devices,
        common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
        common::audio::IToneAutomation& tone_automation, common::audio::ILiveInput& live_input,
        EditorController::Services services, EditorController::ExitFunction exit_function,
        EditorController::ProjectOperations project_operations);
    ~Impl() override;

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void attachView(IEditorView& view);
    [[nodiscard]] const common::core::Session& session() const noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> currentProjectFile() const;
    void restoreLastOpenProject();
    void onOpenRequested(std::filesystem::path file);
    void onImportRequested(std::filesystem::path file);
    void onSaveRequested();
    void onSaveAsRequested(std::filesystem::path file);
    void onPublishRequested(std::filesystem::path file);
    void onSaveAsCancelled();
    void onBusyCancelRequested();
    void onUndoRequested();
    void onRedoRequested();
    void onCloseRequested();
    void onExitRequested();
    void onUnsavedChangesDecision(UnsavedChangesDecision decision);
    void onRestoreInterruptedDecision(RestoreInterruptedDecision decision);
    void onPlayPausePressed();
    void onStopPressed();
    void onTimelineSeekRequested(common::core::TimePosition position);
    void onGridNoteValueChangeRequested(common::core::Fraction note_value);
    void onTimelineZoomChanged(double pixels_per_second);
    void onWaveformVisibleChangeRequested(bool visible);
    void onTabMinimumDisplayedStringsChangeRequested(int minimum_strings);
    void onArrangementSelected(std::string arrangement_id);
    [[nodiscard]] std::string toneRegionIdAt(common::core::TimePosition position) const;
    [[nodiscard]] std::string activeToneRegionId() const;
    [[nodiscard]] std::string activeToneDocumentRef() const;
    [[nodiscard]] std::string activeToneName() const;
    void mergeToneChainIdentities(
        const std::vector<common::audio::LoadedToneChainIdentities>& tone_chains);
    void rebuildToneAutomationCurves();
    [[nodiscard]] std::vector<std::string> captureStableIds();
    [[nodiscard]] std::string automationParameterName(
        const std::string& tone_document_ref, const std::string& instance_id,
        const std::string& param_id) const;
    void applyToneSelection(std::string region_id);
    void activateToneAtCursor();
    void syncAudibleTone();
    void onToneRegionSelected(std::string region_id);
    void onToneRegionActivated();
    void onToneRegionResizeRequested(
        std::string region_id, common::core::GridPosition start, common::core::GridPosition end);
    void onToneRegionCreateRequested(
        common::core::GridPosition position, std::string new_region_id,
        std::string tone_document_ref);
    void onToneRegionDeleteRequested(std::string region_id);
    void onToneRenameRequested(std::string tone_document_ref, std::string name);
    void onToneBoundaryMoveRequested(
        std::string right_region_id, common::core::GridPosition position);
    void onToneCreateNewRequested(common::core::GridPosition position, std::string name);
    void onToneAutomationLaneAddRequested(const std::string& instance_id, std::string param_id);
    void onToneAutomationLaneRemoveRequested(
        const std::string& instance_id, const std::string& param_id);
    void onSetToneAutomationPoints(
        std::string instance_id, std::string param_id,
        std::vector<common::core::ToneAutomationPoint> points);
    void onPluginBrowserRequested();
    void onPluginInsertSlotSelected(std::size_t chain_index, std::size_t block_index);
    void onPluginBrowserClosed();
    void onPluginCatalogScanRequested();
    void onSelectedPluginInsertRequested(std::string plugin_id);
    void onRemovePluginRequested(std::string instance_id);
    void onMovePluginRequested(
        std::string instance_id, std::size_t destination_index,
        std::vector<PluginBlockAssignment> placement);
    void onSignalChainPlacementChanged(std::vector<PluginBlockAssignment> placement);
    void onPluginDisplayTypeOverrideChanged(
        std::string instance_id, std::optional<PluginDisplayType> display_type);
    void onOpenPluginRequested(std::string instance_id);
    void onInputCalibrationRequested();
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted();
    void onInputCalibrationMeasurementCancelled();
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db);
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db);
    void onInputCalibrationDismissed();
    void onOutputGainPreviewChanged(double gain_db);
    void onOutputGainChanged(double gain_db);
    void onAudioDeviceChangeRequested(
        std::function<void()> change_audio_device, std::function<void()> after_busy_cleared);
    [[nodiscard]] bool onAudioDeviceSettingsOpenRequested();
    void onAudioDeviceSettingsClosed();
    void onTransportStateChanged(common::audio::TransportState state) override;
    void onAudioDeviceConfigurationChanged() override;

    void runAction(EditorAction::Action action);
    [[nodiscard]] bool prepareAction(EditorAction::Id action);
    void performAction(EditorAction::Action action);
    void performActionImpl(EditorAction::OpenProject action);
    void performActionImpl(EditorAction::RestoreProject action);
    void performActionImpl(EditorAction::ImportSong action);
    void performActionImpl(EditorAction::SaveProject action);
    void performActionImpl(EditorAction::SaveProjectAs action);
    void performActionImpl(EditorAction::PublishProject action);
    void performActionImpl(EditorAction::CloseProject action);
    void performActionImpl(EditorAction::ExitApplication action);
    void performActionImpl(EditorAction::ResolveUnsavedChangesPrompt action);
    void performActionImpl(EditorAction::CancelSaveAsPrompt action);
    void performActionImpl(EditorAction::CancelBusyOperation action);
    void performActionImpl(EditorAction::Undo action);
    void performActionImpl(EditorAction::Redo action);
    void performActionImpl(EditorAction::PlayPause action);
    void performActionImpl(EditorAction::Stop action);
    void performActionImpl(EditorAction::SeekTimeline action);
    void performActionImpl(EditorAction::SetGridNoteValue action);
    void performActionImpl(const EditorAction::SelectArrangement& action);
    void performActionImpl(EditorAction::SelectToneRegion action);
    void performActionImpl(const EditorAction::ResizeToneRegion& action);
    void performActionImpl(const EditorAction::CreateToneRegion& action);
    void performActionImpl(const EditorAction::DeleteToneRegion& action);
    void performActionImpl(const EditorAction::RenameTone& action);
    void performActionImpl(const EditorAction::MoveToneBoundary& action);
    void performActionImpl(const EditorAction::CreateNewTone& action);
    void performActionImpl(const EditorAction::SetToneAutomationPoints& action);
    void performActionImpl(EditorAction::ShowPluginBrowser action);
    void performActionImpl(EditorAction::BeginPluginInsert action);
    void performActionImpl(EditorAction::ScanPluginCatalog action);
    void performActionImpl(const EditorAction::InsertSelectedPlugin& action);
    void performActionImpl(const EditorAction::RemovePlugin& action);
    void performActionImpl(const EditorAction::MovePlugin& action);
    void performActionImpl(const EditorAction::SetSignalChainPlacement& action);
    void performActionImpl(const EditorAction::SetPluginDisplayTypeOverride& action);
    void performActionImpl(const EditorAction::OpenPlugin& action);
    [[nodiscard]] EditorEditContext editContext() noexcept;
    void pushUndoEntry(std::unique_ptr<IEdit> edit);
    void pushOutputGainUndoEntry(common::audio::Gain before_gain, common::audio::Gain after_gain);
    void enterFaultedSession();
    void faultSessionAfterRollbackContractViolation(
        std::string_view context, const EditorUndoPendingTransition& pending);
    void faultSessionAfterRollbackContractViolation(
        std::string_view context, std::string_view detail);
    void dispatchUndoTransition(const EditorUndoBeginResult& begin);
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPendingEdit(
        const EditorUndoPendingTransition& pending);
    void abortUndoTransition(
        const EditorUndoPendingTransition& pending, EditorUndoFailureCode failure_code);
    void completeUndoTransition(
        const EditorUndoPendingTransition& pending,
        std::expected<void, EditorUndoFailureCode> applied);
    void applyUndoTransitionBehindBusy(const EditorUndoPendingTransition& pending);
    void applyOutputGainChange(double gain_db, OutputGainChangeIntent intent);
    void resetUndoHistory(std::string_view context);
    void markUndoHistoryClean(std::string_view context);
    void markUntrackedUnsavedChanges() noexcept;
    void markUntrackedUnsavedEdit(std::string_view context);
    void clearUndoHistoryAfterUntrackedEdit(std::string_view context);
    void flushPendingPluginEdits(std::string_view context);
    void onPluginEditPendingChanged(bool pending);
    void onPluginStateEditCompleted(common::audio::PluginStateEdit edit);
    [[nodiscard]] ActionConditions currentActionConditions() const;
    [[nodiscard]] ActionConditions currentActionConditions(
        const InputCalibrationWorkflow::Snapshot& input_calibration,
        const common::audio::TransportState& transport_state) const;

    void requestProjectAction(EditorAction::ProjectAction action);
    void runProjectAction(EditorAction::ProjectAction action);
    void runProjectActionImpl(const EditorAction::OpenProject& action);
    void runProjectActionImpl(const EditorAction::RestoreProject& action);
    void runProjectActionImpl(const EditorAction::ImportSong& action);
    void runProjectActionImpl(EditorAction::SaveProject action);
    void runProjectActionImpl(EditorAction::SaveProjectAs action);
    void runProjectActionImpl(EditorAction::PublishProject action);
    void runProjectActionImpl(EditorAction::CloseProject action);
    void runProjectActionImpl(EditorAction::ExitApplication action);
    void openProject(const std::filesystem::path& file, bool clear_last_open_project_on_failure);
    void completeOpenProject(const std::shared_ptr<OpenTaskState>& state);
    void finishOpenProjectAfterLiveRigLoad(
        const std::shared_ptr<OpenTaskState>& state,
        std::expected<void, common::audio::LiveRigError> rig_result);
    void importSongSource(const std::filesystem::path& file);
    void completeImportSongSource(const std::shared_ptr<ImportTaskState>& state);
    void finishImportSongSourceAfterLiveRigLoad(
        const std::shared_ptr<ImportTaskState>& state,
        std::expected<void, common::audio::LiveRigError> rig_result);
    void cancelBusyOperation();
    void cancelPluginCatalogScan();
    void cancelActiveScanToken();
    [[nodiscard]] EditorController::ProjectOperationProgress makeBusyProjectOperationProgress(
        std::uint64_t token);
    [[nodiscard]] common::audio::PluginCatalogScanProgressCallback makePluginCatalogScanProgress(
        std::uint64_t token);
    void completeSelectedPluginInsert(const std::shared_ptr<InsertSelectedPluginTaskState>& state);
    [[nodiscard]] InsertUndoPreparationRollbackResult
    rollbackInsertedPluginAfterUndoPreparationFailure(
        const std::string& inserted_instance_id,
        const std::vector<PluginBlockAssignment>& before_placement);
    void beginInsertKnownPlugin(
        const common::audio::PluginCandidate& plugin_candidate, std::size_t chain_index);
    void applySignalChainMutationSnapshot(const common::audio::PluginChainSnapshot& snapshot);
    void completePluginCatalogScan(const std::shared_ptr<PluginCatalogTaskState>& state);
    void refreshKnownPluginCatalog();
    [[nodiscard]] bool closeProject();
    [[nodiscard]] std::shared_ptr<ProjectWriteTaskState> takeProjectForWrite(
        EditorAction::ProjectWriteAction action);
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> captureLiveRigToDisk(
        const Project& project);
    void runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state);
    void startLiveRigLoadStage(ProjectLoadLiveRigStage stage_state, bool report_progress);
    void restoreLiveRig(
        const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
        std::function<
            void(std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>)>
            on_loaded);
    void clearLiveRig();
    [[nodiscard]] std::filesystem::path currentSongDirectory() const;
    [[nodiscard]] bool loadedRigCoversModelTones() const;
    [[nodiscard]] bool activateEmptyToneBranch(
        const std::string& tone_document_ref, const std::string& select_region_id);
    void reloadLiveRigForToneSet(std::string select_region_id);
    void resetSoleToneRegion(const std::string& region_id);
    void runProjectWriteAction(EditorAction::ProjectWriteAction&& action);
    void completeProjectWriteAction(const std::shared_ptr<ProjectWriteTaskState>& state);
    void applyProjectWriteSuccess(const EditorAction::SaveProject& action);
    void applyProjectWriteSuccess(const EditorAction::SaveProjectAs& action);
    void applyProjectWriteSuccess(const EditorAction::PublishProject& action);
    void replayDiscardedProjectAction(EditorAction::ProjectAction action);
    void replayDeferredProjectActionAfterSave();
    void clearDeferredProjectAction() noexcept;
    void clearInterruptedRestoreMarker();
    void clearLastOpenProjectIfMatches(const std::filesystem::path& project_file);
    [[nodiscard]] common::core::TimePosition cursorPositionForOpenedProject(
        const std::filesystem::path& project_file) const;
    [[nodiscard]] common::core::Fraction gridNoteValueForOpenedProject(
        const std::filesystem::path& project_file) const;
    [[nodiscard]] double timelineZoomForOpenedProject(
        const std::filesystem::path& project_file) const;
    void saveCurrentProjectCursorPositionBestEffort(std::string_view context);
    void saveProjectCursorPositionBestEffort(
        const std::filesystem::path& project_file, common::core::TimePosition cursor_position,
        std::string_view context);
    [[nodiscard]] std::optional<std::filesystem::path> restorableProjectFileForExit() const;
    [[nodiscard]] std::expected<void, common::audio::SongAudioError> loadSessionSong(
        common::core::Song song, const std::filesystem::path& song_directory,
        const std::optional<std::string>& selected_arrangement);
    [[nodiscard]] EditorViewState deriveViewState() const;
    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] std::uint64_t beginBusy(BusyOperation operation);
    void finishBusyOperation();
    void detachView();
    void restoreAudioDeviceState();
    void persistAudioDeviceState();
    void selectInputCalibrationForCurrentRoute();
    void saveActiveInputCalibration();
    void recordSettingsResultBestEffort(
        std::expected<void, EditorSettingsError> result, std::string_view context);
    void executeInputCalibrationEffects(const InputCalibrationWorkflow::Effects& effects);
    [[nodiscard]] InputCalibrationWorkflow::Context inputCalibrationContext() const;
    [[nodiscard]] std::optional<common::audio::InputDeviceIdentity> currentInputDeviceIdentity()
        const;
    void applyLiveInputGate();
    // Snapshot of live-input routing values used to roll back failed calibration setup. This is a
    // backend (live-input port) concern owned by the controller, not the headless workflow.
    struct InputCalibrationRouteState
    {
        common::audio::Gain input_gain;
        bool live_input_monitoring_enabled{false};
        bool calibration_input_monitoring_enabled{false};
    };
    [[nodiscard]] InputCalibrationRouteState currentInputCalibrationRouteState() const;
    void restoreInputCalibrationRouteStateBestEffort(const InputCalibrationRouteState& route_state);
    void clearActiveArrangementBestEffort(std::string_view context);
    bool setLiveInputMonitoringBestEffort(bool enabled, std::string_view context);
    bool setCalibrationInputMonitoringBestEffort(bool enabled, std::string_view context);
    bool setInputGainBestEffort(common::audio::Gain gain, std::string_view context);
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> commitInputCalibration(
        double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity);
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    restoreCalibrationMeasurementState();
    void updateView();
    void reportError(const std::string& message);

    // Wraps an async callback with a liveness guard against this Impl. Captures a weak_ptr to
    // m_alive at the call site; the returned callable checks expiry before invoking the
    // wrapped callback. Use this for any callback that may fire from the task runner or an
    // external backend after Impl is destroyed. For task-runner-driven busy operations,
    // use runWorkerThreadBusyOperation(), which composes the liveness check with token validation.
    template <typename Callback> auto safeCallback(Callback&& callback)
    {
        return [wrapped_callback = std::forward<Callback>(callback),
                alive = std::weak_ptr<bool>{m_alive}](auto&&... args) mutable {
            if (alive.expired())
            {
                return;
            }
            wrapped_callback(std::forward<decltype(args)>(args)...);
        };
    }

    // Begins a busy operation, pushes the resulting view state, and submits a worker+completion
    // pair to the task runner. The completion is wrapped so it runs only if (a) this Impl is
    // still alive AND (b) the busy token captured at submission still matches the current
    // token. Stale completions from superseded or interrupted operations are dropped silently.
    // The completion receives the shared state pointer only, because the helper already
    // guarantees that the captured busy token still matches at the call site.
    template <typename TaskState, typename Worker, typename Completion>
    void runWorkerThreadBusyOperation(
        BusyOperation operation, const std::shared_ptr<TaskState>& state, Worker&& worker,
        Completion&& completion)
    {
        const std::uint64_t token = beginBusy(operation);
        m_task_runner.submit(
            [state, captured_worker = std::forward<Worker>(worker)] { captured_worker(state); },
            safeCallback([this,
                          state,
                          token,
                          captured_completion = std::forward<Completion>(completion)]() mutable {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                captured_completion(state);
            }));
    }
    [[nodiscard]] bool hasLoadedArrangement() const;
    [[nodiscard]] bool shouldShowLiveRigLoadProgress() const;
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;
    [[nodiscard]] bool canStopTransport(const common::audio::TransportState& transport_state) const;

    // Transport port used for control intents and coarse listener delivery.
    common::audio::ITransport& m_transport;

    // Song-audio port used for project audio validation and selected-arrangement loading.
    common::audio::ISongAudio& m_song_audio;

    // Audio-device port used for ASIO input/output routing.
    common::audio::IAudioDeviceConfiguration& m_audio_devices;

    // Plugin-host port used to mutate the processing chain.
    common::audio::IPluginHost& m_plugin_host;

    // Live rig port used to persist and restore arrangement-owned plugin state.
    common::audio::ILiveRig& m_live_rig;

    // Tone parameter automation port used to read and edit tone-chain plugin curves.
    common::audio::IToneAutomation& m_tone_automation;

    // Live input port used to apply app-local calibration and gate monitoring.
    common::audio::ILiveInput& m_live_input;

    // Song aggregate and selected arrangement state currently loaded in the editor.
    common::core::Session m_session;

    // Project IO seams supplied by production composition or tests.
    EditorController::OpenFunction m_open_function;
    EditorController::ImportFunction m_import_function;
    EditorController::SaveFunction m_save_function;
    EditorController::SaveAsFunction m_save_as_function;
    EditorController::PublishFunction m_publish_function;

    // Host-exit callback supplied by app composition or controller tests.
    EditorController::ExitFunction m_exit_function;

    // App-local settings used to restore startup state and persist exit state.
    IEditorSettings& m_settings;

    // Non-owning view binding installed by attachView(); null before the first attachment.
    // updateView() and reportError() tolerate the null window because the constructor's
    // restoreAudioDeviceState() can synchronously fire onAudioDeviceConfigurationChanged()
    // before the host wires up a view.
    IEditorView* m_view{nullptr};

    // Most recently derived view state used as the seed push at view attachment.
    EditorViewState m_last_state{};

    // Monotonic id of the loaded project, bumped only when an open/restore/import completes
    // successfully. Surfaced through EditorViewState::project_load_id so the view can recognize a
    // fresh load (e.g. to recenter the timeline on the restored cursor) without inferring it from
    // busy phases or content diffs.
    std::uint64_t m_project_load_id{0};

    // Headless signal-chain workflow refreshed only from authoritative backend snapshots.
    SignalChainWorkflow m_signal_chain;

    // Product-level undo history for tone edits currently covered by the implementation plan.
    EditorUndoHistory m_undo_history;

    // Current output gain shown by the signal-chain panel and persisted in tone documents.
    double m_output_gain_db{0.0};

    // Timeline grid step as a note value (fraction of a whole note), initialized to the
    // quarter-note default because the Fraction default of 0/1 is a degenerate step. Restored
    // per project from app-local settings on open and reset to the default on close.
    common::core::Fraction m_grid_note_value{1, 4};

    // Horizontal timeline scale last reported by the view, persisted per project as app-local
    // resume state. Zero means no zoom has been reported or restored (view default applies).
    double m_timeline_zoom_pixels_per_second{0.0};

    // Stable id of the selected authored tone region; empty when nothing is selected.
    // Cleared on project close and load because ids are project-local.
    std::string m_selected_tone_region_id{};

    // Durable automation identity of one live tone-chain plugin instance.
    struct ToneAutomationIdentity
    {
        // Minted durable plugin id persisted through PluginRecord and keyed in song.json.
        std::string plugin_id;

        // Tone document whose chain the plugin belongs to.
        std::string tone_document_ref;
    };

    // Runtime association from live plugin instance ids to durable automation identity. Merged
    // from load-result tone chains (minting ids the documents did not carry yet) and extended at
    // plugin insert. Never erased within a session: id-preserving undo can revive an instance id,
    // and a stale entry is harmless because nothing resolves THROUGH dead instance ids — the
    // reverse direction (durable id to live instance) lives in m_tone_plugin_bindings, which is
    // rebuilt wholesale so it can never pick a stale instance.
    std::unordered_map<std::string, ToneAutomationIdentity> m_tone_plugin_identities{};

    // Reverse association from durable plugin ids to the CURRENTLY live instance and owning tone.
    // Replaced wholesale at every rig-load completion (reloads recreate every instance id) and
    // extended at plugin insert; automation lanes and derived-curve rebuilds resolve through it.
    std::unordered_map<std::string, ToneAutomationBinding> m_tone_plugin_bindings{};

    // Tone references the loaded rig currently hosts as branches, from the last load result.
    // Undo/redo can restore model references to tones a later reload dropped (reset repoints and
    // reloads); comparing against this set detects the divergence so the rig can reload.
    std::vector<std::string> m_loaded_tone_refs{};

    // Session-scoped automation lanes opened by the picker that have no authored points yet;
    // they track the parameter's live value. Not persisted and not undoable (a lane with no
    // points is a view arrangement, not an edit). Cleared with the session.
    std::vector<OpenAutomationLane> m_open_automation_lanes{};

    // Memoized tab projection for the displayed arrangement; see deriveViewState for the cache
    // rule (arrangement id keys it because charts are immutable while a project is open).
    // Mutable because the cache refreshes lazily inside the const view-state derivation.
    mutable std::shared_ptr<const TabViewState> m_tab_view_state{};
    mutable std::string m_tab_arrangement_id{};

    // App-wide display preferences for the tablature lane, restored from settings at construction
    // and persisted on change; never part of project content.
    bool m_waveform_visible{true};
    int m_tab_minimum_displayed_strings{0};

    // Present only while a live slider preview is waiting for its final commit.
    std::optional<common::audio::Gain> m_output_gain_preview_before{};

    // True only after arrangement audio and the live rig restore have both committed.
    bool m_project_audio_ready{false};

    // Headless calibration workflow. The controller executes ports, but calibration decisions and
    // view projection live here.
    InputCalibrationWorkflow m_input_calibration;

    // Browser catalog and selection state for adding known plugins.
    PluginCatalogWorkflow m_plugin_catalog;

    // Set true while a session load is in flight so reentrant transport callbacks defer pushing.
    bool m_session_load_in_progress{false};

    // Currently loaded or imported project context; keeps workspace files alive.
    std::optional<Project> m_project{};

    // User-selected editor project path used for project-name-derived UI suggestions.
    std::filesystem::path m_project_file{};

    // Settings-backed restore target currently being opened. Used only so app shutdown during
    // startup restore preserves the previous launch's restorable path until open commits or fails.
    std::optional<std::filesystem::path> m_pending_restore_project_file{};

    // Settings-backed restore target that should ask for user recovery input on this launch.
    std::optional<std::filesystem::path> m_restore_interrupted_prompt_file{};

    // True when Save must first collect an editor project package path, such as after import.
    bool m_save_requires_destination{false};

    // Project file that was open before an import replaced the session. Populated by the import
    // commit or by a discard-confirmed import that first closes a dirty saved project, so
    // close-with-discard can re-open the displaced project instead of leaving the editor empty.
    // Cleared on successful save, explicit open, or close of a saved project.
    std::filesystem::path m_displaced_project_file{};

    // True for dirty state that cannot be tracked by EditorUndoHistory, such as load-time
    // normalization rewrites, undo-recording failures after a mutation, and faulted sessions.
    bool m_has_untracked_unsaved_changes{false};

    // True after an undo/redo rollback-contract violation makes the live backend untrusted.
    bool m_session_faulted{false};

    // Project-lifecycle action and prompt state waiting on unsaved-change or Save As decisions.
    DeferredProjectActionState m_deferred_project_action_state{};

    // Busy operation workflow used by async callbacks to reject stale work and order UI work after
    // the overlay presentation has had a chance to paint.
    BusyOperationWorkflow m_busy;

    // Cancellation token for the active plugin catalog scan. Engaged only while a cancellable scan
    // is in flight so the worker can stop at its next safe checkpoint.
    std::optional<common::core::CancellationToken> m_plugin_scan_cancel{};

    // Reset during destruction so queued completions can detect that the controller is gone.
    std::shared_ptr<bool> m_alive{std::make_shared<bool>(true)};

    // Non-owning reference to the active task runner.
    IEditorTaskRunner& m_task_runner;

    // Declared near the end so callback registration is detached before controller state dies.
    common::audio::ScopedListener<common::audio::ITransport, common::audio::ITransport::Listener>
        m_transport_listener;

    // Optional audio-device-configuration listener registration.
    std::unique_ptr<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>
        m_audio_device_listener;
};

// Per-operation worker state for openProject(). The worker thread writes the result; the
// message-thread completion reads it. Held through a shared_ptr captured by both lambdas so the
// Project's workspace files stay alive until either side is done with them.
struct EditorController::Impl::OpenTaskState
{
    std::filesystem::path file{};
    bool clear_last_open_project_on_failure{false};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Per-operation worker state for importSongSource(). Same shared_ptr ownership pattern as
// OpenTaskState.
struct EditorController::Impl::ImportTaskState
{
    std::filesystem::path file{};
    Project project{};
    std::expected<common::core::Song, ProjectError> result{};
};

// Per-operation state for selected browser-plugin insertion. Actual chain mutation happens on
// the message thread after the busy overlay has painted because Tracktion requires it.
struct EditorController::Impl::InsertSelectedPluginTaskState
{
    common::audio::PluginCandidate plugin_candidate{};
    std::size_t chain_index{};
};

// Per-operation worker state for plugin catalog scanning. The worker owns plugin inspection
// through the audio boundary; the completion replaces the browser catalog.
struct EditorController::Impl::PluginCatalogTaskState
{
    std::expected<void, common::audio::PluginHostError> scan_result{};
};

// Per-operation worker state for project package writes. The Project is moved out of the
// controller before worker execution so background save/publish code never shares mutable project
// ownership with message-thread controller actions.
struct EditorController::Impl::ProjectWriteTaskState
{
    explicit ProjectWriteTaskState(EditorAction::ProjectWriteAction action_value)
        : action(std::move(action_value))
    {}

    EditorAction::ProjectWriteAction action;
    Project project{};
    common::core::Song song{};
    std::expected<void, ProjectError> result{};
};

// Shared message-thread continuation for the live-rig restore stage of project open/import.
// The stage centralizes busy-token checks, paint-fenced progress startup, and routing into the
// final commit callback so open/import do not each hand-roll the same async choreography.
struct EditorController::Impl::ProjectLoadLiveRigStage
{
    std::uint64_t token{};
    std::filesystem::path song_directory{};
    std::function<void(std::expected<void, common::audio::LiveRigError>)> finish;
};

} // namespace rock_hero::editor::core
