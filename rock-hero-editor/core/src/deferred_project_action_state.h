/*!
\file deferred_project_action_state.h
\brief Headless state for project actions deferred by editor prompts.
*/

#pragma once

#include "editor_action.h"

#include <optional>
#include <variant>

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
    /*! \brief Refresh the view; no project action follows from prompt resolution. */
    struct Refresh
    {
    };

    /*! \brief Save the current project now; the deferred action replays after the save succeeds. */
    struct SaveThenReplay
    {
    };

    /*! \brief Discard the current project's changes and replay the deferred action immediately. */
    struct DiscardAndReplay
    {
        /*! \brief Released deferred action to replay now; its identity comes from idOf(action). */
        EditorAction::ProjectAction action;
    };

    /*!
    \brief Next controller step after the user answers a deferred-action prompt.

    A sum type so each step carries exactly the data it needs: only DiscardAndReplay releases the
    deferred action for immediate replay, while SaveThenReplay leaves it stored for post-save
    replay and Refresh asks only for a view refresh. This keeps illegal pairings (such as a replay
    action attached to a save or refresh step) unrepresentable.
    */
    using Resolution = std::variant<Refresh, SaveThenReplay, DiscardAndReplay>;

    /*!
    \brief Reports whether a project action is waiting behind a prompt.
    \return True when a deferred action is stored.
    */
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
    \brief Advances a deferred action past the Save As chooser once its path has been supplied.

    Moves the action out of the Save As prompt phase so the chooser stops showing while its
    protective save runs; the action stays parked for replay after the save succeeds. Does nothing
    when no deferred action is waiting on a Save As path.
    */
    void saveAsPathChosen() noexcept;

    /*!
    \brief Releases the deferred action for replay after a successful save.
    \return Deferred project action, or empty unless an action is parked for post-save replay.
    */
    [[nodiscard]] std::optional<EditorAction::ProjectAction> takeReplay();

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
    /*! \brief No project action is deferred. */
    struct Idle
    {
    };

    /*! \brief A deferred action is waiting on the unsaved-changes prompt. */
    struct AwaitingUnsavedChangesDecision
    {
        EditorAction::ProjectAction action;
    };

    /*! \brief A deferred action is waiting on the user to supply a Save As path. */
    struct AwaitingSaveAsPath
    {
        EditorAction::ProjectAction action;
    };

    /*! \brief A deferred action is parked while its protective save runs, ready to replay. */
    struct SavingBeforeReplay
    {
        EditorAction::ProjectAction action;
    };

    /*!
    \brief Lifecycle phase of a deferred project action.

    Each non-idle phase carries the action it concerns, so "which action is waiting" and "which
    prompt is showing" are one value that changes shape together. Illegal combinations the old
    optional-plus-two-bools storage allowed -- both prompts visible at once, or a prompt visible
    with no action behind it -- cannot be represented.
    */
    using DeferralState =
        std::variant<Idle, AwaitingUnsavedChangesDecision, AwaitingSaveAsPath, SavingBeforeReplay>;

    DeferralState m_state{Idle{}};
};

} // namespace rock_hero::editor::core
