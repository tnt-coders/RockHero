/*!
\file editor_undo_history.h
\brief Pure editor undo history state for project-level undo/redo ordering.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/editor/core/plugin_block_assignment.h>
#include <rock_hero/editor/core/plugin_display_type.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Opaque plugin state bytes captured for a Rock Hero undo memento. */
struct [[nodiscard]] PluginStateMemento
{
    /*! \brief Backend-owned plugin state chunk. */
    std::vector<std::byte> bytes;

    /*! \brief Compares two plugin state mementos by their stored bytes. */
    friend bool operator==(const PluginStateMemento& lhs, const PluginStateMemento& rhs) = default;
};

/*! \brief Editor-owned visual metadata for a plugin instance. */
struct [[nodiscard]] PluginVisualEditState
{
    /*! \brief Opaque runtime plugin instance ID. */
    std::string instance_id;

    /*! \brief Fixed visual block occupied by the plugin. */
    std::size_t block_index{};

    /*! \brief Manual display type override, or empty for automatic classification. */
    std::optional<PluginDisplayType> display_type_override;

    /*! \brief Compares two visual edit states by their stored values. */
    friend bool operator==(const PluginVisualEditState& lhs, const PluginVisualEditState& rhs) =
        default;
};

/*! \brief Undo payload for a plugin insertion. */
struct [[nodiscard]] PluginInsertEdit
{
    /*! \brief Inserted plugin instance ID. */
    std::string instance_id;

    /*! \brief Chain index used for redo insertion. */
    std::size_t chain_index{};

    /*! \brief Editor visual metadata to restore on redo. */
    PluginVisualEditState visual_state;

    /*! \brief Placement snapshot carried so later id remaps update nested placement keys. */
    std::vector<PluginBlockAssignment> placement;

    /*! \brief Compares two insert edits by their stored values. */
    friend bool operator==(const PluginInsertEdit& lhs, const PluginInsertEdit& rhs) = default;
};

/*! \brief Undo payload for removing and later recreating one plugin. */
struct [[nodiscard]] PluginRemoveEdit
{
    /*! \brief Removed plugin instance ID before any recreate remap. */
    std::string instance_id;

    /*! \brief Original chain index for undo recreation. */
    std::size_t chain_index{};

    /*! \brief Captured plugin state used to recreate the removed instance. */
    PluginStateMemento plugin_state;

    /*! \brief Editor visual metadata to restore after recreation. */
    PluginVisualEditState visual_state;

    /*! \brief Placement snapshot carried so later id remaps update nested placement keys. */
    std::vector<PluginBlockAssignment> placement;

    /*! \brief Compares two remove edits by their stored values. */
    friend bool operator==(const PluginRemoveEdit& lhs, const PluginRemoveEdit& rhs) = default;
};

/*! \brief Undo payload for moving one plugin and its visual placement. */
struct [[nodiscard]] PluginMoveEdit
{
    /*! \brief Moved plugin instance ID. */
    std::string instance_id;

    /*! \brief Chain index before the move. */
    std::size_t before_index{};

    /*! \brief Chain index after the move. */
    std::size_t after_index{};

    /*! \brief Placement before the move. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after the move. */
    std::vector<PluginBlockAssignment> after_placement;

    /*! \brief Compares two move edits by their stored values. */
    friend bool operator==(const PluginMoveEdit& lhs, const PluginMoveEdit& rhs) = default;
};

/*! \brief Undo payload for changing signal-chain block placement only. */
struct [[nodiscard]] PluginPlacementEdit
{
    /*! \brief Placement before the edit. */
    std::vector<PluginBlockAssignment> before_placement;

    /*! \brief Placement after the edit. */
    std::vector<PluginBlockAssignment> after_placement;

    /*! \brief Compares two placement edits by their stored values. */
    friend bool operator==(const PluginPlacementEdit& lhs, const PluginPlacementEdit& rhs) =
        default;
};

/*! \brief Undo payload for changing one plugin's manual display type override. */
struct [[nodiscard]] PluginDisplayTypeEdit
{
    /*! \brief Edited plugin instance ID. */
    std::string instance_id;

    /*! \brief Display type override before the edit. */
    std::optional<PluginDisplayType> before_type;

    /*! \brief Display type override after the edit. */
    std::optional<PluginDisplayType> after_type;

