#include "controller/editor_controller.h"

#include "audio_device/audio_device_status_text.h"
#include "busy/busy_operation_workflow.h"
#include "chart/chart_hit_testing.h"
#include "deferred_project_action_state.h"
#include "editor_action.h"
#include "editor_action_availability.h"
#include "editor_controller_impl.h"
#include "editor_undo_history.h"
#include "input_calibration/input_calibration_projection.h"
#include "project/gp_song_importer.h"
#include "project/project_io.h"
#include "project/rock_song_importer.h"
#include "shared/editor_controller_logging.h"
#include "signal_chain/plugin_catalog_workflow.h"
#include "signal_chain/signal_chain_edits.h"
#include "signal_chain/signal_chain_workflow.h"
#include "timeline/section_projection.h"
#include "tone/tone_automation_projection.h"
#include "tone/tone_track_projection.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/device/device_restore_outcome.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/tab/tab_projection.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/busy/busy_view_state.h>
#include <rock_hero/editor/core/controller/i_editor_view.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
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
        case EditorAction::Id::SeekTimeline:
        {
            return "SeekTimeline";
        }
        case EditorAction::Id::SetGridNoteValue:
        {
            return "SetGridNoteValue";
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
        case EditorAction::Id::SelectArrangement:
        {
            return "SelectArrangement";
        }
        case EditorAction::Id::SelectToneRegion:
        {
            return "SelectToneRegion";
        }
        case EditorAction::Id::ResizeToneRegion:
        {
            return "ResizeToneRegion";
        }
        case EditorAction::Id::CreateToneRegion:
        {
            return "CreateToneRegion";
        }
        case EditorAction::Id::DeleteToneRegion:
        {
            return "DeleteToneRegion";
        }
        case EditorAction::Id::RenameTone:
        {
            return "RenameTone";
        }
        case EditorAction::Id::MoveToneBoundary:
        {
            return "MoveToneBoundary";
        }
        case EditorAction::Id::CreateNewTone:
        {
            return "CreateNewTone";
        }
        case EditorAction::Id::SetToneAutomationPoints:
        {
            return "SetToneAutomationPoints";
        }
        case EditorAction::Id::NewToneDocument:
        {
            return "NewToneDocument";
        }
        case EditorAction::Id::OpenToneFile:
        {
            return "OpenToneFile";
        }
        case EditorAction::Id::SaveToneFile:
        {
            return "SaveToneFile";
        }
        case EditorAction::Id::SaveToneFileAs:
        {
            return "SaveToneFileAs";
        }
        case EditorAction::Id::ImportToneFile:
        {
            return "ImportToneFile";
        }
        case EditorAction::Id::ExportToneFile:
        {
            return "ExportToneFile";
        }
        case EditorAction::Id::ResolveToneImportPrompt:
        {
            return "ResolveToneImportPrompt";
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
            case EditorAction::Id::ResizeToneRegion:
            case EditorAction::Id::CreateToneRegion:
            case EditorAction::Id::DeleteToneRegion:
            case EditorAction::Id::RenameTone:
            case EditorAction::Id::MoveToneBoundary:
            case EditorAction::Id::CreateNewTone:
            case EditorAction::Id::SetToneAutomationPoints:
            case EditorAction::Id::SelectArrangement:
            // Tone-document actions replace or persist the signal chain the calibration prompt owns.
            case EditorAction::Id::NewToneDocument:
            case EditorAction::Id::OpenToneFile:
            case EditorAction::Id::SaveToneFile:
            case EditorAction::Id::SaveToneFileAs:
            case EditorAction::Id::ImportToneFile:
            case EditorAction::Id::ExportToneFile:
            case EditorAction::Id::ResolveToneImportPrompt:
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
            case EditorAction::Id::SeekTimeline:
            case EditorAction::Id::SetGridNoteValue:
            case EditorAction::Id::SelectToneRegion:
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
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::SelectArrangement:
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::ResizeToneRegion:
        case EditorAction::Id::CreateToneRegion:
        case EditorAction::Id::DeleteToneRegion:
        case EditorAction::Id::RenameTone:
        case EditorAction::Id::MoveToneBoundary:
        case EditorAction::Id::CreateNewTone:
        case EditorAction::Id::SetToneAutomationPoints:
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
        case EditorAction::Id::NewToneDocument:
        case EditorAction::Id::OpenToneFile:
        case EditorAction::Id::SaveToneFile:
        case EditorAction::Id::SaveToneFileAs:
        {
            return "tone-designer-unavailable";
        }
        case EditorAction::Id::ImportToneFile:
        {
            return "tone-import-unavailable";
        }
        case EditorAction::Id::ExportToneFile:
        {
            return "tone-export-unavailable";
        }
        case EditorAction::Id::ResolveToneImportPrompt:
        {
            return "no-tone-import-prompt";
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
    RH_LOG_INFO("editor.controller", "Action requested action={:?}", actionIdText(action));
}

void logEditorActionAvailabilityRejected(EditorAction::Id action, std::string_view reason)
{
    RH_LOG_INFO(
        "editor.controller",
        "Action availability rejected action={:?} reason={:?}",
        actionIdText(action),
        reason);
}

void logEditorActionStarted(EditorAction::Id action)
{
    RH_LOG_INFO("editor.controller", "Action started action={:?}", actionIdText(action));
}

void logEditorActionDispatchCompleted(EditorAction::Id action)
{
    RH_LOG_INFO("editor.controller", "Action dispatch completed action={:?}", actionIdText(action));
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
        "Undo transition result context={:?} status={:?} failure={:?} requires_fault={}",
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
            "Undo transition event context={:?} type={:?} label={:?} direction={:?} failure={:?} "
            "requires_fault={}",
            context,
            undoEventTypeText(event.type),
            event.label,
            direction,
            undoFailureCodeText(event.failure_code),
            event.requires_fault);
    }
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
    if (extension == ".gp")
    {
        GpSongImporter importer;
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
    Project& project, const common::core::Song& song)
{
    return project.save(song);
}

// Production save-as path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultSaveAs(
    Project& project, const std::filesystem::path& file, const common::core::Song& song)
{
    return project.saveAs(file, song);
}

// Production publish path used when tests do not provide a custom seam.
[[nodiscard]] std::expected<void, ProjectError> defaultPublish(
    Project& project, const std::filesystem::path& file, const common::core::Song& song)
{
    return project.publish(file, song);
}

// Production exit fallback used when a composition host does not provide an exit callback.
void defaultExit()
{}

} // namespace

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
              audio_ports.plugin_host, audio_ports.live_rig, audio_ports.tone_automation, services,
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

void EditorController::onNewToneRequested()
{
    m_impl->runAction(EditorAction::NewToneDocument{});
}

void EditorController::onOpenToneFileRequested(std::filesystem::path file)
{
    m_impl->runAction(EditorAction::OpenToneFile{std::move(file)});
}

void EditorController::onSaveToneRequested()
{
    m_impl->runAction(EditorAction::SaveToneFile{});
}

void EditorController::onSaveToneAsRequested(std::filesystem::path file)
{
    m_impl->runAction(EditorAction::SaveToneFileAs{std::move(file)});
}

void EditorController::onImportToneFileRequested(std::filesystem::path file)
{
    m_impl->runAction(EditorAction::ImportToneFile{std::move(file)});
}

void EditorController::onExportToneFileRequested(std::filesystem::path file)
{
    m_impl->runAction(EditorAction::ExportToneFile{std::move(file)});
}

