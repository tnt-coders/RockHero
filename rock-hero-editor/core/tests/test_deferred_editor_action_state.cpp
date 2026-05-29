#include "deferred_editor_action_state.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>

namespace rock_hero::editor::core
{

// Deferring a project action exposes only the unsaved-changes prompt for that action.
TEST_CASE("DeferredEditorActionState defers action", "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;

    state.defer(EditorAction::OpenProject{std::filesystem::path{"song.rhp"}});

    CHECK(state.hasDeferredAction());
    CHECK(
        state.unsavedChangesPrompt() ==
        std::optional{UnsavedChangesPrompt{EditorActionId::OpenProject}});
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Prompt resolution without a deferred action is ignored and leaves the state idle.
TEST_CASE(
    "DeferredEditorActionState ignores prompt resolution without deferred action",
    "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;

    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Discard, false);

    CHECK(decision.kind == DeferredEditorActionDecisionKind::None);
    CHECK_FALSE(decision.replay.has_value());
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Save without needing a destination keeps the deferred action for post-save replay.
TEST_CASE(
    "DeferredEditorActionState resolves save with current destination",
    "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    state.defer(EditorAction::CloseProject{});

    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, false);

    CHECK(decision.kind == DeferredEditorActionDecisionKind::SaveCurrentProject);
    CHECK_FALSE(decision.replay.has_value());
    CHECK(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Save for an unsaved project asks the root controller to collect a Save As path.
TEST_CASE(
    "DeferredEditorActionState resolves save with missing destination",
    "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    state.defer(EditorAction::ExitApplication{});

    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, true);

    CHECK(decision.kind == DeferredEditorActionDecisionKind::AwaitSaveAsPath);
    CHECK_FALSE(decision.replay.has_value());
    CHECK(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK(state.saveAsPrompt() == std::optional{SaveAsPrompt{EditorActionId::ExitApplication}});
}

// Cancelling a Save As prompt clears the deferred action; cancelling when hidden is ignored.
TEST_CASE(
    "DeferredEditorActionState cancels save as prompt", "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    CHECK_FALSE(state.cancelSaveAsPrompt());
    state.defer(EditorAction::CloseProject{});
    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, true);
    REQUIRE(decision.kind == DeferredEditorActionDecisionKind::AwaitSaveAsPath);

    const bool cancelled = state.cancelSaveAsPrompt();

    CHECK(cancelled);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Discard releases the deferred action for controller-side replay and clears prompts.
TEST_CASE("DeferredEditorActionState resolves discard", "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    state.defer(EditorAction::ImportSong{std::filesystem::path{"song.psarc"}});

    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Discard, false);

    CHECK(decision.kind == DeferredEditorActionDecisionKind::DiscardAndReplay);
    REQUIRE(decision.replay.has_value());
    CHECK(decision.replay->action_id == EditorActionId::ImportSong);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Cancel drops the deferred action and all prompt state.
TEST_CASE("DeferredEditorActionState resolves cancel", "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    state.defer(EditorAction::CloseProject{});

    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Cancel, false);

    CHECK(decision.kind == DeferredEditorActionDecisionKind::Cancelled);
    CHECK_FALSE(decision.replay.has_value());
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Successful saves can release the saved deferred action exactly once.
TEST_CASE(
    "DeferredEditorActionState takes replay after save", "[core][deferred-editor-action-state]")
{
    DeferredEditorActionState state;
    state.defer(EditorAction::CloseProject{});
    const DeferredEditorActionDecision decision =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, false);
    REQUIRE(decision.kind == DeferredEditorActionDecisionKind::SaveCurrentProject);

    std::optional<DeferredEditorActionReplay> replay = state.takeReplayAction();

    REQUIRE(replay.has_value());
    CHECK(replay->action_id == EditorActionId::CloseProject);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.takeReplayAction().has_value());
}

} // namespace rock_hero::editor::core
