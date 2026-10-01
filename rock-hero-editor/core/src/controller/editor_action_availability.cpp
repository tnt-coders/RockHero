#include "editor_action_availability.h"

#include <initializer_list>

namespace rock_hero::editor::core
{

namespace
{

using Reason = ActionUnavailableReason;
using Verdict = std::optional<ActionUnavailableReason>;

// One condition of an action: absent when it holds, else the reason it names.
[[nodiscard]] constexpr Verdict require(bool satisfied, Reason reason) noexcept
{
    return satisfied ? std::nullopt : Verdict{reason};
}

// An action's conditions in reason-priority order: the first that fails is the reason.
[[nodiscard]] constexpr Verdict firstFailure(std::initializer_list<Verdict> checks) noexcept
{
    for (const Verdict& check : checks)
    {
        if (check.has_value())
        {
            return check;
        }
    }
    return std::nullopt;
}

// Names the action set hidden while the input calibration prompt owns the signal-chain flow.
[[nodiscard]] bool actionBlockedByInputCalibrationPrompt(EditorAction::Id action) noexcept
{
    switch (action)
    {
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
        case EditorAction::Id::Undo:
        case EditorAction::Id::Redo:
        case EditorAction::Id::CreateToneRegion:
        case EditorAction::Id::DeleteToneRegion:
        case EditorAction::Id::RenameTone:
        case EditorAction::Id::SetToneRegionTone:
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
        // The chart verbs edit the project the calibration prompt is parked over.
        case EditorAction::Id::StepChartCaret:
        case EditorAction::Id::StepToRowObject:
        case EditorAction::Id::JumpToFocusRow:
        case EditorAction::Id::JumpChartCaret:
        case EditorAction::Id::ExtendTimeSelection:
        case EditorAction::Id::MoveSelection:
        case EditorAction::Id::DeleteSelection:
        case EditorAction::Id::InsertAtCaret:
        case EditorAction::Id::InsertRingPoint:
        case EditorAction::Id::TypeChartFretDigit:
        case EditorAction::Id::ShiftChartFrets:
        case EditorAction::Id::AdjustChartSustain:
        case EditorAction::Id::ToggleChartTechnique:
        case EditorAction::Id::ChooseChartHarmonic:
        case EditorAction::Id::SetChartHarmonicNode:
        case EditorAction::Id::ChooseChartBend:
        case EditorAction::Id::SetChartBend:
        case EditorAction::Id::ToggleChartJunction:
        // The section and hand-marker verbs edit the project the calibration prompt is parked over.
        case EditorAction::Id::InsertSongSection:
        case EditorAction::Id::RenameSongSection:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ClearFretHandEnd:
        {
            return true;
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::ExportSong:
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
        // Selecting mutates nothing, so it stays reachable like the tone-region selection.
        case EditorAction::Id::SelectSongSection:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        case EditorAction::Id::SelectFretHandPosition:
        {
            return false;
        }
    }

    return false;
}

// Applies idle-state action availability without querying controller-owned state.
[[nodiscard]] Verdict whyUnavailableWhenIdle(
    EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    if (conditions.session_faulted)
    {
        switch (action)
        {
            case EditorAction::Id::OpenProject:
            case EditorAction::Id::RestoreProject:
            case EditorAction::Id::ImportSong:
            case EditorAction::Id::CloseProject:
            case EditorAction::Id::ExitApplication:
            {
                return std::nullopt;
            }
            case EditorAction::Id::ResolveUnsavedChangesPrompt:
            {
                return require(
                    conditions.has_unsaved_changes_prompt, Reason::NoUnsavedChangesPrompt);
            }
            case EditorAction::Id::CancelSaveAsPrompt:
            {
                return require(conditions.has_save_as_prompt, Reason::NoSaveAsPrompt);
            }
            case EditorAction::Id::SaveProject:
            case EditorAction::Id::SaveProjectAs:
            case EditorAction::Id::ExportSong:
            case EditorAction::Id::CancelBusyOperation:
            case EditorAction::Id::Undo:
            case EditorAction::Id::Redo:
            case EditorAction::Id::PlayPause:
            case EditorAction::Id::Stop:
            case EditorAction::Id::SeekTimeline:
            case EditorAction::Id::SetGridNoteValue:
            case EditorAction::Id::ToggleGridSnap:
            case EditorAction::Id::SelectArrangement:
            case EditorAction::Id::SelectToneRegion:
            case EditorAction::Id::CreateToneRegion:
            case EditorAction::Id::DeleteToneRegion:
            case EditorAction::Id::RenameTone:
            case EditorAction::Id::SetToneRegionTone:
            case EditorAction::Id::MoveToneBoundary:
            case EditorAction::Id::CreateNewTone:
            case EditorAction::Id::SetToneAutomationPoints:
            case EditorAction::Id::ShowPluginBrowser:
            case EditorAction::Id::BeginPluginInsert:
            case EditorAction::Id::ScanPluginCatalog:
            case EditorAction::Id::InsertSelectedPlugin:
            case EditorAction::Id::RemovePlugin:
            case EditorAction::Id::MovePlugin:
            case EditorAction::Id::SetSignalChainPlacement:
            case EditorAction::Id::SetPluginDisplayTypeOverride:
            case EditorAction::Id::OpenPlugin:
            // A faulted session implies a project; the designer is never active alongside one.
            case EditorAction::Id::NewToneDocument:
            case EditorAction::Id::OpenToneFile:
            case EditorAction::Id::SaveToneFile:
            case EditorAction::Id::SaveToneFileAs:
            // Tone-file import/export mutate or read the untrusted live chain.
            case EditorAction::Id::ImportToneFile:
            case EditorAction::Id::ExportToneFile:
            case EditorAction::Id::ResolveToneImportPrompt:
            case EditorAction::Id::StepChartCaret:
            case EditorAction::Id::StepToRowObject:
            case EditorAction::Id::JumpToFocusRow:
            case EditorAction::Id::JumpChartCaret:
            case EditorAction::Id::ExtendTimeSelection:
            case EditorAction::Id::MoveSelection:
            case EditorAction::Id::DeleteSelection:
            case EditorAction::Id::InsertAtCaret:
            case EditorAction::Id::InsertRingPoint:
            case EditorAction::Id::TypeChartFretDigit:
            case EditorAction::Id::ShiftChartFrets:
            case EditorAction::Id::AdjustChartSustain:
            case EditorAction::Id::ToggleChartTechnique:
            case EditorAction::Id::ChooseChartHarmonic:
            case EditorAction::Id::SetChartHarmonicNode:
            case EditorAction::Id::ChooseChartBend:
            case EditorAction::Id::SetChartBend:
            case EditorAction::Id::ToggleChartJunction:
            case EditorAction::Id::SelectSongSection:
            case EditorAction::Id::InsertSongSection:
            case EditorAction::Id::RenameSongSection:
            case EditorAction::Id::SelectTempoAnchor:
            case EditorAction::Id::SelectTimeSignature:
            case EditorAction::Id::SelectFretHandPosition:
            case EditorAction::Id::AuthorFretHandPositionAtCursor:
            case EditorAction::Id::ClearFretHandEnd:
            {
                return Reason::SessionFaulted;
            }
        }

        return Reason::SessionFaulted;
    }

    if (conditions.input_calibration_prompt_visible &&
        actionBlockedByInputCalibrationPrompt(action))
    {
        return Reason::InputCalibrationPrompt;
    }

    // Signal-chain verbs need a live chain: a loaded arrangement or the Tone Designer's.
    const bool live_chain = conditions.has_loaded_arrangement || conditions.tone_designer_active;

    switch (action)
    {
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::ExitApplication:
        {
            return std::nullopt;
        }
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::ExportSong:
        case EditorAction::Id::CloseProject:
        {
            return require(conditions.has_project, Reason::NoProject);
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            return require(conditions.has_unsaved_changes_prompt, Reason::NoUnsavedChangesPrompt);
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            return require(conditions.has_save_as_prompt, Reason::NoSaveAsPrompt);
        }
        case EditorAction::Id::CancelBusyOperation:
        {
            return Reason::NotBusy;
        }
        case EditorAction::Id::Undo:
        {
            // The designer session owns its own history, so undo works there like in a project.
            return firstFailure({
                require(
                    conditions.has_project || conditions.tone_designer_active, Reason::NoProject),
                require(conditions.undo_available, Reason::HistoryUnavailable),
            });
        }
        case EditorAction::Id::Redo:
        {
            return firstFailure({
                require(
                    conditions.has_project || conditions.tone_designer_active, Reason::NoProject),
                require(conditions.redo_available, Reason::HistoryUnavailable),
            });
        }
        // Play needs an open device, since only its callback moves the playhead; seeking, the grid
        // and the arrangement verbs do not, so they keep working without one.
        case EditorAction::Id::PlayPause:
        {
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(conditions.audio_device_open, Reason::AudioDeviceClosed),
            });
        }
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::SelectArrangement:
        {
            return require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement);
        }
        // THE MARKER PLANE IS PAUSED-ONLY, selection included: while the transport plays the
        // PLAYHEAD owns the timeline, so a marker the charter picked or moved would be a second
        // opinion about where the song is — and Play clears the selection rather than carrying one
        // through. The editor-wide move and delete ride here because every operand they dispatch on
        // is a marker, a lane point, or a chart selection play already cleared.
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::CreateToneRegion:
        case EditorAction::Id::DeleteToneRegion:
        // A tone rename names a catalog document rather than a marker, but it is reached only from
        // the tone row, so it closes with the row: one rule for everything the marker rows offer.
        case EditorAction::Id::RenameTone:
        case EditorAction::Id::SetToneRegionTone:
        case EditorAction::Id::MoveToneBoundary:
        case EditorAction::Id::CreateNewTone:
        case EditorAction::Id::SetToneAutomationPoints:
        case EditorAction::Id::MoveSelection:
        case EditorAction::Id::DeleteSelection:
        {
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(!conditions.transport_playing, Reason::TransportPlaying),
            });
        }
        case EditorAction::Id::Stop:
        {
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(conditions.can_stop_transport, Reason::TransportAtStart),
            });
        }
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(
                    conditions.live_input_audition_available, Reason::LiveInputAuditionUnavailable),
                require(conditions.has_plugin_insert_capacity, Reason::PluginChainFull),
            });
        }
        case EditorAction::Id::ScanPluginCatalog:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(
                    conditions.live_input_audition_available, Reason::LiveInputAuditionUnavailable),
            });
        }
        case EditorAction::Id::InsertSelectedPlugin:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(
                    conditions.live_input_audition_available, Reason::LiveInputAuditionUnavailable),
                require(conditions.has_plugin_candidates, Reason::NoPluginCandidates),
                require(conditions.has_plugin_insert_capacity, Reason::PluginChainFull),
            });
        }
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(
                    conditions.live_input_audition_available, Reason::LiveInputAuditionUnavailable),
                require(conditions.has_loaded_plugins, Reason::NoLoadedPlugins),
            });
        }
        case EditorAction::Id::NewToneDocument:
        case EditorAction::Id::OpenToneFile:
        case EditorAction::Id::SaveToneFile:
        case EditorAction::Id::SaveToneFileAs:
        {
            return require(conditions.tone_designer_active, Reason::ToneDesignerInactive);
        }
        case EditorAction::Id::ImportToneFile:
        {
            // Import replaces the live monitored chain, so it shares the chain-mutation gates.
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(
                    conditions.live_input_audition_available, Reason::LiveInputAuditionUnavailable),
            });
        }
        case EditorAction::Id::ExportToneFile:
        {
            // Export is a pure read of the active tone's rig.
            return require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement);
        }
        case EditorAction::Id::ResolveToneImportPrompt:
        {
            return require(conditions.has_tone_import_prompt, Reason::NoToneImportPrompt);
        }
        // The caret moves are paused-only: arming requires a paused transport (armed implies
        // paused is structural), and play clears the chart selection.
        case EditorAction::Id::StepChartCaret:
        case EditorAction::Id::StepToRowObject:
        case EditorAction::Id::JumpToFocusRow:
        case EditorAction::Id::JumpChartCaret:
        case EditorAction::Id::ExtendTimeSelection:
        // The fret-hand positions are the chart's own, so their marker verbs need a chart; paused-
        // only like every other marker verb.
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ClearFretHandEnd:
        {
            return firstFailure({
                require(conditions.has_chart, Reason::NoChart),
                require(!conditions.transport_playing, Reason::TransportPlaying),
            });
        }
        // The armed caret is the gate; which ROW it rides is each verb's own question, since each
        // lane has its own thing to place. One condition rather than a second "armed on a lane"
        // flag: the verb already reads the caret it needs.
        // Paused-only with the rest of the marker plane, stated here rather than left to the armed
        // caret's own paused-only lifetime, so the table answers for every marker verb alike.
        case EditorAction::Id::InsertAtCaret:
        case EditorAction::Id::InsertRingPoint:
        {
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(!conditions.transport_playing, Reason::TransportPlaying),
                require(conditions.has_armed_caret, Reason::NoArmedCaret),
            });
        }
        // A digit inserts at an armed caret or retypes the selection; which, the verb decides. The
        // bend and technique verbs are the same shape — the selection, else what the armed caret's
        // slot holds, the ring on a covered one — and the bend's answer writes the anchors its
        // question named.
        case EditorAction::Id::TypeChartFretDigit:
        case EditorAction::Id::ChooseChartBend:
        case EditorAction::Id::SetChartBend:
        case EditorAction::Id::ToggleChartTechnique:
        {
            return require(conditions.has_chart, Reason::NoChart);
        }
        case EditorAction::Id::ShiftChartFrets:
        case EditorAction::Id::AdjustChartSustain:
        case EditorAction::Id::ChooseChartHarmonic:
        case EditorAction::Id::SetChartHarmonicNode:
        // The junction toggle is selection-scoped like the verbs beside it: its operand is a
        // selected keyframe or head, however that selection was made.
        case EditorAction::Id::ToggleChartJunction:
        {
            return firstFailure({
                require(conditions.has_chart, Reason::NoChart),
                require(conditions.has_chart_selection, Reason::NoChartSelection),
            });
        }
        // Sections are SONG-level, so they need a project rather than a loaded arrangement: the
        // list is the same under every tab and survives the arrangement switch. The tempo map is
        // song-level too, so its chips follow the same rule. Paused-only like every other marker
        // verb; only the base condition differs.
        case EditorAction::Id::SelectSongSection:
        case EditorAction::Id::InsertSongSection:
        case EditorAction::Id::RenameSongSection:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        {
            return firstFailure({
                require(conditions.has_project, Reason::NoProject),
                require(!conditions.transport_playing, Reason::TransportPlaying),
            });
        }
    }

    return std::nullopt;
}

} // namespace

