/*!
\file tone_schedule.h
\brief Seconds-resolved tone switch schedule derived from an arrangement's tone track.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One seconds-resolved span of the tone timeline during which a single tone is audible.

This is the playback-facing projection of an authored ToneRegion: musical endpoints resolved
through the tempo map, with the gap-hold, lead-in, and terminal rules already applied so
consumers never re-derive scheduling policy.
*/
struct ToneSwitchRegion
{
    /*! \brief Edit-timeline span during which the referenced tone is audible. */
    TimeRange time_range{};

    /*! \brief Package-relative tone document reference interpreted by common/audio. */
    std::string tone_document_ref;

    /*!
    \brief Compares two switch regions by their stored fields.
    \param lhs Left-hand switch region.
    \param rhs Right-hand switch region.
    \return True when both regions store equal values.
    */
    friend bool operator==(const ToneSwitchRegion& lhs, const ToneSwitchRegion& rhs) = default;
};

/*!
\brief Builds the gap-free, seconds-resolved tone switch schedule for one arrangement.

Scheduling policy applied, matching the editor's display projection and the tone-track domain
rules (tone_track.h):

- The first region owns the lead-in: it extends back to the timeline origin, because no
  one-based grid position can address time before measure 1.
- A gap between authored regions holds the previous region's tone, so each span's end extends
  to the next span's start.
- The final span extends to the end of the loaded content (`song_length`), clamping an authored
  end that overshoots the content; playback past the content cannot occur.
- An empty tone track produces an empty schedule (the arrangement has no tones to switch).

\param tone_track Authored tone regions in ascending start order (the ToneTrack invariant).
\param tempo_map Tempo map used to resolve musical endpoints to seconds.
\param song_length Full duration of the arrangement's backing content.
\return Contiguous seconds-resolved switch regions covering [0, song_length], or empty.
*/
[[nodiscard]] std::vector<ToneSwitchRegion> makeToneSchedule(
    const ToneTrack& tone_track, const TempoMap& tempo_map, TimeDuration song_length);

/*!
\brief Length of the linear crossfade at every tone switch boundary, in seconds.

Matches the authored 5-10 ms click-free ramp the tone system uses everywhere; the per-sample
smoothing in the branch gain plugin is tuned at or below this so the smoother never stretches
the baked crossfade.
*/
inline constexpr double g_tone_switch_ramp_seconds{0.01};

/*! \brief One point of a tone's baked gain envelope: linear branch audibility over time. */
struct ToneGainPoint
{
    /*! \brief Edit-timeline position of this envelope point. */
    double seconds{0.0};

    /*! \brief Linear branch audibility in [0, 1]: 1 = audible tone, 0 = silent. */
    float gain{0.0F};

    /*!
    \brief Compares two envelope points by their stored fields.
    \param lhs Left-hand envelope point.
    \param rhs Right-hand envelope point.
    \return True when both points store equal values.
    */
    friend bool operator==(const ToneGainPoint& lhs, const ToneGainPoint& rhs) = default;
};

/*!
\brief Builds one tone's gain envelope from the switch schedule.

Pure scheduling policy, shared so the playback backend is a thin point-writing adapter:

- The envelope always starts with an explicit point at the timeline origin (1 when the schedule
  opens on this tone, else 0), so a tone that is never referenced yields exactly one silent
  point.
- Every boundary where the audible tone changes bakes a linear crossfade of
  `ramp_seconds` starting at the boundary: the outgoing tone holds 1 at the boundary and
  reaches 0 at boundary + ramp; the incoming tone mirrors it. Boundaries between two spans of
  the SAME tone bake nothing (no dip to silence on a re-strike of the same tone).
- The ramp is clamped to half the incoming span so back-to-back short spans cannot overlap
  their crossfades.

\param schedule Contiguous switch regions from makeToneSchedule.
\param tone_document_ref Tone whose envelope to build.
\param ramp_seconds Crossfade length at each switch boundary.
\return Envelope points in ascending time order; never empty.
*/
[[nodiscard]] std::vector<ToneGainPoint> makeToneGainEnvelope(
    std::span<const ToneSwitchRegion> schedule, const std::string& tone_document_ref,
    double ramp_seconds);

} // namespace rock_hero::common::core
