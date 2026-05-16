/*!
\file busy_view_state.h
\brief Editor busy-overlay state owned by the controller and rendered by the view.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Semantic identity for an active editor busy operation.

Used by controller policy, tests, telemetry, and special-case behavior such as audio-device apply
handling. The view should render BusyViewState::message rather than deriving its own copy from
this enum.
*/
enum class BusyOperation : std::uint8_t
{
    /*! \brief Opening an editor project package. */
    OpeningProject,

    /*! \brief Importing a song source into a new workspace. */
    ImportingProject,

    /*! \brief Saving the current project to its current destination. */
    SavingProject,

    /*! \brief Saving the current project to a user-chosen destination. */
    SavingProjectAs,

    /*! \brief Publishing the current project as a native song package. */
    PublishingProject,

    /*! \brief Applying an audio-device configuration change. */
    ChangingAudioDevice,

    /*! \brief Loading a plugin into the signal chain. */
    LoadingPlugin,
};

/*!
\brief Editor-wide busy state pushed to the view through EditorViewState.

The controller sets this state through beginBusy() and clears it through endBusy(). The view
renders an indeterminate spinner, blocks input, and displays message.
*/
struct BusyViewState
{
    /*! \brief Semantic operation identity used by controller policy and tests. */
    BusyOperation operation;

    /*! \brief Canonical user-facing text rendered by the view. */
    std::string message;

    /*! \brief Reserved for future cancellable operations; always false in the first pass. */
    bool cancel_enabled{false};

    /*!
    \brief Compares two busy view states by their stored values.
    \param lhs Left-hand busy view state.
    \param rhs Right-hand busy view state.
    \return True when both busy view states store equal values.
    */
    friend bool operator==(const BusyViewState& lhs, const BusyViewState& rhs) = default;
};

/*!
\brief Returns the default user-facing message for an operation.

Acts as the central source of default busy-overlay copy. The controller fills
BusyViewState::message from this helper at beginBusy() time so every entry point produces the
same text without each call site retyping it.

\param operation Operation whose default message should be returned.
\return Default message for the operation.
*/
[[nodiscard]] std::string busyMessage(BusyOperation operation);

} // namespace rock_hero::editor::core
