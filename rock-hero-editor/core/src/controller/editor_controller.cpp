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

void EditorController::onChartCursorStepRequested(ChartStepDirection direction, bool fine)
{
    m_impl->onChartCursorStepRequested(direction, fine);
}

void EditorController::onChartSelectionMoveRequested(ChartStepDirection direction, bool fine)
{
    m_impl->onChartSelectionMoveRequested(direction, fine);
}

void EditorController::onChartSelectionDeleteRequested()
{
    m_impl->onChartSelectionDeleteRequested();
}

void EditorController::onChartFretDigitTyped(int digit)
{
    m_impl->onChartFretDigitTyped(digit);
}

void EditorController::onChartSustainAdjustRequested(int direction, bool fine)
{
    m_impl->onChartSustainAdjustRequested(direction, fine);
}

void EditorController::onChartGestureCancelled()
{
    m_impl->onChartGestureCancelled();
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
    m_chart_selection.clear();
    m_chart_gesture.reset();
    m_chart_fret_entry.reset();
}

// Resolves the event's snapped musical position and the string lane under the pointer,
// mirroring the timeline's placement rules: plain placement stores the grid line's own exact
// rational, precision (Ctrl) quantizes to the shared 1/960-beat fine grid.
std::optional<std::pair<common::core::GridPosition, int>> EditorController::Impl::chartPlacementAt(
    const ChartPointerEvent& event) const
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || event.geometry.lane_height <= 0.0f)
    {
        return std::nullopt;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::optional<common::core::TimePosition> clicked = timelineCursorPlacementTime(
        tempo_map,
        m_grid_note_value,
        event.geometry.visible_timeline,
        static_cast<int>(event.geometry.bounds_width),
        event.x,
        TimelineCursorPlacementMode::Free);
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

// One grid step in beats at a position: the note value is a fraction of a whole note and a beat
// is one signature-denominator unit, so step_beats = note_value x denominator; the fine step is
// the shared 1/960-beat precision grid.
common::core::Fraction EditorController::Impl::chartGridStepBeats(
    common::core::GridPosition at, bool fine) const
{
    if (fine)
    {
        return common::core::Fraction{1, g_fine_grid_denominator};
    }
    const common::core::TimeSignatureChange signature =
        session().song().tempo_map.timeSignatureAt(at.measure);
    return common::core::Fraction{
        m_grid_note_value.numerator * signature.denominator, m_grid_note_value.denominator
    };
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
        m_chart_selection.applyBox(*select_exactly, false);
    }
    else
    {
        // (selection - removed keys) + inserted keys: retyped/resized notes stay selected even
        // when the edit left some of them unchanged, moved notes follow to their new keys, and
        // deleted notes drop out. Plans never carry unrelated notes, so this never grows the
        // selection past what the user had plus what the edit produced at new keys.
        std::vector<ChartNoteKey> next_selection;
        next_selection.reserve(m_chart_selection.notes().size() + plan->inserted.size());
        for (const ChartNoteKey& key : m_chart_selection.notes())
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
        m_chart_selection.applyBox(next_selection, false);
    }

    pushUndoEntry(std::make_unique<ChartNotesEdit>(std::move(*plan)));
    updateView();
    return true;
}

// Commits the Alt insert quasimode: a note carrying the last-used fret lands at the snapped
// release position on the lane under the pointer.
void EditorController::Impl::insertChartNoteAt(const ChartPointerEvent& event)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }

    const auto placement = chartPlacementAt(event);
    if (!placement.has_value())
    {
        return;
    }

    common::core::ChartNote note;
    note.position = placement->first;
    note.string = placement->second;
    note.fret = std::clamp(m_chart_last_fret, 0, common::core::g_max_fret);
    // Select exactly the placed note (not a 40-Q2-B-truncated neighbor the plan may also carry);
    // its selection highlight is the placement feedback.
    static_cast<void>(applyChartEditPlan(
        planInsertNote(*arrangement->chart, session().song().tempo_map, note),
        std::vector<ChartNoteKey>{ChartNoteKey{.position = note.position, .string = note.string}}));
}