    /*! \brief Compares two display type edits by their stored values. */
    friend bool operator==(const PluginDisplayTypeEdit& lhs, const PluginDisplayTypeEdit& rhs) =
        default;
};

/*! \brief Undo payload for restoring one plugin's full parameter/state chunk. */
struct [[nodiscard]] PluginParameterEdit
{
    /*! \brief Edited plugin instance ID. */
    std::string instance_id;

    /*! \brief Full plugin state before the edit settled. */
    PluginStateMemento before_state;

    /*! \brief Full plugin state after the edit settled. */
    PluginStateMemento after_state;

    /*! \brief Display-only label hint for the parameter or gesture group. */
    std::string label_hint;

    /*! \brief Compares two parameter edits by their stored values. */
    friend bool operator==(const PluginParameterEdit& lhs, const PluginParameterEdit& rhs) =
        default;
};

/*! \brief Undo payload for changing the fixed output-gain plugin value. */
struct [[nodiscard]] OutputGainEdit
{
    /*! \brief Output gain before the edit. */
    common::audio::Gain before_gain;

    /*! \brief Output gain after the edit. */
    common::audio::Gain after_gain;

    /*! \brief Compares two output gain edits by their stored values. */
    friend bool operator==(const OutputGainEdit& lhs, const OutputGainEdit& rhs) = default;
};

/*! \brief Payload variants stored by one editor undo entry. */
using EditorUndoPayload = std::variant<
    PluginInsertEdit, PluginRemoveEdit, PluginMoveEdit, PluginPlacementEdit, PluginDisplayTypeEdit,
    PluginParameterEdit, OutputGainEdit>;

/*! \brief One user-visible undo history entry. */
struct [[nodiscard]] EditorUndoEntry
{
    /*! \brief Human-readable action label used by menus, confirmation text, and diagnostics. */
    std::string label;

    /*! \brief Typed payload used to apply the entry's undo and redo inverse. */
    EditorUndoPayload payload;

    /*! \brief Compares two undo entries by their stored values. */
    friend bool operator==(const EditorUndoEntry& lhs, const EditorUndoEntry& rhs) = default;
};

/*! \brief Direction of a pending undo-history transition. */
enum class EditorUndoDirection
{
    /*! \brief Transition applies an undo entry. */
    Undo,

    /*! \brief Transition applies a redo entry. */
    Redo,
};

/*! \brief Non-commit reason reported by the pure undo history. */
enum class EditorUndoFailureCode
{
    /*! \brief No failure occurred. */
    None,

    /*! \brief Undo was requested while no undo entry is available. */
    NothingToUndo,

    /*! \brief Redo was requested while no redo entry is available. */
    NothingToRedo,

    /*! \brief A transition is already waiting for commit or abort. */
    TransitionAlreadyPending,

    /*! \brief A commit or abort was requested with no pending transition. */
    NoPendingTransition,

    /*! \brief A commit or abort used a stale pending-transition token. */
    PendingTokenMismatch,

    /*! \brief The caller rejected the operation before side effects began. */
    PreflightRejected,

    /*! \brief The caller completed but detected no net mutation to record. */
    NoNetMutation,

    /*! \brief The caller failed after restoring the original state. */
    RepairedFailure,

    /*! \brief The caller could not prove success or rollback and must fault the session. */
    RollbackContractViolation,
};

/*! \brief High-level result shape for one history transition request. */
enum class EditorUndoTransitionStatus
{
    /*! \brief The request changed history state immediately. */
    Applied,

    /*! \brief The request opened a pending undo/redo transition. */
    Pending,

    /*! \brief The request intentionally left history state unchanged. */
    NonCommitFailure,
};

/*! \brief Structured event returned by the pure undo history for controller logging. */
enum class EditorUndoEventType
{
    /*! \brief A new undo entry was committed. */
    EntryPushed,

    /*! \brief Existing redo entries were discarded by a new push. */
    RedoEntriesDiscarded,

    /*! \brief An undo transition began and is waiting for caller commit. */
    UndoBegan,

    /*! \brief A redo transition began and is waiting for caller commit. */
    RedoBegan,

    /*! \brief A pending undo transition committed. */
    UndoCommitted,

    /*! \brief A pending redo transition committed. */
    RedoCommitted,

    /*! \brief A pending transition was aborted without moving the history pointer. */
    TransitionAborted,

