#include "controller/editor_action_availability.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::editor::core
{

namespace
{

using ActionId = EditorAction::Id;

}

// Verifies that busy work blocks every action except explicit lifecycle takeovers and cancel.
TEST_CASE("Busy actions are limited to takeover and cancel", "[core][editor-action]")
{
    const ActionConditions conditions{
        .busy = true,
        .busy_cancel_available = true,
        .live_input_audition_available = true,
        .has_project = true,
        .has_unsaved_changes_prompt = true,
        .has_save_as_prompt = true,
        .undo_available = true,
        .redo_available = true,
        .has_loaded_arrangement = true,
        .can_stop_transport = true,
        .has_plugin_candidates = true,
        .has_plugin_insert_capacity = true,
        .has_loaded_plugins = true,
    };

    CHECK(actionSupersedesBusy(ActionId::CloseProject));
    CHECK(actionSupersedesBusy(ActionId::ExitApplication));
    CHECK_FALSE(actionSupersedesBusy(ActionId::CancelBusyOperation));
    CHECK_FALSE(actionSupersedesBusy(ActionId::SaveProject));
    CHECK_FALSE(actionSupersedesBusy(ActionId::SetSignalChainPlacement));
    CHECK_FALSE(actionSupersedesBusy(ActionId::Undo));
    CHECK_FALSE(actionSupersedesBusy(ActionId::Redo));

    CHECK(isActionAvailable(ActionId::CloseProject, conditions));
    CHECK(isActionAvailable(ActionId::ExitApplication, conditions));
    CHECK(isActionAvailable(ActionId::CancelBusyOperation, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::OpenProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SaveProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Undo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Redo, conditions));

    ActionConditions non_cancellable_busy = conditions;
    non_cancellable_busy.busy_cancel_available = false;
    CHECK_FALSE(isActionAvailable(ActionId::CancelBusyOperation, non_cancellable_busy));
}

// Verifies that project write and close actions are disabled without a loaded project.
TEST_CASE("Project actions require an open project", "[core][editor-action]")
{
    ActionConditions conditions;

    CHECK_FALSE(isActionAvailable(ActionId::SaveProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SaveProjectAs, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::PublishProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::CloseProject, conditions));

    conditions.has_project = true;

    CHECK(isActionAvailable(ActionId::SaveProject, conditions));
    CHECK(isActionAvailable(ActionId::SaveProjectAs, conditions));
    CHECK(isActionAvailable(ActionId::PublishProject, conditions));
    CHECK(isActionAvailable(ActionId::CloseProject, conditions));
}

// Verifies Undo and Redo require both a project and available history direction.
TEST_CASE("Undo and redo actions follow history availability", "[core][editor-action]")
{
    ActionConditions conditions;

    CHECK_FALSE(isActionAvailable(ActionId::Undo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Redo, conditions));

    conditions.undo_available = true;
    conditions.redo_available = true;

    CHECK_FALSE(isActionAvailable(ActionId::Undo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Redo, conditions));

    conditions.has_project = true;

    CHECK(isActionAvailable(ActionId::Undo, conditions));
    CHECK(isActionAvailable(ActionId::Redo, conditions));
}

// Verifies that prompt-resolution actions are available only while their prompt is active.
TEST_CASE("Prompt actions follow active prompt state", "[core][editor-action]")
{
    ActionConditions conditions;

    CHECK_FALSE(isActionAvailable(ActionId::ResolveUnsavedChangesPrompt, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::CancelSaveAsPrompt, conditions));

    conditions.has_unsaved_changes_prompt = true;

    CHECK(isActionAvailable(ActionId::ResolveUnsavedChangesPrompt, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::CancelSaveAsPrompt, conditions));

    conditions.has_save_as_prompt = true;

    CHECK(isActionAvailable(ActionId::CancelSaveAsPrompt, conditions));
}

// Verifies that transport actions follow the loaded arrangement and stop conditions.
TEST_CASE("Transport actions follow loaded arrangement state", "[core][editor-action]")
{
    ActionConditions conditions;

    CHECK_FALSE(isActionAvailable(ActionId::PlayPause, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SeekTimeline, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetGridNoteValue, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Stop, conditions));

    conditions.has_loaded_arrangement = true;

    CHECK(isActionAvailable(ActionId::PlayPause, conditions));
    CHECK(isActionAvailable(ActionId::SeekTimeline, conditions));
    CHECK(isActionAvailable(ActionId::SetGridNoteValue, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Stop, conditions));

    conditions.can_stop_transport = true;

    CHECK(isActionAvailable(ActionId::Stop, conditions));
}

// Verifies plugin actions require arrangement, auditionable input, and insert capacity.
TEST_CASE("Plugin actions require signal-chain readiness", "[core][editor-action]")
{
    ActionConditions conditions{.has_loaded_arrangement = true};

    CHECK_FALSE(isActionAvailable(ActionId::ShowPluginBrowser, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::BeginPluginInsert, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ScanPluginCatalog, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::InsertSelectedPlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::RemovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::MovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::OpenPlugin, conditions));

    conditions.live_input_audition_available = true;

    CHECK_FALSE(isActionAvailable(ActionId::ShowPluginBrowser, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::BeginPluginInsert, conditions));
    CHECK(isActionAvailable(ActionId::ScanPluginCatalog, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::InsertSelectedPlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::RemovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::MovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::OpenPlugin, conditions));

    conditions.has_plugin_insert_capacity = true;

    CHECK(isActionAvailable(ActionId::ShowPluginBrowser, conditions));
    CHECK(isActionAvailable(ActionId::BeginPluginInsert, conditions));
    CHECK(isActionAvailable(ActionId::ScanPluginCatalog, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::InsertSelectedPlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::RemovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::MovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::OpenPlugin, conditions));

    conditions.has_plugin_candidates = true;
    conditions.has_loaded_plugins = true;

    CHECK(isActionAvailable(ActionId::InsertSelectedPlugin, conditions));
    CHECK(isActionAvailable(ActionId::RemovePlugin, conditions));
    CHECK(isActionAvailable(ActionId::MovePlugin, conditions));
    CHECK(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK(isActionAvailable(ActionId::OpenPlugin, conditions));
}

// Verifies that calibration prompt visibility blocks only the conflicting action set.
TEST_CASE("Calibration prompt blocks playback and plugin actions", "[core][editor-action]")
{
    const ActionConditions conditions{
        .input_calibration_prompt_visible = true,
        .live_input_audition_available = true,
        .has_project = true,
        .undo_available = true,
        .redo_available = true,
        .has_loaded_arrangement = true,
        .can_stop_transport = true,
        .has_plugin_candidates = true,
        .has_plugin_insert_capacity = true,
        .has_loaded_plugins = true,
    };

    CHECK_FALSE(isActionAvailable(ActionId::PlayPause, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ShowPluginBrowser, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::BeginPluginInsert, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ScanPluginCatalog, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::InsertSelectedPlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::RemovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::MovePlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetSignalChainPlacement, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::OpenPlugin, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Undo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Redo, conditions));

    CHECK(isActionAvailable(ActionId::SeekTimeline, conditions));
    CHECK(isActionAvailable(ActionId::SetGridNoteValue, conditions));
    CHECK(isActionAvailable(ActionId::Stop, conditions));
    CHECK(isActionAvailable(ActionId::CloseProject, conditions));
}

// Verifies a faulted session leaves only project recovery and prompt-resolution actions available.
TEST_CASE("Faulted session blocks editing and saving", "[core][editor-action]")
{
    ActionConditions conditions{
        .session_faulted = true,
        .live_input_audition_available = true,
        .has_project = true,
        .has_unsaved_changes_prompt = true,
        .undo_available = true,
        .redo_available = true,
        .has_loaded_arrangement = true,
        .can_stop_transport = true,
        .has_plugin_candidates = true,
        .has_plugin_insert_capacity = true,
        .has_loaded_plugins = true,
    };

    CHECK(isActionAvailable(ActionId::OpenProject, conditions));
    CHECK(isActionAvailable(ActionId::RestoreProject, conditions));
    CHECK(isActionAvailable(ActionId::ImportSong, conditions));
    CHECK(isActionAvailable(ActionId::CloseProject, conditions));
    CHECK(isActionAvailable(ActionId::ExitApplication, conditions));
    CHECK(isActionAvailable(ActionId::ResolveUnsavedChangesPrompt, conditions));

    CHECK_FALSE(isActionAvailable(ActionId::SaveProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SaveProjectAs, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::PublishProject, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Undo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::Redo, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::PlayPause, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ShowPluginBrowser, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::RemovePlugin, conditions));

    conditions.has_unsaved_changes_prompt = false;
    CHECK_FALSE(isActionAvailable(ActionId::ResolveUnsavedChangesPrompt, conditions));
}

// The chart verbs run through the one action gate (2026-08-21), so their preconditions are rows
// of this policy rather than guards copied into each verb: busy refuses them all, the caret moves
// are paused-only, the selection verbs need chart notes selected, the Insert key an armed caret.
TEST_CASE("Chart actions follow chart, transport, and selection state", "[core][editor-action]")
{
    ActionConditions conditions{.has_project = true, .has_loaded_arrangement = true};

    // No chart: nothing chart-shaped is available, but the editor-wide selection verbs are.
    CHECK_FALSE(isActionAvailable(ActionId::StepChartCaret, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::JumpChartCaret, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ExtendTimeSelection, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::TypeChartFretDigit, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartTechnique, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartSilentHold, conditions));
    CHECK(isActionAvailable(ActionId::MoveSelection, conditions));
    CHECK(isActionAvailable(ActionId::DeleteSelection, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::InsertAtCaret, conditions));

    conditions.has_chart = true;

    CHECK(isActionAvailable(ActionId::StepChartCaret, conditions));
    CHECK(isActionAvailable(ActionId::JumpChartCaret, conditions));
    CHECK(isActionAvailable(ActionId::ExtendTimeSelection, conditions));
    CHECK(isActionAvailable(ActionId::TypeChartFretDigit, conditions));
    // The selection verbs wait for a chart selection.
    CHECK_FALSE(isActionAvailable(ActionId::ShiftChartFrets, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::AdjustChartSustain, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartTechnique, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::SetChartLeftTap, conditions));

    conditions.has_chart_selection = true;
    CHECK(isActionAvailable(ActionId::ShiftChartFrets, conditions));
    CHECK(isActionAvailable(ActionId::AdjustChartSustain, conditions));
    CHECK(isActionAvailable(ActionId::ToggleChartTechnique, conditions));
    CHECK(isActionAvailable(ActionId::SetChartLeftTap, conditions));
    // The arpeggio hold reads the typed family's WIDER scope — the selection or the armed caret's
    // own slot — which the conditions carry as one answer rather than as two the table must
    // re-combine. A selection alone is a scope, and so is a caret alone.
    conditions.has_chart_verb_scope = true;
    CHECK(isActionAvailable(ActionId::ToggleChartSilentHold, conditions));
    conditions.has_chart_verb_scope = false;
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartSilentHold, conditions));

    conditions.has_armed_caret = true;
    conditions.has_chart_verb_scope = true;
    CHECK(isActionAvailable(ActionId::InsertAtCaret, conditions));
    CHECK(isActionAvailable(ActionId::ToggleChartSilentHold, conditions));

    // The caret moves are paused-only; the edits on a selection are not gated on the transport
    // because play clears the chart selection structurally.
    conditions.transport_playing = true;
    CHECK_FALSE(isActionAvailable(ActionId::StepChartCaret, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::JumpChartCaret, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ExtendTimeSelection, conditions));
    CHECK(isActionAvailable(ActionId::ToggleChartTechnique, conditions));

    // Busy refuses every chart verb, and none of them takes over a busy operation.
    conditions.busy = true;
    CHECK_FALSE(isActionAvailable(ActionId::TypeChartFretDigit, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartTechnique, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::ToggleChartSilentHold, conditions));
    CHECK_FALSE(isActionAvailable(ActionId::MoveSelection, conditions));
    CHECK_FALSE(actionSupersedesBusy(ActionId::ToggleChartTechnique));
    CHECK_FALSE(actionSupersedesBusy(ActionId::ToggleChartSilentHold));
}

} // namespace rock_hero::editor::core
