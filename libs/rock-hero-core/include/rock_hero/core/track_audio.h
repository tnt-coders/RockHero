/*!
\file track_audio.h
\brief Full-source audio value assigned to an editable session track.
*/

#pragma once

#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::core
{

/*! \brief Full audio asset assigned to one session track. */
struct TrackAudio
{
    /*! \brief Audio asset referenced by the track. */
    AudioAsset asset;

    /*! \brief Full natural duration of the referenced asset. */
    TimeDuration duration;

    /*!
    \brief Calculates the range occupied by this audio on the session timeline.
    \return Timeline range from zero through the asset duration.
    */
    [[nodiscard]] constexpr TimeRange timelineRange() const noexcept
    {
        return TimeRange{
            .start = TimePosition{},
            .end = TimePosition{duration.seconds},
        };
    }

    /*!
    \brief Compares two track-audio values by asset and duration.
    \param lhs Left-hand track-audio value.
    \param rhs Right-hand track-audio value.
    \return True when both values store equal fields.
    */
    friend bool operator==(const TrackAudio& lhs, const TrackAudio& rhs) = default;
};

} // namespace rock_hero::core
