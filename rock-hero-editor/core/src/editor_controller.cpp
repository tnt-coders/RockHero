#include "editor_controller.h"

#include "audio_device_status_text.h"
#include "busy_operation_workflow.h"
#include "deferred_project_action_state.h"
#include "editor_action.h"
#include "editor_action_availability.h"
#include "editor_undo_history.h"
#include "input_calibration_workflow.h"
#include "plugin_catalog_workflow.h"
#include "project_io.h"
#include "psarc_song_importer.h"
#include "rock_song_importer.h"
#include "signal_chain_workflow.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_song_audio.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/common/audio/scoped_listener.h>
#include <rock_hero/common/core/cancellation_token.h>
#include <rock_hero/common/core/logger.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/i_editor_settings.h>
#include <rock_hero/editor/core/i_editor_task_runner.h>
#include <rock_hero/editor/core/i_editor_view.h>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Routes non-fatal cleanup/persistence failures to the debug log without hiding the primary
// workflow result being handled by the caller.
void logEditorControllerBestEffortFailure(std::string_view context, const std::string& message)
{
    RH_LOG_WARNING(
        "editor.controller",
        "Best-effort cleanup or persistence failed context={} detail={}",
        context,
        message);
}

// Distinguishes live slider audition from a value that should enter undo history.
enum class OutputGainChangeIntent
{
    Preview,
    Commit,
};

// Outcome of trying to undo a just-completed insert whose undo entry could not be prepared.
enum class InsertUndoPreparationRollbackStatus
{
    RolledBack,
    Failed,
    RollbackContractViolation,
};

// Carries the rollback outcome and detail text for final user-facing/reporting decisions.
struct InsertUndoPreparationRollbackResult
{
    InsertUndoPreparationRollbackStatus status{InsertUndoPreparationRollbackStatus::RolledBack};
    std::string detail;
};

[[nodiscard]] std::string_view actionIdText(EditorAction::Id action) noexcept
{
    switch (action)
    {
        case EditorAction::Id::OpenProject:
        {
            return "OpenProject";
        }
        case EditorAction::Id::RestoreProject:
        {
            return "RestoreProject";
        }
        case EditorAction::Id::ImportSong:
        {
            return "ImportSong";
        }
        case EditorAction::Id::SaveProject:
        {
            return "SaveProject";
        }
        case EditorAction::Id::SaveProjectAs:
        {
            return "SaveProjectAs";
        }
        case EditorAction::Id::PublishProject:
        {
            return "PublishProject";
        }
        case EditorAction::Id::CloseProject:
        {
            return "CloseProject";
        }
        case EditorAction::Id::ExitApplication:
        {
            return "ExitApplication";
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            return "ResolveUnsavedChangesPrompt";
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            return "CancelSaveAsPrompt";
        }
        case EditorAction::Id::CancelBusyOperation:
        {
            return "CancelBusyOperation";
        }
        case EditorAction::Id::Undo:
        {
            return "Undo";
        }
        case EditorAction::Id::Redo:
        {
            return "Redo";
        }
        case EditorAction::Id::PlayPause:
        {
            return "PlayPause";
        }
        case EditorAction::Id::Stop:
        {
            return "Stop";
        }
        case EditorAction::Id::SeekWaveform:
        {
            return "SeekWaveform";
        }
        case EditorAction::Id::ShowPluginBrowser:
        {
            return "ShowPluginBrowser";
        }
        case EditorAction::Id::BeginPluginInsert:
        {
            return "BeginPluginInsert";
        }
        case EditorAction::Id::ScanPluginCatalog:
        {
            return "ScanPluginCatalog";
        }
        case EditorAction::Id::InsertSelectedPlugin:
        {
            return "InsertSelectedPlugin";
        }
        case EditorAction::Id::RemovePlugin:
        {
            return "RemovePlugin";
        }
        case EditorAction::Id::MovePlugin:
        {
            return "MovePlugin";
        }
        case EditorAction::Id::SetSignalChainPlacement:
        {
            return "SetSignalChainPlacement";
        }
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        {
            return "SetPluginDisplayTypeOverride";
        }
        case EditorAction::Id::OpenPlugin:
        {
            return "OpenPlugin";
        }
    }

    return "Unknown";
}

[[nodiscard]] std::string_view actionUnavailableReason(
    EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    if (conditions.busy)
    {
        if (action == EditorAction::Id::CancelBusyOperation)
        {
            return "busy-cancel-unavailable";
        }
        return "busy";
    }

    if (conditions.session_faulted)
    {
        return "session-faulted";
    }

    if (conditions.input_calibration_prompt_visible)
    {
        switch (action)
        {
            case EditorAction::Id::Undo:
            case EditorAction::Id::Redo:
            case EditorAction::Id::PlayPause:
            case EditorAction::Id::ShowPluginBrowser:
            case EditorAction::Id::BeginPluginInsert:
            case EditorAction::Id::ScanPluginCatalog:
            case EditorAction::Id::InsertSelectedPlugin:
            case EditorAction::Id::RemovePlugin:
            case EditorAction::Id::MovePlugin:
            case EditorAction::Id::SetSignalChainPlacement:
            case EditorAction::Id::SetPluginDisplayTypeOverride:
            case EditorAction::Id::OpenPlugin:
            {
                return "input-calibration-prompt";
            }
            case EditorAction::Id::OpenProject:
            case EditorAction::Id::RestoreProject:
            case EditorAction::Id::ImportSong:
            case EditorAction::Id::SaveProject:
            case EditorAction::Id::SaveProjectAs:
            case EditorAction::Id::PublishProject:
            case EditorAction::Id::CloseProject:
            case EditorAction::Id::ExitApplication:
            case EditorAction::Id::ResolveUnsavedChangesPrompt:
            case EditorAction::Id::CancelSaveAsPrompt:
            case EditorAction::Id::CancelBusyOperation:
            case EditorAction::Id::Stop:
            case EditorAction::Id::SeekWaveform:
            {
                break;
            }
        }
    }

    switch (action)
    {
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::CloseProject:
        case EditorAction::Id::Undo:
        case EditorAction::Id::Redo:
        {
            return conditions.has_project ? "history-unavailable" : "no-project";
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            return "no-unsaved-changes-prompt";
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            return "no-save-as-prompt";
        }
        case EditorAction::Id::CancelBusyOperation:
        {
            return "not-busy";
        }
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::SeekWaveform:
        case EditorAction::Id::ScanPluginCatalog:
        {
            return "no-loaded-arrangement";
        }
        case EditorAction::Id::Stop:
        {
            return "transport-reset";
        }
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        case EditorAction::Id::InsertSelectedPlugin:
        {
            return "plugin-insert-unavailable";
        }
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        {
            return "plugin-chain-unavailable";
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::ExitApplication:
        {
            return "available-action-rejected";
        }
    }

    return "state";
}

void logEditorActionRequested(EditorAction::Id action)
{
    RH_LOG_INFO("editor.controller", "Action requested action={}", actionIdText(action));
}

void logEditorActionAvailabilityRejected(EditorAction::Id action, std::string_view reason)
{
    RH_LOG_INFO(
        "editor.controller",
        "Action availability rejected action={} reason={}",
        actionIdText(action),
        reason);
}

void logEditorActionStarted(EditorAction::Id action)
{
    RH_LOG_INFO("editor.controller", "Action started action={}", actionIdText(action));
}

void logEditorActionDispatchCompleted(EditorAction::Id action)
{
    RH_LOG_INFO("editor.controller", "Action dispatch completed action={}", actionIdText(action));
}

[[nodiscard]] std::string_view undoDirectionText(EditorUndoDirection direction) noexcept
{
    switch (direction)
    {
        case EditorUndoDirection::Undo:
        {
            return "undo";
        }
        case EditorUndoDirection::Redo:
        {
            return "redo";
        }
    }

    return "unknown";
}

[[nodiscard]] std::string_view undoTransitionStatusText(EditorUndoTransitionStatus status) noexcept
{
    switch (status)
    {
        case EditorUndoTransitionStatus::Applied:
        {
            return "applied";
        }
        case EditorUndoTransitionStatus::Pending:
        {
            return "pending";
        }
        case EditorUndoTransitionStatus::NonCommitFailure:
        {
            return "non_commit_failure";
        }
    }

    return "unknown";
}

[[nodiscard]] std::string_view undoFailureCodeText(EditorUndoFailureCode failure_code) noexcept
{
    switch (failure_code)
    {
        case EditorUndoFailureCode::None:
        {
            return "none";
        }
        case EditorUndoFailureCode::NothingToUndo:
        {
            return "nothing_to_undo";
        }
        case EditorUndoFailureCode::NothingToRedo:
        {
            return "nothing_to_redo";
        }
        case EditorUndoFailureCode::TransitionAlreadyPending:
        {
            return "transition_already_pending";
        }
        case EditorUndoFailureCode::NoPendingTransition:
        {
            return "no_pending_transition";
        }
        case EditorUndoFailureCode::PendingTokenMismatch:
        {
            return "pending_token_mismatch";
        }
        case EditorUndoFailureCode::PreflightRejected:
        {
            return "preflight_rejected";
        }
        case EditorUndoFailureCode::NoNetMutation:
        {
            return "no_net_mutation";
        }
        case EditorUndoFailureCode::RepairedFailure:
        {
            return "repaired_failure";
        }
        case EditorUndoFailureCode::RollbackContractViolation:
        {
            return "rollback_contract_violation";
        }
    }

    return "unknown";
}

[[nodiscard]] std::string_view undoEventTypeText(EditorUndoEventType type) noexcept
{
    switch (type)
    {
        case EditorUndoEventType::EntryPushed:
        {
            return "entry_pushed";
        }
        case EditorUndoEventType::RedoEntriesDiscarded:
        {
            return "redo_entries_discarded";
        }
        case EditorUndoEventType::UndoBegan:
        {
            return "undo_began";
        }
        case EditorUndoEventType::RedoBegan:
        {
            return "redo_began";
        }
        case EditorUndoEventType::UndoCommitted:
        {
            return "undo_committed";
        }
        case EditorUndoEventType::RedoCommitted:
        {
            return "redo_committed";
        }
        case EditorUndoEventType::TransitionAborted:
        {
            return "transition_aborted";
        }
        case EditorUndoEventType::TransitionRejected:
        {
            return "transition_rejected";
        }
        case EditorUndoEventType::CleanMarked:
        {
            return "clean_marked";
        }
        case EditorUndoEventType::CleanMarkerMadeUnreachable:
        {
            return "clean_marker_made_unreachable";
        }
        case EditorUndoEventType::HistoryReset:
        {
            return "history_reset";
        }
    }

    return "unknown";
}

void logEditorUndoTransitionResult(
    std::string_view context, const EditorUndoTransitionResult& result)
{
    RH_LOG_INFO(
        "editor.controller",
        "Undo transition result context={} status={} failure={} requires_fault={}",
        context,
        undoTransitionStatusText(result.status),
        undoFailureCodeText(result.failure_code),
        result.requires_fault);

    for (const EditorUndoEvent& event : result.events)
    {
        const std::string_view direction =
            event.direction.has_value() ? undoDirectionText(*event.direction) : "none";
        RH_LOG_INFO(
            "editor.controller",
            "Undo transition event context={} type={} label={} direction={} failure={} "
            "requires_fault={}",
            context,
            undoEventTypeText(event.type),
            event.label,
            direction,
            undoFailureCodeText(event.failure_code),
            event.requires_fault);
    }
}

// Reports a boundary result that violated the product cap before it reaches view state.
[[nodiscard]] common::audio::LiveRigError signalChainLimitError(std::size_t plugin_count)
{
    return common::audio::LiveRigError{
        common::audio::LiveRigErrorCode::PluginChainLimitExceeded,
        common::audio::pluginChainLimitExceededMessage(plugin_count),
    };
}

// Creates the project-level analyzer used by production open/import operations while reporting
// the analysis phase at the controller-operation boundary.
[[nodiscard]] AudioNormalizationAnalyzer makeReportingAudioNormalizationAnalyzer(
    const EditorController::ProjectOperationProgress& report_progress)
{
    return [&report_progress](
               const std::filesystem::path& input,
               const common::core::AudioNormalizationTarget& target) {
        if (report_progress)
        {
            report_progress(EditorController::ProjectOperationPhase::AnalyzingBackingAudio);
        }
        return common::audio::analyzeAudioForGainNormalization(input, target);
    };
}

// Production open path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultOpen(
    Project& project, const std::filesystem::path& file,
    const EditorController::ProjectOperationProgress& report_progress)
{
    const AudioNormalizationAnalyzer analyzer =
        makeReportingAudioNormalizationAnalyzer(report_progress);
    return project.load(file, {}, analyzer);
}

// Production import path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<common::core::Song, ProjectError> defaultImport(
    Project& project, const std::filesystem::path& file,
    const EditorController::ProjectOperationProgress& report_progress)
{
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (extension == ".rock")
    {
        RockSongImporter importer;
        const AudioNormalizationAnalyzer analyzer =
            makeReportingAudioNormalizationAnalyzer(report_progress);
        return project.import(file, importer, {}, analyzer);
    }

    if (extension == ".psarc")
    {
        PsarcSongImporter importer;
        const AudioNormalizationAnalyzer analyzer =
            makeReportingAudioNormalizationAnalyzer(report_progress);
        return project.import(file, importer, {}, analyzer);
    }

    return std::unexpected{ProjectError{
        ProjectErrorCode::SongImportFailed,
        "Unsupported song source extension: " + file.extension().string()
    }};
}

// Production save path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultSave(
    Project& project, const common::core::Song& song, ProjectEditorState editor_state)
{
    return project.save(song, std::move(editor_state));
}

// Production save-as path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultSaveAs(
    Project& project, const std::filesystem::path& file, const common::core::Song& song,
    ProjectEditorState editor_state)
{
    return project.saveAs(file, song, std::move(editor_state));
}

// Production publish path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultPublish(
    Project& project, const std::filesystem::path& file, const common::core::Song& song)
{
    return project.publish(file, song);
}

// Closes an optional project after the caller has released subsystem references into its workspace.
[[nodiscard]] std::expected<void, ProjectError> closeExistingProject(
    std::optional<Project>& project)
{
    if (!project.has_value())
    {
        return {};
    }

    return project->close();
}

// Production exit fallback used when a composition host does not provide an exit callback.
void defaultExit()
{}

// Uses the real filesystem to avoid adding another startup-restore callback seam.
[[nodiscard]] bool projectFileExists(const std::filesystem::path& project_file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(project_file, error);
}

// Resolves the extracted native-song directory owned by an editor project workspace.
[[nodiscard]] std::filesystem::path songDirectoryForProject(const Project& project)
{
    return project.workspaceDirectory() /
           std::filesystem::path{std::string{project_io::g_song_directory_name}};
}

// Resolves persisted arrangement IDs to the current song order, falling back to the first item.
[[nodiscard]] std::size_t getSelectedArrangementIndex(
    const common::core::Song& song, const std::optional<std::string>& selected_arrangement)
{
    if (!selected_arrangement.has_value())
    {
        return 0;
    }

    const auto found = std::ranges::find_if(
        song.arrangements, [&selected_arrangement](const common::core::Arrangement& arrangement) {
            return arrangement.id == *selected_arrangement;
        });
    if (found == song.arrangements.end())
    {
        return 0;
    }

    return static_cast<std::size_t>(std::distance(song.arrangements.begin(), found));
}