void EditorController::onToneImportDecision(ToneImportDecision decision)
{
    m_impl->runAction(EditorAction::ResolveToneImportPrompt{decision});
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

void EditorController::onTimelineSeekRequested(common::core::TimePosition position)
{
    m_impl->onTimelineSeekRequested(position);
}

void EditorController::onGridNoteValueChangeRequested(common::core::Fraction note_value)
{
    m_impl->onGridNoteValueChangeRequested(note_value);
}

void EditorController::onTimelineZoomChanged(double pixels_per_second)
{
    m_impl->onTimelineZoomChanged(pixels_per_second);
}

void EditorController::onWaveformVisibleChangeRequested(bool visible)
{
    m_impl->onWaveformVisibleChangeRequested(visible);
}

void EditorController::onTabMinimumDisplayedStringsChangeRequested(int minimum_strings)
{
    m_impl->onTabMinimumDisplayedStringsChangeRequested(minimum_strings);
}

void EditorController::onArrangementSelected(std::string arrangement_id)
{
    m_impl->onArrangementSelected(std::move(arrangement_id));
}

void EditorController::onChartPointerDown(const ChartPointerEvent& event)
{
    m_impl->onChartPointerDown(event);
}

void EditorController::onChartPointerDrag(const ChartPointerEvent& event)
{
    m_impl->onChartPointerDrag(event);
}

void EditorController::onChartPointerUp(const ChartPointerEvent& event)
{
    m_impl->onChartPointerUp(event);
}

void EditorController::onChartPointerMove(const ChartPointerEvent& event)
{
    m_impl->onChartPointerMove(event);
}

void EditorController::onChartPointerExit()
{
    m_impl->onChartPointerExit();
}

void EditorController::onChartCaretStepRequested(ChartStepDirection direction, bool measure)
{
    m_impl->onChartCaretStepRequested(direction, measure);
}

void EditorController::onSelectionMoveRequested(ChartStepDirection direction, bool fine)
{
    m_impl->onSelectionMoveRequested(direction, fine);
}

void EditorController::onSelectionDeleteRequested()
{
    m_impl->onSelectionDeleteRequested();
}

void EditorController::onChartFretDigitTyped(int digit)
{
    m_impl->onChartFretDigitTyped(digit);
}

void EditorController::onChartFretShiftRequested(int direction)
{
    m_impl->onChartFretShiftRequested(direction);
}

void EditorController::onChartSustainAdjustRequested(int direction, bool fine)
{
    m_impl->onChartSustainAdjustRequested(direction, fine);
}

void EditorController::onChartEscapePressed()
{
    m_impl->onChartEscapePressed();
}

void EditorController::onToneRegionSelected(std::string region_id)
{
    m_impl->onToneRegionSelected(std::move(region_id));
}

void EditorController::onToneRegionActivated()
{
    m_impl->onToneRegionActivated();
}

void EditorController::onToneRegionResizeRequested(
    std::string region_id, common::core::GridPosition start, common::core::GridPosition end)
{
    m_impl->onToneRegionResizeRequested(std::move(region_id), start, end);
}

void EditorController::onToneRegionCreateRequested(
    common::core::GridPosition position, std::string new_region_id, std::string tone_document_ref)
{
    m_impl->onToneRegionCreateRequested(
        position, std::move(new_region_id), std::move(tone_document_ref));
}

void EditorController::onToneRegionDeleteRequested(std::string region_id)
{
    m_impl->onToneRegionDeleteRequested(std::move(region_id));
}

void EditorController::onToneRenameRequested(std::string tone_document_ref, std::string name)
{
    m_impl->onToneRenameRequested(std::move(tone_document_ref), std::move(name));
}

void EditorController::onToneBoundaryMoveRequested(
    std::string right_region_id, common::core::GridPosition position)
{
    m_impl->onToneBoundaryMoveRequested(std::move(right_region_id), position);
}

void EditorController::onToneCreateNewRequested(
    common::core::GridPosition position, std::string name)
{
    m_impl->onToneCreateNewRequested(position, std::move(name));
}

void EditorController::onToneAutomationLaneAddRequested(
    std::string instance_id, std::string param_id)
{
    m_impl->onToneAutomationLaneAddRequested(instance_id, std::move(param_id));
}

void EditorController::onToneAutomationLaneRemoveRequested(
    std::string instance_id, std::string param_id)
{
    m_impl->onToneAutomationLaneRemoveRequested(instance_id, param_id);
}

void EditorController::onSetToneAutomationPoints(
    std::string instance_id, std::string param_id,
    std::vector<common::core::ToneAutomationPoint> points)
{
    m_impl->onSetToneAutomationPoints(
        std::move(instance_id), std::move(param_id), std::move(points));
}

void EditorController::onToneAutomationPointSelected(
    std::string instance_id, std::string param_id, common::core::GridPosition position)
{
    m_impl->onToneAutomationPointSelected(std::move(instance_id), std::move(param_id), position);
}

void EditorController::onNeutralInsertRequested()
{
    m_impl->onNeutralInsertRequested();
}

void EditorController::onToneAutomationLaneCaretRequested(
    std::string instance_id, std::string param_id, common::core::TimePosition time)
{
    m_impl->onToneAutomationLaneCaretRequested(std::move(instance_id), std::move(param_id), time);
}

void EditorController::onToneAutomationPointerMove(const ToneAutomationPointerEvent& event)
{
    m_impl->onToneAutomationPointerMove(event);
}

void EditorController::onToneAutomationPointerExit()
{
    m_impl->onToneAutomationPointerExit();
}

void EditorController::onToneAutomationPointerDown(const ToneAutomationPointerEvent& event)
{
    m_impl->onToneAutomationPointerDown(event);
}

void EditorController::onToneAutomationPointerDrag(const ToneAutomationPointerEvent& event)
{
    m_impl->onToneAutomationPointerDrag(event);
}

void EditorController::onToneAutomationPointerUp(const ToneAutomationPointerEvent& event)
{
    m_impl->onToneAutomationPointerUp(event);
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

std::expected<void, GameAudioSourceError> EditorController::onUseGameAudioSettingsChangeRequested(
    bool enabled, const std::function<void(bool)>& set_applying)
{
    return m_impl->onUseGameAudioSettingsChangeRequested(enabled, set_applying);
}

GameAudioSourceState EditorController::gameAudioSourceState() const
{
    return m_impl->gameAudioSourceState();
}

void EditorController::onGameAudioUnavailablePromptDismissed()
{
    m_impl->onGameAudioUnavailablePromptDismissed();
}

void EditorController::onGameAudioRecommendationDecision(
    GameAudioRecommendationDecision decision, bool suppress_future)
{
    m_impl->onGameAudioRecommendationDecision(decision, suppress_future);
}

void EditorController::onInputCalibrationRequested()
{
    m_impl->onInputCalibrationRequested();
}

std::expected<void, common::audio::LiveInputMonitorError> EditorController::
    onInputCalibrationMeasurementStarted()
{
    return m_impl->onInputCalibrationMeasurementStarted();
}

void EditorController::onInputCalibrationMeasurementCancelled()
{
    m_impl->onInputCalibrationMeasurementCancelled();
}

std::expected<void, common::audio::LiveInputMonitorError> EditorController::
    onInputCalibrationSucceeded(double gain_db)
{
    return m_impl->onInputCalibrationSucceeded(gain_db);
}

std::expected<void, common::audio::LiveInputMonitorError> EditorController::
    onInputCalibrationManuallySet(double gain_db)
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

void EditorController::onAudioDeviceSettingsTeardownComplete()
{
    m_impl->onAudioDeviceSettingsTeardownComplete();
}

void EditorController::onAudioDeviceFailureDecision(AudioDeviceFailureDecision decision)
{
    m_impl->onAudioDeviceFailureDecision(decision);
}

// Subscribes for coarse transport transitions and captures an initial derived state, falling back
// to production project IO where an optional project operation is omitted.
EditorController::Impl::Impl(
    common::audio::ITransport& transport, common::audio::ISongAudio& song_audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    common::audio::IToneAutomation& tone_automation, EditorController::Services services,
    EditorController::ExitFunction exit_function,
    EditorController::ProjectOperations project_operations)
    : m_transport(transport)
    , m_song_audio(song_audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_live_rig(live_rig)
    , m_tone_automation(tone_automation)
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
    , m_audio_config_store(services.audio_config_store)
    , m_editor_audio_config_store(services.editor_audio_config_store)
    , m_live_input_monitor(services.live_input_monitor)
    // Busy transitions re-evaluate the failure prompt: staging is suppressed while a device
    // operation is in flight, so the busy-clear callback is what surfaces a still-closed device
    // after a staged apply, toggle flip, or Retry.
    , m_busy(
          services.message_thread_scheduler,
          [this] {
              refreshAudioDeviceFailurePrompt();
              updateView();
          })
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
            .play_pause_requested =
                [this, alive] {
                    if (alive.expired())
                    {
                        return;
                    }
                    onPlayPausePressed();
                },
        });
    m_plugin_host.setPluginEditObserver(
        common::audio::PluginEditObserver{
            .pending_changed = [this, alive](bool pending) {
                if (alive.expired())
                {
                    return;
                }
                onPluginEditPendingChanged(pending);
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
    resolveGameAudioSourceAtStartup();
    // Startup route application: applies the resolved source's saved route inline (no busy
    // presentation exists yet), refreshes the live-input monitor, and stages the failure prompt
    // when the saved device cannot open.
    static_cast<void>(applyAudioSourceAndRoute(AudioSourceSelection::Current, {}));
    m_waveform_visible = m_settings.waveformVisible().value_or(true);
    m_tab_minimum_displayed_strings = std::clamp(
        m_settings.tabMinimumDisplayedStrings().value_or(0), 0, common::core::g_max_chart_strings);
    common::audio::IAudioDeviceConfiguration::Listener& self_as_listener = *this;
    m_audio_device_listener = std::make_unique<common::audio::ScopedListener<
        common::audio::IAudioDeviceConfiguration,
        common::audio::IAudioDeviceConfiguration::Listener>>(m_audio_devices, self_as_listener);
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
    m_plugin_host.setPluginEditObserver({});
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

// Imports a song source and stores the workspace only after audio and Session accept the song.
void EditorController::Impl::onImportRequested(std::filesystem::path file)
{
    runAction(EditorAction::ImportSong{std::move(file)});
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
        // Distinct capture name: clang's -Wshadow-uncaptured-local flags `x = std::move(x)`.
        if (!controller->m_busy.postToMessageThread(controller->safeCallback(
                [controller, token, owned_progress = std::move(progress_snapshot)] {
                    if (!controller->m_busy.isCurrentToken(token))
                    {
                        return;
                    }
                    controller->m_busy.updatePluginCatalogScanProgress(owned_progress);
                })))
        {
            return;
        }
    };
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

// Routes the seek as an action so a missing arrangement or busy state gates it like the other
// transport intents.
void EditorController::Impl::onTimelineSeekRequested(common::core::TimePosition position)
{
    runAction(EditorAction::SeekTimeline{position});
}

// Routes the spacing change as an action so a missing arrangement or busy state gates it like the
// other timeline intents.
void EditorController::Impl::onGridNoteValueChangeRequested(common::core::Fraction note_value)
{
    runAction(EditorAction::SetGridNoteValue{note_value});
}

// Caches and persists the view-reported zoom as app-local per-project resume state. Zoom never
// dirties project content and bypasses the action gate for the same reason cursor saves do.
void EditorController::Impl::onTimelineZoomChanged(double pixels_per_second)
{
    // Exact-equality skip: is_eq keeps -Wfloat-equal builds clean; unchanged-zoom detection is
    // deliberately exact, not tolerance-based.
    if (!std::isfinite(pixels_per_second) || pixels_per_second <= 0.0 ||
        std::is_eq(pixels_per_second <=> m_timeline_zoom_pixels_per_second))
    {
        return;
    }

    m_timeline_zoom_pixels_per_second = pixels_per_second;
    if (!m_project_file.empty())
    {
        recordSettingsResultBestEffort(
            m_settings.saveProjectTimelineZoom(m_project_file, m_timeline_zoom_pixels_per_second),
            "save project timeline zoom");
    }
}

// Caches, persists, and republishes the app-wide waveform visibility preference. Like zoom it
// never dirties project content and bypasses the action gate, but unlike zoom the view renders
// from pushed state, so the change flows back through updateView().
void EditorController::Impl::onWaveformVisibleChangeRequested(bool visible)
{
    if (visible == m_waveform_visible)
    {
        return;
    }

    m_waveform_visible = visible;
    recordSettingsResultBestEffort(
        m_settings.setWaveformVisible(m_waveform_visible), "save waveform visibility");
    updateView();
}

// Caches, persists, and republishes the app-wide tablature string display minimum; see
// onWaveformVisibleChangeRequested for why this pushes state where zoom does not.
void EditorController::Impl::onTabMinimumDisplayedStringsChangeRequested(int minimum_strings)
{
    const int clamped = std::clamp(minimum_strings, 0, common::core::g_max_chart_strings);
    if (clamped == m_tab_minimum_displayed_strings)
    {
        return;
    }

    m_tab_minimum_displayed_strings = clamped;
    recordSettingsResultBestEffort(
        m_settings.setTabMinimumDisplayedStrings(m_tab_minimum_displayed_strings),
        "save tablature string display minimum");
    updateView();
}

namespace
{

// Pointer travel past this distance turns an empty-lane press into a marquee instead of a
// click-to-seek; small enough that deliberate drags always marquee, large enough that a shaky
// click never accidentally selects.
constexpr float g_chart_click_threshold_px = 4.0f;

// The multi-digit fret entry window, shared by selection retyping and pending-insert
// composition: well above deliberate two-digit typing (inter-keystroke ~150-300ms) and below a
// thinking pause, so "12" combines and "2, pause, 3" stays two values (tuned down from 1500ms
// on user feel feedback, settled at 750ms 2026-07-17).
constexpr std::uint32_t g_fret_entry_window_ms = 750;

} // namespace

// The memoized projection deriveViewState pushed is exactly what the lane painted, so pointer
// events resolve against it; null while no chart is displayed.
const common::core::TabViewState* EditorController::Impl::displayedTabProjection() const
{
    return m_tab_view_state.get();
}

// Chart notes are sorted by (position, string) and the tab projection preserves that order one
// to one, so a projection index addresses the chart note directly.
std::optional<ChartNoteKey> EditorController::Impl::chartNoteKeyAt(
    std::size_t projection_index) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() ||
        projection_index >= arrangement->chart->notes.size())
    {
        return std::nullopt;
    }

    const common::core::ChartNote& note = arrangement->chart->notes[projection_index];
    return ChartNoteKey{.position = note.position, .string = note.string};
}

void EditorController::Impl::clearChartEditingState()
{
    clearSelection();
    m_chart_gesture.reset();
    m_chart_fret_entry.reset();
    // A fresh chart-editing context starts passive: the paused cursor at the transport
    // position is the position, and nothing is armed until the first click or arrow (the
    // marker model).
    m_chart_marker = ChartCursor{};
}

// Read-only view of the chart alternative; any other held kind reads as the empty selection,
// which is exactly what "no chart selection" means to every chart handler.
const ChartSelection& EditorController::Impl::chartSelection() const
{
    static const ChartSelection empty{};
    const ChartSelection* const selection = std::get_if<ChartSelection>(&m_selection);
    return selection != nullptr ? *selection : empty;
}

// Mutable access emplaces the chart alternative, so any chart-selection gesture structurally
// replaces a tone-region or automation-point selection (one selection editor-wide).
ChartSelection& EditorController::Impl::chartSelectionMutable()
{
    if (ChartSelection* const selection = std::get_if<ChartSelection>(&m_selection))
    {
        return *selection;
    }
    return m_selection.emplace<ChartSelection>();
}

std::string EditorController::Impl::selectedToneRegionId() const
{
    const ToneRegionSelection* const selection = std::get_if<ToneRegionSelection>(&m_selection);
    return selection != nullptr ? selection->region_id : std::string{};
}

const AutomationPointSelection* EditorController::Impl::selectedAutomationPoint() const
{
    return std::get_if<AutomationPointSelection>(&m_selection);
}

// Every selection replacement outside the chart typing flow funnels through here so the
// invalidation rule below can never be skipped at one of the assignment sites.
void EditorController::Impl::setSelection(EditorSelection selection)
{
    m_selection = std::move(selection);
    // A replaced or cleared selection invalidates any in-flight multi-digit fret entry: the
    // entry is keyed to the chart selection it retypes, and leaving it armed against a
    // vanished selection could widen an undo entry for notes no longer selected.
    m_chart_fret_entry.reset();
}

void EditorController::Impl::clearSelection()
{
    setSelection(std::monostate{});
}

void EditorController::Impl::clearCursorCoupledSelection()
{
    if (std::holds_alternative<ToneRegionSelection>(m_selection) ||
        std::holds_alternative<AutomationPointSelection>(m_selection))
    {
        setSelection(std::monostate{});
    }
}

// Returns the armed caret, or null while the marker is passive.
const EditorController::Impl::ChartCaret* EditorController::Impl::armedChartCaret() const noexcept
{
    return std::get_if<ChartCaret>(&m_chart_marker);
}

// Returns the marker's remembered string in either state.
int EditorController::Impl::chartMarkerString() const noexcept
{
    // Both marker states carry the remembered string. Read it through get_if rather than
    // std::visit so the accessor is genuinely noexcept: std::visit is potentially-throwing
    // (bad_variant_access), which the -Werror exception-escape check rejects in a noexcept
    // function. The marker is always one of the two alternatives, so the cursor branch is total.
    if (const ChartCaret* const caret = armedChartCaret())
    {
        return caret->string;
    }
    return std::get_if<ChartCursor>(&m_chart_marker)->string;
}

// Demotes an armed caret to the passive cursor, leaving the transport where it is. Used by
// the transport-motion handoffs (play, external playback, paused seeks): the transport
// already states the position, so only the string memory survives.
void EditorController::Impl::disarmChartMarker()
{
    if (const ChartCaret* const caret = armedChartCaret())
    {
        m_chart_marker = ChartCursor{.string = caret->string};
    }
}

// Demotes an armed caret to the passive cursor "in its place": a paused seek carries the
// transport to the caret's musical time, so the cursor line appears exactly where the caret
// was. Used by the editing-gesture handoffs (Ctrl+click, double-click, marquee, Esc); the
// seek deliberately skips tone activation — dissolving a caret is a display handoff, not a
// listening move.
void EditorController::Impl::dissolveChartCaretInPlace()
{
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr)
    {
        return;
    }

    m_transport.seek(
        session().timeline().clamp(
            common::core::TimePosition{secondsAtGridPosition(
                session().song().tempo_map, caret->position)}));
    m_chart_marker = ChartCursor{.string = caret->string};
}

// Arms the caret at a slot and re-derives the selection from what sits under it: a note
// becomes the selection (the highlight IS the caret display there), an empty slot clears it
// (the white square shows where typing will insert). Chart notes are sorted by
// (position, string), so slot occupancy is one binary search.
// True when a note already sits on the slot: the chart notes are sorted by (position, string),
// so occupancy is one binary search. Shared by caret arming (selection re-derivation), the
// Alt+click insert (refusal), and the insert ghost's honesty gate.
bool EditorController::Impl::chartSlotOccupied(
    common::core::GridPosition position, int string) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return false;
    }
    const ChartNoteKey key{.position = position, .string = string};
    return std::ranges::binary_search(
        arrangement->chart->notes, key, {}, [](const common::core::ChartNote& note) {
            return ChartNoteKey{.position = note.position, .string = note.string};
        });
}

