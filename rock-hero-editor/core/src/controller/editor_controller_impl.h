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
#include "chart/chart_edits.h"
#include "chart/chart_selection.h"
#include "deferred_project_action_state.h"
#include "editor_action.h"
#include "editor_action_availability.h"
#include "editor_selection.h"
#include "editor_undo_history.h"
#include "input_calibration/input_calibration_projection.h"
#include "project/project_io.h"
#include "signal_chain/plugin_catalog_workflow.h"
#include "signal_chain/signal_chain_workflow.h"
#include "tone/tone_automation_projection.h"
#include "tone_designer/tone_designer_edits.h"
#include "tone_designer/tone_designer_state.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/automation/tone_automation_rebuild.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
#include <rock_hero/editor/core/controller/editor_controller.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_view.h>
#include <rock_hero/editor/core/project/project.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
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
        common::audio::IToneAutomation& tone_automation, EditorController::Services services,
        EditorController::ExitFunction exit_function,
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
    void onChartPointerDown(const ChartPointerEvent& event);
    void onChartPointerDrag(const ChartPointerEvent& event);
    void onChartPointerUp(const ChartPointerEvent& event);
    void onChartPointerMove(const ChartPointerEvent& event);
    void onChartPointerExit();
    void onChartCaretStepRequested(ChartStepDirection direction, bool measure);
    // The one selection-move intent (Alt+arrows): dispatches on the editor-wide selection's
    // kind — automation point, chart notes — and falls back to create-then-nudge at an armed
    // empty lane caret slot. `fine` selects the 1/960-beat tier on the time axis (and the
    // 0.001 value tier on lanes) — the uniform precision escape hatch, both surfaces.
    void onSelectionMoveRequested(ChartStepDirection direction, bool fine);
    void moveChartSelection(ChartStepDirection direction, bool fine);
    void onChartSelectionDeleteRequested();
    void onSelectionDeleteRequested();
    void onChartFretDigitTyped(int digit);
    void onChartFretShiftRequested(int direction);
    void onChartSustainAdjustRequested(int direction, bool fine);
    void onChartEscapePressed();
    [[nodiscard]] const common::core::TabViewState* displayedTabProjection() const;
    [[nodiscard]] std::optional<ChartNoteKey> chartNoteKeyAt(std::size_t projection_index) const;
    void clearChartEditingState();
    // True when a chart note already occupies the given slot (one binary search over the
    // (position, string)-sorted notes). The insert-legality test shared by caret arming, the
    // Alt+click insert, and the insert ghost's honesty gate.
    [[nodiscard]] bool chartSlotOccupied(common::core::GridPosition position, int string) const;
    // Plants a note at an empty slot and makes it the selection with the caret armed on it — the
    // shared primitive behind the Alt+click neutral-create (fret 0) and any future placement. A
    // no-op when the slot is occupied (planInsertNote refuses).
    void insertChartNoteAt(common::core::GridPosition position, int string, int fret);
    // Resolves the Alt-hover insert ghost and publishes it when Alt is held over an insertable
    // empty slot, else clears it. Dirty-checked against the current ghost so a hover that stays
    // within one grid slot pushes no view rebuild.
    void publishChartInsertGhost(const ChartPointerEvent& event);
    // Arms the caret at a slot and re-derives the selection from what sits under it (a note
    // selects, an empty slot clears).
    void armChartCaret(common::core::GridPosition position, int string);
    // Demotes an armed caret to the passive cursor, leaving the transport where it is (the
    // transport-motion handoffs: play, external playback, paused seeks).
    void disarmChartMarker();
    // Demotes an armed caret to the passive cursor "in its place": a paused seek moves the
    // transport to the caret's musical time so the cursor line appears exactly where the caret
    // was (the editing-gesture handoffs: Ctrl+click, double-click, marquee, Esc).
    void dissolveChartCaretInPlace();
    // The one editor-wide selection lives behind these accessors so every surface's handlers
    // share the variant's structural exclusivity (editor_selection.h). Reads never change the
    // held alternative; chart mutation goes through the emplacing accessor so any chart gesture
    // replaces another surface's selection by construction.
    [[nodiscard]] const ChartSelection& chartSelection() const;
    [[nodiscard]] ChartSelection& chartSelectionMutable();
    [[nodiscard]] std::string selectedToneRegionId() const;
    [[nodiscard]] const AutomationPointSelection* selectedAutomationPoint() const;
    void clearSelection();
    // Clears only the selection kinds that follow the cursor (tone region, automation point);
    // a chart selection deliberately survives seeks (the marker model's lifecycle split).
    void clearCursorCoupledSelection();
    [[nodiscard]] std::optional<std::pair<common::core::GridPosition, int>> chartPlacementAt(
        const ChartPointerEvent& event) const;
    [[nodiscard]] common::core::Fraction chartGridStepBeats(common::core::GridPosition at) const;
    bool applyChartEditPlan(
        std::optional<ChartNotesEditPlan> plan,
        std::optional<std::vector<ChartNoteKey>> select_exactly = std::nullopt);
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
    void onToneAutomationPointSelected(
        std::string instance_id, std::string param_id, common::core::GridPosition position);
    // Deletes the selected automation point by replaying its lane's points without it through
    // onSetToneAutomationPoints (the Delete-key dispatch for the automation alternative).
    void deleteSelectedAutomationPoint(const AutomationPointSelection& selection);
    // Moves the selected automation point (the move-intent dispatch for the automation
    // alternative): Up/Down steps the value (one real state on a discrete lane, else 0.01 or
    // 0.001 fine), Left/Right steps the time axis via steppedLaneNudgePosition. Refused moves
    // (stale selection, map edge, neighbor collision, window edge) are silent no-ops.
    void moveSelectedAutomationPoint(
        const AutomationPointSelection& selection, ChartStepDirection direction, bool fine);
    // One lane keyboard time-step through the beat axis so the result stays an exact rational:
    // the adjacent tempo-grid line, or one 1/960-beat fine step.
    [[nodiscard]] common::core::GridPosition steppedLaneNudgePosition(
        const common::core::GridPosition& from, bool later, bool fine) const;
    // The active tone region's time window — the span automation edits clamp inside (the lane
    // is authored per tone but edited per region instance). Empty when no region is active.
    [[nodiscard]] common::core::TimeRange activeToneRegionWindow() const;
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
    [[nodiscard]] std::expected<void, GameAudioSourceError> onUseGameAudioSettingsChangeRequested(
        bool enabled, const std::function<void(bool)>& set_applying);
    [[nodiscard]] GameAudioSourceState gameAudioSourceState() const;
    void onGameAudioUnavailablePromptDismissed();
    void onGameAudioRecommendationDecision(
        GameAudioRecommendationDecision decision, bool suppress_future);
    void onInputCalibrationRequested();
    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    onInputCalibrationMeasurementStarted();
    void onInputCalibrationMeasurementCancelled();
    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    onInputCalibrationSucceeded(double gain_db);
    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    onInputCalibrationManuallySet(double gain_db);
    void onInputCalibrationDismissed();
    void onOutputGainPreviewChanged(double gain_db);
    void onOutputGainChanged(double gain_db);
    void onAudioDeviceChangeRequested(
        std::function<void()> change_audio_device, std::function<void()> after_busy_cleared);
    [[nodiscard]] bool onAudioDeviceSettingsOpenRequested();
    void onAudioDeviceSettingsClosed();
    void onAudioDeviceSettingsTeardownComplete();
    void onAudioDeviceFailureDecision(AudioDeviceFailureDecision decision);
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
    void performActionImpl(EditorAction::NewToneDocument action);
    void performActionImpl(EditorAction::OpenToneFile action);
    void performActionImpl(EditorAction::SaveToneFile action);
    void performActionImpl(const EditorAction::SaveToneFileAs& action);
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
        const InputCalibrationProjection& input_calibration,
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
    void runProjectActionImpl(EditorAction::NewToneDocument action);
    void runProjectActionImpl(EditorAction::OpenToneFile action);

    // Tone Designer feature slice (definitions in tone_designer/tone_designer_handlers.cpp).
    // The designer is the editor's resting mode: with no project open, the live rig stays alive
    // and the signal chain edits a file-backed tone document instead of project content.
    [[nodiscard]] bool hasActiveSignalChain() const;
    [[nodiscard]] bool toneDesignerHasUnsavedChanges() const noexcept;
    void enterToneDesignerIfNoProject(std::string_view context);
    void leaveToneDesigner(std::string_view context);
    void reconcileToneDesignerCleanMarker();
    void runToneDesignerNew();
    void runToneDesignerOpen(std::filesystem::path file);
    void runToneDesignerSave(const std::filesystem::path& destination);
    [[nodiscard]] std::optional<ToneDesignerDocumentSnapshot> captureToneDesignerSnapshot(
        bool matches_file);
    void finishToneDesignerReplace(
        ToneDesignerDocumentSnapshot before, std::optional<std::filesystem::path> opened_file,
        std::string operation_label, const common::audio::LiveRigLoadResult& result);
    void replayDeferredActionAfterToneSave();
    [[nodiscard]] std::vector<PluginVisualEditState> currentChainVisualStates() const;
    [[nodiscard]] std::vector<std::string> activeChainDurablePluginIds() const;
    [[nodiscard]] std::size_t activeToneAutomationEntryCount() const;
    void performActionImpl(EditorAction::ImportToneFile action);
    void performActionImpl(EditorAction::ResolveToneImportPrompt action);
    void performActionImpl(const EditorAction::ExportToneFile& action);
    void runToneImport(std::filesystem::path file);
    void finishToneImport(
        ToneChainSnapshot before, const std::vector<std::pair<std::string, std::string>>& prior_ids,
        const std::string& tone_ref, const std::string& tone_name,
        const std::filesystem::path& import_file, const common::audio::LiveRigLoadResult& result);
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
    [[nodiscard]] bool closeProject(bool reenter_tone_designer = true);
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
    [[nodiscard]] std::optional<EditorProjectMarker> markerForOpenedProject(
        const std::filesystem::path& project_file) const;
    // Typed restore of the stored resume marker (one overload per marker state): both seek
    // the transport, and the caret overload re-arms — or demotes to passive when its string
    // no longer exists on the loaded chart.
    void restoreProjectMarker(const EditorProjectCursor& cursor);
    void restoreProjectMarker(const EditorProjectCaret& caret);
    [[nodiscard]] common::core::Fraction gridNoteValueForOpenedProject(
        const std::filesystem::path& project_file) const;
    [[nodiscard]] double timelineZoomForOpenedProject(
        const std::filesystem::path& project_file) const;
    void saveCurrentProjectMarkerBestEffort(std::string_view context);
    void saveProjectMarkerBestEffort(
        const std::filesystem::path& project_file, const EditorProjectMarker& marker,
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
    // Resolves the persisted "use game audio settings" toggle against a fresh read of the game's
    // configuration before the startup route application (plan 48 amended ruleset): on + adoptable
    // selects the game source so the application adopts the game route; on + broken writes the
    // toggle off and stages the unavailable-game prompt; off + adoptable + unsuppressed stages the
    // recommendation prompt; anything else leaves the editor silently on its own settings.
    void resolveGameAudioSourceAtStartup();

    // Which audio-config source the editor should be running on after applyAudioSourceAndRoute.
    enum class AudioSourceSelection : std::uint8_t
    {
        EditorOwn, // Persist the toggle off and select the editor's own store, then apply.
        Game,      // Validate adoptability fresh; persist the toggle on and select the game view.
        Current,   // Touch neither the toggle nor the source; (re)apply the active route.
    };

    // The one route-application path shared by startup, the settings-window toggle, the startup
    // recommendation decision, and the failure prompt's Retry: optional source flip (validated
    // fresh for Game so a persisted on always means adoption succeeded), then the saved route of
    // the now-active source is applied either inline or behind the OpeningAudioDevice busy
    // overlay (a non-empty set_applying requests the overlay for a genuine device re-open).
    [[nodiscard]] std::expected<void, GameAudioSourceError> applyAudioSourceAndRoute(
        AudioSourceSelection selection, const std::function<void(bool)>& set_applying);

    // The one evaluation deciding whether the audio-device failure prompt should be staged; the
    // blocking overlay rendering it follows the staged value directly, so the prompt is simply
    // re-derived (and its reason text live-updated) whenever the editor is deviceless and no
    // other flow owns the situation.
    void refreshAudioDeviceFailurePrompt();
    void recordSettingsResultBestEffort(
        std::expected<void, EditorSettingsError> result, std::string_view context);
    void recordAudioConfigResultBestEffort(
        std::expected<void, common::audio::AudioConfigError> result, std::string_view context);
    // Builds the two-bool session context the shared live-input monitor gate evaluates; the monitor
    // samples the current input route identity itself.
    [[nodiscard]] common::audio::LiveInputMonitoringContext monitoringContext() const;
    void clearActiveArrangementBestEffort(std::string_view context);
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

    // Per-app audio-config store used to persist and restore the active device route.
    common::audio::IAudioConfigStore& m_audio_config_store;

    // Editor audio-config store the "use game audio settings" toggle re-selects; null in tests that
    // do not exercise the toggle. When set it is the same object as m_audio_config_store, held
    // concretely so onUseGameAudioSettingsChangeRequested can switch its active source.
    EditorAudioConfigStore* m_editor_audio_config_store{nullptr};

    // Startup unavailable-game notice staged by resolveGameAudioSourceAtStartup when the persisted
    // toggle asked for the game's configuration but it regressed; cleared when the view reports the
    // prompt dismissed. Transient by design — the toggle is already written off at staging time, so
    // no standing on-but-broken state exists for any window to render.
    std::optional<GameAudioUnavailablePrompt> m_game_audio_unavailable_prompt{};

    // Startup recommendation staged when the toggle is off, a calibrated game configuration exists,
    // and the user has not suppressed the prompt; cleared when the view reports a decision.
    bool m_game_audio_recommendation_prompt{false};

    // Standing failure notice staged by refreshAudioDeviceFailurePrompt() while the editor runs
    // without an open audio device and no other flow owns the situation. Cleared when a device
    // opens, when the settings window takes over, or when the user answers the overlay.
    std::optional<AudioDeviceFailurePrompt> m_audio_device_failure_prompt{};

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

    // The one editor-wide selection (editor_selection.h): chart notes, a tone region, or an
    // automation point — never more than one at a time, by construction. Access through the
    // selection accessors above. Cleared on project load/close and arrangement switches; each
    // alternative keeps its shipped lifecycle (chart clears on play but survives seeks; the
    // cursor-coupled kinds clear on any cursor move). While the marker is armed the chart
    // alternative is exactly what sits under the caret (the marker model): arming onto a note
    // selects it, onto an empty slot clears it; multi-note selections exist only while the
    // marker is passive.
    EditorSelection m_selection{};

    // In-flight tablature pointer gesture: armed on Down, disambiguated into click vs. marquee by
    // the drag threshold, resolved on Up. The geometry is the Down event's, so one gesture snaps
    // consistently even if a state push relayouts mid-drag.
    struct ChartPointerGesture
    {
        common::ui::TabLaneGeometry geometry{};
        ChartPointerModifiers modifiers{};
        float anchor_x{};
        float anchor_y{};
        float current_x{};
        float current_y{};
        bool marquee{false};
        // Set when Down hit a glyph; the gesture then owns selection instead of click-vs-marquee.
        std::optional<std::size_t> hit_note{};
    };
    std::optional<ChartPointerGesture> m_chart_gesture{};

    // The Alt-hover insert ghost, resolved to its rendered seconds+string (never a musical
    // operation acts on it — it is recomputed wholesale each hover), present only while Alt
    // hovers an insertable empty slot. Published verbatim into the chart-edit view state.
    std::optional<ChartInsertGhostViewState> m_chart_insert_ghost{};

    // In-flight multi-digit fret entry: the value typed so far, the tick of its last keystroke,
    // the pre-entry note values (so the widened undo entry still restores the originals), the
    // selection keys it retypes, whether an undo entry was pushed (a first digit matching every
    // fret is a no-op that pushes nothing), and the history position holding that entry. A
    // second digit inside the window widens the entry in place (replaceTop), so typing "2 then
    // 3" retypes to fret 23 and undoes as ONE action.
    struct ChartFretEntry
    {
        int value{};
        std::uint32_t last_keystroke_ms{};
        std::vector<common::core::ChartNote> base_notes{};
        std::vector<ChartNoteKey> keys{};
        // Set when the entry began as a caret insert: widening rebuilds this plan with the
        // combined fret so the entry stays ONE insert (undo removes the note), never
        // degrading into a retype that would strand it.
        std::optional<ChartNotesEditPlan> insert_plan{};
        bool pushed{false};
        std::size_t history_position{};
    };
    std::optional<ChartFretEntry> m_chart_fret_entry{};

    // The paused-position marker's passive state: the plain paused cursor. The transport
    // position IS the position (dissolutions seek so this always holds), so only the string
    // the next arming lands on is remembered here.
    struct ChartCursor
    {
        int string{1};
    };

    // A caret row on an automation lane, identified durably (instance + parameter, never a
    // display index) so lane reordering cannot move the caret (the row axis, §9b).
    struct AutomationLaneRow
    {
        std::string instance_id;
        std::string param_id;

        friend bool operator==(const AutomationLaneRow& lhs, const AutomationLaneRow& rhs) =
            default;
    };

    // The Alt-hover insert ghost's durable identity: the lane it rides (instance + parameter, like
    // AutomationLaneRow) and the exact snapped slot an Alt+click would plant a point at. Never a
    // musical operation acts on it — it is recomputed wholesale each hover and resolved to a
    // rendered ToneAutomationInsertGhostRef (lane index + seconds) each view push.
    struct ToneInsertGhost
    {
        std::string instance_id;
        std::string param_id;
        common::core::GridPosition position{};

        friend bool operator==(const ToneInsertGhost& lhs, const ToneInsertGhost& rhs) = default;
    };

    // The marker's armed state: the editing caret owning an exact grid slot and a row — where
    // typing inserts and play starts. The row axis spans the chart strings and then the visible
    // automation lanes (§9b): while `lane` is engaged the caret rides that lane row, and
    // `string` is the remembered string it returns to when traversal crosses back up into the
    // tab lane.
    struct ChartCaret
    {
        common::core::GridPosition position{};
        int string{1};
        std::optional<AutomationLaneRow> lane{};
    };

    // The two-state position marker (the marker model, 2026-07-18): always present, exactly
    // one state at a time — passive cursor or armed caret — so "cursor and caret at once" is
    // unrepresentable. Armed implies paused (playback demotes via the transport listener) and
    // implies the selection is what sits under the caret; chartless arrangements simply never
    // arm.
    using ChartMarker = std::variant<ChartCursor, ChartCaret>;
    ChartMarker m_chart_marker{ChartCursor{}};

    // Returns the armed caret, or null while the marker is passive.
    [[nodiscard]] const ChartCaret* armedChartCaret() const noexcept;

    // Returns the marker's remembered string in either state.
    [[nodiscard]] int chartMarkerString() const noexcept;

    // Arms the caret on an automation lane row and re-derives the selection from what sits
    // under it: a point at the slot becomes the editor-wide selection, an empty slot clears it
    // (armChartCaret's row-axis sibling, §9b).
    void armLaneCaret(common::core::GridPosition position, AutomationLaneRow row);

    // The visible automation lane rows in display order — the lower half of the caret's row
    // axis. Derived from the same projection the lanes view renders, so traversal and display
    // can never disagree about which rows exist.
    [[nodiscard]] std::vector<AutomationLaneRow> visibleAutomationLaneRows() const;

    // The caret row's next authored object strictly beyond the caret in the step direction —
    // a note on its string, a point on its lane. Objects are first-class caret stops (the
    // union stop set, settled 2026-07-18): plain arrows step to the nearer of the adjacent
    // grid line and this, so an off-grid object stays reachable from the keyboard.
    [[nodiscard]] std::optional<common::core::GridPosition> nextRowObjectStop(
        const ChartCaret& caret, bool later);

    // The authored points of one lane row, resolved through the durable plugin identity, or
    // null when nothing resolves. Non-const only because the session exposes its automation
    // entries mutably.
    [[nodiscard]] const std::vector<common::core::ToneAutomationPoint>* lanePointsFor(
        const std::string& instance_id, const std::string& param_id);

    // A time-addressed lane caret arm (the row-axis form of the chart lane's empty click): arms the
    // caret at the grid slot nearest the given time. The time-input entry point (exercised by tests);
    // the pixel-input click path arms through onToneAutomationPointerDown's one placement snap.
    void onToneAutomationLaneCaretRequested(
        std::string instance_id, std::string param_id, common::core::TimePosition time);

    // Seeks the transport to a resolved lane slot and arms the caret there (paused) — the seek-and-arm
    // tail the lane click and the time-addressed caret request share, so every lane caret arm rests on
    // one slot with the transport following it.
    void seekAndArmLaneCaret(common::core::GridPosition position, AutomationLaneRow row);

    // A button-less lane hover: resolves the Alt insert ghost and publishes it when Alt is held
    // over an insertable lane slot while paused, else clears it. Inverts the event's raw pixel x
    // through the placement seam (timelinePositionForX then the tempo grid, or the Ctrl fine tier)
    // so the ghost lands on the exact slot an Alt+click would, and a hover that stays within one
    // grid slot leaves the ghost unchanged and pushes no view rebuild.
    void onToneAutomationPointerMove(const ToneAutomationPointerEvent& event);

    // The pointer left the lane row: no hover, so no ghost.
    void onToneAutomationPointerExit();

    // A primary-button press inside a lane: re-resolves the point-vs-empty-area hit from the event
    // geometry and the lane's points, then arms the matching gesture. A press on a point begins a
    // move drag; Alt on empty area begins an on-curve insert (refused on an occupied slot); plain
    // empty area arms the lane caret. A double-click's second press is left to the view's editor.
    void onToneAutomationPointerDown(const ToneAutomationPointerEvent& event);

    // Advances the in-flight move/insert drag preview: snaps and neighbor/window-clamps the
    // position, pulls the value by the pointer's delta from the press (Shift locks the dominant
    // axis), and republishes the preview. A no-op without an active drag.
    void onToneAutomationPointerDrag(const ToneAutomationPointerEvent& event);

    // Ends the drag: a moved gesture commits its replacement list (one undo entry) and selects the
    // landed point; a press that never moved selects the pressed point. A no-op without a drag.
    void onToneAutomationPointerUp(const ToneAutomationPointerEvent& event);

    // The Insert key's neutral create: a fret-0 note at an armed empty string slot, an
    // on-curve point at an armed empty lane slot; a no-op on occupied slots, with a selection,
    // or while passive — Insert never mutates existing objects (2026-07-18).
    void onNeutralInsertRequested();

    // Inserts an on-curve point at an armed lane caret's slot (the Insert dispatch for lane
    // rows); a no-op when a point already sits there.
    void insertLanePointAtCaret(const ChartCaret& caret);

    // The resolved ingredients for planting a point at an armed lane caret's slot: the lane's
    // existing points, the on-curve landing value at the caret, and the parameter's value
    // shape. Null when the slot is occupied or nothing can land (no points and no live value).
    struct LanePointPlan
    {
        std::vector<common::core::ToneAutomationPoint> points;
        float value{0.0F};
        bool is_discrete{false};
        int discrete_value_count{0};
    };
    // Non-const only because the session exposes its automation entries mutably.
    [[nodiscard]] std::optional<LanePointPlan> planLanePointAtCaret(const ChartCaret& caret);

    // Plants a planned point (sorted insert, points-edit intent, select) — the shared creation
    // tail of the Insert verb and the Alt+arrow create-then-nudge.
    void plantLanePoint(
        const AutomationLaneRow& row, LanePointPlan plan, common::core::GridPosition position,
        float value);

    // The move-intent fallback on an armed empty lane slot: creates the on-curve point with the
    // arrow's step already baked in — value step for Up/Down, time step for Left/Right — so
    // "grab the curve here and pull" is one keystroke and ONE undo entry. A refused time step
    // (map edge, neighbor collision, window edge) still creates at the caret itself: the grab
    // succeeded, only the pull refused.
    void createAndNudgeLanePointAtCaret(
        const ChartCaret& caret, ChartStepDirection direction, bool fine);

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
    std::unordered_map<std::string, common::audio::ToneAutomationBinding> m_tone_plugin_bindings{};

    // Tone references the loaded rig currently hosts as branches, from the last load result.
    // Undo/redo can restore model references to tones a later reload dropped (reset repoints and
    // reloads); comparing against this set detects the divergence so the rig can reload.
    std::vector<std::string> m_loaded_tone_refs{};

    // Session-scoped automation lanes opened by the picker that have no authored points yet;
    // they track the parameter's live value. Not persisted and not undoable (a lane with no
    // points is a view arrangement, not an edit). Cleared with the session.
    std::vector<OpenAutomationLane> m_open_automation_lanes{};

    // The Alt-hover insert ghost's durable identity, present only while Alt hovers an insertable
    // lane slot; recomputed wholesale each hover and resolved into the tone-automation view state.
    // The lane-row sibling of m_chart_insert_ghost.
    std::optional<ToneInsertGhost> m_tone_insert_ghost{};

    // In-flight automation-lane point move/insert drag, the lane-row sibling of m_chart_gesture.
    // Armed on Down (a point grab or an Alt-insert placement), advanced on Drag (delta value,
    // neighbor clamp, Shift axis lock, editable-window clamp), committed on Up. Everything the
    // advance reads is frozen at Down — the lane's points, the horizontal geometry, the value-band
    // extent, the press point — so the gesture edits against the model it started with and a
    // mid-drag engine rebuild republishes this preview instead of resetting the edit. The value
    // preview is delta-based from press_y so an on-curve insert landing (or an off-center grab)
    // never jumps to the raw pointer y.
    struct ToneAutomationDrag
    {
        std::string instance_id;
        std::string param_id;
        // The lane's points frozen at Down (the move edits against these, stable indices).
        std::vector<common::core::ToneAutomationPoint> points;
        // Index of the grabbed point (move) or the insertion slot (insert) within `points`.
        std::size_t point_index{};
        // Horizontal geometry and the value-band extent frozen at Down, so one gesture snaps and
        // pulls consistently even if a state push relayouts mid-drag.
        common::core::TimeRange visible_timeline{};
        int content_width{0};
        ToneAutomationLaneExtent value_band{};
        common::core::GridPosition preview_position{};
        float preview_value{};
        common::core::GridPosition start_position{};
        float start_value{};
        // Press point in lane-local pixels: the delta-value origin (press_y) and the Shift
        // dominant-axis anchor (both axes). The click-vs-move threshold rides the event's
        // dragged_since_down flag, not these.
        float press_x{};
        float press_y{};
        // Value shape captured at Down so discrete lanes snap their pull to real states.
        bool is_discrete{false};
        int discrete_value_count{0};
        // Latches once the pointer travels past the click threshold (or true from Down for an
        // insert); until then a point grab is still a click and publishes no preview.
        bool moved{false};
        bool is_new_point{false};
    };
    std::optional<ToneAutomationDrag> m_tone_automation_drag{};

    // Builds the replacement point list an active move/insert drag commits: every frozen point
    // echoed bit-identically except the moved one, with the preview point inserted in sorted order.
    [[nodiscard]] std::vector<common::core::ToneAutomationPoint> toneAutomationDragCommitPoints(
        const ToneAutomationDrag& drag) const;

    // Memoized tab and 3D-highway projections for the displayed arrangement; see
    // deriveViewState for the cache rule (keyed by arrangement id plus the session's chart
    // revision, so chart edits rebuild both projections). Mutable because the caches refresh
    // lazily inside the const view-state derivation.
    mutable std::shared_ptr<const common::core::TabViewState> m_tab_view_state{};
    mutable std::shared_ptr<const common::core::HighwayViewState> m_highway_view_state{};
    mutable std::string m_tab_arrangement_id{};
    mutable std::uint64_t m_tab_chart_revision{0};
    // The displayed-string minimum the memoized highway projection was built with; unlike the 2D
    // tab (which pads in the view), the 3D projection bakes the minimum in, so a change to it must
    // rebuild the highway state even when the arrangement is unchanged. -1 forces the first build.
    mutable int m_highway_min_strings{-1};

    // App-wide display preferences for the tablature lane, restored from settings at construction
    // and persisted on change; never part of project content.
    bool m_waveform_visible{true};
    int m_tab_minimum_displayed_strings{0};

    // Present only while a live slider preview is waiting for its final commit.
    std::optional<common::audio::Gain> m_output_gain_preview_before{};

    // True only after arrangement audio and the live rig restore have both committed.
    bool m_project_audio_ready{false};

    // Shared calibrate-first live-input monitoring service. The controller drives it at lifecycle
    // edges (refresh/applyGate/disableMonitoring) and delegates calibration to it; the service owns
    // the pure calibration workflow and the ILiveInput port driving that used to live here.
    common::audio::LiveInputMonitor& m_live_input_monitor;

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

    // Tone Designer mode and document state; active exactly when no project is open and the
    // editor's resting live rig is the editing surface.
    ToneDesignerState m_tone_designer{};

    // Tone file waiting on the import automation-drop confirmation, plus the entry count the
    // prompt displays. The prompt only carries facts; the import re-derives everything fresh
    // when the user accepts.
    std::optional<std::filesystem::path> m_pending_tone_import{};
    std::size_t m_pending_tone_import_automation_count{0};

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