// Maps Save to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::SaveProject& /*action*/) noexcept
{
    return BusyOperation::SavingProject;
}

// Maps Save As to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::SaveProjectAs& /*action*/) noexcept
{
    return BusyOperation::SavingProjectAs;
}

// Maps Publish to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::PublishProject& /*action*/) noexcept
{
    return BusyOperation::PublishingProject;
}

// Maps write actions to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::ProjectWriteAction& action)
{
    return std::visit(
        [](const auto& alternative) noexcept { return busyOperationForProjectWrite(alternative); },
        action);
}

// Keeps Save failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::SaveProject& /*action*/) noexcept
{
    return "Could not save: ";
}

// Keeps Save As failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::SaveProjectAs& /*action*/) noexcept
{
    return "Could not save as: ";
}

// Keeps Publish failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::PublishProject& /*action*/) noexcept
{
    return "Could not publish: ";
}

// Keeps write failure prefixes coupled to write alternatives rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::ProjectWriteAction& action)
{
    return std::visit(
        [](const auto& alternative) noexcept { return projectWriteErrorPrefix(alternative); },
        action);
}

// Captures editor-owned signal-chain block placement with instance IDs for undo edits.
[[nodiscard]] std::vector<PluginBlockAssignment> pluginBlockAssignmentsFor(
    const std::vector<PluginViewState>& plugins)
{
    std::vector<PluginBlockAssignment> assignments;
    assignments.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        assignments.push_back(
            PluginBlockAssignment{
                .instance_id = plugin.instance_id,
                .block_index = plugin.block_index,
            });
    }

    return assignments;
}

// Checks whether a plugin-host mutation snapshot still contains a supposedly removed instance.
[[nodiscard]] bool pluginSnapshotContainsInstance(
    const common::audio::PluginChainSnapshot& snapshot, const std::string& instance_id)
{
    return std::ranges::any_of(snapshot.plugins, [&instance_id](const auto& plugin) {
        return plugin.instance_id == instance_id;
    });
}

// Reads the current display override for one plugin row.
[[nodiscard]] std::optional<PluginDisplayType> displayTypeOverrideFor(
    const std::vector<PluginViewState>& plugins, std::string_view instance_id)
{
    const auto plugin = std::ranges::find_if(plugins, [instance_id](const PluginViewState& item) {
        return item.instance_id == instance_id;
    });
    if (plugin == plugins.end())
    {
        return std::nullopt;
    }

    return plugin->display_type_override;
}

// Captures editor-owned visual state for one plugin row before an audio mutation removes it.
[[nodiscard]] std::optional<PluginVisualEditState> pluginVisualStateFor(
    const std::vector<PluginViewState>& plugins, std::string_view instance_id)
{
    const auto plugin = std::ranges::find_if(plugins, [instance_id](const PluginViewState& item) {
        return item.instance_id == instance_id;
    });
    if (plugin == plugins.end())
    {
        return std::nullopt;
    }

    return PluginVisualEditState{
        .instance_id = plugin->instance_id,
        .block_index = plugin->block_index,
        .display_type_override = plugin->display_type_override,
    };
}

} // namespace

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
        common::audio::ILiveInput& live_input, EditorController::Services services,
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
    void onWaveformClicked(double normalized_x);
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
    void performActionImpl(EditorAction::SeekWaveform action);
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
    void flushPendingPluginParameterEdits(std::string_view context);
    void onPluginParameterPendingChanged(bool pending);
    void onPluginParameterEditCompleted(common::audio::PluginParameterEdit edit);
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
        const std::shared_ptr<OpenTaskState>& state, const ProjectEditorState& editor_state,
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
    void applySignalChainMutationSnapshot(common::audio::PluginChainSnapshot snapshot);
    void completePluginCatalogScan(const std::shared_ptr<PluginCatalogTaskState>& state);
    void refreshKnownPluginCatalog();
    [[nodiscard]] bool closeProject();
    [[nodiscard]] std::shared_ptr<ProjectWriteTaskState> takeProjectForWrite(
        EditorAction::ProjectWriteAction action);
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> captureLiveRigIntoSong(
        common::core::Song& song, const Project& project);
    void runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state);
    void startLiveRigLoadStage(ProjectLoadLiveRigStage stage_state, bool report_progress);
    void restoreLiveRig(
        const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
        std::function<
            void(std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>)>
            on_loaded);
    void clearLiveRig();
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
    [[nodiscard]] ProjectEditorState projectEditorStateForSave() const;
    [[nodiscard]] std::optional<std::filesystem::path> restorableProjectFileForExit() const;
    [[nodiscard]] std::expected<void, common::audio::SongAudioError> loadSessionSong(
        common::core::Song song, const std::optional<std::string>& selected_arrangement);
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

    // Headless signal-chain workflow refreshed only from authoritative backend snapshots.
    SignalChainWorkflow m_signal_chain;

    // Product-level undo history for tone edits currently covered by the implementation plan.
    EditorUndoHistory m_undo_history;

    // Current output gain shown by the signal-chain panel and persisted in tone documents.
    double m_output_gain_db{0.0};

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
    ProjectEditorState editor_state{};
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

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where an optional project operation is omitted.
EditorController::EditorController(
    EditorController::AudioPorts audio_ports, EditorController::Services services,
    EditorController::ExitFunction exit_function)
    : EditorController(
          audio_ports, services, std::move(exit_function), EditorController::ProjectOperations{})
{}

EditorController::EditorController(
    EditorController::AudioPorts audio_ports, EditorController::Services services,
    EditorController::ExitFunction exit_function,
    EditorController::ProjectOperations project_operations)
    : m_impl(
          std::make_unique<Impl>(
              audio_ports.transport, audio_ports.song_audio, audio_ports.audio_devices,
              audio_ports.plugin_host, audio_ports.live_rig, audio_ports.live_input, services,
              std::move(exit_function), std::move(project_operations)))
{}

// Releases the pimpl after the public controller's listener callbacks can no longer be invoked.
EditorController::~EditorController() = default;

void EditorController::attachView(IEditorView& view)
{
    m_impl->attachView(view);
}

void EditorController::detachView()
{
    m_impl->detachView();
}

const common::core::Session& EditorController::session() const noexcept
{
    return m_impl->session();
}

std::optional<std::filesystem::path> EditorController::currentProjectFile() const
{
    return m_impl->currentProjectFile();
}

void EditorController::restoreLastOpenProject()
{
    m_impl->restoreLastOpenProject();
}

void EditorController::onOpenRequested(std::filesystem::path file)
{
    m_impl->onOpenRequested(std::move(file));
}

void EditorController::onImportRequested(std::filesystem::path file)
{
    m_impl->onImportRequested(std::move(file));
}

void EditorController::onSaveRequested()
{
    m_impl->onSaveRequested();
}

void EditorController::onSaveAsRequested(std::filesystem::path file)
{
    m_impl->onSaveAsRequested(std::move(file));
}

void EditorController::onPublishRequested(std::filesystem::path file)
{
    m_impl->onPublishRequested(std::move(file));
}

void EditorController::onSaveAsCancelled()
{
    m_impl->onSaveAsCancelled();
}

void EditorController::onBusyCancelRequested()
{
    m_impl->onBusyCancelRequested();
}

void EditorController::onUndoRequested()
{
    m_impl->onUndoRequested();
}

void EditorController::onRedoRequested()
{
    m_impl->onRedoRequested();
}

void EditorController::onCloseRequested()
{
    m_impl->onCloseRequested();
}

void EditorController::onExitRequested()
{
    m_impl->onExitRequested();
}

void EditorController::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    m_impl->onUnsavedChangesDecision(decision);
}

void EditorController::onRestoreInterruptedDecision(RestoreInterruptedDecision decision)
{
    m_impl->onRestoreInterruptedDecision(decision);
}

void EditorController::onPlayPausePressed()
{
    m_impl->onPlayPausePressed();
}

void EditorController::onStopPressed()
{
    m_impl->onStopPressed();
}

void EditorController::onWaveformClicked(double normalized_x)
{
    m_impl->onWaveformClicked(normalized_x);
}

void EditorController::onPluginBrowserRequested()
{
    m_impl->onPluginBrowserRequested();
}

void EditorController::onPluginInsertSlotSelected(std::size_t chain_index, std::size_t block_index)
{
    m_impl->onPluginInsertSlotSelected(chain_index, block_index);
}

void EditorController::onPluginBrowserClosed()
{
    m_impl->onPluginBrowserClosed();
}

void EditorController::onPluginCatalogScanRequested()
{
    m_impl->onPluginCatalogScanRequested();
}

void EditorController::onSelectedPluginInsertRequested(std::string plugin_id)
{
    m_impl->onSelectedPluginInsertRequested(std::move(plugin_id));
}

void EditorController::onRemovePluginRequested(std::string instance_id)
{
    m_impl->onRemovePluginRequested(std::move(instance_id));
}

void EditorController::onMovePluginRequested(
    std::string instance_id, std::size_t destination_index,
    std::vector<PluginBlockAssignment> placement)
{
    m_impl->onMovePluginRequested(std::move(instance_id), destination_index, std::move(placement));
}

void EditorController::onSignalChainPlacementChanged(std::vector<PluginBlockAssignment> placement)
{
    m_impl->onSignalChainPlacementChanged(std::move(placement));
}

void EditorController::onPluginDisplayTypeOverrideChanged(
    std::string instance_id, std::optional<PluginDisplayType> display_type)
{
    m_impl->onPluginDisplayTypeOverrideChanged(std::move(instance_id), display_type);
}

void EditorController::onOpenPluginRequested(std::string instance_id)
{
    m_impl->onOpenPluginRequested(std::move(instance_id));
}

void EditorController::onInputCalibrationRequested()
{
    m_impl->onInputCalibrationRequested();
}

std::expected<void, common::audio::LiveInputError> EditorController::
    onInputCalibrationMeasurementStarted()
{
    return m_impl->onInputCalibrationMeasurementStarted();
}

void EditorController::onInputCalibrationMeasurementCancelled()
{
    m_impl->onInputCalibrationMeasurementCancelled();
}

std::expected<void, common::audio::LiveInputError> EditorController::onInputCalibrationSucceeded(
    double gain_db)
{
    return m_impl->onInputCalibrationSucceeded(gain_db);
}

std::expected<void, common::audio::LiveInputError> EditorController::onInputCalibrationManuallySet(
    double gain_db)
{
    return m_impl->onInputCalibrationManuallySet(gain_db);
}

void EditorController::onInputCalibrationDismissed()
{
    m_impl->onInputCalibrationDismissed();
}

void EditorController::onOutputGainPreviewChanged(double gain_db)
{
    m_impl->onOutputGainPreviewChanged(gain_db);
}

void EditorController::onOutputGainChanged(double gain_db)
{
    m_impl->onOutputGainChanged(gain_db);
}

void EditorController::onAudioDeviceChangeRequested(
    std::function<void()> change_audio_device, std::function<void()> after_busy_cleared)
{
    m_impl->onAudioDeviceChangeRequested(
        std::move(change_audio_device), std::move(after_busy_cleared));
}

bool EditorController::onAudioDeviceSettingsOpenRequested()
{
    return m_impl->onAudioDeviceSettingsOpenRequested();
}

void EditorController::onAudioDeviceSettingsClosed()
{
    m_impl->onAudioDeviceSettingsClosed();
}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where an optional project operation is omitted.
EditorController::Impl::Impl(
    common::audio::ITransport& transport, common::audio::ISongAudio& song_audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    common::audio::ILiveInput& live_input, EditorController::Services services,
    EditorController::ExitFunction exit_function,
    EditorController::ProjectOperations project_operations)
    : m_transport(transport)
    , m_song_audio(song_audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_live_rig(live_rig)
    , m_live_input(live_input)
    , m_open_function(
          project_operations.open_function ? std::move(project_operations.open_function)
                                           : EditorController::OpenFunction{defaultOpen})
    , m_import_function(
          project_operations.import_function ? std::move(project_operations.import_function)
                                             : EditorController::ImportFunction{defaultImport})
    , m_save_function(
          project_operations.save_function ? std::move(project_operations.save_function)
                                           : EditorController::SaveFunction{defaultSave})
    , m_save_as_function(
          project_operations.save_as_function ? std::move(project_operations.save_as_function)
                                              : EditorController::SaveAsFunction{defaultSaveAs})
    , m_publish_function(
          project_operations.publish_function ? std::move(project_operations.publish_function)
                                              : EditorController::PublishFunction{defaultPublish})
    , m_exit_function(
          exit_function ? std::move(exit_function) : EditorController::ExitFunction{defaultExit})
    , m_settings(services.settings)
    , m_busy(services.message_thread_scheduler, [this] { updateView(); })
    , m_task_runner(services.task_runner)
    , m_transport_listener(transport, *this)
{
    const std::weak_ptr<bool> alive{m_alive};
    m_plugin_host.setPluginWindowCommandObserver(
        common::audio::PluginWindowCommandObserver{
            .undo_requested =
                [this, alive] {
                    if (alive.expired())
                    {
                        return;
                    }
                    onUndoRequested();
                },
            .redo_requested =
                [this, alive] {
                    if (alive.expired())
                    {
                        return;
                    }
                    onRedoRequested();
                },
        });
    m_plugin_host.setPluginParameterEditObserver(
        common::audio::PluginParameterEditObserver{
            .pending_changed =
                [this, alive](bool pending) {
                    if (alive.expired())
                    {
                        return;
                    }
                    onPluginParameterPendingChanged(pending);
                },
            .edit_completed =
                [this, alive](common::audio::PluginParameterEdit edit) {
                    if (alive.expired())
                    {
                        return;
                    }
                    onPluginParameterEditCompleted(std::move(edit));
                },
        });
    m_plugin_host.setPluginStateEditObserver(
        common::audio::PluginStateEditObserver{
            .edit_completed = [this, alive](common::audio::PluginStateEdit edit) {
                if (alive.expired())
                {
                    return;
                }
                onPluginStateEditCompleted(std::move(edit));
            },
        });
    restoreAudioDeviceState();
    common::audio::IAudioDeviceConfiguration::Listener& self_as_listener = *this;
    m_audio_device_listener = std::make_unique<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>(m_audio_devices, self_as_listener);
    selectInputCalibrationForCurrentRoute();
    applyLiveInputGate();
    m_last_state = deriveViewState();
}

// Resets the liveness flag first so any background task completion that fires after this point
// sees weak_ptr.expired() and skips touching now-destroyed members. JUCE's single-threaded
// message manager already serializes destruction with completion dispatch, so the window of
// concern is between this destructor returning and the MessageManager itself being torn down.
EditorController::Impl::~Impl()
{
    // Stop any in-flight scan worker before teardown so the task runner's join does not block on a
    // scan that no observer will ever consume.
    cancelActiveScanToken();
    m_plugin_host.setPluginWindowCommandObserver({});
    m_plugin_host.setPluginParameterEditObserver({});
    m_plugin_host.setPluginStateEditObserver({});
    detachView();
    m_alive.reset();
}

// Stores the new view binding and immediately satisfies the "first push at attachment" contract
// using whatever state the controller has cached up to this point.
void EditorController::Impl::attachView(IEditorView& view)
{
    m_view = &view;
    m_busy.attachPresentation(
        [this](std::function<void()> callback) {
            if (m_view == nullptr)
            {
                if (callback)
                {
                    callback();
                }
                return;
            }

            m_view->runAfterBusyOverlayPainted(std::move(callback));
        },
        [this](std::function<void()> callback) {
            if (m_view == nullptr)
            {
                if (callback)
                {
                    callback();
                }
                return;
            }

            m_view->runAfterBusyOverlayRemoved(std::move(callback));
        });
    view.setState(m_last_state);
}

// Opens an editor project package and stores it after audio and Session both accept the song.
void EditorController::Impl::onOpenRequested(std::filesystem::path file)
{
    runAction(EditorAction::OpenProject{std::move(file)});
}

// Opens an editor project package after any project-replacement prompt has been satisfied.
// Pushes busy state, dispatches package IO to the task runner, and returns immediately. The
// final commit runs in completeOpenProject() on the message thread once the worker reports the
// result.
void EditorController::Impl::openProject(
    const std::filesystem::path& file, bool clear_last_open_project_on_failure)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<OpenTaskState>();
    state->file = file;
    state->clear_last_open_project_on_failure = clear_last_open_project_on_failure;
    m_pending_restore_project_file = clear_last_open_project_on_failure
                                         ? std::optional<std::filesystem::path>{file}
                                         : std::nullopt;
    m_restore_interrupted_prompt_file.reset();
    if (clear_last_open_project_on_failure)
    {
        recordSettingsResultBestEffort(
            m_settings.setInterruptedRestoreProject(file), "mark interrupted restore");
    }
    else
    {
        recordSettingsResultBestEffort(
            m_settings.setInterruptedRestoreProject(std::nullopt),
            "clear interrupted restore before open");
    }
    const std::uint64_t token = beginBusy(BusyOperation::OpeningProject);
    EditorController::ProjectOperationProgress report_progress =
        makeBusyProjectOperationProgress(token);

    m_task_runner.submit(
        [state, open_function = m_open_function, report_progress = std::move(report_progress)] {
            state->result = open_function(state->project, state->file, report_progress);
        },
        safeCallback([this, state, token]() mutable {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completeOpenProject(state);
        }));
}

