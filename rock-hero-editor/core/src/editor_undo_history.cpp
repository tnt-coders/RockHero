#include "editor_undo_history.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Reports whether a non-commit failure requires the controller to fault the current session.
[[nodiscard]] bool requiresFault(EditorUndoFailureCode failure_code) noexcept
{
    return failure_code == EditorUndoFailureCode::RollbackContractViolation;
}

// Builds the common non-commit event/result shape used by rejected begin/commit/abort requests.
[[nodiscard]] EditorUndoTransitionResult makeNonCommitResult(EditorUndoFailureCode failure_code)
{
    const bool should_fault = requiresFault(failure_code);
    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::NonCommitFailure,
        .failure_code = failure_code,
        .requires_fault = should_fault,
        .events = {EditorUndoEvent{
            .type = EditorUndoEventType::TransitionRejected,
            .failure_code = failure_code,
            .requires_fault = should_fault,
        }},
    };
}

// Replaces a runtime instance ID if this field currently references the old ID.
void remapStringId(std::string& instance_id, const std::string& old_id, const std::string& new_id)
{
    if (instance_id == old_id)
    {
        instance_id = new_id;
    }
}

// Replaces instance IDs in an editor-owned placement vector.
void remapPlacement(
    std::vector<PluginBlockAssignment>& placement, const std::string& old_id,
    const std::string& new_id)
{
    for (PluginBlockAssignment& assignment : placement)
    {
        remapStringId(assignment.instance_id, old_id, new_id);
    }
}

// Replaces the runtime ID stored by one plugin visual-state snapshot.
void remapVisualState(
    PluginVisualEditState& visual_state, const std::string& old_id, const std::string& new_id)
{
    remapStringId(visual_state.instance_id, old_id, new_id);
}

// Replaces runtime IDs held by one typed undo payload.
void remapPayload(EditorUndoPayload& payload, const std::string& old_id, const std::string& new_id)
{
    std::visit(
        [&](auto& edit) {
            using Edit = std::decay_t<decltype(edit)>;
            if constexpr (std::is_same_v<Edit, PluginInsertEdit>)
            {
                remapStringId(edit.instance_id, old_id, new_id);
                remapVisualState(edit.visual_state, old_id, new_id);
                remapPlacement(edit.placement, old_id, new_id);
            }
            else if constexpr (std::is_same_v<Edit, PluginRemoveEdit>)
            {
                remapStringId(edit.instance_id, old_id, new_id);
                remapVisualState(edit.visual_state, old_id, new_id);
                remapPlacement(edit.placement, old_id, new_id);
            }
            else if constexpr (std::is_same_v<Edit, PluginMoveEdit>)
            {
                remapStringId(edit.instance_id, old_id, new_id);
                remapPlacement(edit.before_placement, old_id, new_id);
                remapPlacement(edit.after_placement, old_id, new_id);
            }
            else if constexpr (std::is_same_v<Edit, PluginPlacementEdit>)
            {
                remapPlacement(edit.before_placement, old_id, new_id);
                remapPlacement(edit.after_placement, old_id, new_id);
            }
            else if constexpr (std::is_same_v<Edit, PluginDisplayTypeEdit>)
            {
                remapStringId(edit.instance_id, old_id, new_id);
            }
            else if constexpr (std::is_same_v<Edit, PluginParameterEdit>)
            {
                remapStringId(edit.instance_id, old_id, new_id);
            }
        },
        payload);
}

// Replaces runtime IDs held by one undo entry.
void remapEntry(EditorUndoEntry& entry, const std::string& old_id, const std::string& new_id)
{
    remapPayload(entry.payload, old_id, new_id);
}

} // namespace

EditorUndoHistory::EditorUndoHistory(std::size_t max_entries)
    : m_max_entries(std::max<std::size_t>(1, max_entries))
{}

std::size_t EditorUndoHistory::undoDepth() const noexcept
{
    return m_position;
}

std::size_t EditorUndoHistory::redoDepth() const noexcept
{
    return m_entries.size() - m_position;
}

bool EditorUndoHistory::canUndo() const noexcept
{
    return !m_pending.has_value() && m_position > 0;
}

bool EditorUndoHistory::canRedo() const noexcept
{
    return !m_pending.has_value() && m_position < m_entries.size();
}

bool EditorUndoHistory::hasPendingTransition() const noexcept
{
    return m_pending.has_value();
}

bool EditorUndoHistory::hasUnsavedEdits() const noexcept
{
    if (m_clean_marker_state == CleanMarkerState::Unreachable)
    {
        return true;
    }

    return m_clean_marker_state == CleanMarkerState::Reachable && m_position != m_clean_position;
}

