/*!
\file busy_view_state.h
\brief Editor busy-overlay state owned by the controller and rendered by the view.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Semantic identity for an active editor busy operation.

Used by controller policy, tests, telemetry, and special-case behavior such as audio-device open
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

    /*! \brief Opening or reopening the audio-device route. */
    OpeningAudioDevice,

    /*! \brief Loading a plugin into the signal chain. */
    LoadingPlugin,

    /*! \brief Scanning plugin locations for browser plugins. */
    ScanningPlugins,

    /*! \brief Restoring a saved live rig chain during project load. */
    LoadingLiveRig,

    /*! \brief Rendering normalized backing audio during import or after an open-time prompt. */
    NormalizingBackingAudio,
};

/*!
\brief Visual treatment for the active busy operation.

Animated presentation is for operations whose work runs away from the message thread, letting JUCE
repaint normally. Blocking presentation is for operations that intentionally occupy the message
thread, where animated controls would freeze and misrepresent progress.
*/
enum class BusyPresentation : std::uint8_t
{
    /*! \brief Show a live progress bar while the message thread can repaint. */
    Animated,

    /*! \brief Show only static blocking text while the message thread may be occupied. */
    Blocking,
};

/*!
\brief Editor-wide busy state pushed to the view through EditorViewState.

The controller sets this state through beginBusy() and clears it through endBusy(). The view
renders the requested presentation, blocks input, displays message, and uses progress when a
workflow has entered a determinate phase.
*/
struct BusyViewState
{
    /*! \brief Semantic operation identity used by controller policy and tests. */
    BusyOperation operation;

    /*! \brief Canonical user-facing text rendered by the view. */
    std::string message;

    /*! \brief Visual treatment used by the view for this busy state. */
    BusyPresentation presentation{BusyPresentation::Animated};

    /*!
    \brief Optional determinate progress value from 0.0 to 1.0.

    When present, the view renders a percentage bar instead of an indeterminate animation. The
    open-project flow should set this only after it enters a countable phase such as restoring
    multiple plugins.
    */
    std::optional<double> progress{};

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

/*!
\brief Returns the default visual treatment for an operation.

\param operation Operation whose default presentation should be returned.
\return Default presentation for the operation.
*/
[[nodiscard]] BusyPresentation busyPresentation(BusyOperation operation) noexcept;

} // namespace rock_hero::editor::core
