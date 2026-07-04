/*!
\file editor_view_state.h
\brief Headless editor view state used by the controller and view contracts.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/arrangement_view_state.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/editor_action_id.h>
#include <rock_hero/editor/core/plugin_browser_view_state.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>
#include <rock_hero/editor/core/transport_view_state.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

/*! \brief User choice returned from the unsaved-changes confirmation prompt. */
enum class UnsavedChangesDecision : std::uint8_t
{
    /*! \brief Save the current project before continuing the deferred action. */
    Save,

    /*! \brief Discard current project changes and continue the deferred action. */
    Discard,

    /*! \brief Cancel the deferred action and keep the current project unchanged. */
    Cancel,
};

/*! \brief User choice returned from the interrupted-restore recovery prompt. */
enum class RestoreInterruptedDecision : std::uint8_t
{
    /*! \brief Retry opening the project that was interrupted on the previous run. */
    Retry,

    /*! \brief Skip restoring the interrupted project and clear the recovery marker. */
    Cancel,
};

/*!
\brief Describes the unsaved-changes prompt the view should present.

The prompt only appears in view state when the controller has a deferred action waiting on the
user's decision, so prompted_action has no meaningful default; callers must initialize it
explicitly with the deferred action's identity.
*/
struct UnsavedChangesPrompt
{
    /*!
    \brief Creates a prompt request for a deferred action.
    \param action Action the prompt is currently about.
    */
    explicit constexpr UnsavedChangesPrompt(EditorActionId action) noexcept
        : prompted_action(action)
    {}

    /*! \brief Action the prompt is currently about; controls the prompt text. */
    EditorActionId prompted_action;

    /*!
    \brief Compares two prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const UnsavedChangesPrompt& lhs, const UnsavedChangesPrompt& rhs) =
        default;
};

/*!
\brief Describes a controller-requested Save As chooser.

Same as UnsavedChangesPrompt, prompted_action only exists when the chooser is being requested for
a known deferred action and must be initialized explicitly.
*/
struct SaveAsPrompt
{
    /*!
    \brief Creates a Save As prompt request for a deferred action.
    \param action Action the chooser will continue.
    */
    explicit constexpr SaveAsPrompt(EditorActionId action) noexcept
        : prompted_action(action)
    {}

    /*! \brief Action the chooser will continue once the user selects a save destination. */
    EditorActionId prompted_action;

    /*!
    \brief Compares two Save As prompt requests by their stored values.
    \param lhs Left-hand Save As prompt request.
    \param rhs Right-hand Save As prompt request.
    \return True when both Save As prompt requests store equal values.
    */
    friend bool operator==(const SaveAsPrompt& lhs, const SaveAsPrompt& rhs) = default;
};

/*!
\brief Describes a startup project restore that was interrupted on the previous run.

The view presents this prompt instead of auto-opening the same project again, giving the user a
way to avoid a repeated restore loop while keeping healthy startup restore automatic.
*/
struct RestoreInterruptedPrompt
{
    /*!
    \brief Creates an interrupted-restore prompt request.
    \param project_file_value Project package path that did not finish opening previously.
    */
    explicit RestoreInterruptedPrompt(std::filesystem::path project_file_value)
        : project_file(std::move(project_file_value))
    {}

    /*! \brief Project path that did not finish opening on the previous editor run. */
    std::filesystem::path project_file;

    /*!
    \brief Compares two interrupted-restore prompt requests by their stored paths.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal project paths.
    */
    friend bool operator==(
        const RestoreInterruptedPrompt& lhs, const RestoreInterruptedPrompt& rhs) = default;
};

/*! \brief Describes an active input calibration prompt requested by the controller. */
struct InputCalibrationPrompt
{
    /*! \brief Message shown by the calibration prompt. */
    std::string message;

    /*! \brief Input gain currently displayed by the calibration prompt. */
    double input_gain_db{0.0};

    /*!
    \brief Compares two input calibration prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const InputCalibrationPrompt& lhs, const InputCalibrationPrompt& rhs) =
        default;
};

/*!
\brief Full message-thread state rendered by the editor view.

The controller derives this state from transport, audio, and session information, then pushes it to
the concrete JUCE view through IEditorView.
*/
struct EditorViewState
{
    /*! \brief Enables or disables the File > Open command. */
    bool open_enabled{false};

