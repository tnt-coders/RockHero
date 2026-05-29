#include "deferred_editor_action_state.h"

#include <utility>

namespace rock_hero::editor::core
{

bool DeferredEditorActionState::hasDeferredAction() const noexcept
{
    return m_deferred_action.has_value();
}

void DeferredEditorActionState::defer(EditorAction::ProjectAction action)
{
    m_deferred_action = std::move(action);
    m_unsaved_changes_prompt_visible = true;
    m_save_as_prompt_visible = false;
}

DeferredEditorActionDecision DeferredEditorActionState::resolveUnsavedChanges(
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
                return DeferredEditorActionDecision{
                    .kind = DeferredEditorActionDecisionKind::AwaitSaveAsPath,
                };
            }

            m_save_as_prompt_visible = false;
            return DeferredEditorActionDecision{
                .kind = DeferredEditorActionDecisionKind::SaveCurrentProject,
            };
        }
        case UnsavedChangesDecision::Discard:
        {
            return DeferredEditorActionDecision{
                .kind = DeferredEditorActionDecisionKind::DiscardAndReplay,
                .replay = takeReplayActionUnchecked(),
            };
        }
        case UnsavedChangesDecision::Cancel:
        {
            clear();
            return DeferredEditorActionDecision{
                .kind = DeferredEditorActionDecisionKind::Cancelled,
            };
        }
    }

    return {};
}

bool DeferredEditorActionState::cancelSaveAsPrompt() noexcept
{
    if (!m_save_as_prompt_visible)
    {
        return false;
    }

    clear();
    return true;
}

std::optional<DeferredEditorActionReplay> DeferredEditorActionState::takeReplayAction()
{
    if (!m_deferred_action.has_value())
    {
        return std::nullopt;
    }

    return takeReplayActionUnchecked();
}

void DeferredEditorActionState::clear() noexcept
{
    m_deferred_action.reset();
    m_unsaved_changes_prompt_visible = false;
    m_save_as_prompt_visible = false;
}

std::optional<UnsavedChangesPrompt> DeferredEditorActionState::unsavedChangesPrompt() const
{
    if (!m_deferred_action.has_value() || !m_unsaved_changes_prompt_visible)
    {
        return std::nullopt;
    }

    return UnsavedChangesPrompt{idOf(*m_deferred_action)};
}

std::optional<SaveAsPrompt> DeferredEditorActionState::saveAsPrompt() const
{
    if (!m_deferred_action.has_value() || !m_save_as_prompt_visible)
    {
        return std::nullopt;
    }

    return SaveAsPrompt{idOf(*m_deferred_action)};
}

std::optional<DeferredEditorActionReplay> DeferredEditorActionState::takeReplayActionUnchecked()
{
    EditorAction::ProjectAction action = std::move(*m_deferred_action);
    const EditorAction::Id action_id = idOf(action);
    clear();
    return DeferredEditorActionReplay{
        .action_id = action_id,
        .action = std::move(action),
    };
}

} // namespace rock_hero::editor::core
