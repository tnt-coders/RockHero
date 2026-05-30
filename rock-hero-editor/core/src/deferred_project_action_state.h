/*!
\file deferred_project_action_state.h
\brief Headless state for project actions deferred by editor prompts.
*/

#pragma once

#include "editor_action.h"

#include <cstdint>
#include <optional>

namespace rock_hero::editor::core
{

/*!
\brief Owns project-action deferral and prompt visibility state.

The root controller supplies facts such as dirty state and Save As destination requirements, then
executes the returned resolution. This type does not close projects, save files, update settings,
or call any editor ports.
*/
class DeferredProjectActionState final
{
public:
    /*! \brief High-level controller response after resolving deferred project-action state. */
    enum class Outcome : std::uint8_t
    {
        /*! \brief No controller action is needed. */
        None,

        /*! \brief Save the current project before replaying the deferred project action. */
        SaveCurrentProject,

        /*! \brief Ask the user for a Save As destination before saving and replaying. */
        AwaitSaveAsPath,

        /*! \brief Discard current changes and replay the deferred project action now. */
        DiscardChangesAndReplay,

        /*! \brief Drop the deferred project action and leave the current project unchanged. */
        Cancelled,
    };

    /*! \brief Deferred project action released for controller-side replay. */
    struct Replay
    {
        /*! \brief Stable identity used by controller policy before replaying the action. */
        EditorAction::Id action_id;

        /*! \brief Original project action to replay after prompt resolution or save success. */
        EditorAction::ProjectAction action;
    };

    /*! \brief Resolution returned after the user answers a deferred-action prompt. */
    struct Resolution
    {
        /*! \brief Controller action requested by prompt resolution. */
        Outcome outcome{Outcome::None};

        /*! \brief Deferred action to replay when the outcome requires immediate replay. */
        std::optional<Replay> replay{};
    };

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
    \return Resolution for the root controller to execute.
    */
    [[nodiscard]] Resolution resolveUnsavedChanges(
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
    [[nodiscard]] std::optional<Replay> takeReplay();

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
    [[nodiscard]] std::optional<Replay> takeReplayUnchecked();

    std::optional<EditorAction::ProjectAction> m_deferred_action{};
    bool m_unsaved_changes_prompt_visible{false};
    bool m_save_as_prompt_visible{false};
};

} // namespace rock_hero::editor::core
