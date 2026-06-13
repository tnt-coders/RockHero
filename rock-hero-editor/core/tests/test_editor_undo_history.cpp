#include "editor_undo_history.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Builds a minimal placement payload for tests that only care about stack mechanics.
[[nodiscard]] EditorUndoEntry placementEntry(std::string label)
{
    return EditorUndoEntry{
        .label = std::move(label),
        .payload = PluginPlacementEdit{
            .before_placement = {},
            .after_placement = {},
        },
    };
}

// Pushes setup entries through the real API and asserts the setup transition succeeded.
void pushEntry(EditorUndoHistory& history, EditorUndoEntry entry)
{
    const EditorUndoTransitionResult result = history.push(std::move(entry));
    REQUIRE(result.status == EditorUndoTransitionStatus::Applied);
}

// Marks the current setup position clean through the real API.
void markClean(EditorUndoHistory& history)
{
    const EditorUndoTransitionResult result = history.markClean();
    REQUIRE(result.status == EditorUndoTransitionStatus::Applied);
}

// Commits the next undo transition for tests focused on post-undo state.
void commitUndo(EditorUndoHistory& history)
{
    EditorUndoBeginResult begin = history.beginUndo();
    REQUIRE(begin.pending.has_value());
    const EditorUndoTransitionResult commit = history.commit(*begin.pending);
    REQUIRE(commit.status == EditorUndoTransitionStatus::Applied);
}

// Commits the next redo transition for tests focused on post-redo state.
void commitRedo(EditorUndoHistory& history)
{
    EditorUndoBeginResult begin = history.beginRedo();
    REQUIRE(begin.pending.has_value());
    const EditorUndoTransitionResult commit = history.commit(*begin.pending);
    REQUIRE(commit.status == EditorUndoTransitionStatus::Applied);
}

// Returns true when a transition result contains the requested event type.
[[nodiscard]] bool hasEvent(
    const EditorUndoTransitionResult& result, EditorUndoEventType event_type)
{
    return std::ranges::any_of(result.events, [event_type](const EditorUndoEvent& event) {
        return event.type == event_type;
    });
}

// Finds the typed payload held by an entry.
template <typename Payload> [[nodiscard]] const Payload& payloadOf(const EditorUndoEntry& entry)
{
    return std::get<Payload>(entry.payload);
}

} // namespace

// Pushing one user edit makes it the next undo entry and leaves redo unavailable.
TEST_CASE("EditorUndoHistory push enables undo", "[core][editor-undo-history]")
{
    EditorUndoHistory history;

    const EditorUndoTransitionResult result = history.push(placementEntry("Move Block"));

    CHECK(result.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(result, EditorUndoEventType::EntryPushed));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 0);
    CHECK(history.undoLabel() == std::optional<std::string>{"Move Block"});
    CHECK_FALSE(history.redoLabel().has_value());
}

// Undo and redo entries move stacks only after their pending transitions are committed.
TEST_CASE("EditorUndoHistory commits undo and redo in two phases", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("First"));
    pushEntry(history, placementEntry("Second"));

    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());
    CHECK(undo.result.status == EditorUndoTransitionStatus::Pending);
    CHECK(undo.pending->entry.label == "Second");
    CHECK(history.undoDepth() == 2);
    CHECK(history.redoDepth() == 0);

    EditorUndoTransitionResult undo_commit = history.commit(*undo.pending);

    CHECK(undo_commit.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(undo_commit, EditorUndoEventType::UndoCommitted));
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 1);
    CHECK(history.redoLabel() == std::optional<std::string>{"Second"});

    EditorUndoBeginResult redo = history.beginRedo();
    REQUIRE(redo.pending.has_value());
    CHECK(redo.pending->entry.label == "Second");
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 1);

    EditorUndoTransitionResult redo_commit = history.commit(*redo.pending);

    CHECK(redo_commit.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(redo_commit, EditorUndoEventType::RedoCommitted));
    CHECK(history.undoDepth() == 2);
    CHECK(history.redoDepth() == 0);
}