// Applies the worker's open result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeOpenProject(const std::shared_ptr<OpenTaskState>& state)
{
    assert(isBusy() && "completeOpenProject called outside a busy operation");

    if (!state->result.has_value())
    {
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt), "clear last project after open error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);
    ProjectEditorState editor_state = state->project.editorState();

    auto song_loaded = loadSessionSong(std::move(song), editor_state.selected_arrangement);
    if (!song_loaded.has_value())
    {
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt),
                "clear last project after audio-load error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        finishBusyOperation();
        reportError(
            std::string{"Could not load audio from: "} + state->file.string() + ": " +
            song_loaded.error().message);
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_busy.currentToken(),
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state, captured_editor_state = std::move(editor_state)](
                          std::expected<void, common::audio::LiveRigError> rig_result) {
                finishOpenProjectAfterLiveRigLoad(
                    state, captured_editor_state, std::move(rig_result));
            },
        });
}

// Commits a fully loaded editor project, or tears down the partial session on rig-load failure.
// Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before this
// finalizer runs.
void EditorController::Impl::finishOpenProjectAfterLiveRigLoad(
    const std::shared_ptr<OpenTaskState>& state, const ProjectEditorState& editor_state,
    std::expected<void, common::audio::LiveRigError> rig_result)
{
    assert(isBusy() && "finishOpenProjectAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        clearActiveArrangementBestEffort("open live-rig failure teardown");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt),
                "clear last project after live-rig error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        resetUndoHistory("undo.reset.open_live_rig_failed");
        finishBusyOperation();
        reportError(
            std::string{"Could not load live rig from: "} + state->file.string() + ": " +
            rig_result.error().message);
        return;
    }

    const bool next_has_unsaved_changes = state->project.audioNormalizationUpdatedOnLoad();
    const common::core::TimePosition next_cursor_position =
        session().timeline().clamp(editor_state.cursor_position);
    std::filesystem::path next_project_file{state->file};

    m_project = std::move(state->project);
    m_project_file.swap(next_project_file);
    m_pending_restore_project_file.reset();
    m_displaced_project_file.clear();
    m_save_requires_destination = false;
    m_has_untracked_unsaved_changes = next_has_unsaved_changes;
    m_session_faulted = false;
    clearDeferredProjectAction();
    m_project_audio_ready = true;
    resetUndoHistory("undo.reset.open_project");
    markUndoHistoryClean("undo.mark_clean.open_project");

    m_transport.seek(next_cursor_position);
    if (state->clear_last_open_project_on_failure)
    {
        clearInterruptedRestoreMarker();
    }
    applyLiveInputGate();

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Imports a song source and stores the workspace only after audio and Session accept the song.
void EditorController::Impl::onImportRequested(std::filesystem::path file)
{
    runAction(EditorAction::ImportSong{std::move(file)});
}

// Imports a song source after any current project-replacement prompt has been satisfied. Same
// shape as openProject(): busy + worker dispatch here, commit in completeImportSongSource().
void EditorController::Impl::importSongSource(const std::filesystem::path& file)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<ImportTaskState>();
    state->file = file;

    m_pending_restore_project_file.reset();
    m_restore_interrupted_prompt_file.reset();
    clearInterruptedRestoreMarker();
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    EditorController::ProjectOperationProgress report_progress =
        makeBusyProjectOperationProgress(token);

    m_task_runner.submit(
        [state, import_function = m_import_function, report_progress = std::move(report_progress)] {
            state->result = import_function(state->project, state->file, report_progress);
        },
        safeCallback([this, state, token]() mutable {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completeImportSongSource(state);
        }));
}

// Applies the worker's import result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeImportSongSource(const std::shared_ptr<ImportTaskState>& state)
{
    assert(isBusy() && "completeImportSongSource called outside a busy operation");

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not import: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    auto song_loaded = loadSessionSong(std::move(song), std::nullopt);
    if (!song_loaded.has_value())
    {
        finishBusyOperation();
        reportError(
            std::string{"Could not load imported audio from: "} + state->file.string() + ": " +
            song_loaded.error().message);
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_busy.currentToken(),
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state](std::expected<void, common::audio::LiveRigError> rig_result) {
                finishImportSongSourceAfterLiveRigLoad(state, std::move(rig_result));
            },
        });
}

// Commits a fully imported editor workspace, or tears down the partial session on rig-load
// failure. Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before
// this finalizer runs.
void EditorController::Impl::finishImportSongSourceAfterLiveRigLoad(
    const std::shared_ptr<ImportTaskState>& state,
    std::expected<void, common::audio::LiveRigError> rig_result)
{
    assert(isBusy() && "finishImportSongSourceAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        clearActiveArrangementBestEffort("import live-rig failure teardown");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        resetUndoHistory("undo.reset.import_live_rig_failed");
        finishBusyOperation();
        reportError(
            std::string{"Could not load imported live rig from: "} + state->file.string() + ": " +
            rig_result.error().message);
        return;
    }

    m_displaced_project_file = !m_project_file.empty() ? m_project_file : m_displaced_project_file;
    m_project = std::move(state->project);
    m_project_file.clear();
    m_save_requires_destination = true;
    m_has_untracked_unsaved_changes = false;
    m_session_faulted = false;
    clearDeferredProjectAction();
    m_project_audio_ready = true;
    resetUndoHistory("undo.reset.import_project");
    markUndoHistoryClean("undo.mark_clean.import_project");
    applyLiveInputGate();

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Cancels the active cancellable busy operation. Only the plugin catalog scan is cancellable;
// project open/import run to completion because they replace the live session as they load.
void EditorController::Impl::cancelBusyOperation()
{
    const std::optional<BusyViewState> busy = m_busy.viewState();
    if (!busy.has_value() || !busy->cancel_enabled)
    {
        return;
    }

    if (busy->operation == BusyOperation::ScanningPlugins)
    {
        cancelPluginCatalogScan();
    }
}

// Requests cooperative worker cancellation, then keeps whatever the host already published as known
// and invalidates the pending scan completion.
void EditorController::Impl::cancelPluginCatalogScan()
{
    cancelActiveScanToken();
    m_busy.supersede();
    refreshKnownPluginCatalog();
    updateView();
}

// Trips the in-flight plugin scan's cancellation token so the scan worker stops at the next
// candidate. Used for explicit cancellation and whenever a scan is abandoned by a takeover (close,
// exit) or controller teardown, so a long scan never keeps running unobserved.
void EditorController::Impl::cancelActiveScanToken()
{
    if (m_plugin_scan_cancel.has_value())
    {
        m_plugin_scan_cancel->cancel();
        m_plugin_scan_cancel.reset();
    }
}

// Bridges project-operation progress from worker operations into the controller's busy workflow.
// The audio-analysis phase waits briefly after posting the transition so users see the analyzing
// overlay before expensive normalization work starts.
EditorController::ProjectOperationProgress EditorController::Impl::makeBusyProjectOperationProgress(
    std::uint64_t token)
{
    return [alive = std::weak_ptr<bool>{m_alive}, token, controller = this](
               EditorController::ProjectOperationPhase phase) {
        switch (phase)
        {
            case EditorController::ProjectOperationPhase::AnalyzingBackingAudio:
            {
                if (alive.expired())
                {
                    return;
                }
                controller->m_busy.transitionAfterPaintAndWaitFromWorker(
                    BusyOperation::AnalyzingBackingAudio, token);
                break;
            }
        }
    };
}

common::audio::PluginCatalogScanProgressCallback EditorController::Impl::
    makePluginCatalogScanProgress(std::uint64_t token)
{
    return [alive = std::weak_ptr<bool>{m_alive}, token, controller = this](
               const common::audio::PluginCatalogScanProgress& progress) {
        if (alive.expired())
        {
            return;
        }

        common::audio::PluginCatalogScanProgress progress_snapshot = progress;
        if (!controller->m_busy.postToMessageThread(controller->safeCallback(
                [controller, token, progress_snapshot = std::move(progress_snapshot)] {
                    if (!controller->m_busy.isCurrentToken(token))
                    {
                        return;
                    }
                    controller->m_busy.updatePluginCatalogScanProgress(progress_snapshot);
                })))
        {
            return;
        }
    };
}

// Runs the shared project-load live-rig stage. Tone-bearing arrangements switch the busy overlay
// into determinate progress and wait for that state to paint before live-rig restore starts.
void EditorController::Impl::runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state)
{
    if (!stage_state.finish || !m_busy.isCurrentToken(stage_state.token))
    {
        return;
    }

    const bool report_progress = shouldShowLiveRigLoadProgress();
    if (!report_progress)
    {
        startLiveRigLoadStage(std::move(stage_state), false);
        return;
    }

    m_busy.beginLiveRigLoadProgress();
    m_busy.runAfterBusyPresentationReady([this, captured_stage = std::move(stage_state)]() mutable {
        startLiveRigLoadStage(std::move(captured_stage), true);
    });
}

// Starts the audio-boundary live-rig restore and routes only current-token completions to the
// stage finalizer. Signal-chain view state is updated only after the token check so a
// superseded restore cannot repopulate plugins after close or replacement.
void EditorController::Impl::startLiveRigLoadStage(
    ProjectLoadLiveRigStage stage_state, bool report_progress)
{
    if (!stage_state.finish || !m_busy.isCurrentToken(stage_state.token))
    {
        return;
    }

    const std::uint64_t token = stage_state.token;

    // Resolve the directory before moving the stage into the completion lambda. MSVC may evaluate
    // later call arguments before earlier ones, so reading stage_state.song_directory inline would
    // risk reading after move.
    const std::filesystem::path song_directory = stage_state.song_directory;
    restoreLiveRig(
        song_directory,
        report_progress,
        token,
        safeCallback(
            [this, token, report_progress, captured_stage = std::move(stage_state)](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    rig_result) mutable {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                if (!rig_result.has_value())
                {
                    captured_stage.finish(std::unexpected{std::move(rig_result.error())});
                    return;
                }

                if (rig_result->plugins.size() > common::audio::max_signal_chain_plugins)
                {
                    captured_stage.finish(
                        std::unexpected{signalChainLimitError(rig_result->plugins.size())});
                    return;
                }

                m_signal_chain.replaceSnapshot(
                    common::audio::PluginChainSnapshot{.plugins = rig_result->plugins});
                m_output_gain_db = rig_result->output_gain.db;
                m_output_gain_preview_before.reset();
                applyLiveInputGate();
                if (!report_progress)
                {
                    captured_stage.finish({});
                    return;
                }

                if (m_busy.setLiveRigLoadProgress("Live rig loaded.", 1.0))
                {
                    updateView();
                }

                // Wall-clock delay so the 100% state stays visible long enough for the user to see.
                constexpr std::chrono::milliseconds minimum_completion_display_time{500};
                const std::function<void()> finish_stage_after_hold =
                    [this, token, timer_stage = std::move(captured_stage)]() mutable {
                        if (!m_busy.isCurrentToken(token))
                        {
                            return;
                        }
                        timer_stage.finish({});
                    };

                if (!m_busy.callAfterDelay(
                        minimum_completion_display_time, finish_stage_after_hold))
                {
                    finish_stage_after_hold();
                }
            }));
}

// Saves to the current destination when one exists; Save As is responsible for destination choice.
void EditorController::Impl::onSaveRequested()
{
    runAction(EditorAction::SaveProject{});
}

// Saves to a chosen destination and promotes future Save commands to direct saves.
void EditorController::Impl::onSaveAsRequested(std::filesystem::path file)
{
    runAction(EditorAction::SaveProjectAs{std::move(file)});
}

// Publishes the current project as a native song package without changing save destination or
// dirty state.
void EditorController::Impl::onPublishRequested(std::filesystem::path file)
{
    runAction(EditorAction::PublishProject{std::move(file)});
}

// Cancels only a Save As chooser that was opened to continue a deferred project command.
void EditorController::Impl::onSaveAsCancelled()
{
    runAction(EditorAction::CancelSaveAsPrompt{});
}

// Cancels only operations that published a cancellable busy state to the view.
void EditorController::Impl::onBusyCancelRequested()
{
    runAction(EditorAction::CancelBusyOperation{});
}

// Routes Undo through the central action gate so UI and direct requests share policy.
void EditorController::Impl::onUndoRequested()
{
    runAction(EditorAction::Undo{});
}

// Routes Redo through the central action gate so UI and direct requests share policy.
void EditorController::Impl::onRedoRequested()
{
    runAction(EditorAction::Redo{});
}

// Closes the current project after prompting for unsaved changes when needed.
void EditorController::Impl::onCloseRequested()
{
    runAction(EditorAction::CloseProject{});
}

// Exits through the composition host after prompting for unsaved changes when needed.
void EditorController::Impl::onExitRequested()
{
    runAction(EditorAction::ExitApplication{});
}

// Applies the user's unsaved-changes choice to the stored deferred project command.
void EditorController::Impl::onUnsavedChangesDecision(UnsavedChangesDecision decision)
{
    runAction(EditorAction::ResolveUnsavedChangesPrompt{decision});
}

