#include "deferred_project_action_state.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>

namespace rock_hero::editor::core
{

// Deferring a project action exposes only the unsaved-changes prompt for that action.
TEST_CASE("DeferredProjectActionState defers action", "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;

    state.defer(EditorAction::OpenProject{std::filesystem::path{"song.rhp"}});

    CHECK(state.hasDeferredAction());
    CHECK(
        state.unsavedChangesPrompt() ==
        std::optional{UnsavedChangesPrompt{EditorActionId::OpenProject}});
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Prompt resolution without a deferred action is ignored and leaves the state idle.
TEST_CASE(
    "DeferredProjectActionState ignores resolution without deferred action",
    "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;

    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Discard, false);

    CHECK(resolution.outcome == DeferredProjectActionState::Outcome::None);
    CHECK_FALSE(resolution.replay.has_value());
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Save without needing a destination keeps the deferred action for post-save replay.
TEST_CASE(
    "DeferredProjectActionState resolves save with current destination",
    "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    state.defer(EditorAction::CloseProject{});

    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, false);

    CHECK(resolution.outcome == DeferredProjectActionState::Outcome::SaveCurrentProject);
    CHECK_FALSE(resolution.replay.has_value());
    CHECK(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Save for an unsaved project asks the root controller to collect a Save As path.
TEST_CASE(
    "DeferredProjectActionState resolves save with missing destination",
    "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    state.defer(EditorAction::ExitApplication{});

    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, true);

    CHECK(resolution.outcome == DeferredProjectActionState::Outcome::AwaitSaveAsPath);
    CHECK_FALSE(resolution.replay.has_value());
    CHECK(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK(state.saveAsPrompt() == std::optional{SaveAsPrompt{EditorActionId::ExitApplication}});
}

// Cancelling a Save As prompt clears the deferred action; cancelling when hidden is ignored.
TEST_CASE(
    "DeferredProjectActionState cancels save as prompt", "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    CHECK_FALSE(state.cancelSaveAsPrompt());
    state.defer(EditorAction::CloseProject{});
    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, true);
    REQUIRE(resolution.outcome == DeferredProjectActionState::Outcome::AwaitSaveAsPath);

    const bool cancelled = state.cancelSaveAsPrompt();

    CHECK(cancelled);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Discard releases the deferred action for controller-side replay and clears prompts.
TEST_CASE("DeferredProjectActionState resolves discard", "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    state.defer(EditorAction::ImportSong{std::filesystem::path{"song.psarc"}});

    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Discard, false);

    CHECK(resolution.outcome == DeferredProjectActionState::Outcome::DiscardChangesAndReplay);
    REQUIRE(resolution.replay.has_value());
    CHECK(resolution.replay->action_id == EditorActionId::ImportSong);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Choosing Cancel drops the deferred action and all prompt state.
TEST_CASE("DeferredProjectActionState resolves cancel", "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    state.defer(EditorAction::CloseProject{});

    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Cancel, false);

    CHECK(resolution.outcome == DeferredProjectActionState::Outcome::Cancelled);
    CHECK_FALSE(resolution.replay.has_value());
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.unsavedChangesPrompt().has_value());
    CHECK_FALSE(state.saveAsPrompt().has_value());
}

// Successful saves can release the saved deferred action exactly once.
TEST_CASE(
    "DeferredProjectActionState takes replay after save", "[core][deferred-project-action-state]")
{
    DeferredProjectActionState state;
    state.defer(EditorAction::CloseProject{});
    const DeferredProjectActionState::Resolution resolution =
        state.resolveUnsavedChanges(UnsavedChangesDecision::Save, false);
    REQUIRE(resolution.outcome == DeferredProjectActionState::Outcome::SaveCurrentProject);

    std::optional<DeferredProjectActionState::Replay> replay = state.takeReplay();

    REQUIRE(replay.has_value());
    CHECK(replay->action_id == EditorActionId::CloseProject);
    CHECK_FALSE(state.hasDeferredAction());
    CHECK_FALSE(state.takeReplay().has_value());
}

} // namespace rock_hero::editor::core
