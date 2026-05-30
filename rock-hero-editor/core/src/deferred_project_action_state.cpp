#include "deferred_project_action_state.h"

#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

bool DeferredProjectActionState::hasDeferredAction() const noexcept
{
    return !std::holds_alternative<Idle>(m_state);
}

// Enters the lifecycle at the unsaved-changes prompt with the action it will eventually replay.
void DeferredProjectActionState::defer(EditorAction::ProjectAction action)
{
    m_state = AwaitingUnsavedChangesDecision{std::move(action)};
}

// Converts the user's prompt answer into the next controller instruction without executing it.
DeferredProjectActionState::Resolution DeferredProjectActionState::resolveUnsavedChanges(
    UnsavedChangesDecision decision, bool save_requires_destination)
{
    auto* pending = std::get_if<AwaitingUnsavedChangesDecision>(&m_state);
    if (pending == nullptr)
    {
        m_state = Idle{};
        return Refresh{};
    }

    // Lift the action out before reassigning m_state so we never read a moved-from alternative.
    EditorAction::ProjectAction action = std::move(pending->action);
    switch (decision)
    {
        case UnsavedChangesDecision::Save:
        {
            if (save_requires_destination)
            {
                // Park the action behind the Save As chooser; nothing runs until the path arrives.
                m_state = AwaitingSaveAsPath{std::move(action)};
                return Refresh{};
            }

            m_state = SavingBeforeReplay{std::move(action)};
            return SaveThenReplay{};
        }
        case UnsavedChangesDecision::Discard:
        {
            m_state = Idle{};
            return DiscardAndReplay{.action = std::move(action)};
        }
        case UnsavedChangesDecision::Cancel:
        {
            m_state = Idle{};
            return Refresh{};
        }
    }

    m_state = Idle{};
    return Refresh{};
}

// Cancelling Save As drops the deferred project action because the save path cannot continue.
bool DeferredProjectActionState::cancelSaveAsPrompt() noexcept
{
    if (!std::holds_alternative<AwaitingSaveAsPath>(m_state))
    {
        return false;
    }

    m_state = Idle{};
    return true;
}

// Dismisses the Save As chooser once its path is supplied; the action waits out the save.
void DeferredProjectActionState::saveAsPathChosen() noexcept
{
    auto* awaiting = std::get_if<AwaitingSaveAsPath>(&m_state);
    if (awaiting == nullptr)
    {
        return;
    }

    m_state = SavingBeforeReplay{std::move(awaiting->action)};
}

// Releases the parked deferred action exactly once for post-save controller replay.
std::optional<EditorAction::ProjectAction> DeferredProjectActionState::takeReplay()
{
    auto* saving = std::get_if<SavingBeforeReplay>(&m_state);
    if (saving == nullptr)
    {
        return std::nullopt;
    }

    EditorAction::ProjectAction action = std::move(saving->action);
    m_state = Idle{};
    return action;
}

// Returns the state to the idle prompt-free baseline.
void DeferredProjectActionState::clear() noexcept
{
    m_state = Idle{};
}

// Projects the visible unsaved-changes prompt without exposing the stored action payload.
std::optional<UnsavedChangesPrompt> DeferredProjectActionState::unsavedChangesPrompt() const
{
    if (const auto* pending = std::get_if<AwaitingUnsavedChangesDecision>(&m_state))
    {
        return UnsavedChangesPrompt{idOf(pending->action)};
    }

    return std::nullopt;
}

// Projects the visible Save As prompt without exposing the stored action payload.
std::optional<SaveAsPrompt> DeferredProjectActionState::saveAsPrompt() const
{
    if (const auto* awaiting = std::get_if<AwaitingSaveAsPath>(&m_state))
    {
        return SaveAsPrompt{idOf(awaiting->action)};
    }

    return std::nullopt;
}

} // namespace rock_hero::editor::core