// Resolves the next-launch recovery prompt shown after startup restore was interrupted.
void EditorController::Impl::onRestoreInterruptedDecision(RestoreInterruptedDecision decision)
{
    if (!m_restore_interrupted_prompt_file.has_value())
    {
        updateView();
        return;
    }

    const std::filesystem::path project_file = *m_restore_interrupted_prompt_file;
    m_restore_interrupted_prompt_file.reset();
    switch (decision)
    {
        case RestoreInterruptedDecision::Retry:
        {
            if (!projectFileExists(project_file))
            {
                clearLastOpenProjectIfMatches(project_file);
                clearInterruptedRestoreMarker();
                updateView();
                return;
            }

            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(project_file), "store retry restore project");
            runAction(EditorAction::RestoreProject{project_file});
            break;
        }
        case RestoreInterruptedDecision::Cancel:
        {
            m_pending_restore_project_file.reset();
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt), "clear canceled restore project");
            clearInterruptedRestoreMarker();
            updateView();
            break;
        }
    }
}

// Ignores the intent until audio activation has committed an arrangement, otherwise toggles
// playback.
void EditorController::Impl::onPlayPausePressed()
{
    runAction(EditorAction::PlayPause{});
}

// Mirrors the published transport.stop_enabled gate so the keyboard or alternate input paths
// cannot stop a transport the view considers already reset.
void EditorController::Impl::onStopPressed()
{
    runAction(EditorAction::Stop{});
}

// Clamps the normalized input and converts it through the session timeline so the seek target
// stays inside the loaded content even when the view emits out-of-range values.
void EditorController::Impl::onWaveformClicked(double normalized_x)
{
    runAction(EditorAction::SeekWaveform{normalized_x});
}

// Shows the plugin browser with whatever plugins the host already knows. Full catalog discovery is
// intentionally left behind the explicit Rescan button because plugin scans can be slow.
void EditorController::Impl::onPluginBrowserRequested()
{
    runAction(EditorAction::ShowPluginBrowser{});
}

// Opens the plugin browser for a specific signal-chain insertion slot.
void EditorController::Impl::onPluginInsertSlotSelected(
    std::size_t chain_index, std::size_t block_index)
{
    runAction(EditorAction::BeginPluginInsert{chain_index, block_index});
}

// Hides the browser directly because closing a presentation window should not be blocked by an
// unrelated busy operation. In-flight scans still complete against the cached browser state.
void EditorController::Impl::onPluginBrowserClosed()
{
    m_signal_chain.clearPendingInsertion();
    if (!m_plugin_catalog.close())
    {
        return;
    }
    updateView();
}

// Starts a user-requested catalog refresh through the normal action gate.
void EditorController::Impl::onPluginCatalogScanRequested()
{
    runAction(EditorAction::ScanPluginCatalog{});
}

// Inserts the plugin selected by the browser window.
void EditorController::Impl::onSelectedPluginInsertRequested(std::string plugin_id)
{
    runAction(EditorAction::InsertSelectedPlugin{std::move(plugin_id)});
}

// Removes one runtime plugin instance from the current linear chain.
void EditorController::Impl::onRemovePluginRequested(std::string instance_id)
{
    runAction(EditorAction::RemovePlugin{std::move(instance_id)});
}

// Moves one runtime plugin instance to a new final index in the current linear chain.
void EditorController::Impl::onMovePluginRequested(
    std::string instance_id, std::size_t destination_index,
    std::vector<PluginBlockAssignment> placement)
{
    runAction(
        EditorAction::MovePlugin{std::move(instance_id), destination_index, std::move(placement)});
}

// Records a placement-only edit through the normal action gate. A no-op report is ignored by the
// workflow action.
void EditorController::Impl::onSignalChainPlacementChanged(
    std::vector<PluginBlockAssignment> placement)
{
    runAction(EditorAction::SetSignalChainPlacement{std::move(placement)});
}

// Records a display-only signal-chain edit through the same action gate used by placement.
void EditorController::Impl::onPluginDisplayTypeOverrideChanged(
    std::string instance_id, std::optional<PluginDisplayType> display_type)
{
    runAction(EditorAction::SetPluginDisplayTypeOverride{std::move(instance_id), display_type});
}

// Opens the hosted plugin editor window for a row-level signal-chain request.
void EditorController::Impl::onOpenPluginRequested(std::string instance_id)
{
    runAction(EditorAction::OpenPlugin{std::move(instance_id)});
}

// Wraps the supplied audio-device open work in the editor's busy overlay paint fence so the
// blocking presentation paints once before juce::AudioDeviceManager occupies the message thread.
// The settings dialog launcher provides work already aware of dialog success/failure handling, so
// this method owns only the busy lifecycle.
void EditorController::Impl::onAudioDeviceChangeRequested(
    std::function<void()> change_audio_device, std::function<void()> after_busy_cleared)
{
    if (!change_audio_device || m_input_calibration.promptVisible())
    {
        if (after_busy_cleared)
        {
            after_busy_cleared();
        }
        return;
    }

    m_busy.runMessageThreadBusyOperation(
        BusyOperation::OpeningAudioDevice,
        std::move(change_audio_device),
        std::move(after_busy_cleared));
}

// Persists the new device manager state and re-derives view state after a configuration change.
void EditorController::Impl::onAudioDeviceConfigurationChanged()
{
    persistAudioDeviceState();
    selectInputCalibrationForCurrentRoute();
    applyLiveInputGate();
    updateView();
}

// Marks the audio settings window active so route transitions can be committed as one change.
// Refuses while the calibration prompt is up so the two modal flows never overlap.
bool EditorController::Impl::onAudioDeviceSettingsOpenRequested()
{
    if (m_input_calibration.promptVisible())
    {
        return false;
    }

    if (m_transport.state().playing)
    {
        m_transport.pause();
    }

    executeInputCalibrationEffects(m_input_calibration.openAudioDeviceSettings());
    updateView();
    return true;
}

// Re-applies the route gate after settings closes or restores its previous route.
void EditorController::Impl::onAudioDeviceSettingsClosed()
{
    selectInputCalibrationForCurrentRoute();
    m_input_calibration.closeAudioDeviceSettings();
    applyLiveInputGate();
    updateView();
}

// Applies the central action gate and routes the accepted action.
void EditorController::Impl::runAction(EditorAction::Action action)
{
    const EditorAction::Id action_id = idOf(action);
    logEditorActionRequested(action_id);
    if (!isBusy())
    {
        flushPendingPluginParameterEdits("plugin_parameter.action_dispatch");
    }

    if (!prepareAction(action_id))
    {
        return;
    }

    logEditorActionStarted(action_id);
    performAction(std::move(action));
    logEditorActionDispatchCompleted(action_id);
}

// Applies availability and busy policy before an action mutates state or schedules work.
bool EditorController::Impl::prepareAction(EditorAction::Id action)
{
    const ActionConditions conditions = currentActionConditions();
    if (!isActionAvailable(action, conditions))
    {
        logEditorActionAvailabilityRejected(action, actionUnavailableReason(action, conditions));
        return false;
    }

    if (conditions.busy && actionSupersedesBusy(action))
    {
        // A close/exit takeover abandons any in-flight scan, so stop its worker rather than
        // leaving it running until it finishes on its own (which would also block exit-time join).
        cancelActiveScanToken();
        m_busy.supersede();
    }

    return true;
}

// Visits the variant once and dispatches to a typed overload per case. The overloads keep each
// case body short and individually testable, with payload access through the alternative's fields.
void EditorController::Impl::performAction(EditorAction::Action action)
{
    std::visit(
        [this](auto&& a) { performActionImpl(std::forward<decltype(a)>(a)); }, std::move(action));
}