void EditorController::Impl::armChartCaret(common::core::GridPosition position, int string)
{
    m_chart_marker = ChartCaret{.position = position, .string = string};
    const ChartNoteKey key{.position = position, .string = string};
    if (chartSlotOccupied(position, string))
    {
        chartSelectionMutable().replaceWith(key);
    }
    else
    {
        // Arming onto an empty slot empties the selection — through the chart alternative, so
        // a tone-region or automation-point selection is replaced too (the caret is now the
        // typing scope).
        chartSelectionMutable().clear();
    }
}

// Plants a note at an empty slot, makes it the selection, and arms the caret on it — the mouse
// form of the Insert verb (§9b) behind Alt+click neutral-create. planInsertNote refuses an
// occupied slot, so an Alt+click onto a note is a silent no-op. The insert is one undo entry; a
// following retype (the note lands selected) is its own — "place, then correct the value".
void EditorController::Impl::insertChartNoteAt(
    common::core::GridPosition position, int string, int fret)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    common::core::ChartNote note;
    note.position = position;
    note.string = string;
    note.fret = fret;
    std::optional<ChartNotesEditPlan> plan =
        planInsertNote(*arrangement->chart, session().song().tempo_map, note);
    if (!plan.has_value())
    {
        return;
    }
    if (!applyChartEditPlan(
            std::move(plan),
            std::vector<ChartNoteKey>{ChartNoteKey{.position = position, .string = string}}))
    {
        return;
    }
    // Arm the caret on the freshly placed note so the state reads exactly like a plain click
    // that landed on a note (armed ⟹ the selection is what sits under the caret, §9a) and the
    // next typed digit retypes it.
    armChartCaret(position, string);
}

// Resolves the Alt-hover insert ghost: published only while paused with Alt held over an
// insertable empty slot, so the ring never advertises an insert that would no-op (§7). Snapping
// and occupancy match the click exactly (chartPlacementAt + chartSlotOccupied). Dirty-checked
// against the current ghost — a hover that stays within one grid slot leaves it unchanged and
// pushes no view rebuild, so per-pixel hover stays cheap.
void EditorController::Impl::publishChartInsertGhost(const ChartPointerEvent& event)
{
    std::optional<ChartInsertGhostViewState> ghost;
    if (event.modifiers.alt && !isBusy() && !m_transport.state().playing)
    {
        if (const auto placement = chartPlacementAt(event);
            placement.has_value() && !chartSlotOccupied(placement->first, placement->second))
        {
            const common::core::TempoMap& tempo_map = session().song().tempo_map;
            ghost = ChartInsertGhostViewState{
                .seconds = tempo_map.secondsAtNote(
                    placement->first.measure, placement->first.beat, placement->first.offset),
                .string = placement->second,
            };
        }
    }
    if (ghost == m_chart_insert_ghost)
    {
        return;
    }
    m_chart_insert_ghost = ghost;
    updateView();
}

namespace
{

// The armed caret's published time bounds: its own seconds plus its measure span for the
// keep-in-view glide. One derivation feeds both the chart-row and lane-row caret view structs
// so the two reveal rules can never drift.
struct CaretTimeBounds
{
    double seconds{0.0};
    double measure_start_seconds{0.0};
    double measure_end_seconds{0.0};
};

[[nodiscard]] CaretTimeBounds caretTimeBounds(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position)
{
    return CaretTimeBounds{
        .seconds = secondsAtGridPosition(tempo_map, position),
        .measure_start_seconds = tempo_map.secondsAtNote(position.measure, 1, {}),
        .measure_end_seconds = tempo_map.secondsAtNote(position.measure + 1, 1, {}),
    };
}

} // namespace

// Arms the caret on an automation lane row and re-derives the selection from what sits under
// it — armChartCaret's row-axis sibling (§9b): a point at the slot becomes the editor-wide
// selection, an empty slot clears it. The remembered string survives so crossing back up into
// the tab lane returns where the caret left.
void EditorController::Impl::armLaneCaret(
    common::core::GridPosition position, AutomationLaneRow row)
{
    bool on_point = false;
    if (const std::vector<common::core::ToneAutomationPoint>* const points =
            lanePointsFor(row.instance_id, row.param_id))
    {
        on_point =
            std::ranges::any_of(*points, [&](const common::core::ToneAutomationPoint& point) {
                return point.position == position;
            });
    }

    if (on_point)
    {
        setSelection(
            AutomationPointSelection{
                .instance_id = row.instance_id,
                .param_id = row.param_id,
                .position = position,
            });
    }
    else
    {
        setSelection(std::monostate{});
    }
    m_chart_marker =
        ChartCaret{.position = position, .string = chartMarkerString(), .lane = std::move(row)};
}

// The caret row's next authored object strictly beyond the caret in the step direction: notes
// on the caret's string, points on its lane. Linear scans are fine at keypress cadence.
std::optional<common::core::GridPosition> EditorController::Impl::nextRowObjectStop(
    const ChartCaret& caret, bool later)
{
    std::optional<common::core::GridPosition> best;
    const auto consider = [&](const common::core::GridPosition& position) {
        if (later ? !(caret.position < position) : !(position < caret.position))
        {
            return;
        }
        if (!best.has_value() || (later ? position < *best : *best < position))
        {
            best = position;
        }
    };
    if (caret.lane.has_value())
    {
        if (const std::vector<common::core::ToneAutomationPoint>* const points =
                lanePointsFor(caret.lane->instance_id, caret.lane->param_id))
        {
            for (const common::core::ToneAutomationPoint& point : *points)
            {
                consider(point.position);
            }
        }
        return best;
    }
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return best;
    }
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (note.string == caret.string)
        {
            consider(note.position);
        }
    }
    return best;
}

// The visible automation lane rows in display order, derived from the same lane-source
// enumeration the full projection builds lanes from — so traversal and display can never
// disagree about which rows exist, without paying the full projection (port parameter listing,
// per-point seconds) on every keystroke.
std::vector<EditorController::Impl::AutomationLaneRow> EditorController::Impl::
    visibleAutomationLaneRows() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }
    const std::vector<ToneAutomationLaneSource> sources = toneAutomationLaneSources(
        *arrangement, activeToneDocumentRef(), m_tone_plugin_bindings, m_open_automation_lanes);
    std::vector<AutomationLaneRow> rows;
    rows.reserve(sources.size());
    for (const ToneAutomationLaneSource& source : sources)
    {
        rows.push_back(
            AutomationLaneRow{.instance_id = source.instance_id, .param_id = source.param_id});
    }
    return rows;
}

// Resolves the event's snapped musical position and the string lane under the pointer — the
// chart's single placement seam (arm, Alt insert, and ghost all snap through it, mirroring
// the lane's laneSnapPositionForX). Placement snaps to the displayed grid's exact rational by
// default; Ctrl composes the 1/960-beat fine tier, uniform with the lane arm and placement
// (the off-grid unification — snap default follows the data, the capability is universal).
std::optional<std::pair<common::core::GridPosition, int>> EditorController::Impl::chartPlacementAt(
    const ChartPointerEvent& event) const
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || event.geometry.lane_height <= 0.0f)
    {
        return std::nullopt;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::optional<common::core::TimePosition> clicked = timelinePositionForX(
        event.x, event.geometry.visible_timeline, static_cast<int>(event.geometry.bounds_width));
    if (!clicked.has_value())
    {
        return std::nullopt;
    }

    const common::core::GridPosition position =
        event.modifiers.ctrl
            ? fineGridPositionForBeat(tempo_map, tempo_map.beatPositionAtSeconds(clicked->seconds))
            : nearestTempoGridPosition(tempo_map, m_grid_note_value, *clicked);

    // Lanes stack highest string on top; extra user lanes pad below the chart's strings.
    const float lane = (event.y - event.geometry.bounds_y) / event.geometry.lane_height;
    const int lane_index =
        std::clamp(static_cast<int>(lane), 0, event.geometry.displayed_count - 1);
    const int displayed_string = event.geometry.displayed_count - lane_index;
    const int string =
        std::clamp(displayed_string - event.geometry.extra_lanes, 1, tab->string_count);
    return std::pair{position, string};
}

