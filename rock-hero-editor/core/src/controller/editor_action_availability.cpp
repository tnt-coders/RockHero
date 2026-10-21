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
        case EditorAction::Id::Stop:
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
        // The section, tempo, fret-hand position and hold verbs edit the project the calibration
        // prompt is parked over.
        case EditorAction::Id::InsertSongSection:
        case EditorAction::Id::RenameSongSection:
        case EditorAction::Id::InsertTempoAnchor:
        case EditorAction::Id::SetTempoSpanBpm:
        case EditorAction::Id::SetTimeSignature:
        case EditorAction::Id::MoveTempoAnchor:
        case EditorAction::Id::ReleaseTempoAnchorDrag:
        case EditorAction::Id::ScaleTempo:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ChooseHold:
        case EditorAction::Id::SetHold:
        case EditorAction::Id::ClearFretHandEnd:
        case EditorAction::Id::PruneFretHandPositions:
        // The marker verbs decided at the press ask for a prompt whose answer edits the project.
        case EditorAction::Id::SongSectionChord:
        case EditorAction::Id::SongSectionAt:
        case EditorAction::Id::TempoChord:
        case EditorAction::Id::TimeSignatureChord:
        case EditorAction::Id::TimeSignatureAt:
        case EditorAction::Id::ToneChord:
        case EditorAction::Id::ToneChangeAt:
        case EditorAction::Id::ToneRenamePrompt:
        case EditorAction::Id::ScaleTempoPrompt:
        case EditorAction::Id::RestateSelection:
        case EditorAction::Id::RenameSelection:
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
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::ToggleTempoLock:
        case EditorAction::Id::ToggleNoteLock:
        case EditorAction::Id::SelectToneRegion:
        // Selecting mutates nothing, so it stays reachable like the tone-region selection.
        case EditorAction::Id::SelectSongSection:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::SelectSpanRowMark:
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
            case EditorAction::Id::ToggleTempoLock:
            case EditorAction::Id::ToggleNoteLock:
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
            case EditorAction::Id::InsertTempoAnchor:
            case EditorAction::Id::SetTempoSpanBpm:
            case EditorAction::Id::SetTimeSignature:
            case EditorAction::Id::MoveTempoAnchor:
            case EditorAction::Id::ReleaseTempoAnchorDrag:
            case EditorAction::Id::ScaleTempo:
            case EditorAction::Id::SelectTempoAnchor:
            case EditorAction::Id::SelectTimeSignature:
            case EditorAction::Id::SelectFretHandPosition:
            case EditorAction::Id::SelectSpanRowMark:
            case EditorAction::Id::AuthorFretHandPositionAtCursor:
            case EditorAction::Id::SongSectionChord:
            case EditorAction::Id::SongSectionAt:
            case EditorAction::Id::TempoChord:
            case EditorAction::Id::TimeSignatureChord:
            case EditorAction::Id::TimeSignatureAt:
            case EditorAction::Id::ToneChord:
            case EditorAction::Id::ToneChangeAt:
            case EditorAction::Id::ToneRenamePrompt:
            case EditorAction::Id::ScaleTempoPrompt:
            case EditorAction::Id::RestateSelection:
            case EditorAction::Id::RenameSelection:
            case EditorAction::Id::ChooseHold:
            case EditorAction::Id::SetHold:
            case EditorAction::Id::ClearFretHandEnd:
            case EditorAction::Id::PruneFretHandPositions:
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

    // Signal-chain verbs need only a live chain -- a loaded arrangement or the Tone Designer's --
    // never live input: the chain is edited with or without hearing the guitar through it.
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
        // A device always runs -- the silent one without hardware -- so the transport needs only a
        // song. Stop shares Play's gate exactly: the pair reads as one transport, so it is never
        // half available.
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::ToggleTempoLock:
        case EditorAction::Id::ToggleNoteLock:
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
        // The tone row's verbs decided at the press, closing with the row they ask for.
        case EditorAction::Id::ToneChord:
        case EditorAction::Id::ToneChangeAt:
        case EditorAction::Id::ToneRenamePrompt:
        {
            return firstFailure({
                require(conditions.has_loaded_arrangement, Reason::NoLoadedArrangement),
                require(!conditions.transport_playing, Reason::TransportPlaying),
            });
        }
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(conditions.has_plugin_insert_capacity, Reason::PluginChainFull),
            });
        }
        case EditorAction::Id::ScanPluginCatalog:
        {
            return require(live_chain, Reason::NoLoadedArrangement);
        }
        case EditorAction::Id::InsertSelectedPlugin:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
                require(conditions.has_plugin_candidates, Reason::NoPluginCandidates),
                require(conditions.has_plugin_insert_capacity, Reason::PluginChainFull),
            });
        }
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        case EditorAction::Id::RemovePlugin:
        {
            return firstFailure({
                require(live_chain, Reason::NoLoadedArrangement),
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
        // Import replaces the project tone's chain and export reads it, so both need the song whose
        // tone it is.
        case EditorAction::Id::ImportToneFile:
        case EditorAction::Id::ExportToneFile:
        {
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
        // The fret-hand positions and the shape holds are the chart's own, so their marker verbs
        // need a chart; paused-only like every other marker verb.
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::SelectSpanRowMark:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ChooseHold:
        case EditorAction::Id::SetHold:
        case EditorAction::Id::ClearFretHandEnd:
        case EditorAction::Id::PruneFretHandPositions:
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
        // The duration keys move a selected span's or none hold's end too, so either selection
        // opens them; with neither, the refusal names the chart selection they mostly serve.
        case EditorAction::Id::AdjustChartSustain:
        {
            return firstFailure({
                require(conditions.has_chart, Reason::NoChart),
                require(
                    conditions.has_chart_selection || conditions.has_span_row_selection,
                    Reason::NoChartSelection),
            });
        }
        case EditorAction::Id::ShiftChartFrets:
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
        case EditorAction::Id::InsertTempoAnchor:
        case EditorAction::Id::SetTempoSpanBpm:
        case EditorAction::Id::SetTimeSignature:
        case EditorAction::Id::MoveTempoAnchor:
        case EditorAction::Id::ReleaseTempoAnchorDrag:
        case EditorAction::Id::ScaleTempo:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        // The ruler's verbs decided at the press, and the selection verbs, whose operand is a
        // marker play already cleared.
        case EditorAction::Id::SongSectionChord:
        case EditorAction::Id::SongSectionAt:
        case EditorAction::Id::TempoChord:
        case EditorAction::Id::TimeSignatureChord:
        case EditorAction::Id::TimeSignatureAt:
        case EditorAction::Id::ScaleTempoPrompt:
        case EditorAction::Id::RestateSelection:
        case EditorAction::Id::RenameSelection:
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

// One case list per lock, so a new action cannot compile without saying which lock governs it.
ActionLock governingLock(const EditorAction::Id action) noexcept
{
    switch (action)
    {
        // Content: the notes and everything authored over them, the sections, the tone regions
        // and their catalog tones, the automation points. The pickers and the fret entry ask
        // nothing a lock would let them write, so no picker opens and no digit is typed.
        case EditorAction::Id::CreateToneRegion:
        case EditorAction::Id::DeleteToneRegion:
        case EditorAction::Id::RenameTone:
        case EditorAction::Id::SetToneRegionTone:
        case EditorAction::Id::MoveToneBoundary:
        case EditorAction::Id::CreateNewTone:
        case EditorAction::Id::SetToneAutomationPoints:
        // Removing a plugin takes its automation points out of the song, and replacing the tone's
        // rig drops the automation keyed by the plugins it replaces; with none to drop either is
        // still the one press, refused alike.
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::ImportToneFile:
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
        case EditorAction::Id::InsertSongSection:
        case EditorAction::Id::RenameSongSection:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::ClearFretHandEnd:
        case EditorAction::Id::PruneFretHandPositions:
        case EditorAction::Id::ChooseHold:
        case EditorAction::Id::SetHold:
        // The section and tone verbs decided at the press: no prompt opens that only a refusal
        // could answer, and no bound answers before the lock.
        case EditorAction::Id::SongSectionChord:
        case EditorAction::Id::SongSectionAt:
        case EditorAction::Id::ToneChord:
        case EditorAction::Id::ToneChangeAt:
        case EditorAction::Id::ToneRenamePrompt:
        {
            return ActionLock::Note;
        }
        // The grid: the tempo map and the time signatures, Scale Tempo in both of its forms.
        case EditorAction::Id::InsertTempoAnchor:
        case EditorAction::Id::SetTempoSpanBpm:
        case EditorAction::Id::MoveTempoAnchor:
        case EditorAction::Id::ReleaseTempoAnchorDrag:
        case EditorAction::Id::ScaleTempo:
        case EditorAction::Id::SetTimeSignature:
        case EditorAction::Id::TempoChord:
        case EditorAction::Id::TimeSignatureChord:
        case EditorAction::Id::TimeSignatureAt:
        case EditorAction::Id::ScaleTempoPrompt:
        {
            return ActionLock::Tempo;
        }
        // The editor-wide move and delete, `Enter` and `Ctrl+R` dispatch on the selection's kind.
        case EditorAction::Id::MoveSelection:
        case EditorAction::Id::DeleteSelection:
        case EditorAction::Id::RestateSelection:
        case EditorAction::Id::RenameSelection:
        {
            return ActionLock::Operand;
        }
        // Undo and redo reproduce history already consented to, and a lock's own toggle is not
        // undoable; everything else here changes no song content: the project and the signal
        // chain, the transport, the grid settings, the view, navigation and selection.
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
        case EditorAction::Id::Undo:
        case EditorAction::Id::Redo:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::ToggleGridSnap:
        case EditorAction::Id::ToggleTempoLock:
        case EditorAction::Id::ToggleNoteLock:
        case EditorAction::Id::SelectArrangement:
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        case EditorAction::Id::ScanPluginCatalog:
        case EditorAction::Id::InsertSelectedPlugin:
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        case EditorAction::Id::NewToneDocument:
        case EditorAction::Id::OpenToneFile:
        case EditorAction::Id::SaveToneFile:
        case EditorAction::Id::SaveToneFileAs:
        case EditorAction::Id::ExportToneFile:
        // Answers the confirmation only an import opens, which the Note Lock refused first.
        case EditorAction::Id::ResolveToneImportPrompt:
        case EditorAction::Id::StepChartCaret:
        case EditorAction::Id::StepToRowObject:
        case EditorAction::Id::JumpToFocusRow:
        case EditorAction::Id::JumpChartCaret:
        case EditorAction::Id::ExtendTimeSelection:
        case EditorAction::Id::SelectSongSection:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::SelectSpanRowMark:
        {
            return ActionLock::None;
        }
    }
    return ActionLock::None;
}

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
        case EditorAction::Id::ToggleTempoLock:
        case EditorAction::Id::ToggleNoteLock:
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
        case EditorAction::Id::InsertTempoAnchor:
        case EditorAction::Id::SetTempoSpanBpm:
        case EditorAction::Id::SetTimeSignature:
        case EditorAction::Id::MoveTempoAnchor:
        case EditorAction::Id::ReleaseTempoAnchorDrag:
        case EditorAction::Id::ScaleTempo:
        case EditorAction::Id::SelectTempoAnchor:
        case EditorAction::Id::SelectTimeSignature:
        case EditorAction::Id::SelectFretHandPosition:
        case EditorAction::Id::SelectSpanRowMark:
        case EditorAction::Id::AuthorFretHandPositionAtCursor:
        case EditorAction::Id::SongSectionChord:
        case EditorAction::Id::SongSectionAt:
        case EditorAction::Id::TempoChord:
        case EditorAction::Id::TimeSignatureChord:
        case EditorAction::Id::TimeSignatureAt:
        case EditorAction::Id::ToneChord:
        case EditorAction::Id::ToneChangeAt:
        case EditorAction::Id::ToneRenamePrompt:
        case EditorAction::Id::ScaleTempoPrompt:
        case EditorAction::Id::RestateSelection:
        case EditorAction::Id::RenameSelection:
        case EditorAction::Id::ChooseHold:
        case EditorAction::Id::SetHold:
        case EditorAction::Id::ClearFretHandEnd:
        case EditorAction::Id::PruneFretHandPositions:
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

// True when whyUnavailable() names no reason, so the two can never disagree.
bool isActionAvailable(EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    return !whyUnavailable(action, conditions).has_value();
}

// Names each refusal reason with a stable kebab-case tag for the action log.
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