void EditorController::Impl::performActionImpl(EditorAction::OpenProject action)
{
    requestProjectAction(EditorAction::OpenProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::RestoreProject action)
{
    requestProjectAction(EditorAction::RestoreProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::ImportSong action)
{
    requestProjectAction(EditorAction::ImportSong{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProject /*action*/)
{
    if (m_save_requires_destination)
    {
        return;
    }
    runProjectAction(EditorAction::SaveProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProjectAs action)
{
    // When this Save As is continuing a deferred action, dismiss the chooser phase after the
    // action gate accepts the save. A standalone Save As leaves this a no-op.
    m_deferred_project_action_state.saveAsPathChosen();
    runProjectAction(EditorAction::SaveProjectAs{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::PublishProject action)
{
    runProjectAction(EditorAction::PublishProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::CloseProject /*action*/)
{
    requestProjectAction(EditorAction::CloseProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::ExitApplication /*action*/)
{
    requestProjectAction(EditorAction::ExitApplication{});
}

void EditorController::Impl::performActionImpl(EditorAction::ResolveUnsavedChangesPrompt action)
{
    if (m_session_faulted && action.decision == UnsavedChangesDecision::Save)
    {
        reportError(
            "Cannot save while the live editor state is untrusted. Discard changes to continue, "
            "or cancel and reopen the project.");
        updateView();
        return;
    }

    std::visit(
        [this](auto&& step) {
            using Step = std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<Step, DeferredProjectActionState::Refresh>)
            {
                updateView();
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::SaveThenReplay>)
            {
                runProjectAction(EditorAction::SaveProject{});
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::DiscardAndReplay>)
            {
                replayDiscardedProjectAction(std::move(step.action));
            }
        },
        m_deferred_project_action_state.resolveUnsavedChanges(
            action.decision, m_save_requires_destination));
}

// Discards the current project's unsaved changes and replays the released deferred action now.
void EditorController::Impl::replayDiscardedProjectAction(EditorAction::ProjectAction action)
{
    const EditorAction::Id deferred_id = idOf(action);
    std::filesystem::path displaced_by_import;
    if (deferred_id == EditorAction::Id::ImportSong)
    {
        displaced_by_import = !m_project_file.empty() ? m_project_file : m_displaced_project_file;
    }
    m_has_untracked_unsaved_changes = false;
    m_save_requires_destination = false;
    // CloseProject and ExitApplication both close the current project as part of their own action
    // handler, and ExitApplication additionally needs m_project_file alive when it captures the
    // value to persist as last_open_project. Closing here first would zero that path out, so let
    // the replay action do its own close + capture.
    if (deferred_id == EditorAction::Id::CloseProject ||
        deferred_id == EditorAction::Id::ExitApplication)
    {
        runProjectAction(std::move(action));
        return;
    }

    if (closeProject())
    {
        if (!displaced_by_import.empty())
        {
            m_displaced_project_file = std::move(displaced_by_import);
        }
        runProjectAction(std::move(action));
    }
}

void EditorController::Impl::performActionImpl(EditorAction::CancelSaveAsPrompt /*action*/)
{
    if (!m_deferred_project_action_state.cancelSaveAsPrompt())
    {
        return;
    }

    updateView();
}

void EditorController::Impl::performActionImpl(EditorAction::CancelBusyOperation /*action*/)
{
    cancelBusyOperation();
}

// Begins the next undo transition and dispatches it synchronously or behind the loading fence.
void EditorController::Impl::performActionImpl(EditorAction::Undo /*action*/)
{
    const EditorUndoBeginResult begin = m_undo_history.beginUndo();
    logEditorUndoTransitionResult("undo.begin", begin.result);
    dispatchUndoTransition(begin);
}

// Begins the next redo transition and dispatches it synchronously or behind the loading fence.
void EditorController::Impl::performActionImpl(EditorAction::Redo /*action*/)
{
    const EditorUndoBeginResult begin = m_undo_history.beginRedo();
    logEditorUndoTransitionResult("redo.begin", begin.result);
    dispatchUndoTransition(begin);
}

// Routes a begun transition: plugin-instantiating directions run behind the loading busy fence so
// the message thread is not blocked without feedback; all other edits apply synchronously.
void EditorController::Impl::dispatchUndoTransition(const EditorUndoBeginResult& begin)
{
    if (!begin.pending.has_value())
    {
        updateView();
        return;
    }

    const EditorUndoPendingTransition& pending = *begin.pending;
    if (pending.edit == nullptr)
    {
        abortUndoTransition(pending, EditorUndoFailureCode::PreflightRejected);
        return;
    }

    if (pending.edit->instantiatesPlugin(pending.direction))
    {
        applyUndoTransitionBehindBusy(pending);
        return;
    }

    completeUndoTransition(pending, applyPendingEdit(pending));
}

// Runs the pending edit's side effects in the transition direction without touching history state.
std::expected<void, EditorUndoFailureCode> EditorController::Impl::applyPendingEdit(
    const EditorUndoPendingTransition& pending)
{
    EditorEditContext context = editContext();
    return pending.direction == EditorUndoDirection::Undo ? pending.edit->undo(context)
                                                          : pending.edit->redo(context);
}

// Aborts a pending transition and centralizes the history log, fault, and view-refresh policy.
void EditorController::Impl::abortUndoTransition(
    const EditorUndoPendingTransition& pending, const EditorUndoFailureCode failure_code)
{
    const bool is_undo = pending.direction == EditorUndoDirection::Undo;
    const EditorUndoTransitionResult abort = m_undo_history.abort(pending, failure_code);
    logEditorUndoTransitionResult(is_undo ? "undo.abort" : "redo.abort", abort);
    if (abort.requires_fault)
    {
        faultSessionAfterRollbackContractViolation(is_undo ? "undo.apply" : "redo.apply", pending);
        return;
    }

    updateView();
}

// Commits the applied transition, or aborts and faults the session on a rollback violation.
void EditorController::Impl::completeUndoTransition(
    const EditorUndoPendingTransition& pending, std::expected<void, EditorUndoFailureCode> applied)
{
    const bool is_undo = pending.direction == EditorUndoDirection::Undo;
    if (!applied.has_value())
    {
        abortUndoTransition(pending, applied.error());
        return;
    }

    const EditorUndoTransitionResult commit = m_undo_history.commit(pending);
    logEditorUndoTransitionResult(is_undo ? "undo.commit" : "redo.commit", commit);
    updateView();
}

// Defers a plugin-instantiating undo/redo until the loading overlay has painted, matching insert.
void EditorController::Impl::applyUndoTransitionBehindBusy(
    const EditorUndoPendingTransition& pending)
{
    const std::uint64_t token = beginBusy(BusyOperation::LoadingPlugin);
    m_busy.runAfterBusyPresentationReady([this, pending, token]() {
        // Close/exit can steal the busy token before an unsaved-changes prompt decides whether the
        // project will close. Abort the untouched transition so cancelling that prompt does not
        // leave the history permanently pending.
        if (!m_busy.isCurrentToken(token))
        {
            abortUndoTransition(pending, EditorUndoFailureCode::PreflightRejected);
            return;
        }

        // Run the recreate while the overlay is visible, clear it, then report the outcome so a
        // failure dialog never overlays a stale busy view (matching the insert completion order).
        std::expected<void, EditorUndoFailureCode> applied = applyPendingEdit(pending);
        finishBusyOperation();
        completeUndoTransition(pending, std::move(applied));
    });
}

// Collects the current apply-time dependencies for editor-owned edit objects.
EditorEditContext EditorController::Impl::editContext() noexcept
{
    return EditorEditContext{
        .signal_chain = m_signal_chain,
        .plugin_host = m_plugin_host,
        .live_rig = m_live_rig,
        .output_gain_db = m_output_gain_db,
    };
}

// Pushes one already-applied user edit into the product-level history stack.
void EditorController::Impl::pushUndoEntry(std::unique_ptr<IEdit> edit)
{
    const bool had_edit = edit != nullptr;
    const EditorUndoTransitionResult result = m_undo_history.push(std::move(edit));
    logEditorUndoTransitionResult("undo.push", result);
    if (had_edit && result.status != EditorUndoTransitionStatus::Applied)
    {
        markUntrackedUnsavedEdit("undo.reset.failed_push");
    }
}

// Pushes a net-changed output-gain command into product-level history.
void EditorController::Impl::pushOutputGainUndoEntry(
    common::audio::Gain before_gain, common::audio::Gain after_gain)
{
    if (before_gain == after_gain)
    {
        return;
    }

    RH_LOG_INFO(
        "editor.controller",
        "Completed output gain edit before_db={} after_db={}",
        before_gain.db,
        after_gain.db);
    auto edit = std::make_unique<OutputGainEdit>();
    edit->before_gain = before_gain;
    edit->after_gain = after_gain;
    pushUndoEntry(std::move(edit));
}

// Marks the live backend untrusted and routes the user toward reopening or closing the project.
void EditorController::Impl::enterFaultedSession()
{
    m_session_faulted = true;
    markUntrackedUnsavedChanges();
    updateView();
    reportError(
        "An unexpected internal error left the live editor state untrusted. Please report this "
        "bug and attach the editor log file, then reopen or close the project before continuing.");
}

// Enters the recovery-only state after an undo/redo port reports a broken rollback contract.
void EditorController::Impl::faultSessionAfterRollbackContractViolation(
    std::string_view context, const EditorUndoPendingTransition& pending)
{
    const std::string label = pending.edit != nullptr ? pending.edit->label() : std::string{};
    const std::string_view direction = undoDirectionText(pending.direction);
    RH_LOG_ERROR(
        "editor.controller",
        "Rollback contract violation context={} direction={} label={}",
        context,
        direction,
        label);
    enterFaultedSession();
}

// Enters the recovery-only state after a non-undo rollback contract violation.
void EditorController::Impl::faultSessionAfterRollbackContractViolation(
    std::string_view context, std::string_view detail)
{
    RH_LOG_ERROR(
        "editor.controller", "Rollback contract violation context={} detail={}", context, detail);
    enterFaultedSession();
}

// Applies output gain previews immediately, but records only committed values in undo history.
void EditorController::Impl::applyOutputGainChange(double gain_db, OutputGainChangeIntent intent)
{
    if (!m_project_audio_ready || !hasLoadedArrangement() || isBusy() || m_session_faulted)
    {
        return;
    }

    const bool is_commit = intent == OutputGainChangeIntent::Commit;
    if (is_commit)
    {
        if (!m_output_gain_preview_before.has_value())
        {
            flushPendingPluginParameterEdits("plugin_parameter.output_gain_commit");
        }
    }
    else if (!m_output_gain_preview_before.has_value())
    {
        flushPendingPluginParameterEdits("plugin_parameter.output_gain_preview");
        m_output_gain_preview_before = common::audio::Gain{m_output_gain_db};
    }

    const common::audio::Gain before_gain =
        m_output_gain_preview_before.value_or(common::audio::Gain{m_output_gain_db});
    const auto gain = common::audio::clampGain(common::audio::Gain{gain_db});
    const bool needs_live_update = gain.db != m_output_gain_db;

    if (!needs_live_update)
    {
        if (is_commit && m_output_gain_preview_before.has_value())
        {
            pushOutputGainUndoEntry(before_gain, common::audio::Gain{m_output_gain_db});
            m_output_gain_preview_before.reset();
            updateView();
        }
        return;
    }

    const auto result = m_live_rig.setOutputGain(gain);
    if (!result.has_value())
    {
        reportError(std::string{"Could not set output gain: "} + result.error().message);
        if (is_commit && m_output_gain_preview_before.has_value())
        {
            pushOutputGainUndoEntry(before_gain, common::audio::Gain{m_output_gain_db});
        }
        m_output_gain_preview_before.reset();
        updateView();
        return;
    }

    m_output_gain_db = gain.db;
    if (is_commit)
    {
        pushOutputGainUndoEntry(before_gain, gain);
        m_output_gain_preview_before.reset();
    }
    updateView();
}

// Clears the current undo stack at a project or partial-coverage invalidation boundary.
void EditorController::Impl::resetUndoHistory(std::string_view context)
{
    m_output_gain_preview_before.reset();
    const EditorUndoTransitionResult result = m_undo_history.reset();
    logEditorUndoTransitionResult(context, result);
}

// Marks the current undo position as matching the saved project state.
void EditorController::Impl::markUndoHistoryClean(std::string_view context)
{
    const EditorUndoTransitionResult result = m_undo_history.markClean();
    logEditorUndoTransitionResult(context, result);
}

// Records dirty state that cannot be tracked by a reachable undo-history clean marker.
void EditorController::Impl::markUntrackedUnsavedChanges() noexcept
{
    m_has_untracked_unsaved_changes = true;
}

// Records a successful mutation outside reliable undo coverage and invalidates existing history.
void EditorController::Impl::markUntrackedUnsavedEdit(std::string_view context)
{
    markUntrackedUnsavedChanges();
    clearUndoHistoryAfterUntrackedEdit(context);
}

// Discards history once a newer untracked edit would make the undo stack partial.
void EditorController::Impl::clearUndoHistoryAfterUntrackedEdit(std::string_view context)
{
    if (!m_undo_history.canUndo() && !m_undo_history.canRedo() &&
        !m_undo_history.hasPendingTransition())
    {
        return;
    }

    resetUndoHistory(context);
}

// Settles any host-observed plugin parameter value edit before a controller action runs.
void EditorController::Impl::flushPendingPluginParameterEdits(std::string_view context)
{
    if (!m_plugin_host.hasPendingPluginParameterEdits())
    {
        return;
    }

    RH_LOG_INFO("editor.controller", "Flushing pending plugin parameter edit context={}", context);
    m_plugin_host.flushPendingPluginParameterEdits();
}

// Reflects host parameter-observation state transitions into logs and derived view state.
void EditorController::Impl::onPluginParameterPendingChanged(bool pending)
{
    RH_LOG_INFO("editor.controller", "Plugin parameter edit pending changed pending={}", pending);
    updateView();
}

// Commits one settled host parameter value edit into product-level undo history.
void EditorController::Impl::onPluginParameterEditCompleted(common::audio::PluginParameterEdit edit)
{
    if (!hasLoadedArrangement() || !m_signal_chain.containsInstance(edit.instance_id))
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped stale plugin parameter edit instance_id={}",
            edit.instance_id);
        return;
    }

    if (edit.before_normalized == edit.after_normalized)
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped unchanged plugin parameter edit instance_id={}",
            edit.instance_id);
        return;
    }

    RH_LOG_INFO(
        "editor.controller",
        "Completed plugin parameter edit instance_id={} parameter_id={} label_hint={}",
        edit.instance_id,
        edit.parameter_id,
        edit.label_hint);
    auto undo_edit = std::make_unique<PluginParameterEdit>();
    undo_edit->instance_id = std::move(edit.instance_id);
    undo_edit->parameter_id = std::move(edit.parameter_id);
    undo_edit->parameter_index = edit.parameter_index;
    undo_edit->before_normalized = edit.before_normalized;
    undo_edit->after_normalized = edit.after_normalized;
    undo_edit->label_hint = std::move(edit.label_hint);
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Commits one settled host processor-wide state edit into product-level undo history.
void EditorController::Impl::onPluginStateEditCompleted(common::audio::PluginStateEdit edit)
{
    if (!hasLoadedArrangement() || !m_signal_chain.containsInstance(edit.instance_id))
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped stale plugin state edit instance_id={}",
            edit.instance_id);
        return;
    }

    if (edit.before == edit.after && edit.before_parameters == edit.after_parameters)
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped unchanged plugin state edit instance_id={}",
            edit.instance_id);
        return;
    }

    RH_LOG_INFO(
        "editor.controller",
        "Completed plugin state edit instance_id={} label_hint={}",
        edit.instance_id,
        edit.label_hint);
    auto undo_edit = std::make_unique<PluginStateEdit>();
    undo_edit->instance_id = std::move(edit.instance_id);
    undo_edit->before_state = std::move(edit.before);
    undo_edit->after_state = std::move(edit.after);
    undo_edit->before_parameters = std::move(edit.before_parameters);
    undo_edit->after_parameters = std::move(edit.after_parameters);
    undo_edit->label_hint = std::move(edit.label_hint);
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

void EditorController::Impl::performActionImpl(EditorAction::PlayPause /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (m_transport.state().playing)
    {
        m_transport.pause();
    }
    else
    {
        m_transport.play();
    }
}

void EditorController::Impl::performActionImpl(EditorAction::Stop /*action*/)
{
    const common::audio::TransportState transport_state = m_transport.state();
    if (!canStopTransport(transport_state))
    {
        return;
    }
    m_transport.stop();

    if (!transport_state.playing)
    {
        updateView();
    }
}

void EditorController::Impl::performActionImpl(EditorAction::SeekWaveform action)
{
    const double clamped = std::clamp(action.normalized_x, 0.0, 1.0);
    const common::core::TimeRange timeline_range = session().timeline();
    const double target_seconds =
        timeline_range.start.seconds + clamped * timeline_range.duration().seconds;
    m_transport.seek(timeline_range.clamp(common::core::TimePosition{target_seconds}));
    updateView();
}

// Makes the browser visible and refreshes the lightweight in-memory catalog.
void EditorController::Impl::performActionImpl(EditorAction::ShowPluginBrowser /*action*/)
{
    if (!hasLoadedArrangement() || !m_signal_chain.hasInsertCapacity())
    {
        return;
    }

    m_signal_chain.requestAppend();
    m_plugin_catalog.open(m_plugin_host.knownPluginCatalog());
    updateView();
}

// Makes the browser visible for a specific chain slot and refreshes the lightweight catalog.
void EditorController::Impl::performActionImpl(EditorAction::BeginPluginInsert action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.requestInsertAt(action.chain_index))
    {
        return;
    }

    // Record the chosen visual block so the insertion snapshot keeps the authored gap layout.
    m_signal_chain.setPendingInsertBlock(action.block_index);
    m_plugin_catalog.open(m_plugin_host.knownPluginCatalog());
    updateView();
}

// Offloads catalog scanning to the editor task runner because directory traversal and plugin
// inspection can execute slow third-party code.
void EditorController::Impl::performActionImpl(EditorAction::ScanPluginCatalog /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    auto state = std::make_shared<PluginCatalogTaskState>();
    const std::uint64_t token = beginBusy(BusyOperation::ScanningPlugins);
    const common::core::CancellationToken cancel;
    m_plugin_scan_cancel = cancel;
    auto report_progress = makePluginCatalogScanProgress(token);
    m_task_runner.submit(
        [state,
         plugin_host = &m_plugin_host,
         report_progress = std::move(report_progress),
         cancel] { state->scan_result = plugin_host->scanPluginCatalog(report_progress, cancel); },
        safeCallback([this, state, token] {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completePluginCatalogScan(state);
        }));
}

// Begins inserting the selected browser plugin. The catalog is the authority for display
// metadata, while the audio boundary remains the authority for creating the runtime plugin.
void EditorController::Impl::performActionImpl(const EditorAction::InsertSelectedPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<common::audio::PluginCandidate> plugin_candidate =
        m_plugin_catalog.candidateForId(action.plugin_id);
    if (!plugin_candidate.has_value())
    {
        reportError("Could not insert plugin: selected plugin is no longer available");
        updateView();
        return;
    }

    const std::optional<std::size_t> chain_index = m_signal_chain.insertionIndexForSelection();
    if (!chain_index.has_value())
    {
        reportError("Could not insert plugin: insertion position is no longer available");
        updateView();
        return;
    }

    beginInsertKnownPlugin(*plugin_candidate, *chain_index);
}

// Inserts the selected browser plugin into the live chain after the loading state has painted.
void EditorController::Impl::completeSelectedPluginInsert(
    const std::shared_ptr<InsertSelectedPluginTaskState>& state)
{
    assert(isBusy() && "completeSelectedPluginInsert called outside a busy operation");

    const common::audio::PluginCandidate& plugin_candidate = state->plugin_candidate;
    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto insert_result = m_plugin_host.insertPlugin(plugin_candidate, state->chain_index);
    if (!insert_result.has_value())
    {
        const std::string message = insert_result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not insert plugin: "} + message);
        return;
    }

    const std::string inserted_instance_id = insert_result->inserted_instance_id;
    applySignalChainMutationSnapshot(std::move(insert_result->snapshot));
    auto inserted_state = m_plugin_host.capturePluginState(inserted_instance_id);
    const std::optional<PluginVisualEditState> visual_state =
        pluginVisualStateFor(m_signal_chain.plugins(), inserted_instance_id);
    if (!inserted_state.has_value() || !visual_state.has_value())
    {
        m_signal_chain.clearPendingInsertion();
        m_plugin_catalog.hide();
        const std::string undo_preparation_error = inserted_state.has_value()
                                                       ? "inserted plugin view state is missing"
                                                       : inserted_state.error().message;
        const InsertUndoPreparationRollbackResult rollback =
            rollbackInsertedPluginAfterUndoPreparationFailure(
                inserted_instance_id, before_placement);
        if (rollback.status == InsertUndoPreparationRollbackStatus::Failed)
        {
            markUntrackedUnsavedEdit("undo.reset.untracked_signal_chain_insert");
        }
        finishBusyOperation();

        switch (rollback.status)
        {
            case InsertUndoPreparationRollbackStatus::RolledBack:
            {
                reportError(
                    std::string{"Could not insert plugin: undo state could not be prepared; insert "
                                "was rolled back: "} +
                    undo_preparation_error);
                return;
            }
            case InsertUndoPreparationRollbackStatus::Failed:
            {
                reportError(
                    std::string{"Could not prepare plugin insert undo: "} + undo_preparation_error +
                    ". The inserted plugin remains loaded because rollback failed: " +
                    rollback.detail);
                return;
            }
            case InsertUndoPreparationRollbackStatus::RollbackContractViolation:
            {
                faultSessionAfterRollbackContractViolation(
                    "insert.undo_preparation_rollback", rollback.detail);
                return;
            }
        }

        return;
    }

    auto undo_edit = std::make_unique<PluginInsertEdit>();
    undo_edit->instance_id = inserted_instance_id;
    undo_edit->chain_index = state->chain_index;
    undo_edit->plugin_state = std::move(*inserted_state);
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    undo_edit->visual_state = *visual_state;
    pushUndoEntry(std::move(undo_edit));

    m_signal_chain.clearPendingInsertion();
    m_plugin_catalog.hide();

    finishBusyOperation();
}

// Removes a just-inserted plugin when undo-entry preparation fails, preserving existing history.
auto EditorController::Impl::rollbackInsertedPluginAfterUndoPreparationFailure(
    const std::string& inserted_instance_id,
    const std::vector<PluginBlockAssignment>& before_placement)
    -> InsertUndoPreparationRollbackResult
{
    auto rollback_snapshot = m_plugin_host.removePlugin(inserted_instance_id);
    if (!rollback_snapshot.has_value())
    {
        if (rollback_snapshot.error().code ==
            common::audio::PluginHostErrorCode::RollbackContractViolation)
        {
            return InsertUndoPreparationRollbackResult{
                .status = InsertUndoPreparationRollbackStatus::RollbackContractViolation,
                .detail = rollback_snapshot.error().message,
            };
        }

        return InsertUndoPreparationRollbackResult{
            .status = InsertUndoPreparationRollbackStatus::Failed,
            .detail = rollback_snapshot.error().message,
        };
    }

    if (pluginSnapshotContainsInstance(*rollback_snapshot, inserted_instance_id))
    {
        return InsertUndoPreparationRollbackResult{
            .status = InsertUndoPreparationRollbackStatus::RollbackContractViolation,
            .detail = "inserted plugin was still present after rollback removal",
        };
    }

    applySignalChainMutationSnapshot(std::move(*rollback_snapshot));
    (void)m_signal_chain.setBlockPlacement(before_placement);
    return {};
}

// Starts the blocking plugin-instantiation phase after pushing LoadingPlugin state first.
void EditorController::Impl::beginInsertKnownPlugin(
    const common::audio::PluginCandidate& plugin_candidate, std::size_t chain_index)
{
    auto state = std::make_shared<InsertSelectedPluginTaskState>();
    state->plugin_candidate = plugin_candidate;
    state->chain_index = chain_index;

    const std::uint64_t token = beginBusy(BusyOperation::LoadingPlugin);
    m_busy.runAfterBusyPresentationReady([this, state, token]() {
        if (!m_busy.isCurrentToken(token))
        {
            return;
        }
        completeSelectedPluginInsert(state);
    });
}

// Applies a successful structural chain mutation from the audio boundary.
void EditorController::Impl::applySignalChainMutationSnapshot(
    common::audio::PluginChainSnapshot snapshot)
{
    m_signal_chain.replaceSnapshot(std::move(snapshot));
}

// Replaces the browser catalog with the latest scan result while keeping the browser open.
void EditorController::Impl::completePluginCatalogScan(
    const std::shared_ptr<PluginCatalogTaskState>& state)
{
    assert(isBusy() && "completePluginCatalogScan called outside a busy operation");

    m_plugin_scan_cancel.reset();

    if (!state->scan_result.has_value())
    {
        const std::string message = state->scan_result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not scan plugins: "} + message);
        return;
    }

    // Rescan refreshes the host-owned catalog on a worker thread. The controller reads the
    // canonical known catalog here on the message thread and keeps only a sorted UI snapshot.
    refreshKnownPluginCatalog();
    finishBusyOperation();
}

// Refreshes the browser from Tracktion's already-known plugins without touching the filesystem.
void EditorController::Impl::refreshKnownPluginCatalog()
{
    m_plugin_catalog.replaceCatalog(m_plugin_host.knownPluginCatalog());
}

void EditorController::Impl::performActionImpl(const EditorAction::RemovePlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.containsInstance(action.instance_id))
    {
        return;
    }

    const std::optional<std::size_t> chain_index =
        m_signal_chain.chainIndexForInstance(action.instance_id);
    const std::optional<PluginVisualEditState> visual_state =
        pluginVisualStateFor(m_signal_chain.plugins(), action.instance_id);
    if (!chain_index.has_value() || !visual_state.has_value())
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto plugin_state = m_plugin_host.capturePluginState(action.instance_id);
    if (!plugin_state.has_value())
    {
        reportError(std::string{"Could not remove plugin: "} + plugin_state.error().message);
        updateView();
        return;
    }

    auto snapshot = m_plugin_host.removePlugin(action.instance_id);
    if (!snapshot.has_value())
    {
        reportError(std::string{"Could not remove plugin: "} + snapshot.error().message);
        updateView();
        return;
    }

    applySignalChainMutationSnapshot(std::move(*snapshot));
    auto undo_edit = std::make_unique<PluginRemoveEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->chain_index = *chain_index;
    undo_edit->plugin_state = std::move(*plugin_state);
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    undo_edit->visual_state = *visual_state;
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Moves a plugin through the audio boundary so backend order remains authoritative.
void EditorController::Impl::performActionImpl(const EditorAction::MovePlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<std::size_t> current_index =
        m_signal_chain.chainIndexForInstance(action.instance_id);
    if (!current_index.has_value() || action.destination_index >= m_signal_chain.appendIndex() ||
        *current_index == action.destination_index)
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto snapshot = m_plugin_host.movePlugin(action.instance_id, action.destination_index);
    if (!snapshot.has_value())
    {
        reportError(std::string{"Could not move plugin: "} + snapshot.error().message);
        updateView();
        return;
    }

    applySignalChainMutationSnapshot(std::move(*snapshot));
    // The reorder already changed chain state, so the view must refresh whether or not the
    // instance-keyed placement differed; the [[nodiscard]] result is intentionally ignored.
    (void)m_signal_chain.setBlockPlacement(action.placement);
    auto undo_edit = std::make_unique<PluginMoveEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->before_index = *current_index;
    undo_edit->after_index = action.destination_index;
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Stores a placement-only edit at the controller boundary that receives user placement intents.
void EditorController::Impl::performActionImpl(const EditorAction::SetSignalChainPlacement& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    if (!m_signal_chain.setBlockPlacement(action.placement))
    {
        return;
    }

    auto undo_edit = std::make_unique<PluginPlacementEdit>();
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

void EditorController::Impl::performActionImpl(
    const EditorAction::SetPluginDisplayTypeOverride& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<PluginDisplayType> before_type =
        displayTypeOverrideFor(m_signal_chain.plugins(), action.instance_id);
    if (!m_signal_chain.setPluginDisplayTypeOverride(action.instance_id, action.display_type))
    {
        return;
    }

    auto undo_edit = std::make_unique<PluginDisplayTypeEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->before_type = before_type;
    undo_edit->after_type = action.display_type;
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

void EditorController::Impl::performActionImpl(const EditorAction::OpenPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.containsInstance(action.instance_id))
    {
        return;
    }

    const auto result = m_plugin_host.openPluginWindow(action.instance_id);
    if (!result.has_value())
    {
        reportError(std::string{"Could not open plugin: "} + result.error().message);
    }
}

// Opens the required calibration prompt on explicit user request.
void EditorController::Impl::onInputCalibrationRequested()
{
    if (m_input_calibration.requestPrompt(inputCalibrationContext()) && m_transport.state().playing)
    {
        m_transport.pause();
    }
    updateView();
}

// Prepares the current input route for a raw calibration measurement.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationMeasurementStarted()
{
    auto measurement = m_input_calibration.prepareMeasurementStart(inputCalibrationContext());
    if (!measurement.has_value())
    {
        return std::unexpected{std::move(measurement.error())};
    }

    const InputCalibrationRouteState previous_route_state = currentInputCalibrationRouteState();
    auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
    if (!monitoring_disabled.has_value())
    {
        if (monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(monitoring_disabled.error())};
    }

    auto gain_reset =
        m_live_input.setInputGain(common::audio::Gain{common::audio::defaultGainDb()});
    if (!gain_reset.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        if (gain_reset.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(gain_reset.error())};
    }

    auto calibration_monitoring_enabled = m_live_input.setCalibrationInputMonitoringEnabled(true);
    if (!calibration_monitoring_enabled.has_value())
    {
        restoreInputCalibrationRouteStateBestEffort(previous_route_state);
        if (calibration_monitoring_enabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_enabled.error())};
    }

    m_input_calibration.activateMeasurement(std::move(*measurement));
    updateView();
    return {};
}

// Stops an in-progress measurement without closing the calibration prompt.
void EditorController::Impl::onInputCalibrationMeasurementCancelled()
{
    if (m_input_calibration.hasActiveMeasurement())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
    }
    updateView();
}

// Applies a successful calibration gain, persists it, and enables processed monitoring.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationSucceeded(double gain_db)
{
    return commitInputCalibration(gain_db, std::nullopt);
}

// Applies a user-entered calibration gain without requiring an active automatic attempt.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    onInputCalibrationManuallySet(double gain_db)
{
    return commitInputCalibration(gain_db, std::nullopt);
}

// Closes the calibration prompt without enabling uncalibrated live monitoring.
void EditorController::Impl::onInputCalibrationDismissed()
{
    if (m_input_calibration.hasActiveMeasurement())
    {
        const auto restored = restoreCalibrationMeasurementState();
        if (!restored.has_value())
        {
            reportError(restored.error().message);
        }
    }
    m_input_calibration.closePrompt();
    updateView();
}

// Applies a live output gain preview without adding an undo entry until the final commit arrives.
void EditorController::Impl::onOutputGainPreviewChanged(double gain_db)
{
    applyOutputGainChange(gain_db, OutputGainChangeIntent::Preview);
}

// Applies a clamped output gain to the live rig and records the committed value in undo history.
void EditorController::Impl::onOutputGainChanged(double gain_db)
{
    applyOutputGainChange(gain_db, OutputGainChangeIntent::Commit);
}

// Collects availability inputs using fresh controller snapshots for immediate action gates.
ActionConditions EditorController::Impl::currentActionConditions() const
{
    const InputCalibrationWorkflow::Snapshot input_calibration =
        m_input_calibration.snapshot(inputCalibrationContext());

    return currentActionConditions(input_calibration, m_transport.state());
}

// Reuses already-sampled view projection state so enabled flags share one availability snapshot.
ActionConditions EditorController::Impl::currentActionConditions(
    const InputCalibrationWorkflow::Snapshot& input_calibration,
    const common::audio::TransportState& transport_state) const
{
    const std::optional<BusyViewState> busy = m_busy.viewState();
    return ActionConditions{
        .busy = isBusy(),
        .busy_cancel_available = busy.has_value() && busy->cancel_enabled,
        .input_calibration_prompt_visible = input_calibration.prompt.has_value(),
        .session_faulted = m_session_faulted,
        .live_input_audition_available = input_calibration.live_input_audition_available,
        .has_project = m_project.has_value(),
        .has_unsaved_changes_prompt =
            m_deferred_project_action_state.unsavedChangesPrompt().has_value(),
        .has_save_as_prompt = m_deferred_project_action_state.saveAsPrompt().has_value(),
        // A pending plugin-parameter edit is flushed into a real undo entry at the action gate, so
        // undo is offered for it too (matches the action availability the plan specifies).
        .undo_available =
            m_undo_history.canUndo() || m_plugin_host.hasPendingPluginParameterEdits(),
        .redo_available = m_undo_history.canRedo(),
        .has_loaded_arrangement = hasLoadedArrangement(),
        .can_stop_transport = canStopTransport(transport_state),
        .has_plugin_candidates = m_plugin_catalog.hasCandidates(),
        .has_plugin_insert_capacity = m_signal_chain.hasInsertCapacity(),
        .has_loaded_plugins = m_signal_chain.hasPlugins(),
    };
}

// Coarse-only transport callback. During an in-flight session load, defer the push so the final
// derivation runs against the updated session and transport state instead of stale data.
void EditorController::Impl::onTransportStateChanged(common::audio::TransportState /*state*/)
{
    if (m_session_load_in_progress)
    {
        return;
    }
    updateView();
}

// Starts a project-level action or asks the view to confirm unsaved changes first. Callers pass
// the original action; on dirty state the action itself is stashed for replay after the prompt
// resolves.
void EditorController::Impl::requestProjectAction(EditorAction::ProjectAction action)
{
    if (hasUnsavedChanges())
    {
        m_deferred_project_action_state.defer(std::move(action));
        updateView();
        return;
    }

    runProjectAction(std::move(action));
}

// Runs a project-level action once dirty-state gates have been satisfied. Visits the variant once
// and dispatches to a typed overload per case, mirroring performAction; each alternative arrives by
// value so a write-side move into a fresh ProjectWriteAction cannot dangle into the source variant.
void EditorController::Impl::runProjectAction(EditorAction::ProjectAction action)
{
    std::visit(
        [this](auto&& a) { runProjectActionImpl(std::forward<decltype(a)>(a)); },
        std::move(action));
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::OpenProject& action)
{
    openProject(action.file, false);
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::RestoreProject& action)
{
    openProject(action.file, true);
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::ImportSong& action)
{
    importSongSource(action.file);
}

void EditorController::Impl::runProjectActionImpl(EditorAction::SaveProject action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{action});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::SaveProjectAs action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{std::move(action)});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::PublishProject action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{std::move(action)});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::CloseProject /*action*/)
{
    // Capture the displaced path before closeProject() clears it so we can re-open the project
    // that was displaced by an import the user is now discarding.
    const std::filesystem::path displaced = m_displaced_project_file;
    if (closeProject())
    {
        clearDeferredProjectAction();
        if (!displaced.empty())
        {
            openProject(displaced, false);
        }
        else
        {
            updateView();
        }
    }
}

