#include "controller/editor_undo_history.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Minimal polymorphic edit used by history tests that only care about stack mechanics.
class LabeledEdit final : public IEdit
{
public:
    explicit LabeledEdit(std::string label_value)
        : m_label(std::move(label_value))
    {}

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& /*context*/) const override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& /*context*/) const override
    {
        return {};
    }

    [[nodiscard]] std::string label() const override
    {
        return m_label;
    }

private:
    std::string m_label;
};

// Builds a minimal edit for tests that only care about history mechanics.
[[nodiscard]] std::unique_ptr<IEdit> makeEdit(std::string label)
{
    return std::make_unique<LabeledEdit>(std::move(label));
}

// Pushes setup entries through the real API and asserts the setup transition succeeded.
void pushEntry(EditorUndoHistory& history, std::string label)
{
    const EditorUndoTransitionResult result = history.push(makeEdit(std::move(label)));
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
    if (begin.pending.has_value())
    {
        const EditorUndoTransitionResult commit = history.commit(*begin.pending);
        REQUIRE(commit.status == EditorUndoTransitionStatus::Applied);
    }
}

// Commits the next redo transition for tests focused on post-redo state.
void commitRedo(EditorUndoHistory& history)
{
    EditorUndoBeginResult begin = history.beginRedo();
    REQUIRE(begin.pending.has_value());
    if (begin.pending.has_value())
    {
        const EditorUndoTransitionResult commit = history.commit(*begin.pending);
        REQUIRE(commit.status == EditorUndoTransitionStatus::Applied);
    }
}

// Returns true when a transition result contains the requested event type.
[[nodiscard]] bool hasEvent(
    const EditorUndoTransitionResult& result, EditorUndoEventType event_type)
{
    return std::ranges::any_of(result.events, [event_type](const EditorUndoEvent& event) {
        return event.type == event_type;
    });
}

} // namespace

// Pushing one user edit makes it the next undo entry and leaves redo unavailable.
TEST_CASE("EditorUndoHistory push enables undo", "[core][editor-undo-history]")
{
    EditorUndoHistory history;

    const EditorUndoTransitionResult result = history.push(makeEdit("Move Block"));

    CHECK(result.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(result, EditorUndoEventType::EntryPushed));
    CHECK(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 0);
    CHECK(history.undoLabel() == std::optional<std::string>{"Move Block"});
    CHECK_FALSE(history.redoLabel().has_value());
}

// The snapshot exposes the whole stack, its cursor, and any clean marker for the history inspector.
TEST_CASE("EditorUndoHistory snapshot reflects the whole stack", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, "A");
    pushEntry(history, "B");
    pushEntry(history, "C");

    {
        const EditorUndoHistorySnapshot snapshot = history.snapshot();
        CHECK(snapshot.labels == std::vector<std::string>{"A", "B", "C"});
        CHECK(snapshot.position == 3);
        CHECK_FALSE(snapshot.clean_position.has_value());
    }

    // Undoing moves the cursor back but keeps the redoable entry visible in the snapshot.
    commitUndo(history);
    {
        const EditorUndoHistorySnapshot snapshot = history.snapshot();
        CHECK(snapshot.labels == std::vector<std::string>{"A", "B", "C"});
        CHECK(snapshot.position == 2);
    }

    // Marking clean records the current cursor position as the saved revision.
    markClean(history);
    {
        const EditorUndoHistorySnapshot snapshot = history.snapshot();
        REQUIRE(snapshot.clean_position.has_value());
        if (snapshot.clean_position.has_value())
        {
            CHECK(*snapshot.clean_position == 2);
        }
    }
}

// Null edit pushes are rejected without mutating history.
TEST_CASE("EditorUndoHistory rejects null edits", "[core][editor-undo-history]")
{
    EditorUndoHistory history;

    const EditorUndoTransitionResult result = history.push(nullptr);

    CHECK(result.status == EditorUndoTransitionStatus::NonCommitFailure);
    CHECK(result.failure_code == EditorUndoFailureCode::PreflightRejected);
    CHECK_FALSE(history.canUndo());
    CHECK(history.undoDepth() == 0);
}