// One grid step in beats at a position (the shared gridStepBeats seam under the session's
// current grid). The grid step is the default move; the Ctrl fine tier (1/960 beat, settled
// 2026-07-18) is the uniform precision escape hatch on both surfaces.
common::core::Fraction EditorController::Impl::chartGridStepBeats(
    common::core::GridPosition at) const
{
    return gridStepBeats(session().song().tempo_map, m_grid_note_value, at.measure);
}

// Applies a planned chart-note change through the session's mutable chart (bumping the revision
// so every projection rebuilds) and records it as one undo entry.
bool EditorController::Impl::applyChartEditPlan(
    std::optional<ChartNotesEditPlan> plan, std::optional<std::vector<ChartNoteKey>> select_exactly)
{
    if (!plan.has_value())
    {
        return false;
    }

    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr)
    {
        return false;
    }

    if (const auto applied = applyChartNotesChange(*chart, plan->removed, plan->inserted);
        !applied.has_value())
    {
        // The plan was computed against this exact chart, so a precondition failure means a
        // logic error rather than user input; surface it instead of silently dropping the edit.
        reportError("Could not apply chart edit: " + plan->label);
        return false;
    }

    // A typing-style edit interrupts any in-flight fret entry unless the caller re-arms it.
    m_chart_fret_entry.reset();

    // The selection follows the edit: retyped/moved/inserted notes stay selected under their
    // new keys, deleted notes drop out (their keys no longer resolve).
    if (select_exactly.has_value())
    {
        chartSelectionMutable().applyBox(*select_exactly, false);
    }
    else
    {
        // (selection - removed keys) + inserted keys: retyped/resized notes stay selected even
        // when the edit left some of them unchanged, moved notes follow to their new keys, and
        // deleted notes drop out. Plans never carry unrelated notes, so this never grows the
        // selection past what the user had plus what the edit produced at new keys.
        std::vector<ChartNoteKey> next_selection;
        next_selection.reserve(chartSelection().notes().size() + plan->inserted.size());
        for (const ChartNoteKey& key : chartSelection().notes())
        {
            const bool removed =
                std::ranges::any_of(plan->removed, [&key](const common::core::ChartNote& note) {
                    return ChartNoteKey{.position = note.position, .string = note.string} == key;
                });
            if (!removed)
            {
                next_selection.push_back(key);
            }
        }
        for (const common::core::ChartNote& note : plan->inserted)
        {
            next_selection.push_back(
                ChartNoteKey{.position = note.position, .string = note.string});
        }
        chartSelectionMutable().applyBox(next_selection, false);
    }

    pushUndoEntry(std::make_unique<ChartNotesEdit>(std::move(*plan)));
    updateView();
    return true;
}

// Arms the gesture and applies glyph-press selection per the containment hierarchy (settled
// 2026-07-17): a plain single press selects the individual note — keeping an existing
// multi-selection intact so a future drag can move it — a double press selects the note's
// whole onset group (its chord), and Ctrl toggles individual membership. Shift is reassigned
// to plan 52's time-range selection and behaves as plain until that lands. Marker handoffs
// (the marker model, 2026-07-18): a plain press on an unselected note arms the caret there;
// every multi-select gesture — Ctrl, double-click — dissolves the caret into a cursor in its
// place, so the visible glyph always states whether typing inserts or acts on the selection.
void EditorController::Impl::onChartPointerDown(const ChartPointerEvent& event)
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || isBusy())
    {
        return;
    }

    // While playing there is no caret to place and no selection to build (playback dissolves
    // both), so the lane is a plain seek surface exactly like the waveform around it; routing
    // through SeekTimeline gives the click the same gating, snapping, and tone-follow as the
    // overlay's own click-to-seek.
    if (m_transport.state().playing)
    {
        const std::optional<common::core::TimePosition> clicked = timelineCursorPlacementTime(
            session().song().tempo_map,
            m_grid_note_value,
            event.geometry.visible_timeline,
            static_cast<int>(event.geometry.bounds_width),
            event.x,
            event.modifiers.ctrl ? TimelineCursorPlacementMode::Free
                                 : TimelineCursorPlacementMode::SnapToGrid);
        if (clicked.has_value())
        {
            runAction(EditorAction::SeekTimeline{*clicked});
        }
        return;
    }

    // A press ends any Alt-hover preview: a live gesture owns the lane now, and an Alt-drag
    // re-shows the ring as it follows. Refresh only when a ghost was actually showing.
    const bool had_insert_ghost = m_chart_insert_ghost.has_value();
    m_chart_insert_ghost.reset();

    ChartPointerGesture gesture;
    gesture.geometry = event.geometry;
    gesture.modifiers = event.modifiers;
    gesture.anchor_x = event.x;
    gesture.anchor_y = event.y;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    gesture.hit_note = chartNoteHitIndex(*tab, event.geometry, event.x, event.y);
    m_chart_gesture = gesture;

    if (!gesture.hit_note.has_value())
    {
        if (had_insert_ghost)
        {
            updateView();
        }
        return;
    }

    const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
    if (!key.has_value())
    {
        return;
    }

    if (event.modifiers.ctrl)
    {
        chartSelectionMutable().toggle(*key);
        dissolveChartCaretInPlace();
    }
    else if (event.clicks >= 2)
    {
        const common::core::Arrangement* const arrangement = session().currentArrangement();
        if (arrangement != nullptr && arrangement->chart.has_value())
        {
            chartSelectionMutable().replaceWith(
                chartOnsetGroupKeys(arrangement->chart->notes, key->position));
        }
        dissolveChartCaretInPlace();
    }
    else if (!chartSelection().contains(*key))
    {
        // Arming re-derives the singleton selection from the note under the caret. A press on
        // an already-selected note keeps the standing selection (and marker) untouched until
        // the release collapses it — the gap a future drag-move gesture lives in.
        armChartCaret(key->position, key->string);
    }
    updateView();
}

// Disambiguates an empty-lane press into a marquee once the pointer travels past the click
// threshold and republishes the marquee rectangle while it grows. Glyph-press drags are the
// future move gesture and do nothing yet.
void EditorController::Impl::onChartPointerDrag(const ChartPointerEvent& event)
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    ChartPointerGesture& gesture = *m_chart_gesture;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    if (gesture.hit_note.has_value())
    {
        return;
    }

    if (gesture.modifiers.alt)
    {
        // Alt on an empty slot is the neutral-create gesture, never a marquee: the ring follows
        // the pointer and the release plants the note, so press-drag-release places in one
        // gesture just as the automation lane's Alt-drag does.
        publishChartInsertGhost(event);
        return;
    }

    const bool beyond_threshold =
        std::abs(event.x - gesture.anchor_x) > g_chart_click_threshold_px ||
        std::abs(event.y - gesture.anchor_y) > g_chart_click_threshold_px;
    if (!gesture.marquee && !beyond_threshold)
    {
        return;
    }

    // The in-flight marquee leaves the marker alone: dissolution is a rule over OUTCOMES (the
    // marker model), and the outcome is unknown until release — an empty box must leave an
    // armed caret exactly where it was.
    gesture.marquee = true;
    updateView();
}

// Resolves the gesture: a marquee release selects the boxed notes (Shift extends), an
// empty-lane click arms the caret at the snapped slot, and a plain click-release on an
// already-selected note collapses the selection to it and arms the caret there.
void EditorController::Impl::onChartPointerUp(const ChartPointerEvent& event)
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    const ChartPointerGesture gesture = *m_chart_gesture;
    m_chart_gesture.reset();
    // A release ends the hover preview; every path below refreshes the view, so a ghost left
    // following an Alt-drag clears here.
    m_chart_insert_ghost.reset();

    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
    {
        updateView();
        return;
    }

    if (gesture.hit_note.has_value())
    {
        const bool clicked = std::abs(event.x - gesture.anchor_x) <= g_chart_click_threshold_px &&
                             std::abs(event.y - gesture.anchor_y) <= g_chart_click_threshold_px;
        // A completed plain click on a selected note collapses the selection to that note and
        // arms the caret there (the press deferred both while a drag was still possible); the
        // second release of a double click leaves the group selection standing.
        if (clicked && !gesture.modifiers.ctrl && event.clicks < 2)
        {
            if (const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
                key.has_value())
            {
                armChartCaret(key->position, key->string);
            }
        }
        updateView();
        return;
    }

    if (gesture.marquee)
    {
        const float left = std::min(gesture.anchor_x, event.x);
        const float right = std::max(gesture.anchor_x, event.x);
        const float top = std::min(gesture.anchor_y, event.y);
        const float bottom = std::max(gesture.anchor_y, event.y);
        const std::vector<std::size_t> boxed =
            chartNoteIndicesInBox(*tab, gesture.geometry, left, top, right, bottom);
        std::vector<ChartNoteKey> keys;
        keys.reserve(boxed.size());
        for (const std::size_t index : boxed)
        {
            if (const std::optional<ChartNoteKey> key = chartNoteKeyAt(index); key.has_value())
            {
                keys.push_back(*key);
            }
        }
        // Dissolution is a rule over outcomes (the marker model): a box that caught notes is
        // a multi-select outcome and demotes the caret to a cursor in its place; an empty box
        // has no selection outcome, so an armed caret survives untouched.
        if (!keys.empty())
        {
            chartSelectionMutable().applyBox(keys, gesture.modifiers.shift);
            dissolveChartCaretInPlace();
        }
        updateView();
        return;
    }

    // Empty release: Alt plants a fret-0 note here and selects it for an immediate retype (the
    // neutral-create verb's mouse form, §9b — the chart sibling of the lane's on-curve Alt+click);
    // a plain release arms the caret at the snapped slot — with play-from-the-marker this IS the
    // seek, the selection clearing via the caret's re-derivation.
    if (const auto placement = chartPlacementAt(event); placement.has_value())
    {
        if (gesture.modifiers.alt)
        {
            insertChartNoteAt(placement->first, placement->second, 0);
        }
        else
        {
            armChartCaret(placement->first, placement->second);
        }
    }
    updateView();
}

// A button-less hover: publish the Alt insert ghost when Alt is held over an insertable empty
// slot, else clear it. The controller resolves snap + occupancy so the ring can only appear
// where an Alt+click would actually plant a note (§7, no lying affordance).
void EditorController::Impl::onChartPointerMove(const ChartPointerEvent& event)
{
    publishChartInsertGhost(event);
}

// The pointer left the lane: no hover, so no ghost.
void EditorController::Impl::onChartPointerExit()
{
    if (!m_chart_insert_ghost.has_value())
    {
        return;
    }
    m_chart_insert_ghost.reset();
    updateView();
}