bool EditorUndoHistory::hasReachableCleanMarker() const noexcept
{
    return m_clean_marker_state == CleanMarkerState::Reachable;
}

std::optional<std::string> EditorUndoHistory::undoLabel() const
{
    if (!canUndo())
    {
        return std::nullopt;
    }

    return m_entries[m_position - 1].label;
}

std::optional<std::string> EditorUndoHistory::redoLabel() const
{
    if (!canRedo())
    {
        return std::nullopt;
    }

    return m_entries[m_position].label;
}

const std::vector<EditorUndoEntry>& EditorUndoHistory::entries() const noexcept
{
    return m_entries;
}

std::optional<EditorUndoPendingTransition> EditorUndoHistory::pendingTransition() const
{
    return m_pending;
}

// Appends a successfully-applied user edit and discards any no-longer-linear redo branch.
EditorUndoTransitionResult EditorUndoHistory::push(EditorUndoEntry entry)
{
    if (m_pending.has_value())
    {
        return nonCommitFailure(EditorUndoFailureCode::TransitionAlreadyPending);
    }

    std::vector<EditorUndoEvent> events;
    truncateRedo(events);

    const std::string label = entry.label;
    m_entries.push_back(std::move(entry));
    m_position = m_entries.size();
    events.push_back(EditorUndoEvent{.type = EditorUndoEventType::EntryPushed, .label = label});
    enforceMaxEntries(events);

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Applied,
        .events = std::move(events),
    };
}

EditorUndoBeginResult EditorUndoHistory::beginUndo()
{
    EditorUndoTransitionResult result = begin(EditorUndoDirection::Undo);
    std::optional<EditorUndoPendingTransition> pending;
    if (result.status == EditorUndoTransitionStatus::Pending)
    {
        pending = m_pending;
    }

    return EditorUndoBeginResult{.pending = std::move(pending), .result = std::move(result)};
}

EditorUndoBeginResult EditorUndoHistory::beginRedo()
{
    EditorUndoTransitionResult result = begin(EditorUndoDirection::Redo);
    std::optional<EditorUndoPendingTransition> pending;
    if (result.status == EditorUndoTransitionStatus::Pending)
    {
        pending = m_pending;
    }

    return EditorUndoBeginResult{.pending = std::move(pending), .result = std::move(result)};
}

// Opens a pending transition without mutating the committed history position.
EditorUndoTransitionResult EditorUndoHistory::begin(EditorUndoDirection direction)
{
    if (m_pending.has_value())
    {
        return nonCommitFailure(EditorUndoFailureCode::TransitionAlreadyPending);
    }

    const bool is_undo = direction == EditorUndoDirection::Undo;
    if (is_undo && m_position == 0)
    {
        return nonCommitFailure(EditorUndoFailureCode::NothingToUndo);
    }

    if (!is_undo && m_position >= m_entries.size())
    {
        return nonCommitFailure(EditorUndoFailureCode::NothingToRedo);
    }

    const std::size_t entry_index = is_undo ? m_position - 1 : m_position;
    m_pending = EditorUndoPendingTransition{
        .token = m_next_pending_token++,
        .direction = direction,
        .entry = m_entries[entry_index],
    };

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Pending,
        .events = {EditorUndoEvent{
            .type = is_undo ? EditorUndoEventType::UndoBegan : EditorUndoEventType::RedoBegan,
            .label = m_pending->entry.label,
            .direction = direction,
        }},
    };
}

// Moves the history pointer after the caller has successfully applied the pending transition.
EditorUndoTransitionResult EditorUndoHistory::commit(const EditorUndoPendingTransition& pending)
{
    if (!m_pending.has_value())
    {
        return nonCommitFailure(EditorUndoFailureCode::NoPendingTransition);
    }

    if (!pendingMatches(pending))
    {
        return nonCommitFailure(EditorUndoFailureCode::PendingTokenMismatch);
    }

    const EditorUndoDirection direction = m_pending->direction;
    const bool is_undo = direction == EditorUndoDirection::Undo;
    if (is_undo)
    {
        --m_position;
    }
    else
    {
        ++m_position;
    }

    const std::string label = m_pending->entry.label;
    m_pending.reset();

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Applied,
        .events = {EditorUndoEvent{
            .type =
                is_undo ? EditorUndoEventType::UndoCommitted : EditorUndoEventType::RedoCommitted,
            .label = label,
            .direction = direction,
        }},
    };
}

