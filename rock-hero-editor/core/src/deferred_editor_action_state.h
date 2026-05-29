/*!
\file deferred_editor_action_state.h
\brief Headless state for project actions deferred by editor prompts.
*/

#pragma once

#include "editor_action.h"

#include <cstdint>
#include <optional>

namespace rock_hero::editor::core
{

/*! \brief High-level outcome after resolving deferred editor-action state. */
enum class DeferredEditorActionDecisionKind : std::uint8_t
{
    None,
    SaveCurrentProject,
    AwaitSaveAsPath,
    DiscardAndReplay,
    Cancelled,
};

/*! \brief Deferred project action released for controller-side replay. */
struct DeferredEditorActionReplay
{
    EditorAction::Id action_id;
    EditorAction::ProjectAction action;
};

/*! \brief Decision returned by deferred-action prompt state. */
struct DeferredEditorActionDecision
{
    DeferredEditorActionDecisionKind kind{DeferredEditorActionDecisionKind::None};
    std::optional<DeferredEditorActionReplay> replay{};
};

/*!
\brief Owns project-action deferral and prompt visibility state.

The root controller supplies facts such as dirty state and Save As destination requirements, then
executes the returned decision. This type does not close projects, save files, update settings, or
call any editor ports.
*/
class DeferredEditorActionState final
{
public:
    /*! \brief Reports whether a project action is waiting behind a prompt. */
    [[nodiscard]] bool hasDeferredAction() const noexcept;

    /*!
    \brief Defers a project action behind an unsaved-changes prompt.
    \param action Project action to replay after prompt resolution.
    */
    void defer(EditorAction::ProjectAction action);

    /*!
    \brief Resolves the unsaved-changes prompt and returns the requested controller action.
    \param decision User-selected prompt decision.
    \param save_requires_destination True when Save must first ask for a Save As path.
    \return Decision for the root controller to execute.
    */
    [[nodiscard]] DeferredEditorActionDecision resolveUnsavedChanges(
        UnsavedChangesDecision decision, bool save_requires_destination);

    /*!
    \brief Cancels an active Save As prompt.
    \return True when a prompt was active and was cleared.
    */
    [[nodiscard]] bool cancelSaveAsPrompt() noexcept;

    /*!
    \brief Releases the deferred action for replay after a successful save.
    \return Replay action, or empty when nothing is deferred.
    */
    [[nodiscard]] std::optional<DeferredEditorActionReplay> takeReplayAction();

    /*! \brief Clears all deferred action and prompt state. */
    void clear() noexcept;

    /*!
    \brief Builds the unsaved-changes prompt snapshot.
    \return Prompt state, or empty when no unsaved-changes prompt is visible.
    */
    [[nodiscard]] std::optional<UnsavedChangesPrompt> unsavedChangesPrompt() const;

    /*!
    \brief Builds the Save As prompt snapshot.
    \return Prompt state, or empty when no Save As prompt is visible.
    */
    [[nodiscard]] std::optional<SaveAsPrompt> saveAsPrompt() const;

private:
    [[nodiscard]] std::optional<DeferredEditorActionReplay> takeReplayActionUnchecked();

    std::optional<EditorAction::ProjectAction> m_deferred_action{};
    bool m_unsaved_changes_prompt_visible{false};
    bool m_save_as_prompt_visible{false};
};

} // namespace rock_hero::editor::core