// Undo and redo entries move stacks only after their pending transitions are committed.
TEST_CASE("EditorUndoHistory commits undo and redo in two phases", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, "First");
    pushEntry(history, "Second");

    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());
    if (undo.pending.has_value())
    {
        REQUIRE(undo.pending->edit != nullptr);
        CHECK(undo.pending->edit->label() == "Second");
        CHECK(undo.result.status == EditorUndoTransitionStatus::Pending);
        CHECK(history.undoDepth() == 2);
        CHECK(history.redoDepth() == 0);

        const EditorUndoTransitionResult undo_commit = history.commit(*undo.pending);

        CHECK(undo_commit.status == EditorUndoTransitionStatus::Applied);
        CHECK(hasEvent(undo_commit, EditorUndoEventType::UndoCommitted));
    }
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 1);
    CHECK(history.redoLabel() == std::optional<std::string>{"Second"});

    EditorUndoBeginResult redo = history.beginRedo();
    REQUIRE(redo.pending.has_value());
    if (redo.pending.has_value())
    {
        REQUIRE(redo.pending->edit != nullptr);
        CHECK(redo.pending->edit->label() == "Second");
        CHECK(history.undoDepth() == 1);
        CHECK(history.redoDepth() == 1);

        const EditorUndoTransitionResult redo_commit = history.commit(*redo.pending);

        CHECK(redo_commit.status == EditorUndoTransitionStatus::Applied);
        CHECK(hasEvent(redo_commit, EditorUndoEventType::RedoCommitted));
    }
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
        pushEntry(history, "Edit");
        EditorUndoBeginResult undo = history.beginUndo();
        REQUIRE(undo.pending.has_value());
        if (undo.pending.has_value())
        {
            const EditorUndoTransitionResult result = history.abort(*undo.pending, failure_code);

            CHECK(result.status == EditorUndoTransitionStatus::NonCommitFailure);
            CHECK(result.failure_code == failure_code);
            CHECK_FALSE(result.requires_fault);
        }
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
    pushEntry(history, "Edit");
    EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());
    if (undo.pending.has_value())
    {
        const EditorUndoTransitionResult result =
            history.abort(*undo.pending, EditorUndoFailureCode::RollbackContractViolation);

        CHECK(result.status == EditorUndoTransitionStatus::NonCommitFailure);
        CHECK(result.failure_code == EditorUndoFailureCode::RollbackContractViolation);
        CHECK(result.requires_fault);
        CHECK(hasEvent(result, EditorUndoEventType::TransitionAborted));
    }
    CHECK_FALSE(history.hasPendingTransition());
    CHECK(history.undoDepth() == 1);
    CHECK(history.redoDepth() == 0);
}

// Starting unavailable or overlapping transitions returns non-commit failures.
TEST_CASE("EditorUndoHistory rejects unavailable transitions", "[core][editor-undo-history]")
{
    EditorUndoHistory history;

    const EditorUndoBeginResult empty_undo = history.beginUndo();
    CHECK_FALSE(empty_undo.pending.has_value());
    CHECK(empty_undo.result.failure_code == EditorUndoFailureCode::NothingToUndo);

    pushEntry(history, "Edit");
    const EditorUndoBeginResult undo = history.beginUndo();
    REQUIRE(undo.pending.has_value());

    const EditorUndoBeginResult overlapping = history.beginUndo();
    CHECK_FALSE(overlapping.pending.has_value());
    CHECK(overlapping.result.failure_code == EditorUndoFailureCode::TransitionAlreadyPending);

    const EditorUndoTransitionResult stale_commit = history.commit(EditorUndoPendingTransition{});
    CHECK(stale_commit.failure_code == EditorUndoFailureCode::PendingTokenMismatch);
}

// Pushing after an undo forks history, clears redo, and invalidates clean markers in redo.
TEST_CASE("EditorUndoHistory push after undo clears redo", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, "A");
    pushEntry(history, "B");
    markClean(history);
    commitUndo(history);

    const EditorUndoTransitionResult push = history.push(makeEdit("C"));

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
    pushEntry(history, "A");
    markClean(history);
    CHECK_FALSE(history.hasUnsavedEdits());

    pushEntry(history, "B");
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
    pushEntry(history, "A");
    markClean(history);
    pushEntry(history, "B");
    pushEntry(history, "C");
    CHECK(history.hasReachableCleanMarker());
    CHECK(history.undoDepth() == 2);

    const EditorUndoTransitionResult push = history.push(makeEdit("D"));

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
    pushEntry(history, "A");
    markClean(history);
    pushEntry(history, "B");
    const EditorUndoBeginResult undo = history.beginUndo();
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

// Without a clean marker, history alone does not report edits as unsaved.
TEST_CASE(
    "EditorUndoHistory has no unsaved edits without a clean marker", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    CHECK_FALSE(history.hasUnsavedEdits());

    pushEntry(history, "Edit");

    CHECK_FALSE(history.hasReachableCleanMarker());
    CHECK_FALSE(history.hasUnsavedEdits());
}

// Marking the current position clean emits a clean event and clears unsaved edits.
TEST_CASE("EditorUndoHistory mark clean emits a clean event", "[core][editor-undo-history]")
{
    EditorUndoHistory history;
    pushEntry(history, "Edit");

    const EditorUndoTransitionResult result = history.markClean();

    CHECK(result.status == EditorUndoTransitionStatus::Applied);
    CHECK(hasEvent(result, EditorUndoEventType::CleanMarked));
    CHECK(history.hasReachableCleanMarker());
    CHECK_FALSE(history.hasUnsavedEdits());
}

} // namespace rock_hero::editor::core