// Clears the pending transition while preserving the previously committed history position.
EditorUndoTransitionResult EditorUndoHistory::abort(
    const EditorUndoPendingTransition& pending, EditorUndoFailureCode failure_code)
{
    if (!m_pending.has_value())
    {
        return nonCommitFailure(EditorUndoFailureCode::NoPendingTransition);
    }

    if (!pendingMatches(pending))
    {
        return nonCommitFailure(EditorUndoFailureCode::PendingTokenMismatch);
    }

    const std::string label = m_pending->entry.label;
    const EditorUndoDirection direction = m_pending->direction;
    m_pending.reset();

    const bool should_fault = requiresFault(failure_code);
    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::NonCommitFailure,
        .failure_code = failure_code,
        .requires_fault = should_fault,
        .events = {EditorUndoEvent{
            .type = EditorUndoEventType::TransitionAborted,
            .label = label,
            .direction = direction,
            .failure_code = failure_code,
            .requires_fault = should_fault,
        }},
    };
}

// Marks the current stack position as matching the trusted saved project model.
EditorUndoTransitionResult EditorUndoHistory::markClean()
{
    m_clean_marker_state = CleanMarkerState::Reachable;
    m_clean_position = m_position;

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Applied,
        .events = {EditorUndoEvent{.type = EditorUndoEventType::CleanMarked}},
    };
}

// Returns the history to an empty baseline; callers decide whether to mark that baseline clean.
EditorUndoTransitionResult EditorUndoHistory::reset()
{
    m_entries.clear();
    m_position = 0;
    m_clean_marker_state = CleanMarkerState::None;
    m_clean_position = 0;
    m_pending.reset();

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Applied,
        .events = {EditorUndoEvent{.type = EditorUndoEventType::HistoryReset}},
    };
}

// Remaps runtime IDs across all stored entries and the active pending entry copy.
EditorUndoTransitionResult EditorUndoHistory::remapInstanceId(
    const std::string& old_instance_id, const std::string& new_instance_id)
{
    if (old_instance_id == new_instance_id)
    {
        return EditorUndoTransitionResult{.status = EditorUndoTransitionStatus::Applied};
    }

    for (EditorUndoEntry& entry : m_entries)
    {
        remapEntry(entry, old_instance_id, new_instance_id);
    }

    if (m_pending.has_value())
    {
        remapEntry(m_pending->entry, old_instance_id, new_instance_id);
    }

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Applied,
        .events = {EditorUndoEvent{
            .type = EditorUndoEventType::InstanceIdRemapped,
            .old_instance_id = old_instance_id,
            .new_instance_id = new_instance_id,
        }},
    };
}

EditorUndoTransitionResult EditorUndoHistory::nonCommitFailure(
    EditorUndoFailureCode failure_code) const
{
    return makeNonCommitResult(failure_code);
}

// Drops redo entries after a new user edit and invalidates a clean marker stranded in that branch.
void EditorUndoHistory::truncateRedo(std::vector<EditorUndoEvent>& events)
{
    if (m_position == m_entries.size())
    {
        return;
    }

    if (m_clean_marker_state == CleanMarkerState::Reachable && m_clean_position > m_position)
    {
        makeCleanMarkerUnreachable(events);
    }

    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(m_position), m_entries.end());
    events.push_back(EditorUndoEvent{.type = EditorUndoEventType::RedoEntriesDiscarded});
}

// Enforces bounded history depth while keeping the current position and clean marker aligned.
void EditorUndoHistory::enforceMaxEntries(std::vector<EditorUndoEvent>& events)
{
    while (m_entries.size() > m_max_entries)
    {
        m_entries.erase(m_entries.begin());

        if (m_position > 0)
        {
            --m_position;
        }

        if (m_clean_marker_state == CleanMarkerState::Reachable)
        {
            if (m_clean_position == 0)
            {
                makeCleanMarkerUnreachable(events);
            }
            else
            {
                --m_clean_position;
            }
        }
    }
}

// Records the first transition that makes the saved clean position impossible to reach.
void EditorUndoHistory::makeCleanMarkerUnreachable(std::vector<EditorUndoEvent>& events)
{
    if (m_clean_marker_state == CleanMarkerState::Unreachable)
    {
        return;
    }

    m_clean_marker_state = CleanMarkerState::Unreachable;
    events.push_back(EditorUndoEvent{.type = EditorUndoEventType::CleanMarkerMadeUnreachable});
}

bool EditorUndoHistory::pendingMatches(const EditorUndoPendingTransition& pending) const noexcept
{
    return m_pending.has_value() && m_pending->token == pending.token;
}

} // namespace rock_hero::editor::core