// Recoverable apply failures abort the pending transition without moving stack state.
TEST_CASE("EditorUndoHistory aborts recoverable failures", "[core][editor-undo-history]")
{
    const std::vector<EditorUndoFailureCode> failure_codes{
        EditorUndoFailureCode::PreflightRejected,
        EditorUndoFailureCode::NoNetMutation,
        EditorUndoFailureCode::RepairedFailure,
    };

    for (const EditorUndoFailureCode failure_code : failure_codes)
    {
        EditorUndoHistory history;
        pushEntry(history, placementEntry("Edit"));
        EditorUndoBeginResult undo = history.beginUndo();
        REQUIRE(undo.pending.has_value());

        const EditorUndoTransitionResult result = history.abort(*undo.pending, failure_code);

        CHECK(result.status == EditorUndoTransitionStatus::NonCommitFailure);
        CHECK(result.failure_code == failure_code);
        CHECK_FALSE(result.requires_fault);
        CHECK_FALSE(history.hasPendingTransition());
        CHECK(history.undoDepth() == 1);
        CHECK(history.redoDepth() == 0);
        CHECK(history.canUndo());
        CHECK_FALSE(history.canRedo());
    }
}

// Rollback-contract violations do not commit and tell the controller to fault the session.
TEST_CASE("EditorUndoHistory reports rollback contract faults", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("Edit"));
    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());

    const EditorUndoTransitionResult result =
        history.abort(*undo.pending, EditorUndoFailureCode::RollbackContractViolation);

    CHECK(result.status == EditorUndoTransitionStatus::NonCommitFailure);
    CHECK(result.failure_code == EditorUndoFailureCode::RollbackContractViolation);
    CHECK(result.requires_fault);
    CHECK(hasEvent(result, EditorUndoEventType::TransitionAborted));
    CHECK_FALSE(history.hasPendingTransition());
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 0);
}

// Starting unavailable or overlapping transitions returns non-commit failures.
TEST_CASE("EditorUndoHistory rejects unavailable transitions", "[core][editor-undo-history]")
{
    EditorUndoHistory history;

    EditorUndoBeginResult empty_undo = history.beginUndo();
    CHECK_FALSE(empty_undo.pending.has_value());
    CHECK(empty_undo.result.failure_code == EditorUndoFailureCode::NothingToUndo);

    pushEntry(history, placementEntry("Edit"));
    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());

    EditorUndoBeginResult overlapping = history.beginUndo();
    CHECK_FALSE(overlapping.pending.has_value());
    CHECK(overlapping.result.failure_code == EditorUndoFailureCode::TransitionAlreadyPending);

    EditorUndoTransitionResult stale_commit = history.commit(EditorUndoPendingTransition{});
    CHECK(stale_commit.failure_code == EditorUndoFailureCode::PendingTokenMismatch);
}

// Pushing after an undo forks history, clears redo, and invalidates clean markers in redo.
TEST_CASE("EditorUndoHistory push after undo clears redo", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("A"));
    pushEntry(history, placementEntry("B"));
    markClean(history);
    commitUndo(history);

    const EditorUndoTransitionResult push = history.push(placementEntry("C"));

    CHECK(history.undoDepth() == 2);
    CHECK(history.redoDepth() == 0);
    CHECK_FALSE(history.canRedo());
    CHECK(history.hasUnsavedEdits());
    CHECK_FALSE(history.hasReachableCleanMarker());
    CHECK(hasEvent(push, EditorUndoEventType::RedoEntriesDiscarded));
    CHECK(hasEvent(push, EditorUndoEventType::CleanMarkerMadeUnreachable));
}