void EditorController::Impl::runProjectActionImpl(EditorAction::ExitApplication /*action*/)
{
    const std::optional<std::filesystem::path> restorable_project_file =
        restorableProjectFileForExit();
    if (closeProject())
    {
        m_pending_restore_project_file.reset();
        clearDeferredProjectAction();
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(restorable_project_file), "store exit restore project");
        updateView();
        m_exit_function();
    }
}

// Closes the current editor document across transport, backend audio, session, and workspace.
// Always supersedes any in-flight busy operation so closing or exiting during background work
// invalidates the worker's busy token; the worker's completion then sees a mismatch and
// discards itself rather than committing on top of a now-empty session.
bool EditorController::Impl::closeProject()
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    if (!m_project.has_value())
    {
        if (hasLoadedArrangement())
        {
            m_transport.stop();
        }
        clearLiveRig();
        clearActiveArrangementBestEffort("close empty project");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_untracked_unsaved_changes = false;
        m_session_faulted = false;
        m_plugin_catalog.hide();
        resetUndoHistory("undo.reset.close_empty_project");
        return true;
    }

    m_transport.stop();
    clearLiveRig();
    clearActiveArrangementBestEffort("close project");
    m_session.reset();
    m_signal_chain.clear();
    m_output_gain_db = 0.0;

    auto closed = closeExistingProject(m_project);
    if (!closed.has_value())
    {
        reportError(std::string{"Could not close: "} + closed.error().message);
        m_project.reset();
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_untracked_unsaved_changes = false;
        m_session_faulted = false;
        m_plugin_catalog.hide();
        resetUndoHistory("undo.reset.close_project_failed");
        updateView();
        return false;
    }

    m_project.reset();
    m_project_file.clear();
    m_displaced_project_file.clear();
    m_save_requires_destination = false;
    m_has_untracked_unsaved_changes = false;
    m_session_faulted = false;
    m_plugin_catalog.hide();
    resetUndoHistory("undo.reset.close_project");
    return true;
}

// Snapshots message-thread state and transfers Project ownership to a write task so worker-side
// package IO cannot race with controller-owned Project mutation.
auto EditorController::Impl::takeProjectForWrite(EditorAction::ProjectWriteAction action)
    -> std::shared_ptr<ProjectWriteTaskState>
{
    if (!m_project.has_value())
    {
        return {};
    }

    Project& project = m_project.value();
    auto state = std::make_shared<ProjectWriteTaskState>(std::move(action));
    common::core::Song song = session().song();
    if (const auto rig_captured = captureLiveRigIntoSong(song, project); !rig_captured.has_value())
    {
        reportError(std::string{"Could not capture live rig: "} + rig_captured.error().message);
        return {};
    }

    state->project = std::move(project);
    state->song = std::move(song);
    state->editor_state = projectEditorStateForSave();
    m_project.reset();
    return state;
}