// Encodes the actions that intentionally remain available while async work owns the editor.
bool actionSupersedesBusy(EditorAction::Id action) noexcept
{
    switch (action)
    {
        case EditorAction::Id::CloseProject:
        case EditorAction::Id::ExitApplication:
        {
            return true;
        }
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::ExportSong:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::CancelBusyOperation:
        case EditorAction::Id::Undo:
        case EditorAction::Id::Redo:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::SelectArrangement:
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::CreateToneRegion:
        case EditorAction::Id::DeleteToneRegion:
        case EditorAction::Id::RenameTone:
        case EditorAction::Id::SetToneRegionTone:
        case EditorAction::Id::MoveToneBoundary:
        case EditorAction::Id::CreateNewTone:
        case EditorAction::Id::SetToneAutomationPoints:
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        case EditorAction::Id::ScanPluginCatalog:
        case EditorAction::Id::InsertSelectedPlugin:
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        case EditorAction::Id::NewToneDocument:
        case EditorAction::Id::OpenToneFile:
        case EditorAction::Id::SaveToneFile:
        case EditorAction::Id::SaveToneFileAs:
        case EditorAction::Id::ImportToneFile:
        case EditorAction::Id::ExportToneFile:
        case EditorAction::Id::ResolveToneImportPrompt:
        case EditorAction::Id::StepChartCaret:
        case EditorAction::Id::StepToRowObject:
        case EditorAction::Id::JumpToFocusRow:
        case EditorAction::Id::JumpChartCaret:
        case EditorAction::Id::ExtendTimeSelection:
        case EditorAction::Id::MoveSelection:
        case EditorAction::Id::DeleteSelection:
        case EditorAction::Id::InsertAtCaret:
        case EditorAction::Id::InsertRingPoint:
        case EditorAction::Id::TypeChartFretDigit:
        case EditorAction::Id::ShiftChartFrets:
        case EditorAction::Id::AdjustChartSustain:
        case EditorAction::Id::ToggleChartTechnique:
        case EditorAction::Id::ChooseChartHarmonic:
        case EditorAction::Id::SetChartHarmonicNode:
        case EditorAction::Id::ChooseChartBend:
        case EditorAction::Id::SetChartBend:
        case EditorAction::Id::ToggleChartJunction:
        case EditorAction::Id::SelectSongSection:
        case EditorAction::Id::InsertSongSection:
        case EditorAction::Id::RenameSongSection:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ClearFretHandEnd:
        {
            return false;
        }
    }

    return false;
}