// The vertical half of caret stepping — the row axis (§9b): strings render top-to-bottom with
// string 1 at the visual bottom, and the visible automation lanes continue the stack below
// it, so Down from string 1 crosses into the first lane and Up from the first lane returns to
// string 1. Edges clamp (re-arm in place) exactly like the string edges always have, and a
// caret whose lane left the visible set (tone switch, lane removal) falls back onto the
// remembered string (§9b demotion posture).
void EditorController::Impl::stepCaretRow(const ChartCaret& caret, bool up, int string_count)
{
    const std::vector<AutomationLaneRow> lanes = visibleAutomationLaneRows();
    if (caret.lane.has_value())
    {
        const auto row = std::ranges::find(lanes, *caret.lane);
        if (row == lanes.end())
        {
            armChartCaret(caret.position, std::clamp(caret.string, 1, string_count));
            return;
        }
        const std::size_t row_index = static_cast<std::size_t>(row - lanes.begin());
        if (up)
        {
            if (row_index == 0)
            {
                armChartCaret(caret.position, 1);
            }
            else
            {
                armLaneCaret(caret.position, lanes[row_index - 1]);
            }
        }
        else if (row_index + 1 < lanes.size())
        {
            armLaneCaret(caret.position, lanes[row_index + 1]);
        }
        else
        {
            armLaneCaret(caret.position, lanes[row_index]);
        }
        return;
    }
    if (!up && caret.string == 1 && !lanes.empty())
    {
        armLaneCaret(caret.position, lanes.front());
        return;
    }
    armChartCaret(caret.position, std::clamp(caret.string + (up ? 1 : -1), 1, string_count));
}

// Arrow keys on the marker (the marker model): while passive, the first press arms the caret
// at the paused cursor — nearest grid line, remembered string — without stepping; while
// armed, Left/Right step one grid line on the caret's string — or jump measures under the
// modifier (the Guitar Pro jump) — and Up/Down move across strings. Every move re-derives
// the selection from what sits under the caret. Inert while playing: arming requires a
// paused transport (armed ⟹ paused is structural).
void EditorController::Impl::onChartCaretStepRequested(ChartStepDirection direction, bool measure)
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || isBusy() || m_transport.state().playing)
    {
        return;
    }

    const ChartCaret* const armed = armedChartCaret();
    if (armed == nullptr)
    {
        armChartCaret(
            nearestTempoGridPosition(
                session().song().tempo_map, m_grid_note_value, m_transport.position()),
            std::clamp(chartMarkerString(), 1, tab->string_count));
        updateView();
        return;
    }

    const ChartCaret caret = *armed;
    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        stepCaretRow(caret, direction == ChartStepDirection::Up, tab->string_count);
        updateView();
        return;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const int sign = direction == ChartStepDirection::Right ? 1 : -1;
    common::core::GridPosition stepped;
    if (measure)
    {
        // The Guitar Pro measure jump: Right goes to the next measure's start; Left to the
        // current measure's start when mid-measure, else the previous measure's.
        const bool at_measure_start =
            caret.position.beat == 1 && caret.position.offset.numerator == 0;
        const int target_measure = sign > 0           ? caret.position.measure + 1
                                   : at_measure_start ? std::max(1, caret.position.measure - 1)
                                                      : caret.position.measure;
        stepped = common::core::GridPosition{.measure = target_measure, .beat = 1, .offset = {}};
    }
    else
    {
        // The caret steps the union stop set (settled 2026-07-18): the adjacent grid line OR
        // the row's next authored object — a note on this string, a point on this lane —
        // whichever is nearer. Off-grid objects are first-class stops, so a fine-placed note
        // stays reachable from plain arrows; landing on one arms onto it (selecting it)
        // exactly like landing on an occupied grid slot. The grid stop comes from the shared
        // adjacent-line primitive the lane point nudge steps with, so the two surfaces can
        // never land on different slots for the same verb.
        stepped = adjacentTempoGridPosition(tempo_map, m_grid_note_value, caret.position, sign > 0);
        if (const std::optional<common::core::GridPosition> object_stop =
                nextRowObjectStop(caret, sign > 0);
            object_stop.has_value())
        {
            const bool grid_advanced =
                sign > 0 ? caret.position < stepped : stepped < caret.position;
            const bool object_nearer = sign > 0 ? *object_stop < stepped : stepped < *object_stop;
            if (!grid_advanced || object_nearer)
            {
                stepped = *object_stop;
            }
        }
    }
    // Time stepping is row-agnostic: a lane caret steps the same grid and keeps its row.
    if (caret.lane.has_value())
    {
        armLaneCaret(stepped, *caret.lane);
    }
    else
    {
        armChartCaret(stepped, caret.string);
    }
    updateView();
}
// The one selection-move intent (Alt+arrows): one editor-wide selection exists, so the move
// dispatches on its kind exactly like the Delete dispatch. With no selection at all, an armed
// caret on an empty lane slot turns the arrow into create-then-nudge (grab the curve and pull
// in one keystroke); every other combination is a silent no-op.
void EditorController::Impl::onSelectionMoveRequested(ChartStepDirection direction, bool fine)
{
    if (isBusy())
    {
        return;
    }
    // The handlers below run full action dispatches that may reassign the selection variant or
    // the marker; the dispatched value is copied here so no handler ever holds a reference into
    // the object it (or a reentrant view callback) might replace. The lane branches are
    // deliberately reachable while playing — live automation editing during playback is a
    // supported workflow (the points port edits safely mid-play) — while chart branches stay
    // structurally paused-only because play clears the chart selection.
    if (const AutomationPointSelection* const point = selectedAutomationPoint())
    {
        const AutomationPointSelection selected = *point;
        moveSelectedAutomationPoint(selected, direction, fine);
        return;
    }
    if (!chartSelection().empty())
    {
        moveChartSelection(direction, fine);
        return;
    }
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && caret->lane.has_value())
    {
        const ChartCaret armed = *caret;
        createAndNudgeLanePointAtCaret(armed, direction, fine);
    }
}

// Moves the selected chart notes: Left/Right by one grid step — or one 1/960-beat fine step,
// the uniform precision tier (settled 2026-07-18); the move is relative either way, so an
// off-grid note keeps its offset under grid steps — and Up/Down across strings (fine has no
// meaning on the discrete string axis). A refused move (edge of the neck, occupied slot, grid
// origin collision) is a silent no-op — the selection stays put, matching refuse-not-clamp
// everywhere else.
void EditorController::Impl::moveChartSelection(ChartStepDirection direction, bool fine)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    common::core::Fraction beat_delta{};
    int string_delta = 0;
    switch (direction)
    {
        case ChartStepDirection::Left:
        case ChartStepDirection::Right:
        {
            const common::core::GridPosition reference = chartSelection().notes().front().position;
            const common::core::Fraction step =
                fine ? common::core::Fraction{1, 960} : chartGridStepBeats(reference);
            beat_delta = direction == ChartStepDirection::Right
                             ? step
                             : common::core::Fraction{-step.numerator, step.denominator};
            break;
        }
        case ChartStepDirection::Up:
        {
            string_delta = 1;
            break;
        }
        case ChartStepDirection::Down:
        {
            string_delta = -1;
            break;
        }
    }
    // A caret sitting exactly on the single moved note rides along (an object stop stays under
    // the caret through its own nudge); the marker moves directly — no re-arm — so the derived
    // selection cannot widen to a chord unit mid-nudge.
    const ChartCaret* const caret = armedChartCaret();
    const bool caret_rides = caret != nullptr && !caret->lane.has_value() &&
                             chartSelection().notes().size() == 1 &&
                             caret->position == chartSelection().notes().front().position &&
                             caret->string == chartSelection().notes().front().string;
    if (applyChartEditPlan(planMoveNotes(
            *arrangement->chart,
            session().song().tempo_map,
            chartSelection().notes(),
            beat_delta,
            string_delta,
            chartSelection().notes().size() == 1 ? "Move Note" : "Move Notes")) &&
        caret_rides && !chartSelection().empty())
    {
        m_chart_marker = ChartCaret{
            .position = chartSelection().notes().front().position,
            .string = chartSelection().notes().front().string,
        };
        updateView();
    }
}

// Deletes the selected notes as one compound undo entry; the selection empties with them.
void EditorController::Impl::deleteChartSelection()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        chartSelection().empty())
    {
        return;
    }

    static_cast<void>(
        applyChartEditPlan(planDeleteNotes(*arrangement->chart, chartSelection().notes())));
}

// The Insert key's neutral create (2026-07-18): the surface's neutral object appears at an
// armed EMPTY caret slot — a fret-0 note on a string row, an on-curve point on a lane row —
// and nothing else ever happens: occupied slots, selections, and the passive state are
// no-ops, so Insert never mutates existing objects.
void EditorController::Impl::onNeutralInsertRequested()
{
    if (isBusy())
    {
        return;
    }
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr)
    {
        return;
    }
    if (caret->lane.has_value())
    {
        // Copied so the planting (a full action dispatch that re-points the selection and may
        // touch the marker) never reads back through the marker variant it aliases.
        const ChartCaret armed = *caret;
        insertLanePointAtCaret(armed);
        return;
    }

    // String row: only an armed empty slot inserts (a selection means the slot is occupied).
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || !chartSelection().empty())
    {
        return;
    }
    common::core::ChartNote note;
    note.position = caret->position;
    note.string = caret->string;
    note.fret = 0;
    std::optional<ChartNotesEditPlan> plan =
        planInsertNote(*arrangement->chart, session().song().tempo_map, note);
    if (!plan.has_value())
    {
        return;
    }
    const ChartNoteKey key{.position = note.position, .string = note.string};
    static_cast<void>(applyChartEditPlan(std::move(plan), std::vector<ChartNoteKey>{key}));
}

// The Delete key's one dispatch: exactly one selection exists editor-wide, so Delete deletes
// whatever kind it holds. This is dispatch on the variant's alternative, not the retired
// automation-point → chart → tone-region precedence ladder — once two live selections became
// unrepresentable, there is nothing to disambiguate.
void EditorController::Impl::onSelectionDeleteRequested()
{
    if (isBusy())
    {
        return;
    }
    // Copied for the same aliasing reason as the move dispatch: the delete replays a points
    // edit through a full action dispatch, which must never read back through the variant.
    if (const AutomationPointSelection* const point = selectedAutomationPoint())
    {
        const AutomationPointSelection selected = *point;
        deleteSelectedAutomationPoint(selected);
        return;
    }
    if (!chartSelection().empty())
    {
        deleteChartSelection();
        return;
    }
    if (std::string region_id = selectedToneRegionId(); !region_id.empty())
    {
        onToneRegionDeleteRequested(std::move(region_id));
    }
}

// Typed digits SET every selected note to the typed value — what you type is what appears —
// or, with no selection, INSERT a note at the empty caret carrying the typed fret (the caret
// model, 2026-07-17). Keystrokes inside the entry window combine into multi-digit values (a
// widened insert stays ONE insert, so undo removes the note); each keystroke applies
// immediately so the notation always shows the value being typed. The three flows live in
// their own helpers below; this dispatcher only orders them.
void EditorController::Impl::onChartFretDigitTyped(int digit)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() || digit < 0 ||
        digit > 9)
    {
        return;
    }

    const std::uint32_t now_ms = juce::Time::getMillisecondCounter();
    if (m_chart_fret_entry.has_value() && widenChartFretEntry(digit, now_ms))
    {
        return;
    }
    if (chartSelection().empty())
    {
        insertChartFretAtCaret(digit, now_ms);
        return;
    }
    retypeChartSelectionFret(digit, now_ms);
}

