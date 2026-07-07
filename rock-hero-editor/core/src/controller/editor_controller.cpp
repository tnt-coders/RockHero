#include "controller/editor_controller.h"

#include "audio_device/audio_device_status_text.h"
#include "busy/busy_operation_workflow.h"
#include "deferred_project_action_state.h"
#include "editor_action.h"
#include "editor_action_availability.h"
#include "editor_controller_impl.h"
#include "editor_undo_history.h"
#include "input_calibration/input_calibration_workflow.h"
#include "project/gp_song_importer.h"
#include "project/project_io.h"
#include "project/rock_song_importer.h"
#include "shared/editor_controller_logging.h"
#include "signal_chain/plugin_catalog_workflow.h"
#include "signal_chain/signal_chain_edits.h"
#include "signal_chain/signal_chain_workflow.h"
#include "tab/tab_projection.h"
#include "tone/tone_automation_projection.h"
#include "tone/tone_track_projection.h"

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
#include <numeric>
#include <optional>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/shared/scoped_listener.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/common/core/shared/logger.h>
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
              audio_ports.plugin_host, audio_ports.live_rig, audio_ports.tone_automation,
              audio_ports.live_input, services, std::move(exit_function),
              std::move(project_operations)))
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

void EditorController::onToneRegionSelected(std::string region_id)
{
    m_impl->onToneRegionSelected(std::move(region_id));
}

void EditorController::onToneRegionResizeRequested(
    std::string region_id, common::core::ToneGridPosition start, common::core::ToneGridPosition end)
{
    m_impl->onToneRegionResizeRequested(std::move(region_id), start, end);
}

void EditorController::onToneRegionCreateRequested(
    common::core::ToneGridPosition position, std::string new_region_id,
    std::string tone_document_ref)
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
    std::string right_region_id, common::core::ToneGridPosition position)
{
    m_impl->onToneBoundaryMoveRequested(std::move(right_region_id), position);
}

void EditorController::onToneCreateNewRequested(
    common::core::ToneGridPosition position, std::string name)
{
    m_impl->onToneCreateNewRequested(position, std::move(name));
}

void EditorController::onToneAutomationLaneAddRequested(
    std::string instance_id, std::string param_id)
{
    m_impl->onToneAutomationLaneAddRequested(std::move(instance_id), std::move(param_id));
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
    common::audio::IToneAutomation& tone_automation, common::audio::ILiveInput& live_input,
    EditorController::Services services, EditorController::ExitFunction exit_function,
    EditorController::ProjectOperations project_operations)
    : m_transport(transport)
    , m_song_audio(song_audio)
    , m_audio_devices(audio_devices)
    , m_plugin_host(plugin_host)
    , m_live_rig(live_rig)
    , m_tone_automation(tone_automation)
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
    restoreAudioDeviceState();
    m_waveform_visible = m_settings.waveformVisible().value_or(true);
    m_tab_minimum_displayed_strings = std::clamp(
        m_settings.tabMinimumDisplayedStrings().value_or(0), 0, common::core::g_max_chart_strings);
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
    if (!std::isfinite(pixels_per_second) || pixels_per_second <= 0.0 ||
        pixels_per_second == m_timeline_zoom_pixels_per_second)
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
        // Starting playback snaps the tone selection to the region under the cursor; the
        // tone row then keeps it following boundary crossings at render cadence.
        applyToneSelection(toneRegionIdAt(m_transport.position()));
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
    applyToneSelection(toneRegionIdAt(common::core::TimePosition{}));

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
    // Selection follows the cursor: the region under the new position becomes the tone
    // context.
    applyToneSelection(toneRegionIdAt(position));
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
        // A pending plugin edit is flushed into a real undo entry at the action gate, so undo is
        // offered for it too.
        .undo_available = m_undo_history.canUndo() || m_plugin_host.hasPendingPluginEdits(),
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
    std::vector<std::size_t> display_order(arrangements.size());
    std::ranges::iota(display_order, std::size_t{0});
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

EditorViewState EditorController::Impl::deriveViewState() const
{
    const common::audio::TransportState transport_state = m_transport.state();
    const common::core::TimeRange timeline_range = session().timeline();
    const InputCalibrationWorkflow::Snapshot input_calibration =
        m_input_calibration.snapshot(inputCalibrationContext());
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
    state.audio_devices_available = true;
    state.audio_device_settings_enabled = input_calibration.audio_device_settings_enabled;
    state.audio_device_status_text = audioDeviceStatusText(m_audio_devices.currentDeviceStatus());
    state.visible_timeline = timeline_range;
    state.tempo_map = session().song().tempo_map;
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
            .choices = arrangementChoicesFor(session().arrangements(), arrangement->id),
        };
        state.tone_track =
            toneTrackViewStateFor(*arrangement, state.tempo_map, m_selected_tone_region_id);
        std::unordered_map<std::string, ToneAutomationBinding> automation_bindings;
        automation_bindings.reserve(m_tone_plugin_identities.size());
        for (const auto& [instance_id, identity] : m_tone_plugin_identities)
        {
            automation_bindings.emplace(
                identity.plugin_id,
                ToneAutomationBinding{
                    .instance_id = instance_id,
                    .tone_document_ref = identity.tone_document_ref,
                });
        }
        state.tone_automation = toneAutomationViewStateFor(
            *arrangement,
            state.tempo_map,
            selectedToneDocumentRef(),
            automation_bindings,
            m_tone_automation);

        // The tab projection resolves thousands of positions to seconds, so it is memoized per
        // displayed arrangement. Charts and the tempo map are immutable while a project is open,
        // which makes the arrangement id a sufficient cache key.
        if (m_tab_arrangement_id != arrangement->id)
        {
            m_tab_view_state = std::make_shared<const TabViewState>(
                tabViewStateFor(*arrangement, state.tempo_map));
            m_tab_arrangement_id = arrangement->id;
        }
        state.tab = m_tab_view_state;
    }
    else
    {
        m_tab_view_state.reset();
        m_tab_arrangement_id.clear();
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

void EditorController::Impl::recordSettingsResultBestEffort(
    std::expected<void, EditorSettingsError> result, std::string_view context)
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
    return arrangement != nullptr && !arrangement->tone_document_ref.empty();
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
