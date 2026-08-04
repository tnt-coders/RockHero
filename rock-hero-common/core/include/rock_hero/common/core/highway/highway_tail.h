/*!
\file highway_tail.h
\brief Pure sustain-tail math: adaptive sampling, taper envelopes, and technique modulation.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <span>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Fallback vibrato wobble period in seconds — the sixteenth note at 120 BPM.

The drawn vibrato completes one full wobble per sixteenth note of the song grid
(highwayVibratoPeriodSeconds), so the wobble breathes with the song's tempo instead of a
fixed wall-clock rate (the prior 160 ms sine read too frantic; sixteenths sighted as the
sweet spot 2026-08-02). This constant only covers grids that yield no beat interval around
the onset.
*/
inline constexpr double g_highway_vibrato_period_seconds = 0.125;

/*!
\brief Vibrato wobble depth in semitones of bend lift — an eighth of a step each way.

A quarter of a semitone (sighted direction 2026-08-02, halved from the first-pass quarter
bend). Drawing the wobble at the unit factor's full swing (±1 semitone of lift) reads as a
whammy dive, not a vibrato. Callers multiply this into the wobble factor when converting it
to bend-lift semitones.
*/
inline constexpr double g_highway_vibrato_depth_semitones = 0.25;

/*!
\brief The head's vibrato swing as a fraction of the tail's depth.

Half the tail's eighth-step swing — a sixteenth of a step each way. A fully pinned head
looked odd against the wobbling tail and a full-depth head bounced (sighted 2026-08-02);
the head breathing at half depth keeps it visibly alive while the tail carries the motion.
*/
inline constexpr double g_highway_vibrato_head_depth_fraction = 0.5;

/*! \brief Tremolo wobble period in seconds (the source game's 60 ms triangle wave). */
inline constexpr double g_highway_tremolo_period_seconds = 0.060;

/*!
\brief Fraction of the tail duration over which wobble amplitude ramps in and out.

The source game modulates at full amplitude to the tail's very ends (its rails start and end off
the string line); the taper is this project's deliberate fix so rails always anchor on the
string line.
*/
inline constexpr double g_highway_tail_taper_fraction = 0.1;

/*!
\brief Returns the number of centerline samples a tail needs at a screen-space resolution.

Replaces the source game's per-millisecond tessellation: sampling density follows the tail's
projected on-screen length, so a long sustain costs vertices proportional to its visible size,
never its duration.

\param projected_length_pixels On-screen length of the tail between its endpoints.
\param pixels_per_sample Target screen-space distance between samples.
\param sample_cap Hard upper bound on the sample count.
\return Sample count in [2, sample_cap].
*/
[[nodiscard]] std::size_t highwayTailSampleCount(
    double projected_length_pixels, double pixels_per_sample, std::size_t sample_cap) noexcept;

/*!
\brief Returns the wobble amplitude envelope at a position along the tail.

Zero at both ends, ramping linearly to one over the taper fraction, so modulated rails start
and end exactly on the string line.

\param progress Position along the tail in [0, 1]; values outside clamp.
\param taper_fraction Fraction of the tail duration each ramp covers; clamped to (0, 0.5].
\return Amplitude scale in [0, 1].
*/
[[nodiscard]] double highwayTailTaper(double progress, double taper_fraction) noexcept;

/*!
\brief Evaluates a note's bend curve at an absolute time.

Monotone cubic Hermite interpolation with Fritsch–Carlson tangents (the standard
shape-preserving interpolant) through the control points: where consecutive segments move in
the same direction the curve flows THROUGH the control point with continuous nonzero velocity
— the way a real bending finger passes an intermediate target — instead of easing to a flat
shelf at every point (the previous per-segment smoothstep, whose terraced look read rigid and
mechanical on multi-stage bends, user report 2026-07-28). Plateaus and direction reversals
still get an exactly flat tangent, and the Fritsch–Carlson limits guarantee no overshoot past
any control value. The curve starts and settles at rest: zero tangent at the first point
(easing from zero at the onset — unless the first point sits at the onset itself, a prebend,
which anchors the start value) and at the last point, whose value then holds.

\param bend Bend curve points in ascending time order.
\param onset_seconds The note's onset time (the zero anchor for the pre-first-point ramp).
\param seconds Absolute time to evaluate at.
\return Bend amount in semitones; zero when the curve is empty.
*/
[[nodiscard]] double highwayBendSemitonesAt(
    std::span<const HighwayBendPointView> bend, double onset_seconds, double seconds) noexcept;