// Captures the selected arrangement's live rig state into song files before Project IO leaves the
// message thread.
std::expected<void, common::audio::LiveRigError> EditorController::Impl::captureLiveRigIntoSong(
    common::core::Song& song, const Project& project)
{
    const common::core::Arrangement* const current_arrangement = session().currentArrangement();
    if (current_arrangement == nullptr)
    {
        return {};
    }

    auto arrangement = std::ranges::find_if(
        song.arrangements, [current_arrangement](const common::core::Arrangement& item) {
            return item.id == current_arrangement->id;
        });
    if (arrangement == song.arrangements.end())
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest,
            "Current arrangement is missing from the song",
        }};
    }

    auto snapshot = m_live_rig.captureActiveRig(
        common::audio::LiveRigCaptureRequest{
            .song_directory = songDirectoryForProject(project),
            .arrangement_id = arrangement->id,
            .existing_tone_document_ref = arrangement->tone_document_ref,
            .block_indices = m_signal_chain.blockIndices(),
            .display_type_overrides = m_signal_chain.displayTypeOverrideTokens(),
        });
    if (!snapshot.has_value())
    {
        return std::unexpected{std::move(snapshot.error())};
    }

    if (snapshot->plugins.size() > common::audio::max_signal_chain_plugins)
    {
        return std::unexpected{signalChainLimitError(snapshot->plugins.size())};
    }

    arrangement->tone_document_ref = snapshot->tone_document_ref;
    m_signal_chain.replaceSnapshot(
        common::audio::PluginChainSnapshot{.plugins = snapshot->plugins});
    m_output_gain_db = snapshot->output_gain.db;
    m_output_gain_preview_before.reset();
    return {};
}

// Restores the selected arrangement's saved tone document after the backing audio is active.
// Live rig restore runs cooperatively on the message thread inside the audio adapter, so this
// method always returns immediately and routes the audio load result through the on_loaded callback
// without mutating controller state.
void EditorController::Impl::restoreLiveRig(
    const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
    std::function<
        void(std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>)>
        on_loaded)
{
    if (!on_loaded)
    {
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        on_loaded(common::audio::LiveRigLoadResult{});
        return;
    }

    common::audio::LiveRigLoadRequest request{
        .song_directory = song_directory,
        .tone_document_ref = arrangement->tone_document_ref,
        .progress_callback = {},
        .yield_callback = {},
    };
    if (report_progress)
    {
        request.progress_callback =
            safeCallback([this, token](const common::audio::LiveRigLoadProgress& progress) {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                m_busy.updateLiveRigLoadProgress(progress);
            });
        // Route the engine's per-step yield through the busy-overlay paint fence so each plugin's
        // progress update actually paints before the next step blocks the message thread.
        request.yield_callback = safeCallback([this, token](std::function<void()> next) {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            m_busy.runAfterBusyPresentationReady(
                [this, token, continuation = std::move(next)]() mutable {
                    if (!m_busy.isCurrentToken(token))
                    {
                        return;
                    }
                    if (continuation)
                    {
                        continuation();
                    }
                });
        });
    }

    m_live_rig.loadLiveRig(
        std::move(request),
        safeCallback(
            [completion = std::move(on_loaded)](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    loaded) {
                if (!loaded.has_value())
                {
                    completion(std::unexpected{std::move(loaded.error())});
                    return;
                }
                completion(std::move(*loaded));
            }));
}

// Clears the audio backend's live rig chain as part of project teardown.
void EditorController::Impl::clearLiveRig()
{
    const auto cleared = m_live_rig.clearLiveRig();
    if (!cleared.has_value())
    {
        reportError(std::string{"Could not clear live rig: "} + cleared.error().message);
    }
}

// Runs project write actions through one task-runner path so busy lifetime, stale completion
// checks, and project restoration stay consistent across save, save-as, and publish.
void EditorController::Impl::runProjectWriteAction(EditorAction::ProjectWriteAction&& action)
{
    auto state = takeProjectForWrite(std::move(action));
    if (state == nullptr)
    {
        return;
    }

    runWorkerThreadBusyOperation(
        busyOperationForProjectWrite(state->action),
        state,
        [save_function = m_save_function,
         save_as_function = m_save_as_function,
         publish_function =
             m_publish_function](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            std::visit(
                [&task_state, &save_function, &save_as_function, &publish_function](
                    auto&& alternative) {
                    using A = std::decay_t<decltype(alternative)>;
                    if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
                    {
                        task_state->result = save_function(
                            task_state->project, task_state->song, task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
                    {
                        task_state->result = save_as_function(
                            task_state->project,
                            alternative.file,
                            task_state->song,
                            task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
                    {
                        task_state->result = publish_function(
                            task_state->project, alternative.file, task_state->song);
                    }
                },
                task_state->action);
        },
        [this](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            completeProjectWriteAction(task_state);
        });
}

// Restores the Project context, clears busy before errors, and applies the successful write's
// action-specific state transition on the message thread.
void EditorController::Impl::completeProjectWriteAction(
    const std::shared_ptr<ProjectWriteTaskState>& state)
{
    assert(isBusy() && "completeProjectWriteAction called outside a busy operation");

    const EditorAction::Id action_id = idOf(state->action);
    m_project = std::move(state->project);
    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        // A deferred action present at the moment of a failed save means this save was the one
        // synthesized by the unsaved-changes prompt's Save branch; the user wanted to protect
        // their work first, so dropping the deferred replay is the right escape.
        if (m_deferred_project_action_state.hasDeferredAction())
        {
            clearDeferredProjectAction();
        }
        finishBusyOperation();
        reportError(std::string{projectWriteErrorPrefix(state->action)} + message);
        return;
    }

    std::visit(
        [this](const auto& alternative) { applyProjectWriteSuccess(alternative); }, state->action);

    finishBusyOperation();
    if ((action_id == EditorAction::Id::SaveProject ||
         action_id == EditorAction::Id::SaveProjectAs) &&
        m_deferred_project_action_state.hasDeferredAction())
    {
        replayDeferredProjectActionAfterSave();
    }
}

void EditorController::Impl::applyProjectWriteSuccess(const EditorAction::SaveProject& /*action*/)
{
    m_has_untracked_unsaved_changes = false;
    markUndoHistoryClean("undo.mark_clean.save_project");
}

void EditorController::Impl::applyProjectWriteSuccess(const EditorAction::SaveProjectAs& action)
{
    m_save_requires_destination = false;
    m_project_file = action.file;
    m_displaced_project_file.clear();
    m_has_untracked_unsaved_changes = false;
    markUndoHistoryClean("undo.mark_clean.save_project_as");
}

void EditorController::Impl::applyProjectWriteSuccess(
    const EditorAction::PublishProject& /*action*/)
{
    // Publish does not change save destination or dirty state.
}

// Resumes a deferred project action after Save or Save As has protected user changes.
void EditorController::Impl::replayDeferredProjectActionAfterSave()
{
    std::optional<EditorAction::ProjectAction> deferred_action =
        m_deferred_project_action_state.takeReplay();
    if (!deferred_action.has_value())
    {
        updateView();
        return;
    }

    runProjectAction(std::move(*deferred_action));
}

// Clears all prompt-related state without changing the currently loaded project.
void EditorController::Impl::clearDeferredProjectAction() noexcept
{
    m_deferred_project_action_state.clear();
}

// Clears the restore-interrupted marker without touching the normal last-project path.
void EditorController::Impl::clearInterruptedRestoreMarker()
{
    recordSettingsResultBestEffort(
        m_settings.setInterruptedRestoreProject(std::nullopt), "clear interrupted restore marker");
}

// Removes the regular last-project path only when it refers to a now-invalid restore target.
void EditorController::Impl::clearLastOpenProjectIfMatches(
    const std::filesystem::path& project_file)
{
    const std::optional<std::filesystem::path> last_project = m_settings.lastOpenProject();
    if (last_project.has_value() && *last_project == project_file)
    {
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(std::nullopt), "clear missing restore project");
    }
}

// Returns the controller-owned editor session through the read-only access boundary.
const common::core::Session& EditorController::Impl::session() const noexcept
{
    return m_session;
}

// Returns an editor project file only when the current workspace is backed by one.
std::optional<std::filesystem::path> EditorController::Impl::currentProjectFile() const
{
    if (m_project_file.empty() || !hasLoadedArrangement())
    {
        return std::nullopt;
    }

    return m_project_file;
}

// Selects the project path persisted for next launch before exit tears down controller state.
std::optional<std::filesystem::path> EditorController::Impl::restorableProjectFileForExit() const
{
    std::optional<std::filesystem::path> restorable_project_file = currentProjectFile();
    if (!restorable_project_file.has_value() && !m_displaced_project_file.empty())
    {
        restorable_project_file = m_displaced_project_file;
    }
    if (!restorable_project_file.has_value() && m_pending_restore_project_file.has_value())
    {
        restorable_project_file = m_pending_restore_project_file;
    }

    return restorable_project_file;
}

// Restores a settings-backed project, or prompts first if the previous restore was interrupted.
void EditorController::Impl::restoreLastOpenProject()
{
    m_restore_interrupted_prompt_file.reset();
    const std::optional<std::filesystem::path> interrupted_project_file =
        m_settings.interruptedRestoreProject();
    if (interrupted_project_file.has_value())
    {
        if (!projectFileExists(*interrupted_project_file))
        {
            clearLastOpenProjectIfMatches(*interrupted_project_file);
            clearInterruptedRestoreMarker();
        }
        else
        {
            m_restore_interrupted_prompt_file = *interrupted_project_file;
            updateView();
            return;
        }
    }

    const std::optional<std::filesystem::path> project_file = m_settings.lastOpenProject();
    if (!project_file.has_value())
    {
        return;
    }

    if (!projectFileExists(*project_file))
    {
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(std::nullopt), "clear missing last project");
        clearInterruptedRestoreMarker();
        return;
    }

    runAction(EditorAction::RestoreProject{*project_file});
}

// Captures editor-only persistence state from the current transport and displayed arrangement.
ProjectEditorState EditorController::Impl::projectEditorStateForSave() const
{
    ProjectEditorState editor_state{
        .cursor_position = m_transport.position(),
        .selected_arrangement = std::nullopt,
    };

    const auto* arrangement = session().currentArrangement();
    if (arrangement != nullptr && !arrangement->id.empty())
    {
        editor_state.selected_arrangement = arrangement->id;
    }

    return editor_state;
}

// Prepares project audio, activates the selected arrangement, and commits the song to Session.
std::expected<void, common::audio::SongAudioError> EditorController::Impl::loadSessionSong(
    common::core::Song song, const std::optional<std::string>& selected_arrangement)
{
    if (song.arrangements.empty())
    {
        return std::unexpected{common::audio::SongAudioError{
            common::audio::SongAudioErrorCode::MissingAudioAssetPath,
            "Project song contains no arrangements",
        }};
    }

    auto prepared = m_song_audio.prepareSong(song);
    if (!prepared.has_value())
    {
        return std::unexpected{std::move(prepared.error())};
    }

    const std::size_t selected_index = getSelectedArrangementIndex(song, selected_arrangement);
    m_session_load_in_progress = true;
    auto active_arrangement_set =
        m_song_audio.setActiveArrangement(song.arrangements[selected_index]);
    bool committed = false;
    if (active_arrangement_set.has_value())
    {
        committed = m_session.loadSong(std::move(song), selected_index);
        assert(committed && "Session rejected backend-accepted project song");
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_output_gain_preview_before.reset();
        m_plugin_catalog.hide();
    }
    m_session_load_in_progress = false;

    if (!active_arrangement_set.has_value())
    {
        return std::unexpected{std::move(active_arrangement_set.error())};
    }

    if (!committed)
    {
        return std::unexpected{common::audio::SongAudioError{
            common::audio::SongAudioErrorCode::BackendClipInsertionFailed,
            "Editor session rejected backend-accepted project song",
        }};
    }

    return {};
}

// Builds the message-thread view state from the session and transport state. Current cursor
// position is only sampled to derive stop enabledness; the view receives discrete mapping state
// rather than a continuously pushed playhead position.
EditorViewState EditorController::Impl::deriveViewState() const
{
    const common::audio::TransportState transport_state = m_transport.state();
    const common::core::TimeRange timeline_range = session().timeline();
    const InputCalibrationWorkflow::Snapshot input_calibration =
        m_input_calibration.snapshot(inputCalibrationContext());
    const ActionConditions action_conditions =
        currentActionConditions(input_calibration, transport_state);

    EditorViewState state;
    state.open_enabled = isActionAvailable(EditorAction::Id::OpenProject, action_conditions);
    state.import_enabled = isActionAvailable(EditorAction::Id::ImportSong, action_conditions);
    state.save_enabled = isActionAvailable(EditorAction::Id::SaveProject, action_conditions);
    state.save_as_enabled = isActionAvailable(EditorAction::Id::SaveProjectAs, action_conditions);
    state.publish_enabled = isActionAvailable(EditorAction::Id::PublishProject, action_conditions);
    state.undo_enabled = isActionAvailable(EditorAction::Id::Undo, action_conditions);
    state.undo_label = m_undo_history.undoLabel();
    state.redo_enabled = isActionAvailable(EditorAction::Id::Redo, action_conditions);
    state.redo_label = m_undo_history.redoLabel();
    if (!m_project_file.empty())
    {
        state.suggested_publish_file = m_project_file;
        state.suggested_publish_file.replace_extension(".rock");
    }
    state.close_enabled = isActionAvailable(EditorAction::Id::CloseProject, action_conditions);
    state.project_loaded = action_conditions.has_loaded_arrangement;
    state.save_requires_destination = m_save_requires_destination;
    state.transport.play_pause_enabled =
        isActionAvailable(EditorAction::Id::PlayPause, action_conditions);
    state.transport.stop_enabled = isActionAvailable(EditorAction::Id::Stop, action_conditions);
    state.transport.play_pause_shows_pause_icon = transport_state.playing;
    state.audio_devices_available = true;
    state.audio_device_settings_enabled = input_calibration.audio_device_settings_enabled;
    state.audio_device_status_text = audioDeviceStatusText(m_audio_devices.currentDeviceStatus());
    state.visible_timeline = timeline_range;
    state.signal_chain = SignalChainViewState{
        .insert_plugin_enabled =
            isActionAvailable(EditorAction::Id::BeginPluginInsert, action_conditions),
        .move_plugins_enabled = isActionAvailable(EditorAction::Id::MovePlugin, action_conditions),
        .remove_plugins_enabled =
            isActionAvailable(EditorAction::Id::RemovePlugin, action_conditions),
        .plugins = m_signal_chain.plugins(),
        .input_calibration_status = input_calibration.status,
        .input_calibrate_enabled = input_calibration.calibrate_enabled,
        .disabled_message = input_calibration.disabled_message,
        .output_gain_controls_enabled = m_project_audio_ready &&
                                        action_conditions.has_loaded_arrangement &&
                                        !action_conditions.session_faulted,
        .output_gain_db = m_output_gain_db,
    };
    state.plugin_browser = m_plugin_catalog.viewState(
        isActionAvailable(EditorAction::Id::ScanPluginCatalog, action_conditions),
        isActionAvailable(EditorAction::Id::InsertSelectedPlugin, action_conditions));

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
        };
    }
    state.unsaved_changes_prompt = m_deferred_project_action_state.unsavedChangesPrompt();
    state.save_as_prompt = m_deferred_project_action_state.saveAsPrompt();

    if (m_restore_interrupted_prompt_file.has_value())
    {
        state.restore_interrupted_prompt =
            RestoreInterruptedPrompt{*m_restore_interrupted_prompt_file};
    }

    state.input_calibration_prompt = input_calibration.prompt;

    state.busy = m_busy.viewState();

    return state;
}