// Arms the gesture and applies glyph-press selection per the interaction grammar: plain press
// selects (keeping an existing multi-selection intact so a future drag can move it), Ctrl
// toggles membership, Shift extends. Alt is the insertion quasimode: the press arms it and the
// release commits the note at the snapped release point.
void EditorController::Impl::onChartPointerDown(const ChartPointerEvent& event)
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || isBusy())
    {
        return;
    }

    ChartPointerGesture gesture;
    gesture.geometry = event.geometry;
    gesture.modifiers = event.modifiers;
    gesture.anchor_x = event.x;
    gesture.anchor_y = event.y;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    if (event.modifiers.alt)
    {
        gesture.alt_insert = true;
        m_chart_gesture = gesture;
        return;
    }
    gesture.hit_note = chartNoteHitIndex(*tab, event.geometry, event.x, event.y);
    m_chart_gesture = gesture;

    if (!gesture.hit_note.has_value())
    {
        return;
    }

    const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
    if (!key.has_value())
    {
        return;
    }

    if (event.modifiers.ctrl)
    {
        m_chart_selection.toggle(*key);
    }
    else if (event.modifiers.shift)
    {
        m_chart_selection.add(*key);
    }
    else if (!m_chart_selection.contains(*key))
    {
        m_chart_selection.replaceWith(*key);
    }
    // The selection highlight is the whole feedback for a glyph press (user feedback
    // 2026-07-17: no extra cursor furniture on selection).
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
    if (gesture.hit_note.has_value() || gesture.alt_insert)
    {
        return;
    }

    const bool beyond_threshold =
        std::abs(event.x - gesture.anchor_x) > g_chart_click_threshold_px ||
        std::abs(event.y - gesture.anchor_y) > g_chart_click_threshold_px;
    if (!gesture.marquee && !beyond_threshold)
    {
        return;
    }

    gesture.marquee = true;
    updateView();
}

// Resolves the gesture: a marquee release selects the boxed notes (Shift extends), an
// empty-lane click seeks the snapped click position, clears the selection, and places the
// a plain click-release on an already-selected note collapses the selection to it.
void EditorController::Impl::onChartPointerUp(const ChartPointerEvent& event)
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    const ChartPointerGesture gesture = *m_chart_gesture;
    m_chart_gesture.reset();

    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
    {
        updateView();
        return;
    }

    if (gesture.alt_insert)
    {
        // The insert quasimode commits at the release point (press-drag-release places in one
        // gesture; a plain Alt+click inserts where it clicked).
        insertChartNoteAt(event);
        return;
    }

    if (gesture.hit_note.has_value())
    {
        const bool clicked = std::abs(event.x - gesture.anchor_x) <= g_chart_click_threshold_px &&
                             std::abs(event.y - gesture.anchor_y) <= g_chart_click_threshold_px;
        if (clicked && !gesture.modifiers.ctrl && !gesture.modifiers.shift)
        {
            if (const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
                key.has_value())
            {
                m_chart_selection.replaceWith(*key);
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
        m_chart_selection.applyBox(keys, gesture.modifiers.shift);
        updateView();
        return;
    }

    // Empty click: seek + deselect, exactly the overlay's click semantics.
    m_chart_selection.clear();
    const std::optional<common::core::TimePosition> seek_time = timelineCursorPlacementTime(
        session().song().tempo_map,
        m_grid_note_value,
        gesture.geometry.visible_timeline,
        static_cast<int>(gesture.geometry.bounds_width),
        event.x,
        event.modifiers.ctrl ? TimelineCursorPlacementMode::Free
                             : TimelineCursorPlacementMode::SnapToGrid);
    if (seek_time.has_value())
    {
        onTimelineSeekRequested(*seek_time);
    }
    updateView();
}

// Arrow-key navigation: Left/Right step the ONE timeline cursor (the transport position) along
// the rendered tempo grid - pure navigation, never a mutation. There is no separate editing
// caret (user decision 2026-07-17: the playhead is the position feedback, and a second cursor
// icon is noise). Vertical directions have no navigation meaning and are ignored.
void EditorController::Impl::onChartCursorStepRequested(ChartStepDirection direction, bool fine)
{
    const common::core::TabViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || isBusy() ||
        (direction != ChartStepDirection::Left && direction != ChartStepDirection::Right))
    {
        return;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const int sign = direction == ChartStepDirection::Right ? 1 : -1;
    const common::core::TimePosition transport_position = m_transport.position();
    common::core::GridPosition stepped;
    if (fine)
    {
        // Precision stepping quantizes the playhead to the shared fine grid, then moves one
        // fine step.
        stepped = common::core::advanceGridPosition(
            tempo_map,
            fineGridPositionForBeat(
                tempo_map, tempo_map.beatPositionAtSeconds(transport_position.seconds)),
            common::core::Fraction{sign, g_fine_grid_denominator});
    }
    else
    {
        // Snap the playhead onto the grid, then step one grid line; the re-snap-and-push guard
        // keeps the cursor progressing across measure-anchored grid restarts.
        const common::core::GridPosition current =
            nearestTempoGridPosition(tempo_map, m_grid_note_value, transport_position);
        const common::core::Fraction unsigned_step = chartGridStepBeats(current, false);
        const common::core::Fraction step{
            sign * unsigned_step.numerator, unsigned_step.denominator
        };
        stepped = common::core::snapGridPosition(
            tempo_map,
            common::core::advanceGridPosition(tempo_map, current, step),
            m_grid_note_value);
        if (stepped == current)
        {
            stepped = common::core::snapGridPosition(
                tempo_map,
                common::core::advanceGridPosition(tempo_map, stepped, step),
                m_grid_note_value);
        }
    }
    onTimelineSeekRequested(
        common::core::TimePosition{tempo_map.secondsAtNote(
            stepped.measure, stepped.beat, stepped.offset)});
}
// Moves the selection under the Alt authoring modifier: Left/Right by one grid step (Ctrl
// fine), Up/Down across strings. A refused move (edge of the neck, occupied slot, grid origin
// collision) is a silent no-op — the selection stays put, matching refuse-not-clamp everywhere
// else.
void EditorController::Impl::onChartSelectionMoveRequested(ChartStepDirection direction, bool fine)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        m_chart_selection.empty())
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
            const common::core::GridPosition reference = m_chart_selection.notes().front().position;
            const common::core::Fraction step = chartGridStepBeats(reference, fine);
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
    static_cast<void>(applyChartEditPlan(planMoveNotes(
        *arrangement->chart,
        session().song().tempo_map,
        m_chart_selection.notes(),
        beat_delta,
        string_delta,
        m_chart_selection.notes().size() == 1 ? "Move Note" : "Move Notes")));
}

