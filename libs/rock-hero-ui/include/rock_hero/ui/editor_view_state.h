/*!
\file editor_view_state.h
\brief Framework-free editor view state used by the controller and view contracts.
*/

#pragma once

#include <optional>
#include <rock_hero/core/timeline.h>
#include <rock_hero/ui/track_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::ui
{

/*!
\brief Full message-thread state rendered by the editor view.

The controller derives this state from transport, edit, and session information, then pushes it to
the concrete JUCE view through IEditorView.
*/
struct EditorViewState
{
    /*! \brief Enables or disables the load-audio command. */
    bool load_button_enabled{false};

    /*! \brief Enables or disables the play/pause command. */
    bool play_pause_enabled{false};

    /*! \brief Enables or disables the stop command. */
    bool stop_enabled{false};

    /*! \brief Selects whether the play/pause control should render a pause icon. */
    bool play_pause_shows_pause_icon{false};

    /*! \brief Start of the visible timeline range used to map cursor position to pixels. */
    core::TimePosition visible_timeline_start{};

    /*! \brief Duration of the visible timeline range used to map cursor position to pixels. */
    core::TimeDuration visible_timeline_duration{};

    /*! \brief Current waveform rows shown by the editor. */
    std::vector<TrackViewState> tracks;

    /*! \brief Most recent load error to display, if one should currently be shown. */
    std::optional<std::string> last_load_error;

    /*!
    \brief Compares two editor view states by their stored values.
    \param lhs Left-hand editor view state.
    \param rhs Right-hand editor view state.
    \return True when both editor view states store equal values.
    */
    friend bool operator==(const EditorViewState& lhs, const EditorViewState& rhs) = default;
};

} // namespace rock_hero::ui