// Applies the serialized audio-device state stored by a previous editor session, if any.
void EditorController::Impl::restoreAudioDeviceState()
{
    const std::optional<std::string> serialized_state = m_settings.audioDeviceState();
    if (!serialized_state.has_value() || serialized_state->empty())
    {
        return;
    }

    auto restored = m_audio_devices.restoreSerializedDeviceState(*serialized_state);
    if (!restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore serialized audio-device state", restored.error().message);
        recordSettingsResultBestEffort(
            m_settings.setAudioDeviceState(std::nullopt),
            "clear invalid serialized audio-device state");
    }
}

// Stores the current device manager state so the next launch can restore the user's selection.
void EditorController::Impl::persistAudioDeviceState()
{
    recordSettingsResultBestEffort(
        m_settings.setAudioDeviceState(m_audio_devices.serializedDeviceState()),
        "persist serialized audio-device state");
}

// Returns the active physical input route identity, if the audio backend can provide one.
std::optional<common::audio::InputDeviceIdentity> EditorController::Impl::
    currentInputDeviceIdentity() const
{
    return m_audio_devices.currentInputDeviceIdentity();
}

// Selects saved calibration for the current physical route before the live-input gate runs.
void EditorController::Impl::selectInputCalibrationForCurrentRoute()
{
    const std::optional<common::audio::InputDeviceIdentity> current_identity =
        currentInputDeviceIdentity();
    std::optional<common::audio::InputCalibrationState> saved_calibration;
    if (current_identity.has_value())
    {
        auto loaded_calibration = m_settings.inputCalibrationFor(*current_identity);
        if (loaded_calibration.has_value())
        {
            saved_calibration = std::move(*loaded_calibration);
        }
        else
        {
            logEditorControllerBestEffortFailure(
                "load input calibration", loaded_calibration.error().message);
        }
    }

    executeInputCalibrationEffects(m_input_calibration.syncCommittedInputDeviceIdentity(
        current_identity, std::move(saved_calibration)));
}

// Saves the active physical-route calibration after workflow and live-input side effects succeed.
void EditorController::Impl::saveActiveInputCalibration()
{
    const std::optional<common::audio::InputCalibrationState> calibration =
        m_input_calibration.activeCalibrationState();
    if (calibration.has_value())
    {
        recordSettingsResultBestEffort(
            m_settings.saveInputCalibration(*calibration), "save input calibration");
    }
}

void EditorController::Impl::recordSettingsResultBestEffort(
    std::expected<void, EditorSettingsError> result, std::string_view context)
{
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
    }
}

// Executes workflow-requested side effects against root-owned ports and settings.
void EditorController::Impl::executeInputCalibrationEffects(
    const InputCalibrationWorkflow::Effects& effects)
{
    for (const InputCalibrationWorkflow::Effect effect : effects)
    {
        switch (effect)
        {
            case InputCalibrationWorkflow::Effect::DisableLiveInputMonitoring:
            {
                setLiveInputMonitoringBestEffort(false, "workflow disable live input monitoring");
                break;
            }
            case InputCalibrationWorkflow::Effect::DisableCalibrationInputMonitoring:
            {
                setCalibrationInputMonitoringBestEffort(
                    false, "workflow disable calibration monitoring");
                break;
            }
        }
    }
}

// Captures the root-owned context the calibration workflow needs to project state.
InputCalibrationWorkflow::Context EditorController::Impl::inputCalibrationContext() const
{
    return InputCalibrationWorkflow::Context{
        .project_audio_ready = m_project_audio_ready,
        .arrangement_loaded = hasLoadedArrangement(),
        .current_input_device_identity = currentInputDeviceIdentity(),
    };
}

// Applies the backend live-input gate from the current route and calibration state.
void EditorController::Impl::applyLiveInputGate()
{
    setCalibrationInputMonitoringBestEffort(false, "live-input gate calibration disable");

    if (m_input_calibration.audioDeviceSettingsOpen())
    {
        setLiveInputMonitoringBestEffort(false, "audio-device settings gate disable");
        return;
    }

    const InputCalibrationWorkflow::Context context = inputCalibrationContext();
    if (!context.project_audio_ready || !context.arrangement_loaded)
    {
        setLiveInputMonitoringBestEffort(false, "project-audio gate disable");
        return;
    }

    if (!context.current_input_device_identity.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-input-route gate disable");
        return;
    }

    const std::optional<common::audio::InputCalibrationState> calibration =
        m_input_calibration.activeCalibrationState();
    if (!calibration.has_value())
    {
        setLiveInputMonitoringBestEffort(false, "missing-calibration gate disable");
        return;
    }

    if (!m_input_calibration.calibrationMatches(context.current_input_device_identity))
    {
        setLiveInputMonitoringBestEffort(false, "mismatched-calibration gate disable");
        return;
    }

    auto gain_applied = m_live_input.setInputGain(calibration->calibration_gain);
    if (!gain_applied.has_value())
    {
        m_input_calibration.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate gain failure disable");
        return;
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        m_input_calibration.markBackendUnavailable();
        setLiveInputMonitoringBestEffort(false, "live-input gate enable failure disable");
        return;
    }

    m_input_calibration.markBackendAvailable();
}

// Reads the current live-input route values before a multi-step calibration setup mutation.
EditorController::Impl::InputCalibrationRouteState EditorController::Impl::
    currentInputCalibrationRouteState() const
{
    return InputCalibrationRouteState{
        .input_gain = m_live_input.inputGain(),
        .live_input_monitoring_enabled = m_live_input.liveInputMonitoringEnabled(),
        .calibration_input_monitoring_enabled = m_live_input.calibrationInputMonitoringEnabled(),
    };
}

// Best-effort rollback for setup failures before an automatic attempt exists.
void EditorController::Impl::restoreInputCalibrationRouteStateBestEffort(
    const InputCalibrationRouteState& route_state)
{
    setCalibrationInputMonitoringBestEffort(
        route_state.calibration_input_monitoring_enabled,
        "restore calibration route monitoring state");
    auto gain_restored = m_live_input.setInputGain(route_state.input_gain);
    if (!gain_restored.has_value())
    {
        logEditorControllerBestEffortFailure(
            "restore calibration route input gain", gain_restored.error().message);
    }
    if (!route_state.live_input_monitoring_enabled || gain_restored.has_value())
    {
        setLiveInputMonitoringBestEffort(
            route_state.live_input_monitoring_enabled, "restore calibration route monitoring");
    }
}

void EditorController::Impl::clearActiveArrangementBestEffort(std::string_view context)
{
    auto cleared = m_song_audio.clearActiveArrangement();
    if (!cleared.has_value())
    {
        logEditorControllerBestEffortFailure(context, cleared.error().message);
    }
}

bool EditorController::Impl::setLiveInputMonitoringBestEffort(
    bool enabled, std::string_view context)
{
    auto result = m_live_input.setLiveInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

bool EditorController::Impl::setCalibrationInputMonitoringBestEffort(
    bool enabled, std::string_view context)
{
    auto result = m_live_input.setCalibrationInputMonitoringEnabled(enabled);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

bool EditorController::Impl::setInputGainBestEffort(
    common::audio::Gain gain, std::string_view context)
{
    auto result = m_live_input.setInputGain(gain);
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
        return false;
    }

    return true;
}

// Applies a completed calibration value to the current live route and persists it.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::commitInputCalibration(
    double gain_db, const std::optional<common::audio::InputDeviceIdentity>& expected_identity)
{
    const InputCalibrationWorkflow::Context context = inputCalibrationContext();
    const auto plan_result =
        expected_identity.has_value()
            ? m_input_calibration.prepareCommit(gain_db, expected_identity, context)
            : (m_input_calibration.hasActiveMeasurement()
                   ? m_input_calibration.prepareActiveMeasurementCommit(gain_db, context)
                   : m_input_calibration.prepareCommit(gain_db, std::nullopt, context));
    if (!plan_result.has_value())
    {
        return std::unexpected{plan_result.error()};
    }

    const InputCalibrationWorkflow::CommitPlan& plan = *plan_result;
    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    auto gain_applied = m_live_input.setInputGain(plan.calibration_gain);
    if (!gain_applied.has_value())
    {
        // The gain was not applied, so prior live-route state is intact. Only a route-unavailable
        // failure means the calibrated route is gone and must be torn down; other errors are
        // reported without disturbing existing monitoring.
        if (gain_applied.error().code == common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
            saveActiveInputCalibration();
            setLiveInputMonitoringBestEffort(false, "commit calibration route-unavailable disable");
            updateView();
        }
        return std::unexpected{std::move(gain_applied.error())};
    }

    auto monitoring_enabled = m_live_input.setLiveInputMonitoringEnabled(true);
    if (!monitoring_enabled.has_value())
    {
        // The new gain is already on the live route, so roll back to the preserved calibration's
        // gain and disable monitoring regardless of the error code; otherwise the route would be
        // left armed at the rejected gain.
        const std::optional<common::audio::Gain> restore_gain =
            m_input_calibration.preservePreviousCalibrationAfterCommitFailure(
                plan.previous_calibration_state, context.current_input_device_identity);
        if (restore_gain.has_value())
        {
            setInputGainBestEffort(*restore_gain, "commit calibration gain rollback");
        }
        saveActiveInputCalibration();
        setLiveInputMonitoringBestEffort(false, "commit calibration enable-failure disable");
        updateView();
        return std::unexpected{std::move(monitoring_enabled.error())};
    }

    m_input_calibration.commitCalibration(plan.calibration_gain, plan.input_device_identity);
    saveActiveInputCalibration();
    updateView();
    return {};
}

// Restores the previous matching calibration if a manual recalibration measurement is cancelled.
std::expected<void, common::audio::LiveInputError> EditorController::Impl::
    restoreCalibrationMeasurementState()
{
    using MeasurementRestore = InputCalibrationWorkflow::MeasurementRestore;

    const InputCalibrationWorkflow::MeasurementRestorePlan plan =
        m_input_calibration.prepareMeasurementRestore(inputCalibrationContext());
    if (std::holds_alternative<MeasurementRestore::NoRestore>(plan))
    {
        return {};
    }

    auto calibration_monitoring_disabled = m_live_input.setCalibrationInputMonitoringEnabled(false);
    if (!calibration_monitoring_disabled.has_value())
    {
        if (calibration_monitoring_disabled.error().code ==
            common::audio::LiveInputErrorCode::InputRouteUnavailable)
        {
            m_input_calibration.markBackendUnavailable();
            updateView();
        }
        return std::unexpected{std::move(calibration_monitoring_disabled.error())};
    }

    return std::visit(
        [this](const auto& restore) -> std::expected<void, common::audio::LiveInputError> {
            using Restore = std::decay_t<decltype(restore)>;
            if constexpr (std::is_same_v<Restore, MeasurementRestore::NoRestore>)
            {
                return {};
            }
            else if constexpr (std::is_same_v<Restore, MeasurementRestore::DisableLiveInput>)
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearActiveMeasurement();
                return {};
            }
            else if constexpr (std::is_same_v<Restore, MeasurementRestore::ClearCalibration>)
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearCalibrationAfterMeasurement();
                return {};
            }
            else if constexpr (
                std::is_same_v<Restore, MeasurementRestore::ClearCalibrationAndClosePrompt>
            )
            {
                auto monitoring_disabled = m_live_input.setLiveInputMonitoringEnabled(false);
                if (!monitoring_disabled.has_value())
                {
                    return std::unexpected{std::move(monitoring_disabled.error())};
                }
                m_input_calibration.clearCalibrationAfterMeasurement();
                m_input_calibration.closePrompt();
                return {};
            }
            else
            {
                const common::audio::InputCalibrationState& previous_state =
                    restore.previous_calibration_state;

                auto gain_restored = m_live_input.setInputGain(previous_state.calibration_gain);
                if (!gain_restored.has_value())
                {
                    if (gain_restored.error().code ==
                        common::audio::LiveInputErrorCode::InputRouteUnavailable)
                    {
                        m_input_calibration.restorePreviousCalibration(previous_state, false);
                    }
                    else
                    {
                        m_input_calibration.clearCalibrationAfterMeasurement();
                    }
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore gain-failure disable");
                    return std::unexpected{std::move(gain_restored.error())};
                }

                auto monitoring_restored = m_live_input.setLiveInputMonitoringEnabled(true);
                if (!monitoring_restored.has_value())
                {
                    m_input_calibration.restorePreviousCalibration(previous_state, false);
                    saveActiveInputCalibration();
                    setLiveInputMonitoringBestEffort(
                        false, "measurement restore enable-failure disable");
                    return std::unexpected{std::move(monitoring_restored.error())};
                }

                m_input_calibration.restorePreviousCalibration(previous_state, true);
                saveActiveInputCalibration();
                return {};
            }
        },
        plan);
}

// Caches the derived state as the seed for future attachView() pushes and forwards it to the
// currently attached view if any. The null branch covers the construction window during which
// restoreAudioDeviceState() may fire onAudioDeviceConfigurationChanged() before attachView().
void EditorController::Impl::updateView()
{
    m_last_state = deriveViewState();
    if (m_view != nullptr)
    {
        m_view->setState(m_last_state);
    }
}

// Sends transient workflow failures through the view effect channel rather than render state.
void EditorController::Impl::reportError(const std::string& message)
{
    if (m_view != nullptr)
    {
        m_view->showError(message);
    }
}

// Answers the "has loading committed a usable arrangement" question used by intent gates.
bool EditorController::Impl::hasLoadedArrangement() const
{
    return session().currentArrangement() != nullptr;
}

// Reports whether the active arrangement has persisted plugin state worth showing as progress.
bool EditorController::Impl::shouldShowLiveRigLoadProgress() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    return arrangement != nullptr && !arrangement->tone_document_ref.empty();
}

// Reports whether a busy operation is currently active.
bool EditorController::Impl::isBusy() const noexcept
{
    return m_busy.isBusy();
}

// Begins a busy operation and advances the current busy token. The workflow refreshes the view
// after changing busy state.
std::uint64_t EditorController::Impl::beginBusy(BusyOperation operation)
{
    return m_busy.begin(operation);
}

// Normal operation completion: clears busy state and pushes the resulting view state so the
// overlay clears in the same frame. Completion paths call this only after their captured busy
// token has already matched the current busy token. Failure sites call this BEFORE
// reportError() so the cleared state is pushed before any modal dialog the error path may
// raise. Otherwise the dialog overlays a stale "busy" view.
void EditorController::Impl::finishBusyOperation()
{
    m_busy.finish();
}

void EditorController::Impl::detachView()
{
    m_busy.detachPresentation();
    m_view = nullptr;
}

// Dirty state comes from imported unsaved projects, undo-history clean markers, and narrow
// untracked cases such as load-time normalization rewrites or faulted sessions.
bool EditorController::Impl::hasUnsavedChanges() const noexcept
{
    return m_project.has_value() &&
           (m_has_untracked_unsaved_changes || m_undo_history.hasUnsavedEdits() ||
            m_save_requires_destination);
}

// Stop is useful while playback is running or when a paused/stopped cursor can still be reset to
// the start of the loaded timeline.
bool EditorController::Impl::canStopTransport(
    const common::audio::TransportState& transport_state) const
{
    return hasLoadedArrangement() &&
           (transport_state.playing || m_transport.position() != session().timeline().start);
}

} // namespace rock_hero::editor::core