// Deletes the selected notes as one compound undo entry; the selection empties with them.
void EditorController::Impl::onChartSelectionDeleteRequested()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        m_chart_selection.empty())
    {
        return;
    }

    static_cast<void>(
        applyChartEditPlan(planDeleteNotes(*arrangement->chart, m_chart_selection.notes())));
}

// Retypes the selection's fret from typed digits: keystrokes inside the entry window combine
// into multi-digit frets, clamped to the fret cap; each keystroke applies immediately so the
// notation always shows the value being typed.
void EditorController::Impl::onChartFretDigitTyped(int digit)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        m_chart_selection.empty() || digit < 0 || digit > 9)
    {
        return;
    }

    // The entry window sits well above deliberate two-digit typing (inter-keystroke ~150-300ms)
    // and below a thinking pause, so "12" combines and "2, pause, 3" stays two values (tuned
    // down from 1500ms on user feel feedback 2026-07-17).
    constexpr std::uint32_t entry_window_ms = 800;
    const std::uint32_t now_ms = juce::Time::getMillisecondCounter();

    // A second digit inside the window WIDENS the in-flight entry: the chart moves to the
    // combined value and the just-pushed undo entry is replaced by one spanning from the
    // pre-entry originals, so the whole typed number undoes as one action. The widen requires
    // the same selection, a combinable value, and the history top still being our entry (any
    // interleaved edit or undo moves the position and kills the window).
    if (m_chart_fret_entry.has_value())
    {
        const ChartFretEntry entry = *m_chart_fret_entry;
        const int combined = entry.value * 10 + digit;
        const bool widenable = now_ms - entry.last_keystroke_ms <= entry_window_ms &&
                               combined <= common::core::g_max_fret &&
                               entry.keys == m_chart_selection.notes() &&
                               m_undo_history.snapshot().position == entry.history_position;
        if (widenable)
        {
            common::core::Chart* const chart = m_session.currentChart();
            if (chart == nullptr)
            {
                return;
            }
            if (const auto incremental = planSetFret(*chart, entry.keys, combined);
                incremental.has_value())
            {
                if (!applyChartNotesChange(*chart, incremental->removed, incremental->inserted)
                         .has_value())
                {
                    reportError("Could not apply chart edit: " + incremental->label);
                    m_chart_fret_entry.reset();
                    return;
                }
            }

            ChartNotesEditPlan widened;
            widened.label = "Set Fret " + std::to_string(combined);
            for (const common::core::ChartNote& base : entry.base_notes)
            {
                common::core::ChartNote retyped = base;
                retyped.fret = combined;
                if (!(retyped == base))
                {
                    widened.removed.push_back(base);
                    widened.inserted.push_back(std::move(retyped));
                }
            }
            bool now_pushed = entry.pushed;
            if (!widened.removed.empty())
            {
                bool replaced = false;
                if (entry.pushed)
                {
                    replaced = m_undo_history.replaceTop(std::make_unique<ChartNotesEdit>(widened))
                                   .status == EditorUndoTransitionStatus::Applied;
                }
                if (!replaced)
                {
                    // No entry of ours to widen (the first digit was a no-op) or the history
                    // refused the swap (for example a save marked the top entry clean mid-
                    // window): the combined change lands as its own entry instead — two
                    // undo steps in a rare edge beats a stack that lies about the file.
                    pushUndoEntry(std::make_unique<ChartNotesEdit>(std::move(widened)));
                }
                now_pushed = true;
            }
            m_chart_fret_entry = ChartFretEntry{
                .value = combined,
                .last_keystroke_ms = now_ms,
                .base_notes = entry.base_notes,
                .keys = entry.keys,
                .pushed = now_pushed,
                .history_position = m_undo_history.snapshot().position,
            };
            m_chart_last_fret = combined;
            updateView();
            return;
        }
    }

    // Fresh entry: capture the pre-entry values first so a later widen still restores them,
    // apply the digit as its own undo entry, and open the window only while a second digit
    // could still fit under the fret cap.
    std::vector<common::core::ChartNote> base_notes;
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (m_chart_selection.contains(
                ChartNoteKey{.position = note.position, .string = note.string}))
        {
            base_notes.push_back(note);
        }
    }
    const std::vector<ChartNoteKey> keys = m_chart_selection.notes();
    const bool pushed = applyChartEditPlan(planSetFret(*arrangement->chart, keys, digit));
    m_chart_last_fret = digit;
    if (digit * 10 <= common::core::g_max_fret)
    {
        m_chart_fret_entry = ChartFretEntry{
            .value = digit,
            .last_keystroke_ms = now_ms,
            .base_notes = std::move(base_notes),
            .keys = keys,
            .pushed = pushed,
            .history_position = m_undo_history.snapshot().position,
        };
    }
    else
    {
        m_chart_fret_entry.reset();
    }
}