// Combines natural action availability with the action's busy-state policy.
std::optional<ActionUnavailableReason> whyUnavailable(
    EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    if (conditions.busy)
    {
        if (action == EditorAction::Id::CancelBusyOperation)
        {
            return require(conditions.busy_cancel_available, Reason::BusyCancelUnavailable);
        }

        return require(actionSupersedesBusy(action), Reason::Busy);
    }

    return whyUnavailableWhenIdle(action, conditions);
}

bool isActionAvailable(EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    return !whyUnavailable(action, conditions).has_value();
}

std::string_view actionUnavailableReasonTag(ActionUnavailableReason reason) noexcept
{
    switch (reason)
    {
        case Reason::Busy:
        {
            return "busy";
        }
        case Reason::BusyCancelUnavailable:
        {
            return "busy-cancel-unavailable";
        }
        case Reason::NotBusy:
        {
            return "not-busy";
        }
        case Reason::SessionFaulted:
        {
            return "session-faulted";
        }
        case Reason::InputCalibrationPrompt:
        {
            return "input-calibration-prompt";
        }
        case Reason::NoProject:
        {
            return "no-project";
        }
        case Reason::NoLoadedArrangement:
        {
            return "no-loaded-arrangement";
        }
        case Reason::NoChart:
        {
            return "no-chart";
        }
        case Reason::NoChartSelection:
        {
            return "no-chart-selection";
        }
        case Reason::NoArmedCaret:
        {
            return "no-armed-caret";
        }
        case Reason::NoUnsavedChangesPrompt:
        {
            return "no-unsaved-changes-prompt";
        }
        case Reason::NoSaveAsPrompt:
        {
            return "no-save-as-prompt";
        }
        case Reason::NoToneImportPrompt:
        {
            return "no-tone-import-prompt";
        }
        case Reason::HistoryUnavailable:
        {
            return "history-unavailable";
        }
        case Reason::TransportPlaying:
        {
            return "transport-playing";
        }
        case Reason::TransportAtStart:
        {
            return "transport-at-start";
        }
        case Reason::AudioDeviceClosed:
        {
            return "audio-device-closed";
        }
        case Reason::LiveInputAuditionUnavailable:
        {
            return "live-input-audition-unavailable";
        }
        case Reason::ToneDesignerInactive:
        {
            return "tone-designer-inactive";
        }
        case Reason::PluginChainFull:
        {
            return "plugin-chain-full";
        }
        case Reason::NoPluginCandidates:
        {
            return "no-plugin-candidates";
        }
        case Reason::NoLoadedPlugins:
        {
            return "no-loaded-plugins";
        }
    }

    return "unknown";
}

} // namespace rock_hero::editor::core