// The clean marker follows push, undo, redo, and explicit mark-clean transitions.
TEST_CASE("EditorUndoHistory tracks clean marker", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("A"));
    markClean(history);
    CHECK_FALSE(history.hasUnsavedEdits());

    pushEntry(history, placementEntry("B"));
    CHECK(history.hasUnsavedEdits());

    commitUndo(history);
    CHECK_FALSE(history.hasUnsavedEdits());

    commitRedo(history);
    CHECK(history.hasUnsavedEdits());

    markClean(history);
    CHECK_FALSE(history.hasUnsavedEdits());
}

// Bounded history makes a clean marker unreachable once its position is evicted.
TEST_CASE("EditorUndoHistory marks clean unreachable on eviction", "[core][editor-undo-history]")
{
    EditorUndoHistory history{2};
    pushEntry(history, placementEntry("A"));
    markClean(history);
    pushEntry(history, placementEntry("B"));
    pushEntry(history, placementEntry("C"));
    CHECK(history.hasReachableCleanMarker());
    CHECK(history.undoDepth() == 2);

    const EditorUndoTransitionResult push = history.push(placementEntry("D"));

    CHECK(history.undoDepth() == 2);
    CHECK(history.redoDepth() == 0);
    CHECK(history.hasUnsavedEdits());
    CHECK_FALSE(history.hasReachableCleanMarker());
    CHECK(hasEvent(push, EditorUndoEventType::CleanMarkerMadeUnreachable));
}

// Reset clears undo, redo, pending state, and any clean marker.
TEST_CASE("EditorUndoHistory reset clears history", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("A"));
    markClean(history);
    pushEntry(history, placementEntry("B"));
    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());

    const EditorUndoTransitionResult reset = history.reset();

    CHECK(reset.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(reset, EditorUndoEventType::HistoryReset));
    CHECK(history.undoDepth() == 0);
    CHECK(history.redoDepth() == 0);
    CHECK_FALSE(history.hasPendingTransition());
    CHECK_FALSE(history.hasReachableCleanMarker());
    CHECK_FALSE(history.hasUnsavedEdits());
}

