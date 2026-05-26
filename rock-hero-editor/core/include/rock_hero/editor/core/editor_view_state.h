/*!
\file editor_view_state.h
\brief Headless editor view state used by the controller and view contracts.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/arrangement_view_state.h>
#include <rock_hero/editor/core/audio_device_status_text.h>
#include <rock_hero/editor/core/busy_view_state.h>
#include <rock_hero/editor/core/editor_action_id.h>
#include <rock_hero/editor/core/plugin_browser_view_state.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

/*! \brief User choice returned from the unsaved-changes confirmation prompt. */
enum class UnsavedChangesDecision : std::uint8_t
{
    Save,
    Discard,
    Cancel,
};

/*! \brief User choice returned from the interrupted-restore recovery prompt. */
enum class RestoreInterruptedDecision : std::uint8_t
{
    Retry,
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

    /*! \brief Suggested .rock destination used to pre-fill the publish chooser. */
    std::filesystem::path suggested_publish_file{};

    /*! \brief Enables or disables the File > Close command. */
    bool close_enabled{false};

    /*! \brief Reports whether a project arrangement is currently loaded for display. */
    bool project_loaded{false};

    /*! \brief Selects whether File > Save should ask for a destination first. */
    bool save_requires_destination{false};

    /*! \brief Enables or disables the play/pause command. */
    bool play_pause_enabled{false};

    /*! \brief Enables or disables the stop command. */
    bool stop_enabled{false};

    /*! \brief Selects whether the play/pause control should render a pause icon. */
    bool play_pause_shows_pause_icon{false};

    /*! \brief Menu-bar status text for the current audio-device route. */
    std::string audio_device_status_text{g_closed_audio_device_text};

    /*! \brief True when the controller has an audio-device backend available. */
    bool audio_devices_available{false};

    /*! \brief Enables or disables opening audio-device settings. */
    bool audio_device_settings_enabled{true};

    /*!
    \brief Visible timeline range used to map cursor position and waveform content to pixels.
    */
    common::core::TimeRange visible_timeline{};

    /*! \brief Current arrangement waveform state shown by the editor. */
    ArrangementViewState arrangement;

    /*! \brief Current signal-chain panel state. */
    SignalChainViewState signal_chain;

    /*! \brief Current plugin browser window state. */
    PluginBrowserViewState plugin_browser;

    /*! \brief Unsaved-changes prompt to present, if the controller is awaiting a decision. */
    std::optional<UnsavedChangesPrompt> unsaved_changes_prompt;

    /*! \brief Save As chooser request to present, if the controller needs a destination. */
    std::optional<SaveAsPrompt> save_as_prompt;

    /*! \brief Interrupted startup restore prompt to present, if recovery input is needed. */
    std::optional<RestoreInterruptedPrompt> restore_interrupted_prompt;

    /*! \brief Input calibration prompt to present, if live input setup is required. */
    std::optional<InputCalibrationPrompt> input_calibration_prompt;

    /*!
    \brief Active editor-wide busy state, if any.

    When set, the view displays the busy overlay, blocks input, and the controller drops new
    intents until the operation completes or is superseded.
    */
    std::optional<BusyViewState> busy;

    /*!
    \brief Compares two editor view states by their stored values.
    \param lhs Left-hand editor view state.
    \param rhs Right-hand editor view state.
    \return True when both editor view states store equal values.
    */
    friend bool operator==(const EditorViewState& lhs, const EditorViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
