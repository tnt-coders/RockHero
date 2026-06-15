#include "editor_undo_history.h"

#include "signal_chain_workflow.h"

#include <algorithm>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/editor/core/plugin_view_state.h>
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

// Validates that a stored placement snapshot addresses the current plugin set before side effects.
[[nodiscard]] bool placementTargetsPlugins(
    const std::vector<PluginBlockAssignment>& placement,
    const std::vector<PluginViewState>& plugins)
{
    if (placement.size() != plugins.size())
    {
        return false;
    }

    return std::ranges::all_of(plugins, [&placement](const PluginViewState& plugin) {
        return std::ranges::any_of(placement, [&plugin](const PluginBlockAssignment& assignment) {
            return assignment.instance_id == plugin.instance_id;
        });
    });
}

// Applies a move edit in one direction and restores the matching editor-owned placement snapshot.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPluginMoveEdit(
    const PluginMoveEdit& edit, EditorUndoDirection direction, EditorEditContext& context)
{
    const std::size_t target_index =
        direction == EditorUndoDirection::Undo ? edit.before_index : edit.after_index;
    const std::vector<PluginBlockAssignment>& placement =
        direction == EditorUndoDirection::Undo ? edit.before_placement : edit.after_placement;
    const std::vector<PluginViewState>& plugins = context.signal_chain.plugins();
    if (!placementTargetsPlugins(placement, plugins))
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const std::optional<std::size_t> current_index =
        context.signal_chain.chainIndexForInstance(edit.instance_id);
    if (!current_index.has_value() || target_index >= context.signal_chain.appendIndex())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    if (*current_index == target_index)
    {
        if (context.signal_chain.setBlockPlacement(placement))
        {
            return {};
        }

        return std::unexpected{EditorUndoFailureCode::NoNetMutation};
    }

    auto snapshot = context.plugin_host.movePlugin(edit.instance_id, target_index);
    if (!snapshot.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::RepairedFailure};
    }

    context.signal_chain.replaceSnapshot(std::move(*snapshot));
    (void)context.signal_chain.setBlockPlacement(placement);
    return {};
}

// Applies a placement edit in one direction.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPluginPlacementEdit(
    const PluginPlacementEdit& edit, EditorUndoDirection direction, EditorEditContext& context)
{
    const std::vector<PluginBlockAssignment>& placement =
        direction == EditorUndoDirection::Undo ? edit.before_placement : edit.after_placement;
    if (!context.signal_chain.setBlockPlacement(placement))
    {
        return std::unexpected{EditorUndoFailureCode::NoNetMutation};
    }

    return {};
}

// Applies a display-type edit in one direction.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPluginDisplayTypeEdit(
    const PluginDisplayTypeEdit& edit, EditorUndoDirection direction, EditorEditContext& context)
{
    const std::optional<PluginDisplayType> display_type =
        direction == EditorUndoDirection::Undo ? edit.before_type : edit.after_type;
    if (!context.signal_chain.setPluginDisplayTypeOverride(edit.instance_id, display_type))
    {
        return std::unexpected{EditorUndoFailureCode::NoNetMutation};
    }

    return {};
}

// Applies a full plugin-state memento in one direction through the audio boundary.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyPluginParameterEdit(
    const PluginParameterEdit& edit, EditorUndoDirection direction, EditorEditContext& context)
{
    if (!context.signal_chain.containsInstance(edit.instance_id))
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const common::audio::PluginInstanceState& state =
        direction == EditorUndoDirection::Undo ? edit.before_state : edit.after_state;
    const common::audio::PluginInstanceState& opposite_state =
        direction == EditorUndoDirection::Undo ? edit.after_state : edit.before_state;
    if (state == opposite_state)
    {
        return std::unexpected{EditorUndoFailureCode::NoNetMutation};
    }

    if (const auto restored = context.plugin_host.setPluginState(edit.instance_id, state);
        !restored.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::RepairedFailure};
    }

    return {};
}

// Applies an output-gain edit in one direction through the live-rig boundary.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyOutputGainEdit(
    const OutputGainEdit& edit, EditorUndoDirection direction, EditorEditContext& context)
{
    const common::audio::Gain gain =
        direction == EditorUndoDirection::Undo ? edit.before_gain : edit.after_gain;
    const common::audio::Gain opposite_gain =
        direction == EditorUndoDirection::Undo ? edit.after_gain : edit.before_gain;
    if (gain == opposite_gain)
    {
        return std::unexpected{EditorUndoFailureCode::NoNetMutation};
    }

    if (const auto applied = context.live_rig.setOutputGain(gain); !applied.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::RepairedFailure};
    }

    context.output_gain_db = gain.db;
    return {};
}

} // namespace

