#include "editor_action_availability.h"

namespace rock_hero::editor::core
{

namespace
{

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
        case EditorAction::Id::ResizeToneRegion:
        {
            return true;
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
            return false;
        }
    }

    return false;
}

// Applies idle-state action availability without querying controller-owned state.
[[nodiscard]] bool actionAvailableWhenIdle(
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
                return true;
            }
            case EditorAction::Id::ResolveUnsavedChangesPrompt:
            {
                return conditions.has_unsaved_changes_prompt;
            }
            case EditorAction::Id::CancelSaveAsPrompt:
            {
                return conditions.has_save_as_prompt;
            }
            case EditorAction::Id::SaveProject:
            case EditorAction::Id::SaveProjectAs:
            case EditorAction::Id::PublishProject:
            case EditorAction::Id::CancelBusyOperation:
            case EditorAction::Id::Undo:
            case EditorAction::Id::Redo:
            case EditorAction::Id::PlayPause:
            case EditorAction::Id::Stop:
            case EditorAction::Id::SeekTimeline:
            case EditorAction::Id::SetGridNoteValue:
            case EditorAction::Id::SelectToneRegion:
            case EditorAction::Id::ResizeToneRegion:
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
                return false;
            }
        }

        return false;
    }

    if (conditions.input_calibration_prompt_visible &&
        actionBlockedByInputCalibrationPrompt(action))
    {
        return false;
    }

    switch (action)
    {
        case EditorAction::Id::OpenProject:
        case EditorAction::Id::RestoreProject:
        case EditorAction::Id::ImportSong:
        case EditorAction::Id::ExitApplication:
        {
            return true;
        }
        case EditorAction::Id::SaveProject:
        case EditorAction::Id::SaveProjectAs:
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::CloseProject:
        {
            return conditions.has_project;
        }
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        {
            return conditions.has_unsaved_changes_prompt;
        }
        case EditorAction::Id::CancelSaveAsPrompt:
        {
            return conditions.has_save_as_prompt;
        }
        case EditorAction::Id::CancelBusyOperation:
        {
            return false;
        }
        case EditorAction::Id::Undo:
        {
            return conditions.has_project && conditions.undo_available;
        }
        case EditorAction::Id::Redo:
        {
            return conditions.has_project && conditions.redo_available;
        }
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::ResizeToneRegion:
        {
            return conditions.has_loaded_arrangement;
        }
        case EditorAction::Id::Stop:
        {
            return conditions.can_stop_transport;
        }
        case EditorAction::Id::ShowPluginBrowser:
        case EditorAction::Id::BeginPluginInsert:
        {
            return conditions.has_loaded_arrangement && conditions.live_input_audition_available &&
                   conditions.has_plugin_insert_capacity;
        }
        case EditorAction::Id::ScanPluginCatalog:
        {
            return conditions.has_loaded_arrangement && conditions.live_input_audition_available;
        }
        case EditorAction::Id::InsertSelectedPlugin:
        {
            return conditions.has_loaded_arrangement && conditions.live_input_audition_available &&
                   conditions.has_plugin_candidates && conditions.has_plugin_insert_capacity;
        }
        case EditorAction::Id::RemovePlugin:
        case EditorAction::Id::MovePlugin:
        case EditorAction::Id::SetSignalChainPlacement:
        case EditorAction::Id::SetPluginDisplayTypeOverride:
        case EditorAction::Id::OpenPlugin:
        {
            return conditions.has_loaded_arrangement && conditions.live_input_audition_available &&
                   conditions.has_loaded_plugins;
        }
    }

    return false;
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
        case EditorAction::Id::PublishProject:
        case EditorAction::Id::ResolveUnsavedChangesPrompt:
        case EditorAction::Id::CancelSaveAsPrompt:
        case EditorAction::Id::CancelBusyOperation:
        case EditorAction::Id::Undo:
        case EditorAction::Id::Redo:
        case EditorAction::Id::PlayPause:
        case EditorAction::Id::Stop:
        case EditorAction::Id::SeekTimeline:
        case EditorAction::Id::SetGridNoteValue:
        case EditorAction::Id::SelectToneRegion:
        case EditorAction::Id::ResizeToneRegion:
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
            return false;
        }
    }

    return false;
}

// Combines natural action availability with the action's busy-state policy.
bool isActionAvailable(EditorAction::Id action, const ActionConditions& conditions) noexcept
{
    if (conditions.busy)
    {
        if (action == EditorAction::Id::CancelBusyOperation)
        {
            return conditions.busy_cancel_available;
        }

        return actionSupersedesBusy(action);
    }

    return actionAvailableWhenIdle(action, conditions);
}

} // namespace rock_hero::editor::core