// Grows or shrinks the selection's sustains by one grid step (fine 1/960 with precision) as one
// compound undo entry; 40-Q2-B clamps growth against the next same-string onset.
void EditorController::Impl::onChartSustainAdjustRequested(int direction, bool fine)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || isBusy() ||
        m_chart_selection.empty() || direction == 0)
    {
        return;
    }

    const common::core::GridPosition reference = m_chart_selection.notes().front().position;
    const common::core::Fraction step = chartGridStepBeats(reference, fine);
    const common::core::Fraction delta =
        direction > 0 ? step : common::core::Fraction{-step.numerator, step.denominator};
    static_cast<void>(applyChartEditPlan(planAdjustSustain(
        *arrangement->chart, session().song().tempo_map, m_chart_selection.notes(), delta)));
}

// Escape abandons the in-flight pointer gesture (marquee or armed press) without mutating.
void EditorController::Impl::onChartGestureCancelled()
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    m_chart_gesture.reset();
    updateView();
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
        reloadLiveRigForToneSet(m_selected_tone_region_id);
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
        m_transport.pause();
    }
    else
    {
        // Starting playback makes the region under the cursor the active tone (clearing any formal
        // selection); the tone row then keeps it following boundary crossings at render cadence.
        activateToneAtCursor();
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
    activateToneAtCursor();

    if (!transport_state.playing)
    {
        updateView();
    }
}

// Clamps the requested position into the session timeline so out-of-range view intents cannot
// move the cursor outside the loaded content.
void EditorController::Impl::performActionImpl(EditorAction::SeekTimeline action)
{
    const common::core::TimePosition position = session().timeline().clamp(action.position);
    m_transport.seek(position);
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
void EditorController::Impl::onTransportStateChanged(common::audio::TransportState /*state*/)
{
    if (m_session_load_in_progress)
    {
        return;
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
            *arrangement, state.tempo_map, activeToneRegionId(), m_selected_tone_region_id);
        state.tone_automation = makeToneAutomationViewState(
            *arrangement,
            state.tempo_map,
            activeToneDocumentRef(),
            m_tone_plugin_bindings,
            m_open_automation_lanes,
            m_tone_automation);

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
                selectedNoteIndices(arrangement->chart->notes, m_chart_selection);
            if (m_chart_gesture.has_value() && m_chart_gesture->marquee &&
                m_chart_gesture->geometry.bounds_width > 0.0f &&
                m_chart_gesture->geometry.bounds_height > 0.0f)
            {
                const ChartPointerGesture& gesture = *m_chart_gesture;
                const double seconds_per_pixel =
                    gesture.geometry.visible_timeline.duration().seconds /
                    static_cast<double>(gesture.geometry.bounds_width);
                const double start_offset =
                    static_cast<double>(std::min(gesture.anchor_x, gesture.current_x));
                const double end_offset =
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