/*!
\brief Returns whether a note's bend lift points downward on a displayed lane.

Bends on the upper half of the displayed string stack curve downward so the curve stays inside
the board (the source game's outer-string bend inversion, restated in display space so it holds
for any string count and stacking order).

\param displayed_lane One-based displayed lane, 1 at the bottom of the stack.
\param string_count Number of displayed lanes.
\return True when the bend lift is inverted (downward).
*/
[[nodiscard]] bool highwayBendInverted(int displayed_lane, int string_count) noexcept;

/*!
\brief Returns the eased interpolation weight of a slide at a segment progress.

The source game's easing curves: pitched slides accelerate into the target
(sin(progress * pi / 2) cubed); unpitched slides release early (1 - sin((1 - progress) * pi / 2)).

\param progress Position within the slide segment in [0, 1]; values outside clamp.
\param unpitched True for the unpitched (pressure-release) easing.
\return Interpolation weight in [0, 1].
*/
[[nodiscard]] double highwaySlideEaseWeight(double progress, bool unpitched) noexcept;

/*!
\brief Returns the vibrato wobble period at an onset: one full wobble per sixteenth note.

A quarter of the song-grid beat interval containing the onset (the nearest interval when
the onset falls outside the grid), so the wobble tracks the song's tempo; falls back to
g_highway_vibrato_period_seconds when the grid yields no positive interval.

\param beats The song grid beats in ascending order.
\param onset_seconds The note onset.
\return Period in seconds, always positive.
*/
[[nodiscard]] double highwayVibratoPeriodSeconds(
    std::span<const HighwayBeatView> beats, double onset_seconds) noexcept;

/*!
\brief Returns the vibrato wobble at a time from the note onset, as a signed unit factor.

Onset-phased on purpose (absolute-time phasing desynchronizes repeated notes); callers scale
by the bend lift distance and the taper envelope.

\param seconds_from_onset Time since the note onset.
\param period_seconds Wobble period, from highwayVibratoPeriodSeconds.
\return Wobble factor in [-1, 1].
*/
[[nodiscard]] double highwayVibratoWobble(
    double seconds_from_onset, double period_seconds) noexcept;

/*!
\brief Returns the tremolo wobble at a time from the note onset, as a signed factor.

The source game's triangle wave, onset-phased; callers scale by the tail half-width and the taper
envelope.

\param seconds_from_onset Time since the note onset.
\return Wobble factor in [-0.75, 0.75].
*/
[[nodiscard]] double highwayTremoloWobble(double seconds_from_onset) noexcept;

/*! \brief Scrape wobble period at a leg's start — the noise ribbon's largest tooth. */
inline constexpr double g_highway_scrape_period_seconds = 0.030;

/*! \brief Scrape wobble depth at a leg's start, as a fraction of the tail half width. */
inline constexpr double g_highway_scrape_depth = 1.2;

/*! \brief Fraction of the scrape wobble's depth and frequency remaining at a leg's end. */
inline constexpr double g_highway_scrape_decay_floor = 0.5;

/*!
\brief Returns the scrape's noise wobble at a time, as a signed factor.

A deep chirped triangle wave (the tremolo family's teeth, oversized), leg-phased along the
note's waypoint path: amplitude and frequency decay to the floor across each leg and restart
at the next — the pick re-bites on a direction change. Callers scale by the tail half-width
and the taper envelope. (A second incommensurate grit layer was tried and reverted on sight:
it read as random noise, not scraping.)

\param note The scrape note whose waypoint path phases the wobble.
\param seconds Absolute sample time.
\return Wobble factor within plus-or-minus g_highway_scrape_depth.
*/
[[nodiscard]] double highwayScrapeWobble(const HighwayNoteView& note, double seconds) noexcept;

/*!
\brief Builds the ascending sample times for one tail's visible span.

Uniform samples cover the span at the requested count, and every bend point and slide waypoint
inside the span is included exactly, so piecewise-linear technique curves hit their control
points instead of aliasing across them.

\param note The note whose bend and slide times are folded in.
\param from_seconds Visible span start (already clamped to the hit line by the caller).
\param to_seconds Visible span end.
\param uniform_count Uniform sample count from highwayTailSampleCount.
\return Ascending, deduplicated sample times spanning [from_seconds, to_seconds]; empty when
        the span is empty.
*/
[[nodiscard]] std::vector<double> makeHighwayTailSampleTimes(
    const HighwayNoteView& note, double from_seconds, double to_seconds, std::size_t uniform_count);

} // namespace rock_hero::common::core
