#include "controller/editor_controller.h"

#include "audio_device/audio_device_status_text.h"
#include "busy/busy_operation_workflow.h"
#include "chart/chart_hit_testing.h"
#include "chart/chart_navigation.h"
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
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <ranges>
#include <rock_hero/common/audio/device/device_restore_outcome.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/shared/logger.h>
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
        case EditorAction::Id::ToggleGridSnap:
        {
            return "ToggleGridSnap";
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
        case EditorAction::Id::StepChartCaret:
        {
            return "StepChartCaret";
        }
        case EditorAction::Id::JumpChartCaret:
        {
            return "JumpChartCaret";
        }
        case EditorAction::Id::ExtendTimeSelection:
        {
            return "ExtendTimeSelection";
        }
        case EditorAction::Id::MoveSelection:
        {
            return "MoveSelection";
        }
        case EditorAction::Id::DeleteSelection:
        {
            return "DeleteSelection";
        }
        case EditorAction::Id::InsertAtCaret:
        {
            return "InsertAtCaret";
        }
        case EditorAction::Id::TypeChartFretDigit:
        {
            return "TypeChartFretDigit";
        }
        case EditorAction::Id::ShiftChartFrets:
        {
            return "ShiftChartFrets";
        }
        case EditorAction::Id::AdjustChartSustain:
        {
            return "AdjustChartSustain";
        }
        case EditorAction::Id::ToggleChartTechnique:
        {
            return "ToggleChartTechnique";
        }
        case EditorAction::Id::SetChartLeftTap:
        {
            return "SetChartLeftTap";
        }
        case EditorAction::Id::ToggleChartHoldMarker:
        {
            return "ToggleChartHoldMarker";
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
            case EditorAction::Id::CreateToneRegion:
            case EditorAction::Id::DeleteToneRegion:
            case EditorAction::Id::RenameTone:
            case EditorAction::Id::MoveToneBoundary:
            case EditorAction::Id::CreateNewTone:
            case EditorAction::Id::SetToneAutomationPoints:
            case EditorAction::Id::SelectArrangement:
            // Tone-document actions replace or persist the signal
            // chain the calibration prompt owns.
            case EditorAction::Id::NewToneDocument:
            case EditorAction::Id::OpenToneFile:
            case EditorAction::Id::SaveToneFile:
            case EditorAction::Id::SaveToneFileAs:
            case EditorAction::Id::ImportToneFile:
            case EditorAction::Id::ExportToneFile:
            case EditorAction::Id::ResolveToneImportPrompt:
            case EditorAction::Id::StepChartCaret:
            case EditorAction::Id::JumpChartCaret:
            case EditorAction::Id::ExtendTimeSelection:
            case EditorAction::Id::MoveSelection:
            case EditorAction::Id::DeleteSelection:
            case EditorAction::Id::InsertAtCaret:
            case EditorAction::Id::TypeChartFretDigit:
            case EditorAction::Id::ShiftChartFrets:
            case EditorAction::Id::AdjustChartSustain:
            case EditorAction::Id::ToggleChartTechnique:
            case EditorAction::Id::SetChartLeftTap:
            case EditorAction::Id::ToggleChartHoldMarker:
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
            case EditorAction::Id::ToggleGridSnap:
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
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::SelectArrangement:
        case EditorAction::Id::SelectToneRegion:
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
        case EditorAction::Id::StepChartCaret:
        case EditorAction::Id::JumpChartCaret:
        case EditorAction::Id::ExtendTimeSelection:
        {
            return conditions.has_chart ? "transport-playing" : "no-chart";
        }
        case EditorAction::Id::MoveSelection:
        case EditorAction::Id::DeleteSelection:
        {
            return "no-loaded-arrangement";
        }
        case EditorAction::Id::InsertAtCaret:
        {
            return conditions.has_loaded_arrangement ? "no-armed-caret" : "no-loaded-arrangement";
        }
        case EditorAction::Id::TypeChartFretDigit:
        {
            return "no-chart";
        }
        case EditorAction::Id::ShiftChartFrets:
        case EditorAction::Id::AdjustChartSustain:
        case EditorAction::Id::ToggleChartTechnique:
        case EditorAction::Id::SetChartLeftTap:
        {
            return conditions.has_chart ? "no-chart-selection" : "no-chart";
        }
        case EditorAction::Id::ToggleChartHoldMarker:
        {
            return conditions.has_chart ? "no-armed-caret" : "no-chart";
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
        case EditorUndoEventType::EntryDropped:
        {
            return "entry_dropped";
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
    if (common::core::hasExtensionIgnoringCase(file, ".rock"))
    {
        RockSongImporter importer;
        const AudioNormalizationAnalyzer analyzer =
            makeReportingAudioNormalizationAnalyzer(report_progress);
        return project.import(file, importer, {}, analyzer);
    }
    if (common::core::hasExtensionIgnoringCase(file, ".gp"))
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
    EditorController::AudioPorts audio_ports, const EditorController::Services& services,
    EditorController::ExitFunction exit_function)
    : EditorController(
          audio_ports, services, std::move(exit_function), EditorController::ProjectOperations{})
{}

EditorController::EditorController(
    EditorController::AudioPorts audio_ports, const EditorController::Services& services,
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

void EditorController::onGridSnapToggleRequested()
{
    m_impl->onGridSnapToggleRequested();
}

void EditorController::onGridSnapWarningDecision(GridSnapWarningDecision decision)
{
    m_impl->onGridSnapWarningDecision(decision);
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
    m_impl->runAction(EditorAction::StepChartCaret{.direction = direction, .measure = measure});
}

void EditorController::onChartCaretJumpRequested(ChartCaretJump target)
{
    m_impl->runAction(EditorAction::JumpChartCaret{.target = target});
}

void EditorController::onTimeSelectionExtendRequested(
    TimeSelectionExtent extent, ChartStepDirection direction)
{
    m_impl->runAction(EditorAction::ExtendTimeSelection{.extent = extent, .direction = direction});
}

void EditorController::onSelectionMoveRequested(ChartStepDirection direction)
{
    m_impl->runAction(EditorAction::MoveSelection{.direction = direction});
}

void EditorController::onSelectionDeleteRequested()
{
    m_impl->runAction(EditorAction::DeleteSelection{});
}

void EditorController::onChartFretDigitTyped(int digit)
{
    m_impl->runAction(EditorAction::TypeChartFretDigit{.digit = digit});
}

void EditorController::onChartFretShiftRequested(int direction)
{
    m_impl->runAction(EditorAction::ShiftChartFrets{.direction = direction});
}

void EditorController::onChartSustainAdjustRequested(int direction)
{
    m_impl->runAction(EditorAction::AdjustChartSustain{.direction = direction});
}

void EditorController::onChartTechniqueToggleRequested(const ChartTechnique technique)
{
    m_impl->runAction(EditorAction::ToggleChartTechnique{.technique = technique});
}

void EditorController::onChartLeftTapRequested()
{
    m_impl->runAction(EditorAction::SetChartLeftTap{});
}

void EditorController::onChartHoldMarkerToggleRequested()
{
    m_impl->runAction(EditorAction::ToggleChartHoldMarker{});
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

void EditorController::onToneAutomationPointsEditRequested(
    std::string instance_id, std::string param_id,
    std::vector<common::core::ToneAutomationPoint> points)
{
    m_impl->onToneAutomationPointsEditRequested(
        std::move(instance_id), std::move(param_id), std::move(points));
}

void EditorController::onToneAutomationPointSelectRequested(
    std::string instance_id, std::string param_id, common::core::GridPosition position)
{
    m_impl->onToneAutomationPointSelectRequested(
        std::move(instance_id), std::move(param_id), position);
}

void EditorController::onNeutralInsertRequested()
{
    m_impl->runAction(EditorAction::InsertAtCaret{});
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
    , m_message_thread_scheduler(services.message_thread_scheduler)
    , m_transport_listener(transport, *this)
{
    // Resolve the fret-entry coalescing clock: an injected source (tests) or the real wall clock.
    // Core stays off the wall clock behind an injectable seam (Time Must Be a Dependency).
    m_now_milliseconds =
        services.now_milliseconds
            ? std::move(services.now_milliseconds)
            : std::function<std::uint32_t()>{[] { return juce::Time::getMillisecondCounter(); }};

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

// Routes the snap toggle as an action so it gates like the other timeline intents.
void EditorController::Impl::onGridSnapToggleRequested()
{
    runAction(EditorAction::ToggleGridSnap{});
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
        // The uniform settle prologue: every gated action settles the pending fret entry first
        // — commit if valid, discard if invalid — so no action ever runs against a half-typed
        // value. Deliberately BEFORE the availability gate: Undo on a valid pending value must
        // commit it and then undo it (the ruled behavior), which requires the commit to land
        // before undo availability is judged. Digits are refused while busy, so no entry can
        // exist on the busy branch. The one exemption is the digit itself, which EXTENDS the
        // entry rather than settling it; this is the whole site list, so no verb can miss it.
        if (!std::holds_alternative<EditorAction::TypeChartFretDigit>(action))
        {
            settleChartFretEntry();
        }
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
    // Undo/redo are deliberately not settle events, but they ARE context switches: the burst is
    // over, so no coalescing window may reach across one. Stated here rather than left to the
    // position proof, which an undo followed by a redo restores.
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    const EditorUndoBeginResult begin = m_undo_history.beginUndo();
    logEditorUndoTransitionResult("undo.begin", begin.result);
    dispatchUndoTransition(begin);
}

// Begins the next redo transition and dispatches it synchronously or behind the loading fence.
void EditorController::Impl::performActionImpl(EditorAction::Redo /*action*/)
{
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
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

        // The token guard above only catches a token CHANGE. A same-token reset could clear the
        // pending transition during the presentation yield, and applying a transition the history
        // no longer holds would work a released edit — so re-check the history itself.
        if (!m_undo_history.hasPendingTransition())
        {
            updateView();
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
        .tone_plugin_bindings = m_tone_plugin_bindings,
        .output_gain_db = m_output_gain_db,
        .tone_designer = m_tone_designer,
    };
}

// Pushes one already-applied user edit into the product-level history stack.
bool EditorController::Impl::pushUndoEntry(std::unique_ptr<IEdit> edit)
{
    const bool had_edit = edit != nullptr;
    const EditorUndoTransitionResult result = m_undo_history.push(std::move(edit));
    logEditorUndoTransitionResult("undo.push", result);
    if (had_edit && result.status != EditorUndoTransitionStatus::Applied)
    {
        markUntrackedUnsavedEdit("undo.reset.failed_push");
    }
    return result.status == EditorUndoTransitionStatus::Applied;
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
    // Every entry the coalescing windows name is gone with the stack.
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    const EditorUndoTransitionResult result = m_undo_history.reset();
    logEditorUndoTransitionResult(context, result);
}

// Marks the current undo position as matching the saved project state.
void EditorController::Impl::markUndoHistoryClean(std::string_view context)
{
    const EditorUndoTransitionResult result = m_undo_history.markClean();
    logEditorUndoTransitionResult(context, result);
}

// Marks clean only when the history still sits where a write's content was captured. `markClean`
// can only mark NOW, and a write runs on a worker with the message thread live, so an entry pushed
// in between (a plugin's own window sits outside the busy overlay, and its dirty tracker settles on
// a timer) would be declared saved though the write never saw it — and the app would then close
// without a prompt. Leaving the project dirty is the honest answer, and it is also right when the
// history moved by an UNDO rather than a push, since that state was not written either.
void EditorController::Impl::markUndoHistoryCleanIfUnmoved(
    const std::size_t undo_depth_at_capture, const std::string_view context)
{
    if (m_undo_history.undoDepth() != undo_depth_at_capture)
    {
        return;
    }
    markUndoHistoryClean(context);
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
        // Starting playback is a settle event: authoring is over for now, so a claim the last burst
        // broke stops being transient before it can be heard as something it is not. The
        // clearSelection above usually settles it already; stated here so a selection that was
        // already empty still settles.
        static_cast<void>(settleChartLegato());
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
    // Moving the transport away is leaving the place being edited, so it settles too — after the
    // seek's own state changes, like every shared settle event.
    static_cast<void>(settleChartLegato());
    // The active tone follows the cursor: the region under the new position becomes the tone
    // context, and any formal selection is cleared so a stray Delete cannot remove a tone.
    activateToneAtCursor();
    updateView();
}

// Applies a validated grid note-value change, persists it as app-local per-project state, and
// republishes view state so the grid, ruler, and snapping move together.
void EditorController::Impl::performActionImpl(EditorAction::SetGridNoteValue action)
{
    // The SELECTABLE gate, not the lattice one: the grid box is free text, and this is the only
    // thing standing between a typed 1/3840 and a drawn grid the line walk cannot afford.
    if (!isSelectableTempoGridNoteValue(action.note_value) ||
        action.note_value == m_grid_note_value)
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

// Flips grid snap and republishes, so the placement quantum, the lane's grid ink, and the grid
// readout all move together off the one fact. Nothing is written anywhere: snap is session-only by
// design, and resetGridSession puts it back on at every project boundary — the mode is meant to be
// entered deliberately and left behind, not inherited.
//
// Turning snapping OFF is asked about first: the toggle raises the warning prompt instead of
// flipping, because Ctrl+G is one key away from a mode almost nobody wants and an accidental press
// otherwise lands the user in it with nothing but quieted grid ink to say so. The warning is
// unconditional and unsuppressable, which is why this handler needs no stored flag to consult.
void EditorController::Impl::performActionImpl(EditorAction::ToggleGridSnap /*action*/)
{
    if (m_grid_snap)
    {
        m_grid_snap_warning_prompt = true;
        updateView();
        return;
    }

    m_grid_snap = true;
    updateView();
}

// Applies the user's answer to that warning: the only path that can turn snapping off. Every
// non-confirming close resolves to KeepSnappingOn at the dialog, so this handler simply clears the
// prompt and moves the switch when — and only when — the user chose the off path.
void EditorController::Impl::onGridSnapWarningDecision(GridSnapWarningDecision decision)
{
    m_grid_snap_warning_prompt = false;
    if (decision == GridSnapWarningDecision::TurnSnappingOff)
    {
        m_grid_snap = false;
    }
    updateView();
}

// The controller's read of the one quantum authority. Every position-quantizing verb across the
// chart, tone, and timeline slices funnels here, which is what makes "one quantum, no per-verb
// opt-outs" structural rather than a convention each handler has to remember.
common::core::Fraction EditorController::Impl::placementQuantum() const noexcept
{
    return placementQuantumNoteValue(m_grid_note_value, m_grid_snap);
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
        .has_chart = hasLoadedChart(),
        .transport_playing = transport_state.playing,
        .has_chart_selection = !chartSelection().empty(),
        .has_armed_caret = armedChartCaret() != nullptr,
    };
}

// Answers the "is there a chart to edit" question the chart verbs' availability reads.
bool EditorController::Impl::hasLoadedChart() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    return arrangement != nullptr && arrangement->chart.has_value();
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

    // Sized from the Part enumeration, not a literal count: a new part would otherwise index past
    // the end of both tallies.
    std::vector<int> part_totals(common::core::g_parts.size(), 0);
    for (const common::core::Arrangement& arrangement : arrangements)
    {
        part_totals[static_cast<std::size_t>(arrangement.part)] += 1;
    }

    std::vector<ArrangementChoiceViewState> choices;
    choices.reserve(arrangements.size());
    std::vector<int> part_counts(common::core::g_parts.size(), 0);
    for (const std::size_t index : display_order)
    {
        const common::core::Arrangement& arrangement = arrangements[index];
        const auto part_index = static_cast<std::size_t>(arrangement.part);
        part_counts[part_index] += 1;
        // The persisted token doubles as the display label: the switcher shows exactly the part
        // vocabulary the song document speaks, so there is no second table to keep in step.
        std::string label{common::core::partToken(arrangement.part)};
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
    state.grid_snap = m_grid_snap;
    state.grid_snap_warning_prompt = m_grid_snap_warning_prompt;
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
        .output_gain = common::audio::Gain{m_output_gain_db},
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
        // the ghost — located by (instance, parameter) — and is published only once the gesture
        // holds a live edit (it moved, or it is the Alt insert that authored on its press), so a
        // plain point click that only selects, and an anchor press that authors nothing, publish
        // none. The view paints it in place of the moved point.
        if (m_tone_automation_drag.has_value() && m_tone_automation_drag->hasLiveEdit())
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
                    .is_new_point = drag.createsPoint(),
                    .source_point_index = drag.point_index,
                };
                break;
            }
        }

        // The chart projection resolves thousands of positions to seconds, so it is memoized per
        // displayed arrangement and chart revision: the arrangement id keys which chart is shown,
        // and the session's chart revision (bumped by every mutable chart acquisition) keys its
        // edit state, so chart edits invalidate without any explicit notification path. The 3D
        // highway projection rides the same rule (plan 44): one shared scene-model snapshot per
        // displayed arrangement, consumed by the preview window exactly as the game consumes it.
        const bool arrangement_changed = m_tab_arrangement_id != arrangement->id ||
                                         m_tab_chart_revision != session().chartRevision();
        if (arrangement_changed)
        {
            m_tab_view_state = std::make_shared<const common::core::ChartViewState>(
                common::core::makeChartViewState(*arrangement, state.tempo_map));
            // The lane's actual-ring reveal draws the SAME chart with every note at its stored
            // ring, so it needs a whole second projection rather than a swapped end: the
            // presented state has already dropped the payload points its trims clipped, and no
            // view-side transform can put those back.
            //
            // Built eagerly, under the same key. Building it only while the reveal is on would
            // mean this derivation knew the reveal is on, and the reveal is a fact about which
            // key is physically down in one window (tab_view.h: "the controller never learns of
            // it"). The cost is real and accepted: a sustain gesture bumps the chart revision on
            // every wheel notch, and each notch already projected the chart twice — here and
            // again inside the highway projection below — so this makes three. The shape that
            // removes it is one producer returning both forms from a single chartResolutions
            // pass, which would also make the two forms' index alignment — which the lane's
            // per-note pick reads on the paint path — structural rather than asserted. Tracked
            // in docs/tracking/watch-items.md; unbuilt.
            m_tab_actual_view_state = std::make_shared<const common::core::ChartViewState>(
                common::core::makeChartViewState(
                    *arrangement, state.tempo_map, common::core::ChartNoteForm::Actual));
        }
        // The highway state carries the display options the renderer applies per frame (the
        // displayed-string minimum among them — the scene itself is never padded), so it is
        // republished on an arrangement change OR a minimum change. Lowest-pitched string on top
        // is the 3D default (recorded in plan 25).
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
        state.tab_actual = m_tab_actual_view_state;
        state.highway = m_highway_view_state;

        // Chart-editing overlays resolve against exactly the projection instance pushed above:
        // selection keys re-resolve to indices every push, so keys whose notes vanished simply
        // drop out instead of pointing at the wrong glyph.
        if (arrangement->chart.has_value())
        {
            state.chart_edit.selected_notes =
                selectedNoteIndices(arrangement->chart->notes, chartSelection());
            state.chart_edit.selected_hold_markers =
                selectedHoldMarkerIndices(arrangement->chart->hold_markers, chartSelection());
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
            // The pending fret entry: a retype's box rides the affected heads (indices into the
            // same projection instance the selection resolves against), an insert entry carries
            // its slot, where no head exists yet. Text and validity publish from the entry's own
            // plan — NoChange stays valid, because a no-op is not a refusal and must not read
            // red.
            if (m_chart_fret_entry.has_value())
            {
                ChartPendingFretViewState pending;
                pending.text = std::to_string(m_chart_fret_entry->value);
                pending.valid = m_chart_fret_entry->plan.has_value() ||
                                m_chart_fret_entry->plan.error() != ChartPlanRefusal::Invalid;
                if (const auto* const insert =
                        std::get_if<Impl::ChartFretEntry::InsertAt>(&m_chart_fret_entry->target))
                {
                    pending.at = ChartSlotViewState{
                        .seconds =
                            caretTimeBounds(session().song().tempo_map, insert->slot.position)
                                .seconds,
                        .string = insert->slot.string,
                    };
                }
                else
                {
                    pending.at = slotIndicesForKeys(
                        arrangement->chart->notes,
                        std::get<Impl::ChartFretEntry::Retype>(m_chart_fret_entry->target).keys);
                }
                state.chart_edit.pending_fret = std::move(pending);
            }
        }
    }
    else
    {
        m_tab_view_state.reset();
        m_tab_actual_view_state.reset();
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

    // The grid-locked time selection resolves both endpoints to seconds so the full-canvas overlay
    // maps them to pixels exactly as it maps the cursor. Grid positions -> seconds through the same
    // helper the caret display uses.
    if (const TimeSelection* const range = selectedTimeSelection())
    {
        const common::core::TempoMap& tempo_map = session().song().tempo_map;
        state.time_selection = common::core::TimeRange{
            .start = common::core::TimePosition{secondsAtGridPosition(tempo_map, range->start())},
            .end = common::core::TimePosition{secondsAtGridPosition(tempo_map, range->end())},
        };
    }

    // Derived from the PUBLISHED per-surface states plus the time span, not the raw variant: a
    // stale selection (one whose object vanished) publishes nothing, and Delete must keep
    // propagating then.
    state.selection_present =
        !state.chart_edit.selected_notes.empty() ||
        !state.chart_edit.selected_hold_markers.empty() ||
        state.tone_automation.selected_point.has_value() || state.time_selection.has_value() ||
        std::ranges::any_of(state.tone_track.regions, [](const ToneRegionViewState& region) {
            return region.selected;
        });

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

// Shows a one-shot notice when a view is attached; news with no view to tell is simply dropped,
// exactly as an error is.
void EditorController::Impl::reportNotice(const std::string& title, const std::string& message)
{
    if (m_view != nullptr)
    {
        m_view->showNotice(title, message);
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
//
// "A project is open" is asked of `m_project_file` and the write in flight, NOT of `m_project`,
// which a write legitimately empties for its whole duration: a project is moved out to the worker
// so background IO never shares mutable ownership with message-thread actions. Reading openness
// from that optional made this return false for the entire write — and close and exit both
// SUPERSEDE busy, so a close during a publish (which deliberately leaves the project dirty) skipped
// the unsaved-changes prompt entirely and dropped the edits with no warning.
bool EditorController::Impl::hasUnsavedChanges() const noexcept
{
    const bool project_open = m_project.has_value() || m_project_write_in_flight;
    return project_open && (m_has_untracked_unsaved_changes || m_undo_history.hasUnsavedEdits() ||
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
