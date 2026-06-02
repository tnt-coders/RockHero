/*!
\file busy_view_state.h
\brief Editor busy-overlay render state consumed by the view.
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

    /*! \brief Analyzing backing audio while validating normalization metadata. */
    AnalyzingBackingAudio,
};

/*!
\brief Progress indicator shown by the busy overlay for the active operation.

Indeterminate progress is for work that can repaint but does not expose a known fraction.
Determinate progress is for countable phases that report a current fraction. Message-only mode is
for operations that occupy the message thread, where a progress bar would freeze and misrepresent
progress.
*/
enum class BusyIndicator : std::uint8_t
{
    /*! \brief Show an animated progress bar without a known fraction. */
    IndeterminateProgress,

    /*! \brief Show a progress bar with a known fraction. */
    DeterminateProgress,

    /*! \brief Show only the busy message with no progress bar. */
    MessageOnly,
};

/*!
\brief Editor-wide busy state pushed to the view through EditorViewState.

The root controller pushes this snapshot through EditorViewState. The view renders the requested
indicator, blocks input, displays message, and uses progress when busy state has entered a
determinate phase.
*/
struct BusyViewState
{
    /*! \brief Semantic operation identity used by controller policy and tests. */
    BusyOperation operation;

    /*! \brief Canonical user-facing text rendered by the view. */
    std::string message;

    /*! \brief Progress indicator rendered by the busy overlay. */
    BusyIndicator indicator{BusyIndicator::IndeterminateProgress};

    /*!
    \brief Optional determinate progress value from 0.0 to 1.0.

    Used by BusyIndicator::DeterminateProgress. The open-project flow should set this only after
    it enters a countable phase such as restoring multiple plugins.
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

Acts as the central source of default busy-overlay copy. Busy-state projection fills
BusyViewState::message from this helper so every entry point produces the same text without each
call site retyping it.

\param operation Operation whose default message should be returned.
\return Default message for the operation.
*/
[[nodiscard]] std::string busyMessage(BusyOperation operation);

/*!
\brief Returns the default busy-overlay indicator for an operation.

\param operation Operation whose default indicator should be returned.
\return Default indicator for the operation.
*/
[[nodiscard]] BusyIndicator busyIndicator(BusyOperation operation) noexcept;

} // namespace rock_hero::editor::core