    /*! \brief A request was rejected before opening or committing a transition. */
    TransitionRejected,

    /*! \brief The current history position was marked clean. */
    CleanMarked,

    /*! \brief The clean position was removed from reachable history. */
    CleanMarkerMadeUnreachable,

    /*! \brief Undo and redo entries were cleared. */
    HistoryReset,

    /*! \brief A runtime plugin instance ID was remapped through stored entries. */
    InstanceIdRemapped,
};

/*! \brief One transition event plus optional detail useful for diagnostics. */
struct [[nodiscard]] EditorUndoEvent
{
    /*! \brief Event kind. */
    EditorUndoEventType type{};

    /*! \brief Entry label involved in the event, when one entry was involved. */
    std::string label;

    /*! \brief Direction involved in the event, when applicable. */
    std::optional<EditorUndoDirection> direction;

    /*! \brief Non-commit failure code, when applicable. */
    EditorUndoFailureCode failure_code{EditorUndoFailureCode::None};

    /*! \brief Previous plugin instance ID for remap events. */
    std::string old_instance_id;

    /*! \brief New plugin instance ID for remap events. */
    std::string new_instance_id;

    /*! \brief True when the controller must fault the editor session. */
    bool requires_fault{false};

    /*! \brief Compares two events by their stored values. */
    friend bool operator==(const EditorUndoEvent& lhs, const EditorUndoEvent& rhs) = default;
};

/*! \brief Result returned by a pure undo-history state transition. */
struct [[nodiscard]] EditorUndoTransitionResult
{
    /*! \brief Overall transition status. */
    EditorUndoTransitionStatus status{EditorUndoTransitionStatus::Applied};

    /*! \brief Failure reason for NonCommitFailure results. */
    EditorUndoFailureCode failure_code{EditorUndoFailureCode::None};

    /*! \brief True when the controller must fault the editor session. */
    bool requires_fault{false};

    /*! \brief Transition events for controller-side logging. */
    std::vector<EditorUndoEvent> events;

    /*! \brief Compares two results by their stored values. */
    friend bool operator==(
        const EditorUndoTransitionResult& lhs, const EditorUndoTransitionResult& rhs) = default;
};

/*! \brief Pending undo/redo transition returned before side effects are applied. */
struct [[nodiscard]] EditorUndoPendingTransition
{
    /*! \brief Token that identifies the currently pending transition. */
    std::uint64_t token{};

    /*! \brief Pending transition direction. */
    EditorUndoDirection direction{EditorUndoDirection::Undo};

    /*! \brief Entry the caller should apply before committing the transition. */
    EditorUndoEntry entry;

    /*! \brief Compares two pending transitions by their stored values. */
    friend bool operator==(
        const EditorUndoPendingTransition& lhs, const EditorUndoPendingTransition& rhs) = default;
};

/*! \brief Result of trying to begin an undo or redo transition. */
struct [[nodiscard]] EditorUndoBeginResult
{
    /*! \brief Pending transition to apply, or empty when the request did not begin. */
    std::optional<EditorUndoPendingTransition> pending;

    /*! \brief Result and events for the begin request. */
    EditorUndoTransitionResult result;
};

/*!
\brief Owns the pure project-level undo/redo stack.

The history never applies editor or audio side effects. Undo and redo are explicit two-phase
transitions: begin returns the entry to apply, and the caller commits only after the side effects
succeed. Abort paths leave the history pointer unchanged and can report rollback-contract
violations so the controller can fault the session.
*/
class EditorUndoHistory final
{
public:
    /*! \brief Creates an empty history with a bounded entry depth. */
    explicit EditorUndoHistory(std::size_t max_entries = 100);

    /*! \brief Reports the number of entries currently available to undo. */
    [[nodiscard]] std::size_t undoDepth() const noexcept;

    /*! \brief Reports the number of entries currently available to redo. */
    [[nodiscard]] std::size_t redoDepth() const noexcept;

    /*! \brief Reports whether an undo transition can begin now. */
    [[nodiscard]] bool canUndo() const noexcept;

    /*! \brief Reports whether a redo transition can begin now. */
    [[nodiscard]] bool canRedo() const noexcept;

    /*! \brief Reports whether a transition is waiting for commit or abort. */
    [[nodiscard]] bool hasPendingTransition() const noexcept;

