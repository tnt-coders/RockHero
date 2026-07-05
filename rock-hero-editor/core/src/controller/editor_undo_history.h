/*!
\file editor_undo_history.h
\brief Editor undo edit objects and project-level undo/redo ordering state.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{
class ILiveRig;
class IPluginHost;
} // namespace rock_hero::common::audio

namespace rock_hero::common::core
{
class Session;
} // namespace rock_hero::common::core

namespace rock_hero::editor::core
{

class SignalChainWorkflow;

/*! \brief Direction of a pending undo-history transition. */
enum class EditorUndoDirection : std::uint8_t
{
    /*! \brief Transition applies an undo entry. */
    Undo,

    /*! \brief Transition applies a redo entry. */
    Redo,
};

/*! \brief Non-commit reason reported by the pure undo history and edit objects. */
enum class EditorUndoFailureCode : std::uint8_t
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

/*! \brief Apply-time dependencies passed to concrete editor edit objects. */
struct [[nodiscard]] EditorEditContext
{
    /*! \brief Session whose current arrangement owns the tone schedule edits mutate. */
    common::core::Session& session;

    /*! \brief Editor-owned signal-chain model updated by visual and snapshot edits. */
    SignalChainWorkflow& signal_chain;

    /*! \brief Audio boundary used by plugin-chain and plugin-state edits. */
    common::audio::IPluginHost& plugin_host;

    /*! \brief Live-rig boundary used by output-gain edits. */
    common::audio::ILiveRig& live_rig;

    /*! \brief Controller-owned output-gain mirror refreshed after output-gain undo/redo. */
    double& output_gain_db;
};

/*! \brief Polymorphic editor-core undo entry. */
class IEdit
{
public:
    /*! \brief Destroys the edit object. */
    virtual ~IEdit() = default;

    /*!
    \brief Applies the inverse of this edit.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] virtual std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const = 0;

    /*!
    \brief Re-applies this edit.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the non-commit failure that should abort the transition.
    */
    [[nodiscard]] virtual std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const = 0;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label for this edit. */
    [[nodiscard]] virtual std::string label() const = 0;

    /*!
    \brief Reports whether applying this edit in the given direction recreates a plugin.

    Directions that instantiate a plugin (insert redo, remove undo) are potentially slow and must
    run behind the editor's plugin-loading busy presentation. All other edits apply synchronously.

    \return True when applying in this direction instantiates a plugin.
    */
    [[nodiscard]] virtual bool instantiatesPlugin(EditorUndoDirection /*direction*/) const
    {
        return false;
    }

protected:
    /*! \brief Allows construction only through derived edit objects. */
    IEdit() = default;

public:
    /*! \brief Copying and moving are disabled; edits are owned by the history. */
    IEdit(const IEdit&) = delete;

    /*! \brief Copy assignment is disabled; edits are owned by the history. */
    IEdit& operator=(const IEdit&) = delete;

    /*! \brief Moving is disabled; edits are owned by the history. */
    IEdit(IEdit&&) = delete;

    /*! \brief Move assignment is disabled; edits are owned by the history. */
    IEdit& operator=(IEdit&&) = delete;
};

/*! \brief High-level result shape for one history transition request. */
enum class EditorUndoTransitionStatus : std::uint8_t
{
    /*! \brief The request changed history state immediately. */
    Applied,

    /*! \brief The request opened a pending undo/redo transition. */
    Pending,

    /*! \brief The request intentionally left history state unchanged. */
    NonCommitFailure,
};

/*! \brief Structured event returned by the pure undo history for controller logging. */
enum class EditorUndoEventType : std::uint8_t
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

    /*! \brief Non-owning edit object to apply before committing the transition. */
    const IEdit* edit{};

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
    /*! \brief Creates an empty history with a bounded entry depth.
    \param max_entries Maximum retained undo entries before the oldest is dropped. */
    explicit EditorUndoHistory(std::size_t max_entries = 100);

    /*! \brief Reports the number of entries currently available to undo.
    \return Current undo depth. */
    [[nodiscard]] std::size_t undoDepth() const noexcept;

    /*! \brief Reports the number of entries currently available to redo.
    \return Current redo depth. */
    [[nodiscard]] std::size_t redoDepth() const noexcept;

    /*! \brief Reports whether an undo transition can begin now.
    \return True when an undo entry is available and no transition is pending. */
    [[nodiscard]] bool canUndo() const noexcept;

    /*! \brief Reports whether a redo transition can begin now.
    \return True when a redo entry is available and no transition is pending. */
    [[nodiscard]] bool canRedo() const noexcept;

    /*! \brief Reports whether a transition is waiting for commit or abort.
    \return True while a begun transition awaits commit() or abort(). */
    [[nodiscard]] bool hasPendingTransition() const noexcept;

    /*!
    \brief Reports whether the current history position differs from the clean marker.

    Returns false when no clean marker is set: cleanliness is defined relative to a marker the
    caller establishes (for example markClean() after a project loads), so a history that holds
    edits but has no baseline is not reported as unsaved here. Callers that must treat edits as
    unsaved before any baseline exists track that separately (for example a
    save-requires-destination flag).

    \return True when the current position differs from a reachable clean marker.
    */
    [[nodiscard]] bool hasUnsavedEdits() const noexcept;

    /*! \brief Reports whether a clean marker is currently set and reachable.
    \return True when markClean() ran and its position is still reachable. */
    [[nodiscard]] bool hasReachableCleanMarker() const noexcept;

    /*! \brief Returns the label of the entry that would be undone next.
    \return Entry label, or empty when nothing can be undone. */
    [[nodiscard]] std::optional<std::string> undoLabel() const;

    /*! \brief Returns the label of the entry that would be redone next.
    \return Entry label, or empty when nothing can be redone. */
    [[nodiscard]] std::optional<std::string> redoLabel() const;

    /*!
    \brief Commits a new user edit and clears any redo branch.
    \param edit User-visible undo entry to append.
    \return Transition result and events for controller logging.
    */
    [[nodiscard]] EditorUndoTransitionResult push(std::unique_ptr<IEdit> edit);

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

    /*! \brief Marks the current history position as the clean revision.
    \return Transition result and events for controller logging. */
    [[nodiscard]] EditorUndoTransitionResult markClean();

    /*! \brief Clears all entries, pending state, and clean-marker state.
    \return Transition result and events for controller logging. */
    [[nodiscard]] EditorUndoTransitionResult reset();

private:
    enum class CleanMarkerState : std::uint8_t
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
    [[nodiscard]] std::string pendingLabel() const;

    std::vector<std::unique_ptr<IEdit>> m_entries;
    std::size_t m_position{};
    std::size_t m_max_entries{100};
    CleanMarkerState m_clean_marker_state{CleanMarkerState::None};
    std::size_t m_clean_position{};
    std::optional<EditorUndoPendingTransition> m_pending;
    std::uint64_t m_next_pending_token{1};
};

} // namespace rock_hero::editor::core
