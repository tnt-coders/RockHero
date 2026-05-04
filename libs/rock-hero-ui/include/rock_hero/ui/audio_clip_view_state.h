/*!
\file audio_clip_view_state.h
\brief Framework-free state for one audio clip view.
*/

#pragma once

#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/audio_clip.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::ui
{

/*!
\brief View-facing state for one rendered audio clip.

The clip view state preserves both source-file range and session timeline placement so the view
layer can grow toward multi-clip layout without re-querying Session from JUCE components.
*/
struct AudioClipViewState
{
    /*! \brief Stable id of the session audio clip represented by this view. */
    core::AudioClipId audio_clip_id;

    /*! \brief Audio asset whose thumbnail data should be rendered. */
    core::AudioAsset asset;

    /*! \brief Range inside the audio asset that this clip renders. */
    core::TimeRange source_range;

    /*! \brief Range occupied by this clip on the editor timeline. */
    core::TimeRange timeline_range;

    /*!
    \brief Compares two audio clip view states by their stored values.
    \param lhs Left-hand audio clip view state.
    \param rhs Right-hand audio clip view state.
    \return True when both audio clip view states store equal values.
    */
    friend bool operator==(const AudioClipViewState& lhs, const AudioClipViewState& rhs) = default;
};

} // namespace rock_hero::ui