// Runtime id remapping updates every current payload, redo payload, and pending payload copy.
TEST_CASE("EditorUndoHistory remaps plugin instance ids", "[core][editor-undo-history]")
{
    constexpr std::size_t old_block = 2;
    constexpr std::size_t new_block = 4;
    const std::string old_id{"old-plugin"};
    const std::string new_id{"new-plugin"};
    const std::vector<PluginBlockAssignment> before_placement{
        PluginBlockAssignment{.instance_id = old_id, .block_index = old_block}
    };
    const std::vector<PluginBlockAssignment> after_placement{
        PluginBlockAssignment{.instance_id = old_id, .block_index = new_block}
    };
    const PluginVisualEditState visual_state{
        .instance_id = old_id,
        .block_index = old_block,
        .display_type_override = PluginDisplayType::Amp,
    };

    EditorUndoHistory history;
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Insert",
            .payload = PluginInsertEdit{
                .instance_id = old_id,
                .chain_index = 0,
                .visual_state = visual_state,
                .placement = before_placement,
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Remove",
            .payload = PluginRemoveEdit{
                .instance_id = old_id,
                .chain_index = 0,
                .plugin_state = common::audio::PluginInstanceState{.opaque_data = {std::byte{1}}},
                .visual_state = visual_state,
                .placement = before_placement,
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Move",
            .payload = PluginMoveEdit{
                .instance_id = old_id,
                .before_index = 0,
                .after_index = 1,
                .before_placement = before_placement,
                .after_placement = after_placement,
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Placement",
            .payload = PluginPlacementEdit{
                .before_placement = before_placement,
                .after_placement = after_placement,
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Display",
            .payload = PluginDisplayTypeEdit{
                .instance_id = old_id,
                .before_type = PluginDisplayType::Amp,
                .after_type = PluginDisplayType::Delay,
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Parameter",
            .payload = PluginParameterEdit{
                .instance_id = old_id,
                .before_state = common::audio::PluginInstanceState{.opaque_data = {std::byte{2}}},
                .after_state = common::audio::PluginInstanceState{.opaque_data = {std::byte{3}}},
                .label_hint = "Gain",
            },
        });
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Output Gain",
            .payload = OutputGainEdit{
                .before_gain = common::audio::Gain{-1.0},
                .after_gain = common::audio::Gain{1.0},
            },
        });

    commitUndo(history);
    EditorUndoBeginResult pending_undo = history.beginUndo();
    REQUIRE(pending_undo.pending.has_value());

    const EditorUndoTransitionResult result = history.remapInstanceId(old_id, new_id);

    CHECK(hasEvent(result, EditorUndoEventType::InstanceIdRemapped));
    CHECK(payloadOf<PluginInsertEdit>(history.entries()[0]).instance_id == new_id);
    CHECK(payloadOf<PluginInsertEdit>(history.entries()[0]).visual_state.instance_id == new_id);
    CHECK(payloadOf<PluginInsertEdit>(history.entries()[0]).placement[0].instance_id == new_id);
    CHECK(payloadOf<PluginRemoveEdit>(history.entries()[1]).instance_id == new_id);
    CHECK(payloadOf<PluginRemoveEdit>(history.entries()[1]).visual_state.instance_id == new_id);
    CHECK(payloadOf<PluginRemoveEdit>(history.entries()[1]).placement[0].instance_id == new_id);
    CHECK(payloadOf<PluginMoveEdit>(history.entries()[2]).instance_id == new_id);
    CHECK(
        payloadOf<PluginMoveEdit>(history.entries()[2]).before_placement[0].instance_id == new_id);
    CHECK(payloadOf<PluginMoveEdit>(history.entries()[2]).after_placement[0].instance_id == new_id);
    CHECK(
        payloadOf<PluginPlacementEdit>(history.entries()[3]).before_placement[0].instance_id ==
        new_id);
    CHECK(
        payloadOf<PluginPlacementEdit>(history.entries()[3]).after_placement[0].instance_id ==
        new_id);
    CHECK(payloadOf<PluginDisplayTypeEdit>(history.entries()[4]).instance_id == new_id);
    CHECK(payloadOf<PluginParameterEdit>(history.entries()[5]).instance_id == new_id);
    REQUIRE(history.pendingTransition().has_value());
    CHECK(payloadOf<PluginParameterEdit>(history.pendingTransition()->entry).instance_id == new_id);
}

// Without a clean marker, history alone does not report edits as unsaved.
TEST_CASE(
    "EditorUndoHistory has no unsaved edits without a clean marker", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    CHECK_FALSE(history.hasUnsavedEdits());

    pushEntry(history, placementEntry("Edit"));

    CHECK_FALSE(history.hasReachableCleanMarker());
    CHECK_FALSE(history.hasUnsavedEdits());
}

// Marking the current position clean emits a clean event and clears unsaved edits.
TEST_CASE("EditorUndoHistory mark clean emits a clean event", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, placementEntry("Edit"));

    const EditorUndoTransitionResult result = history.markClean();

    CHECK(result.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(result, EditorUndoEventType::CleanMarked));
    CHECK(history.hasReachableCleanMarker());
    CHECK_FALSE(history.hasUnsavedEdits());
}

// Remapping an instance id to itself is a no-op that records no remap event.
TEST_CASE("EditorUndoHistory remap to same id is a no-op", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(
        history,
        EditorUndoEntry{
            .label = "Display",
            .payload = PluginDisplayTypeEdit{
                .instance_id = "plugin",
                .before_type = PluginDisplayType::Amp,
                .after_type = PluginDisplayType::Delay,
            },
        });

    const EditorUndoTransitionResult result = history.remapInstanceId("plugin", "plugin");

    CHECK(result.status == EditorUndoTransitionStatus::Applied);
    CHECK_FALSE(hasEvent(result, EditorUndoEventType::InstanceIdRemapped));
    CHECK(payloadOf<PluginDisplayTypeEdit>(history.entries()[0]).instance_id == "plugin");
}

} // namespace rock_hero::editor::core