    /*! \brief Enables or disables the File > Import command. */
    bool import_enabled{false};

    /*! \brief Enables or disables the File > Save command. */
    bool save_enabled{false};

    /*! \brief Enables or disables the File > Save As command. */
    bool save_as_enabled{false};

    /*! \brief Enables or disables the File > Publish command. */
    bool publish_enabled{false};

    /*! \brief Enables or disables the Edit > Undo command. */
    bool undo_enabled{false};

    /*! \brief Label for the undoable edit, if a command-specific label is available. */
    std::optional<std::string> undo_label{};

    /*! \brief Enables or disables the Edit > Redo command. */
    bool redo_enabled{false};

    /*! \brief Label for the redoable edit, if a command-specific label is available. */
    std::optional<std::string> redo_label{};

    /*! \brief Suggested .rock destination used to pre-fill the publish chooser. */
    std::filesystem::path suggested_publish_file{};

    /*! \brief Enables or disables the File > Close command. */
    bool close_enabled{false};

    /*! \brief Reports whether a project arrangement is currently loaded for display. */
    bool project_loaded{false};

    /*!
    \brief Monotonic id of the loaded project, bumped on each successful open/restore/import.

    The view recognizes a freshly loaded project by a change in this value versus the previously
    rendered state, and uses it to trigger one-shot load behavior such as centering the timeline on
    the restored cursor. It stays constant across non-load state pushes (so re-rendering identical
    state never re-triggers) and is left unchanged on a failed load.
    */
    std::uint64_t project_load_id{0};

    /*! \brief Selects whether File > Save should ask for a destination first. */
    bool save_requires_destination{false};

    /*! \brief Current transport state shown by the editor. */
    TransportViewState transport{};

    /*! \brief Menu-bar status text for the current audio-device route. */
    std::string audio_device_status_text{"[audio device closed]"};

    /*! \brief True when the controller has an audio-device backend available. */
    bool audio_devices_available{false};

    /*! \brief Enables or disables opening audio-device settings. */
    bool audio_device_settings_enabled{true};

    /*!
    \brief Visible timeline range used to map cursor position and waveform content to pixels.
    */
    common::core::TimeRange visible_timeline{};

    /*! \brief Song-level tempo map used to render the editor beat grid. */
    common::core::TempoMap tempo_map{};

    /*!
    \brief Grid step as a fraction of a whole note, shared by the track grid, ruler, and snapping.

    A 1/8 grid means eighth notes in every meter. Initialized to the quarter-note default because
    the Fraction default of 0/1 is a degenerate step.
    */
    common::core::Fraction grid_note_value{1, 4};

    /*! \brief Current arrangement waveform state shown by the editor. */
    ArrangementViewState arrangement{};

    /*! \brief Current signal-chain view state. */
    SignalChainViewState signal_chain{};

    /*! \brief Current plugin browser window state. */
    PluginBrowserViewState plugin_browser{};

    /*! \brief Unsaved-changes prompt to present, if the controller is awaiting a decision. */
    std::optional<UnsavedChangesPrompt> unsaved_changes_prompt{};

    /*! \brief Save As chooser request to present, if the controller needs a destination. */
    std::optional<SaveAsPrompt> save_as_prompt{};

    /*! \brief Interrupted startup restore prompt to present, if recovery input is needed. */
    std::optional<RestoreInterruptedPrompt> restore_interrupted_prompt{};

    /*! \brief Input calibration prompt to present, if live input setup is required. */
    std::optional<InputCalibrationPrompt> input_calibration_prompt{};

    /*!
    \brief Active editor-wide busy state, if any.

    When set, the view displays the busy overlay, blocks input, and the controller drops new
    intents until the operation completes or is superseded.
    */
    std::optional<BusyViewState> busy{};

    /*!
    \brief Compares two editor view states by their stored values.
    \param lhs Left-hand editor view state.
    \param rhs Right-hand editor view state.
    \return True when both editor view states store equal values.
    */
    friend bool operator==(const EditorViewState& lhs, const EditorViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
