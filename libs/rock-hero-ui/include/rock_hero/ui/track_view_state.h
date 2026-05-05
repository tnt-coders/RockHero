/*!
\file track_view_state.h
\brief Framework-free per-track view state used by editor view contracts.
*/

#pragma once

#include <rock_hero/core/track.h>
#include <rock_hero/ui/audio_clip_view_state.h>
#include <string>
#include <vector>

namespace rock_hero::ui
{

/*!
\brief View-facing state for one track in the editor.

This state stays focused on the information the current editor UI can actually render. In the
current stage that is still waveform-centric, but the type name stays track-oriented so the view
layer can grow without another naming reset.
*/
struct TrackViewState
{
    /*! \brief Stable id of the session track represented by this view. */
    core::TrackId track_id;

    /*! \brief User-visible label shown for the track. */
    std::string display_name;

    /*! \brief Audio clips currently rendered in this track row. */
    std::vector<AudioClipViewState> audio_clips;

    /*!
    \brief Compares two track-view states by their stored values.
    \param lhs Left-hand track-view state.
    \param rhs Right-hand track-view state.
    \return True when both track-view states store equal values.
    */
    friend bool operator==(const TrackViewState& lhs, const TrackViewState& rhs) = default;
};

} // namespace rock_hero::ui