// A second digit inside the window WIDENS the in-flight entry: the chart moves to the
// combined value and the just-pushed undo entry is replaced by one spanning from the
// pre-entry originals, so the whole typed number undoes as one action. The widen requires the
// same selection, a combinable value, and the history top still being our entry (any
// interleaved edit or undo moves the position and kills the window); anything else reports
// unhandled so the digit falls through to a fresh flow.
bool EditorController::Impl::widenChartFretEntry(int digit, std::uint32_t now_ms)
{
    const ChartFretEntry entry = *m_chart_fret_entry;
    const int combined = entry.value * 10 + digit;
    const bool widenable = now_ms - entry.last_keystroke_ms <= g_fret_entry_window_ms &&
                           combined <= common::core::g_max_fret &&
                           entry.keys == chartSelection().notes() &&
                           m_undo_history.snapshot().position == entry.history_position;
    if (widenable)
    {
        // The widened whole-entry plan runs from the pre-entry originals: an insert entry
        // re-plans as the SAME insert carrying the combined fret, a retype entry from the
        // captured base notes.
        std::optional<ChartNotesEditPlan> widened;
        if (entry.insert_plan.has_value())
        {
            widened = entry.insert_plan;
            for (common::core::ChartNote& inserted : widened->inserted)
            {
                if (ChartNoteKey{.position = inserted.position, .string = inserted.string} ==
                    entry.keys.front())
                {
                    inserted.fret = combined;
                }
            }
        }
        else
        {
            widened = planRetypeFrets(entry.base_notes, combined, /*set_exact=*/true);
        }
        if (!widened.has_value())
        {
            return true;
        }

        common::core::Chart* const chart = m_session.currentChart();
        if (chart == nullptr)
        {
            return true;
        }
        // Step the live chart from its current values to the combined target. The current
        // values are the base shape already moved by the first digit, so a plan that was
        // valid from base is valid from here too.
        const std::vector<common::core::ChartNote> current_notes = chartNotesForKeys(entry.keys);
        if (const std::optional<ChartNotesEditPlan> incremental =
                planRetypeFrets(current_notes, combined, /*set_exact=*/true);
            incremental.has_value() && !incremental->removed.empty())
        {
            if (!applyChartNotesChange(*chart, incremental->removed, incremental->inserted)
                     .has_value())
            {
                reportError("Could not apply chart edit: " + incremental->label);
                m_chart_fret_entry.reset();
                return true;
            }
        }

        bool now_pushed = entry.pushed;
        // A pure insert carries no removed notes, so the emptiness check must span both
        // sides — skipping the swap would leave history holding the first digit's insert
        // while the chart shows the combined fret, and undo would preflight-fail.
        if (!widened->removed.empty() || !widened->inserted.empty())
        {
            bool replaced = false;
            if (entry.pushed)
            {
                replaced =
                    m_undo_history.replaceTop(std::make_unique<ChartNotesEdit>(*widened)).status ==
                    EditorUndoTransitionStatus::Applied;
            }
            if (!replaced)
            {
                // No entry of ours to widen (the first digit was a no-op) or the history
                // refused the swap (for example a save marked the top entry clean mid-
                // window): the combined change lands as its own entry instead — two
                // undo steps in a rare edge beats a stack that lies about the file.
                pushUndoEntry(std::make_unique<ChartNotesEdit>(*widened));
            }
            now_pushed = true;
        }
        m_chart_fret_entry = ChartFretEntry{
            .value = combined,
            .last_keystroke_ms = now_ms,
            .base_notes = entry.base_notes,
            .keys = entry.keys,
            .insert_plan = entry.insert_plan.has_value() ? widened : std::nullopt,
            .pushed = now_pushed,
            .history_position = m_undo_history.snapshot().position,
        };
        updateView();
        return true;
    }
    return false;
}

// Fresh insert: with no selection, the typed digit becomes a note at the armed empty caret.
// While the marker is passive, digits are inert by design (the marker model) — a stray
// keystroke after listening authors nothing.
void EditorController::Impl::insertChartFretAtCaret(int digit, std::uint32_t now_ms)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const ChartCaret* const caret = armedChartCaret();
    if (arrangement == nullptr || caret == nullptr || caret->lane.has_value())
    {
        // No caret, or the caret rides an automation lane row — lane typing is the
        // typed-value editor (routed in the view), never a fret insert.
        return;
    }
    common::core::ChartNote note;
    note.position = caret->position;
    note.string = caret->string;
    note.fret = digit;
    std::optional<ChartNotesEditPlan> plan =
        planInsertNote(*arrangement->chart, session().song().tempo_map, note);
    if (!plan.has_value())
    {
        return;
    }
    const ChartNotesEditPlan inserted = *plan;
    const ChartNoteKey key{.position = note.position, .string = note.string};
    if (!applyChartEditPlan(std::move(plan), std::vector<ChartNoteKey>{key}))
    {
        return;
    }
    if (digit * 10 <= common::core::g_max_fret)
    {
        m_chart_fret_entry = ChartFretEntry{
            .value = digit,
            .last_keystroke_ms = now_ms,
            .base_notes = {},
            .keys = {key},
            .insert_plan = inserted,
            .pushed = true,
            .history_position = m_undo_history.snapshot().position,
        };
    }
}

// Fresh retype: capture the selection's pre-entry values first so a later widen still
// restores them, apply the digit as its own undo entry, and open the window only while a
// second digit could still fit under the fret cap.
void EditorController::Impl::retypeChartSelectionFret(int digit, std::uint32_t now_ms)
{
    std::vector<common::core::ChartNote> base_notes = chartNotesForKeys(chartSelection().notes());
    const std::vector<ChartNoteKey> keys = chartSelection().notes();
    std::optional<ChartNotesEditPlan> plan = planRetypeFrets(base_notes, digit, /*set_exact=*/true);
    if (!plan.has_value())
    {
        return;
    }
    const bool pushed = !plan->removed.empty() && applyChartEditPlan(std::move(plan));
    if (digit * 10 <= common::core::g_max_fret)
    {
        m_chart_fret_entry = ChartFretEntry{
            .value = digit,
            .last_keystroke_ms = now_ms,
            .base_notes = std::move(base_notes),
            .keys = keys,
            .insert_plan = std::nullopt,
            .pushed = pushed,
            .history_position = m_undo_history.snapshot().position,
        };
    }
    else
    {
        m_chart_fret_entry.reset();
    }
}

// The full note values behind a sorted key set, in chart order — the one selection-snapshot
// loop the typing and fret-shift verbs share.
std::vector<common::core::ChartNote> EditorController::Impl::chartNotesForKeys(
    const std::vector<ChartNoteKey>& keys) const
{
    std::vector<common::core::ChartNote> notes;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return notes;
    }
    notes.reserve(keys.size());
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (std::ranges::binary_search(
                keys, ChartNoteKey{.position = note.position, .string = note.string}))
        {
            notes.push_back(note);
        }
    }
    return notes;
}

// Shifts every selected note's fret by one (Alt+Shift+wheel), shape-preserving by
// construction; a shift pushing the lowest fret below zero or the highest past the cap is
// refused by the planner, never clamped.
void EditorController::Impl::onChartFretShiftRequested(int direction)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        chartSelection().empty() || direction == 0)
    {
        return;
    }

    const std::vector<common::core::ChartNote> selected =
        chartNotesForKeys(chartSelection().notes());
    std::optional<int> lowest;
    for (const common::core::ChartNote& note : selected)
    {
        if (!lowest.has_value() || note.fret < *lowest)
        {
            lowest = note.fret;
        }
    }
    if (!lowest.has_value())
    {
        return;
    }

    static_cast<void>(applyChartEditPlan(
        planRetypeFrets(selected, *lowest + (direction > 0 ? 1 : -1), /*set_exact=*/false)));
}

// Grows or shrinks the selection's sustains by one grid step — or one 1/960-beat fine step,
// the uniform Ctrl precision tier on the extent verbs — as one compound undo entry; growth
// clamps to the minimum-note-distance margin before the next onset on any string.
void EditorController::Impl::onChartSustainAdjustRequested(int direction, bool fine)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        chartSelection().empty() || direction == 0)
    {
        return;
    }

    const common::core::GridPosition reference = chartSelection().notes().front().position;
    const common::core::Fraction step =
        fine ? common::core::Fraction{1, 960} : chartGridStepBeats(reference);
    const common::core::Fraction delta =
        direction > 0 ? step : common::core::Fraction{-step.numerator, step.denominator};
    static_cast<void>(applyChartEditPlan(planAdjustSustain(
        *arrangement->chart, session().song().tempo_map, chartSelection().notes(), delta)));
}

