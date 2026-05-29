#include "deferred_project_action_state.h"

#include <utility>

namespace rock_hero::editor::core
{

bool DeferredProjectActionState::hasDeferredAction() const noexcept
{
    return m_deferred_action.has_value();
}

// Stores the project action and makes the unsaved-changes prompt visible.
void DeferredProjectActionState::defer(EditorAction::ProjectAction action)
{
    m_deferred_action = std::move(action);
    m_unsaved_changes_prompt_visible = true;
    m_save_as_prompt_visible = false;
}

// Converts the user's prompt answer into the next controller instruction without executing it.
DeferredProjectActionState::Resolution DeferredProjectActionState::resolveUnsavedChanges(
    UnsavedChangesDecision decision, bool save_requires_destination)
{
    if (!m_deferred_action.has_value())
    {
        clear();
        return {};
    }

    m_unsaved_changes_prompt_visible = false;
    switch (decision)
    {
        case UnsavedChangesDecision::Save:
        {
            if (save_requires_destination)
            {
                m_save_as_prompt_visible = true;
                return Resolution{
                    .outcome = Outcome::AwaitSaveAsPath,
                };
            }

            m_save_as_prompt_visible = false;
            return Resolution{
                .outcome = Outcome::SaveCurrentProject,
            };
        }
        case UnsavedChangesDecision::Discard:
        {
            return Resolution{
                .outcome = Outcome::DiscardChangesAndReplay,
                .replay = takeReplayUnchecked(),
            };
        }
        case UnsavedChangesDecision::Cancel:
        {
            clear();
            return Resolution{
                .outcome = Outcome::Cancelled,
            };
        }
    }

    return {};
}

// Cancelling Save As drops the deferred project action because the save path cannot continue.
bool DeferredProjectActionState::cancelSaveAsPrompt() noexcept
{
    if (!m_save_as_prompt_visible)
    {
        return false;
    }

    clear();
    return true;
}

// Releases a saved deferred action exactly once for post-save controller replay.
std::optional<DeferredProjectActionState::Replay> DeferredProjectActionState::takeReplay()
{
    if (!m_deferred_action.has_value())
    {
        return std::nullopt;
    }

    return takeReplayUnchecked();
}

// Returns the state to the idle prompt-free baseline.
void DeferredProjectActionState::clear() noexcept
{
    m_deferred_action.reset();
    m_unsaved_changes_prompt_visible = false;
    m_save_as_prompt_visible = false;
}

// Projects the visible unsaved-changes prompt without exposing the stored action payload.
std::optional<UnsavedChangesPrompt> DeferredProjectActionState::unsavedChangesPrompt() const
{
    if (!m_deferred_action.has_value() || !m_unsaved_changes_prompt_visible)
    {
        return std::nullopt;
    }

    return UnsavedChangesPrompt{idOf(*m_deferred_action)};
}

// Projects the visible Save As prompt without exposing the stored action payload.
std::optional<SaveAsPrompt> DeferredProjectActionState::saveAsPrompt() const
{
    if (!m_deferred_action.has_value() || !m_save_as_prompt_visible)
    {
        return std::nullopt;
    }

    return SaveAsPrompt{idOf(*m_deferred_action)};
}

// Moves the deferred action out after callers have already proven one exists.
std::optional<DeferredProjectActionState::Replay> DeferredProjectActionState::takeReplayUnchecked()
{
    EditorAction::ProjectAction action = std::move(*m_deferred_action);
    const EditorAction::Id action_id = idOf(action);
    clear();
    return Replay{
        .action_id = action_id,
        .action = std::move(action),
    };
}

} // namespace rock_hero::editor::core
