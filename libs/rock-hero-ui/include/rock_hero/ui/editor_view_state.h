/*!
\file editor_view_state.h
\brief Framework-free editor view state used by the controller and view contracts.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/arrangement_view_state.h>
#include <string>

namespace rock_hero::ui
{

/*! \brief Project-level action waiting for unsaved-change confirmation. */
enum class PendingProjectAction : std::uint8_t
{
    Close,
    Open,
    Import,
    Exit,
};

/*! \brief User choice returned from the unsaved-changes confirmation prompt. */
enum class UnsavedChangesDecision : std::uint8_t
{
    Save,
    Discard,
    Cancel,
};

/*! \brief Describes the unsaved-changes prompt the view should present. */
struct UnsavedChangesPrompt
{
    /*! \brief Action that will continue if the user saves or discards changes. */
    PendingProjectAction action{PendingProjectAction::Close};

    /*!
    \brief Compares two prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const UnsavedChangesPrompt& lhs, const UnsavedChangesPrompt& rhs) =
        default;
};

/*! \brief Describes a controller-requested Save As chooser. */
struct SaveAsPrompt
{
    /*! \brief Action that will continue after the user selects a save destination. */
    PendingProjectAction action{PendingProjectAction::Close};

    /*!
    \brief Compares two Save As prompt requests by their stored values.
    \param lhs Left-hand Save As prompt request.
    \param rhs Right-hand Save As prompt request.
    \return True when both Save As prompt requests store equal values.
    */
    friend bool operator==(const SaveAsPrompt& lhs, const SaveAsPrompt& rhs) = default;
};

/*!
\brief Full message-thread state rendered by the editor view.

The controller derives this state from transport, edit, and session information, then pushes it to
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

    /*!
    \brief Visible timeline range used to map cursor position and waveform content to pixels.
    */
    core::TimeRange visible_timeline{};

    /*! \brief Current arrangement waveform state shown by the editor. */
    ArrangementViewState arrangement;

    /*! \brief Most recent workflow error to display, if one should currently be shown. */
    std::optional<std::string> last_error;

    /*! \brief Unsaved-changes prompt to present, if the controller is awaiting a decision. */
    std::optional<UnsavedChangesPrompt> unsaved_changes_prompt;

    /*! \brief Save As chooser request to present, if the controller needs a destination. */
    std::optional<SaveAsPrompt> save_as_prompt;

    /*!
    \brief Compares two editor view states by their stored values.
    \param lhs Left-hand editor view state.
    \param rhs Right-hand editor view state.
    \return True when both editor view states store equal values.
    */
    friend bool operator==(const EditorViewState& lhs, const EditorViewState& rhs) = default;
};

} // namespace rock_hero::ui