// The Esc ladder (the marker model): an in-flight pointer gesture is abandoned without
// mutating; else an armed caret — on any row, lane carets included — dissolves to the passive
// cursor in its place, keeping the selection; else THE selection clears, whatever its kind
// (one selection editor-wide, so Esc's last rung is kind-agnostic like Delete's dispatch; a
// region deselect routes through applyToneSelection so the audible tone re-syncs). The marker
// rungs also end the multi-digit fret-entry window — after a cancel, the next digit must not
// widen a dead entry.
void EditorController::Impl::onChartEscapePressed()
{
    // Gesture cancels outrank the marker/selection ladder: an in-flight pointer drag simply never
    // commits. A move/insert lane drag drops without touching the model; the next rebuild paints
    // the lane back without the preview.
    if (m_tone_automation_drag.has_value())
    {
        m_tone_automation_drag.reset();
        updateView();
        return;
    }

    if (m_chart_gesture.has_value())
    {
        m_chart_gesture.reset();
        updateView();
        return;
    }

    if (armedChartCaret() != nullptr)
    {
        dissolveChartCaretInPlace();
        m_chart_fret_entry.reset();
        updateView();
        return;
    }

    if (!selectedToneRegionId().empty())
    {
        applyToneSelection({});
        updateView();
        return;
    }
    if (!std::holds_alternative<std::monostate>(m_selection))
    {
        clearSelection();
        updateView();
    }
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

// Applies the central action gate and routes the accepted action.
void EditorController::Impl::runAction(EditorAction::Action action)
{
    const EditorAction::Id action_id = idOf(action);
    logEditorActionRequested(action_id);
    if (!isBusy())
    {
        flushPendingPluginEdits("plugin_edit.action_dispatch");
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
    reconcileToneDesignerCleanMarker();

    // Tone-set edits reload the rig when applied, dropping branches the model no longer
    // references; undoing or redoing them can restore references to those dropped tones (reset
    // undo brings back the old tone with its plugins). Reload so the rig hosts every referenced
    // tone again instead of leaving the restored model pointing at missing branches.
    if (m_project.has_value() && m_project_audio_ready && !loadedRigCoversModelTones())
    {
        reloadLiveRigForToneSet(selectedToneRegionId());
        return;
    }

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
        .session = m_session,
        .signal_chain = m_signal_chain,
        .plugin_host = m_plugin_host,
        .live_rig = m_live_rig,
        .tone_automation = m_tone_automation,
        .output_gain_db = m_output_gain_db,
        .tone_designer = m_tone_designer,
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
        "Rollback contract violation context={:?} direction={:?} label={:?}",
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
        "editor.controller",
        "Rollback contract violation context={:?} detail={:?}",
        context,
        detail);
    enterFaultedSession();
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

void EditorController::Impl::performActionImpl(EditorAction::PlayPause /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (m_transport.state().playing)
    {
        // Pause rests the marker passive at the raw stop point (the marker model): the paused
        // cursor line simply stays where the playhead stopped — no snapping, which happens
        // only at arming. The marker was already demoted at play, so there is nothing to do
        // beyond pausing and republishing.
        m_transport.pause();
        updateView();
    }
    else
    {
        // Play FROM THE MARKER: an armed caret seeks playback to its slot; a passive cursor
        // already IS the transport position, so playback resumes in place. Playback then
        // dissolves the caret and clears the note selection — one position concept per
        // transport state, with only the string memory surviving for the next arming.
        // Starting playback also makes the region under the cursor the active tone; the tone
        // row keeps it following boundary crossings at render cadence.
        if (const ChartCaret* const caret = armedChartCaret())
        {
            const common::core::TempoMap& tempo_map = session().song().tempo_map;
            m_transport.seek(
                session().timeline().clamp(
                    common::core::TimePosition{tempo_map.secondsAtNote(
                        caret->position.measure, caret->position.beat, caret->position.offset)}));
        }
        disarmChartMarker();
        clearSelection();
        activateToneAtCursor();
        m_transport.play();
        updateView();
    }
}

void EditorController::Impl::performActionImpl(EditorAction::Stop /*action*/)
{
    const common::audio::TransportState transport_state = m_transport.state();
    if (!canStopTransport(transport_state))
    {
        return;
    }
    // Stop rests the marker passive wherever the transport resets to; a stopped-while-paused
    // armed caret dissolves because Stop is a transport action, not an editing one.
    m_transport.stop();
    disarmChartMarker();
    activateToneAtCursor();
    updateView();
}

// Clamps the requested position into the session timeline so out-of-range view intents cannot
// move the cursor outside the loaded content.
void EditorController::Impl::performActionImpl(EditorAction::SeekTimeline action)
{
    const common::core::TimePosition position = session().timeline().clamp(action.position);
    m_transport.seek(position);
    // A seek is transport motion, so the marker demotes to its passive state (the marker
    // model): the paused cursor line rests exactly at the seek target — the ruler click, a
    // waveform click — and arming waits for the next editing gesture. Playing seeks move only
    // the live playhead (the marker is already passive).
    disarmChartMarker();
    // The active tone follows the cursor: the region under the new position becomes the tone
    // context, and any formal selection is cleared so a stray Delete cannot remove a tone.
    activateToneAtCursor();
    updateView();
}

// Applies a validated grid note-value change, persists it as app-local per-project state, and
// republishes view state so the grid, ruler, and snapping move together.
void EditorController::Impl::performActionImpl(EditorAction::SetGridNoteValue action)
{
    if (!isValidTempoGridNoteValue(action.note_value) || action.note_value == m_grid_note_value)
    {
        return;
    }

    m_grid_note_value = action.note_value;
    if (!m_project_file.empty())
    {
        recordSettingsResultBestEffort(
            m_settings.saveProjectGridNoteValue(m_project_file, m_grid_note_value),
            "save project grid note value");
    }
    updateView();
}

// Collects availability inputs using fresh controller snapshots for immediate action gates.
ActionConditions EditorController::Impl::currentActionConditions() const
{
    const InputCalibrationProjection input_calibration =
        makeInputCalibrationProjection(m_live_input_monitor, monitoringContext());

    return currentActionConditions(input_calibration, m_transport.state());
}

// Reuses already-sampled view projection state so enabled flags share one availability snapshot.
ActionConditions EditorController::Impl::currentActionConditions(
    const InputCalibrationProjection& input_calibration,
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
        .has_tone_import_prompt = m_pending_tone_import.has_value(),
        .has_save_as_prompt = m_deferred_project_action_state.saveAsPrompt().has_value(),
        // A pending plugin edit is flushed into a real undo entry at the action gate, so undo is
        // offered for it too.
        .undo_available = m_undo_history.canUndo() || m_plugin_host.hasPendingPluginEdits(),
        .redo_available = m_undo_history.canRedo(),
        .has_loaded_arrangement = hasLoadedArrangement(),
        .tone_designer_active = m_tone_designer.active,
        .can_stop_transport = canStopTransport(transport_state),
        .has_plugin_candidates = m_plugin_catalog.hasCandidates(),
        .has_plugin_insert_capacity = m_signal_chain.hasInsertCapacity(),
        .has_loaded_plugins = m_signal_chain.hasPlugins(),
    };
}

// Coarse-only transport callback. During an in-flight session load, defer the push so the final
// derivation runs against the updated session and transport state instead of stale data.
void EditorController::Impl::onTransportStateChanged(common::audio::TransportState state)
{
    if (m_session_load_in_progress)
    {
        return;
    }
    // Playback dissolves the caret and the note selection no matter what started it (the
    // marker model's armed ⟹ paused invariant, enforced here for transports the PlayPause
    // action did not drive — external starts, test doubles flipping state directly).
    if (state.playing)
    {
        disarmChartMarker();
        clearSelection();
    }
    updateView();
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

// Builds the message-thread view state from the session and transport state. Current cursor
// position is only sampled to derive stop enabledness; the view receives discrete mapping state
// rather than a continuously pushed playhead position.
namespace
{

// Labels one arrangement by its part, numbering duplicates ("Rhythm 1", "Rhythm 2") so every
// switcher entry stays distinguishable.
[[nodiscard]] std::string arrangementPartLabel(common::core::Part part)
{
    switch (part)
    {
        case common::core::Part::Lead:
        {
            return "Lead";
        }
        case common::core::Part::Rhythm:
        {
            return "Rhythm";
        }
        case common::core::Part::Bass:
        {
            return "Bass";
        }
    }

    return "Arrangement";
}

// Builds the switcher entries for every arrangement of the loaded song, ordered Lead, Rhythm,
// Bass regardless of how the song stores its arrangements. The Part enum already ranks the parts
// in that order, and a stable sort keeps the original order within each part so duplicate
// numbering ("Rhythm 1", "Rhythm 2") stays predictable.
[[nodiscard]] std::vector<ArrangementChoiceViewState> arrangementChoicesFor(
    const std::vector<common::core::Arrangement>& arrangements, const std::string& current_id)
{
    // A plain index loop on purpose: Apple's libc++ has no C++23 std::ranges::iota, clang-tidy's
    // modernize-use-ranges rejects the classic std::iota, and clang-tidy 22 cannot parse
    // libstdc++'s ranges::to pipe machinery — every library spelling fails one CI toolchain.
    std::vector<std::size_t> display_order;
    display_order.reserve(arrangements.size());
    for (std::size_t index = 0; index < arrangements.size(); ++index)
    {
        display_order.push_back(index);
    }
    std::ranges::stable_sort(display_order, {}, [&arrangements](std::size_t index) {
        return static_cast<int>(arrangements[index].part);
    });

    std::vector<int> part_totals(3, 0);
    for (const common::core::Arrangement& arrangement : arrangements)
    {
        part_totals[static_cast<std::size_t>(arrangement.part)] += 1;
    }

    std::vector<ArrangementChoiceViewState> choices;
    choices.reserve(arrangements.size());
    std::vector<int> part_counts(3, 0);
    for (const std::size_t index : display_order)
    {
        const common::core::Arrangement& arrangement = arrangements[index];
        const auto part_index = static_cast<std::size_t>(arrangement.part);
        part_counts[part_index] += 1;
        std::string label = arrangementPartLabel(arrangement.part);
        if (part_totals[part_index] > 1)
        {
            label += " " + std::to_string(part_counts[part_index]);
        }
        choices.push_back(
            ArrangementChoiceViewState{
                .id = arrangement.id,
                .label = std::move(label),
                .selected = arrangement.id == current_id,
            });
    }

    return choices;
}

} // namespace

EditorViewState EditorController::Impl::deriveViewState() const
{
    const common::audio::TransportState transport_state = m_transport.state();
    const common::core::TimeRange timeline_range = session().timeline();
    const InputCalibrationProjection input_calibration =
        makeInputCalibrationProjection(m_live_input_monitor, monitoringContext());
    const ActionConditions action_conditions =
        currentActionConditions(input_calibration, transport_state);

    EditorViewState state;
    state.project_file = currentProjectFile();
    state.open_enabled = isActionAvailable(EditorAction::Id::OpenProject, action_conditions);
    state.import_enabled = isActionAvailable(EditorAction::Id::ImportSong, action_conditions);
    state.save_enabled = isActionAvailable(EditorAction::Id::SaveProject, action_conditions);
    state.save_as_enabled = isActionAvailable(EditorAction::Id::SaveProjectAs, action_conditions);
    state.publish_enabled = isActionAvailable(EditorAction::Id::PublishProject, action_conditions);
    state.undo_enabled = isActionAvailable(EditorAction::Id::Undo, action_conditions);
    state.undo_label = m_undo_history.undoLabel();
    state.redo_enabled = isActionAvailable(EditorAction::Id::Redo, action_conditions);
    state.redo_label = m_undo_history.redoLabel();
    {
        // Destructured so the label list can move out of the snapshot instead of copying.
        auto [labels, position, clean_position] = m_undo_history.snapshot();
        state.undo_history.labels = std::move(labels);
        state.undo_history.position = position;
        state.undo_history.clean_position = clean_position;
    }
    if (!m_project_file.empty())
    {
        state.suggested_publish_file = m_project_file;
        state.suggested_publish_file.replace_extension(".rock");
    }
    state.close_enabled = isActionAvailable(EditorAction::Id::CloseProject, action_conditions);
    state.project_loaded = action_conditions.has_loaded_arrangement;
    state.project_load_id = m_project_load_id;
    state.save_requires_destination = m_save_requires_destination;
    state.transport.play_pause_enabled =
        isActionAvailable(EditorAction::Id::PlayPause, action_conditions);
    state.transport.stop_enabled = isActionAvailable(EditorAction::Id::Stop, action_conditions);
    state.transport.play_pause_shows_pause_icon = transport_state.playing;
    state.audio_device_settings_enabled = input_calibration.audio_device_settings_enabled;
    state.audio_device_status_text = audioDeviceStatusText(m_audio_devices.currentDeviceStatus());
    // The live routing truth, not a re-read of the persisted toggle: the store pointer is what
    // every audio-config read actually flows through, and it can only be set by a successful
    // adoption.
    state.use_game_audio_settings =
        m_editor_audio_config_store != nullptr && m_editor_audio_config_store->usingGameSource();
    state.game_audio_unavailable_prompt = m_game_audio_unavailable_prompt;
    state.game_audio_recommendation_prompt = m_game_audio_recommendation_prompt;
    state.audio_device_failure_prompt = m_audio_device_failure_prompt;
    state.visible_timeline = timeline_range;
    state.tempo_map = session().song().tempo_map;
    // Song-level, so they resolve here rather than in the per-arrangement tab projection; the
    // list is small enough that per-push resolution needs no memoization.
    state.sections = makeSongSectionViews(session().song().sections, state.tempo_map);
    state.grid_note_value = m_grid_note_value;
    state.timeline_zoom_pixels_per_second = m_timeline_zoom_pixels_per_second;
    state.waveform_visible = m_waveform_visible;
    state.tab_minimum_displayed_strings = m_tab_minimum_displayed_strings;
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
        .output_gain_controls_enabled =
            ((m_project_audio_ready && action_conditions.has_loaded_arrangement) ||
             m_tone_designer.active) &&
            !action_conditions.session_faulted,
        .output_gain_db = m_output_gain_db,
        .tone_import_enabled =
            isActionAvailable(EditorAction::Id::ImportToneFile, action_conditions),
        .tone_export_enabled =
            isActionAvailable(EditorAction::Id::ExportToneFile, action_conditions),
    };
    state.plugin_browser = m_plugin_catalog.viewState(
        isActionAvailable(EditorAction::Id::ScanPluginCatalog, action_conditions),
        isActionAvailable(EditorAction::Id::InsertSelectedPlugin, action_conditions));
    state.tone_designer = ToneDesignerViewState{
        .active = m_tone_designer.active,
        .document_name = m_tone_designer.document_path.has_value()
                             ? m_tone_designer.document_path->stem().string()
                             : std::string{"Untitled"},
        .dirty = toneDesignerHasUnsavedChanges(),
        .has_destination = m_tone_designer.document_path.has_value(),
        .chooser_directory = m_settings.toneFileDirectory().value_or(std::filesystem::path{}),
    };

    if (const auto* arrangement = session().currentArrangement(); arrangement != nullptr)
    {
        state.arrangement = ArrangementViewState{
            .audio_asset = arrangement->audio_asset,
            .audio_duration = arrangement->audio_duration,
            .choices = arrangementChoicesFor(session().arrangements(), arrangement->id),
        };
        state.tone_track = makeToneTrackViewState(
            *arrangement, state.tempo_map, activeToneRegionId(), selectedToneRegionId());
        state.tone_automation = makeToneAutomationViewState(
            *arrangement,
            state.tempo_map,
            activeToneDocumentRef(),
            m_tone_plugin_bindings,
            m_open_automation_lanes,
            m_tone_automation,
            selectedAutomationPoint());
        // A lane-riding caret resolves against the published lanes exactly like the selected
        // point: a caret whose lane is not visible publishes as nothing (§9b).
        if (const ChartCaret* const caret = armedChartCaret();
            caret != nullptr && caret->lane.has_value())
        {
            for (std::size_t lane_index = 0; lane_index < state.tone_automation.lanes.size();
                 ++lane_index)
            {
                const ToneAutomationLaneViewState& lane = state.tone_automation.lanes[lane_index];
                if (lane.instance_id == caret->lane->instance_id &&
                    lane.param_id == caret->lane->param_id)
                {
                    const CaretTimeBounds bounds =
                        caretTimeBounds(state.tempo_map, caret->position);
                    state.tone_automation.lane_caret = ToneAutomationLaneCaretRef{
                        .lane_index = lane_index,
                        .seconds = bounds.seconds,
                        .position = caret->position,
                        .measure_start_seconds = bounds.measure_start_seconds,
                        .measure_end_seconds = bounds.measure_end_seconds,
                    };
                    break;
                }
            }
        }

        // The Alt-hover insert ghost resolves against the published lanes exactly like the lane
        // caret: located by (instance, parameter), with seconds derived through the tempo map
        // identically to the caret's so the ring rides the same visible-timeline convention. The
        // occupancy gate keeps the ring honest (§7): now that mouse placement refuses an occupied
        // slot (onToneAutomationPointerDown's Alt branch shares the keyboard Insert's refusal), the
        // ring is hidden over a slot that already carries a point so it never previews an insert
        // that would no-op. A standing drag owns the lane, so its preview masks the ghost too.
        if (m_tone_insert_ghost.has_value() && !m_tone_automation_drag.has_value())
        {
            for (std::size_t lane_index = 0; lane_index < state.tone_automation.lanes.size();
                 ++lane_index)
            {
                const ToneAutomationLaneViewState& lane = state.tone_automation.lanes[lane_index];
                if (lane.instance_id != m_tone_insert_ghost->instance_id ||
                    lane.param_id != m_tone_insert_ghost->param_id)
                {
                    continue;
                }
                const common::core::GridPosition& position = m_tone_insert_ghost->position;
                const bool occupied = std::ranges::any_of(
                    lane.points, [&position](const ToneAutomationPointViewState& point) {
                        return point.position == position;
                    });
                if (occupied)
                {
                    break;
                }
                state.tone_automation.insert_ghost = ToneAutomationInsertGhostRef{
                    .lane_index = lane_index,
                    .seconds = state.tempo_map.secondsAtNote(
                        position.measure, position.beat, position.offset),
                };
                break;
            }
        }

        // The in-flight move/insert drag preview resolves against the published lanes exactly like
        // the ghost — located by (instance, parameter) — and is published only once the drag has
        // produced a preview (moved, or an insert from the press), so a plain point click that only
        // selects publishes none. The view paints it in place of the moved point.
        if (m_tone_automation_drag.has_value() && m_tone_automation_drag->moved)
        {
            const ToneAutomationDrag& drag = *m_tone_automation_drag;
            for (std::size_t lane_index = 0; lane_index < state.tone_automation.lanes.size();
                 ++lane_index)
            {
                const ToneAutomationLaneViewState& lane = state.tone_automation.lanes[lane_index];
                if (lane.instance_id != drag.instance_id || lane.param_id != drag.param_id)
                {
                    continue;
                }
                state.tone_automation.drag_preview = ToneAutomationDragPreviewRef{
                    .lane_index = lane_index,
                    .position = drag.preview_position,
                    .value = drag.preview_value,
                    .is_new_point = drag.is_new_point,
                    .source_point_index = drag.point_index,
                };
                break;
            }
        }

        // The tab projection resolves thousands of positions to seconds, so it is memoized per
        // displayed arrangement and chart revision: the arrangement id keys which chart is shown,
        // and the session's chart revision (bumped by every mutable chart acquisition) keys its
        // edit state, so chart edits invalidate without any explicit notification path. The 3D
        // highway projection rides the same rule (plan 44): one shared scene-model snapshot per
        // displayed arrangement, consumed by the preview window exactly as the game consumes it.
        const bool arrangement_changed = m_tab_arrangement_id != arrangement->id ||
                                         m_tab_chart_revision != session().chartRevision();
        if (arrangement_changed)
        {
            m_tab_view_state = std::make_shared<const common::core::TabViewState>(
                common::core::makeTabViewState(*arrangement, state.tempo_map));
        }
        // The 3D projection bakes in the displayed-string minimum (the shared palette must anchor
        // exactly as the 2D tab's), so it rebuilds on an arrangement change OR a minimum change —
        // the tab, which pads in the view, needs neither. Lowest-pitched string on top is the 3D
        // default (user decision 2026-07-11, recorded in plan 25).
        if (arrangement_changed || m_highway_min_strings != m_tab_minimum_displayed_strings)
        {
            m_highway_view_state = std::make_shared<const common::core::HighwayViewState>(
                common::core::makeHighwayViewState(
                    *arrangement,
                    state.tempo_map,
                    session().song().sections,
                    common::core::HighwayDisplayOptions{
                        .mirrored = false,
                        .invert_string_order = true,
                        .minimum_string_count = m_tab_minimum_displayed_strings,
                    }));
            m_highway_min_strings = m_tab_minimum_displayed_strings;
        }
        m_tab_arrangement_id = arrangement->id;
        m_tab_chart_revision = session().chartRevision();
        state.tab = m_tab_view_state;
        state.highway = m_highway_view_state;

        // Chart-editing overlays resolve against exactly the projection instance pushed above:
        // selection keys re-resolve to indices every push, so keys whose notes vanished simply
        // drop out instead of pointing at the wrong glyph.
        if (arrangement->chart.has_value())
        {
            state.chart_edit.selected_notes =
                selectedNoteIndices(arrangement->chart->notes, chartSelection());
            // The marker publishes plainly from its state — armed ⟹ paused is structural
            // (play and the transport listener demote), so no transport check re-derives it
            // here. The caret publishes whenever armed, empty slot or note alike: the square
            // stays visible through a single selection, and its presence is the armed signal
            // that hides the paused playhead. A lane-riding caret publishes through the
            // tone-automation state instead (§9b), so the tab lane draws no square for it.
            if (const ChartCaret* const caret = armedChartCaret();
                caret != nullptr && !caret->lane.has_value())
            {
                const CaretTimeBounds bounds =
                    caretTimeBounds(session().song().tempo_map, caret->position);
                state.chart_edit.caret = ChartCaretViewState{
                    .seconds = bounds.seconds,
                    .string = caret->string,
                    .measure_start_seconds = bounds.measure_start_seconds,
                    .measure_end_seconds = bounds.measure_end_seconds,
                };
            }
            if (m_chart_gesture.has_value() && m_chart_gesture->marquee &&
                m_chart_gesture->geometry.bounds_width > 0.0f &&
                m_chart_gesture->geometry.bounds_height > 0.0f)
            {
                const ChartPointerGesture& gesture = *m_chart_gesture;
                const double seconds_per_pixel =
                    gesture.geometry.visible_timeline.duration().seconds /
                    static_cast<double>(gesture.geometry.bounds_width);
                const auto start_offset =
                    static_cast<double>(std::min(gesture.anchor_x, gesture.current_x));
                const auto end_offset =
                    static_cast<double>(std::max(gesture.anchor_x, gesture.current_x));
                const float top = std::min(gesture.anchor_y, gesture.current_y);
                const float bottom = std::max(gesture.anchor_y, gesture.current_y);
                state.chart_edit.marquee = ChartMarqueeViewState{
                    .start_seconds = gesture.geometry.visible_timeline.start.seconds +
                                     start_offset * seconds_per_pixel,
                    .end_seconds = gesture.geometry.visible_timeline.start.seconds +
                                   end_offset * seconds_per_pixel,
                    .top_fraction = std::clamp(
                        (top - gesture.geometry.bounds_y) / gesture.geometry.bounds_height,
                        0.0f,
                        1.0f),
                    .bottom_fraction = std::clamp(
                        (bottom - gesture.geometry.bounds_y) / gesture.geometry.bounds_height,
                        0.0f,
                        1.0f),
                };
            }
            // The Alt-hover insert ghost publishes verbatim: it is already resolved to seconds +
            // string, and is set only while Alt hovers an insertable empty slot (else absent).
            state.chart_edit.insert_ghost = m_chart_insert_ghost;
        }
    }
    else
    {
        m_tab_view_state.reset();
        m_highway_view_state.reset();
        m_tab_arrangement_id.clear();
    }
    state.unsaved_changes_prompt = m_deferred_project_action_state.unsavedChangesPrompt();
    state.save_as_prompt = m_deferred_project_action_state.saveAsPrompt();
    state.tone_import_prompt =
        m_pending_tone_import.has_value()
            ? std::optional{ToneImportPrompt{m_pending_tone_import_automation_count}}
            : std::nullopt;

    if (m_restore_interrupted_prompt_file.has_value())
    {
        state.restore_interrupted_prompt =
            RestoreInterruptedPrompt{*m_restore_interrupted_prompt_file};
    }

    state.input_calibration_prompt = input_calibration.prompt;

    state.busy = m_busy.viewState();

    return state;
}

void EditorController::Impl::recordSettingsResultBestEffort(
    std::expected<void, EditorSettingsError> result, std::string_view context)
{
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
    }
}

void EditorController::Impl::recordAudioConfigResultBestEffort(
    std::expected<void, common::audio::AudioConfigError> result, std::string_view context)
{
    if (!result.has_value())
    {
        logEditorControllerBestEffortFailure(context, result.error().message);
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
    return arrangement != nullptr && !arrangement->tones.empty();
}

// Reports whether every tone the current arrangement references has a loaded rig branch. An
// empty loaded set means no load has reported branches yet (or the port under test does not
// report them); coverage is then unknowable and treated as satisfied.
bool EditorController::Impl::loadedRigCoversModelTones() const
{
    if (m_loaded_tone_refs.empty())
    {
        return true;
    }
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return true;
    }
    return std::ranges::all_of(
        arrangement->tone_track.regions, [this](const common::core::ToneRegion& region) {
            return region.tone_document_ref.empty() ||
                   std::ranges::find(m_loaded_tone_refs, region.tone_document_ref) !=
                       m_loaded_tone_refs.end();
        });
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