std::expected<void, EditorUndoFailureCode> PluginMoveEdit::undo(EditorEditContext& context) const
{
    return applyPluginMoveEdit(*this, EditorUndoDirection::Undo, context);
}

std::expected<void, EditorUndoFailureCode> PluginMoveEdit::redo(EditorEditContext& context) const
{
    return applyPluginMoveEdit(*this, EditorUndoDirection::Redo, context);
}

std::string PluginMoveEdit::label() const
{
    return "Move Plugin";
}

std::expected<void, EditorUndoFailureCode> PluginPlacementEdit::undo(
    EditorEditContext& context) const
{
    return applyPluginPlacementEdit(*this, EditorUndoDirection::Undo, context);
}

std::expected<void, EditorUndoFailureCode> PluginPlacementEdit::redo(
    EditorEditContext& context) const
{
    return applyPluginPlacementEdit(*this, EditorUndoDirection::Redo, context);
}

std::string PluginPlacementEdit::label() const
{
    return "Move Plugin Block";
}

std::expected<void, EditorUndoFailureCode> PluginDisplayTypeEdit::undo(
    EditorEditContext& context) const
{
    return applyPluginDisplayTypeEdit(*this, EditorUndoDirection::Undo, context);
}

std::expected<void, EditorUndoFailureCode> PluginDisplayTypeEdit::redo(
    EditorEditContext& context) const
{
    return applyPluginDisplayTypeEdit(*this, EditorUndoDirection::Redo, context);
}

std::string PluginDisplayTypeEdit::label() const
{
    return "Set Plugin Display Type";
}

std::expected<void, EditorUndoFailureCode> PluginParameterEdit::undo(
    EditorEditContext& context) const
{
    return applyPluginParameterEdit(*this, EditorUndoDirection::Undo, context);
}

std::expected<void, EditorUndoFailureCode> PluginParameterEdit::redo(
    EditorEditContext& context) const
{
    return applyPluginParameterEdit(*this, EditorUndoDirection::Redo, context);
}

std::string PluginParameterEdit::label() const
{
    if (label_hint.empty())
    {
        return "Edit Plugin Parameter";
    }

    return "Edit " + label_hint;
}

std::expected<void, EditorUndoFailureCode> OutputGainEdit::undo(EditorEditContext& context) const
{
    return applyOutputGainEdit(*this, EditorUndoDirection::Undo, context);
}

std::expected<void, EditorUndoFailureCode> OutputGainEdit::redo(EditorEditContext& context) const
{
    return applyOutputGainEdit(*this, EditorUndoDirection::Redo, context);
}

std::string OutputGainEdit::label() const
{
    return "Set Output Gain";
}

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

    return m_entries[m_position - 1]->label();
}

std::optional<std::string> EditorUndoHistory::redoLabel() const
{
    if (!canRedo())
    {
        return std::nullopt;
    }

    return m_entries[m_position]->label();
}

// Appends a successfully-applied user edit and discards any no-longer-linear redo branch.
EditorUndoTransitionResult EditorUndoHistory::push(std::unique_ptr<IEdit> edit)
{
    if (m_pending.has_value())
    {
        return nonCommitFailure(EditorUndoFailureCode::TransitionAlreadyPending);
    }

    if (edit == nullptr)
    {
        return nonCommitFailure(EditorUndoFailureCode::PreflightRejected);
    }

    std::vector<EditorUndoEvent> events;
    truncateRedo(events);

    const std::string label = edit->label();
    m_entries.push_back(std::move(edit));
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
        .edit = m_entries[entry_index].get(),
    };

    return EditorUndoTransitionResult{
        .status = EditorUndoTransitionStatus::Pending,
        .events = {EditorUndoEvent{
            .type = is_undo ? EditorUndoEventType::UndoBegan : EditorUndoEventType::RedoBegan,
            .label = m_pending->edit->label(),
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

    const std::string label = pendingLabel();
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

    const std::string label = pendingLabel();
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

std::string EditorUndoHistory::pendingLabel() const
{
    if (!m_pending.has_value() || m_pending->edit == nullptr)
    {
        return {};
    }

    return m_pending->edit->label();
}

} // namespace rock_hero::editor::core