    /*!
    \brief Reports whether the current history position differs from the clean marker.

    Returns false when no clean marker is set: cleanliness is defined relative to a marker the caller
    establishes (for example markClean() after a project loads), so a history that holds edits but has
    no baseline is not reported as unsaved here. Callers that must treat edits as unsaved before any
    baseline exists track that separately (for example a save-requires-destination flag).
    */
    [[nodiscard]] bool hasUnsavedEdits() const noexcept;

    /*! \brief Reports whether a clean marker is currently set and reachable. */
    [[nodiscard]] bool hasReachableCleanMarker() const noexcept;

    /*! \brief Returns the label of the entry that would be undone next. */
    [[nodiscard]] std::optional<std::string> undoLabel() const;

    /*! \brief Returns the label of the entry that would be redone next. */
    [[nodiscard]] std::optional<std::string> redoLabel() const;

    /*! \brief Returns all stored entries for direct headless history tests. */
    [[nodiscard]] const std::vector<EditorUndoEntry>& entries() const noexcept;

    /*! \brief Returns the currently pending transition, if any. */
    [[nodiscard]] std::optional<EditorUndoPendingTransition> pendingTransition() const;

    /*!
    \brief Commits a new user edit and clears any redo branch.
    \param entry User-visible undo entry to append.
    \return Transition result and events for controller logging.
    */
    [[nodiscard]] EditorUndoTransitionResult push(EditorUndoEntry entry);

    /*!
    \brief Begins applying the next undo entry without moving the history pointer.
    \return Pending transition and result events, or a non-commit failure.
    */
    [[nodiscard]] EditorUndoBeginResult beginUndo();

    /*!
    \brief Begins applying the next redo entry without moving the history pointer.
    \return Pending transition and result events, or a non-commit failure.
    */
    [[nodiscard]] EditorUndoBeginResult beginRedo();

    /*!
    \brief Commits a pending undo or redo after caller side effects succeed.
    \param pending Pending transition previously returned by beginUndo() or beginRedo().
    \return Transition result and events for controller logging.
    */
    [[nodiscard]] EditorUndoTransitionResult commit(const EditorUndoPendingTransition& pending);

    /*!
    \brief Aborts a pending transition without moving the history pointer.
    \param pending Pending transition previously returned by beginUndo() or beginRedo().
    \param failure_code Non-commit reason to report to the controller.
    \return Transition result and events for controller logging.
    */
    [[nodiscard]] EditorUndoTransitionResult abort(
        const EditorUndoPendingTransition& pending, EditorUndoFailureCode failure_code);

    /*! \brief Marks the current history position as the clean revision. */
    [[nodiscard]] EditorUndoTransitionResult markClean();

    /*! \brief Clears all entries, pending state, and clean-marker state. */
    [[nodiscard]] EditorUndoTransitionResult reset();

    /*!
    \brief Replaces one runtime plugin instance ID throughout stored undo payloads.
    \param old_instance_id Runtime plugin instance ID currently stored by undo entries.
    \param new_instance_id Replacement runtime plugin instance ID.
    \return Transition result and events for controller logging.
    */
    [[nodiscard]] EditorUndoTransitionResult remapInstanceId(
        const std::string& old_instance_id, const std::string& new_instance_id);

private:
    enum class CleanMarkerState
    {
        None,
        Reachable,
        Unreachable,
    };

    [[nodiscard]] EditorUndoTransitionResult begin(EditorUndoDirection direction);
    [[nodiscard]] EditorUndoTransitionResult nonCommitFailure(
        EditorUndoFailureCode failure_code) const;
    void truncateRedo(std::vector<EditorUndoEvent>& events);
    void enforceMaxEntries(std::vector<EditorUndoEvent>& events);
    void makeCleanMarkerUnreachable(std::vector<EditorUndoEvent>& events);
    [[nodiscard]] bool pendingMatches(const EditorUndoPendingTransition& pending) const noexcept;

    std::vector<EditorUndoEntry> m_entries;
    std::size_t m_position{};
    std::size_t m_max_entries{100};
    CleanMarkerState m_clean_marker_state{CleanMarkerState::None};
    std::size_t m_clean_position{};
    std::optional<EditorUndoPendingTransition> m_pending;
    std::uint64_t m_next_pending_token{1};
};

} // namespace rock_hero::editor::core
