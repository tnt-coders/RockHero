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

/*! \brief Vibrato wobble period in seconds (the reference's sin(t_ms * pi / 80) sine). */
inline constexpr double g_highway_vibrato_period_seconds = 0.160;

/*! \brief Tremolo wobble period in seconds (the reference's 60 ms triangle wave). */
inline constexpr double g_highway_tremolo_period_seconds = 0.060;

/*!
\brief Fraction of the tail duration over which wobble amplitude ramps in and out.

The reference modulates at full amplitude to the tail's very ends (its rails start and end off
the string line); the taper is this project's deliberate fix so rails always anchor on the
string line.
*/
inline constexpr double g_highway_tail_taper_fraction = 0.1;

/*!
\brief Returns the number of centerline samples a tail needs at a screen-space resolution.

Replaces the reference's per-millisecond tessellation: sampling density follows the tail's
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

Piecewise-linear through the bend points (the reference's interpolation): before the first
point the curve ramps from zero at the onset — unless the first point sits at the onset itself
(a prebend), which anchors the start value; after the last point the final value holds.

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
the board (the reference's outer-string bend inversion, restated in display space so it holds
for any string count and stacking order).

\param displayed_lane One-based displayed lane, 1 at the bottom of the stack.
\param string_count Number of displayed lanes.
\return True when the bend lift is inverted (downward).
*/
[[nodiscard]] bool highwayBendInverted(int displayed_lane, int string_count) noexcept;

/*!
\brief Returns the eased interpolation weight of a slide at a segment progress.

The reference's easing curves: pitched slides accelerate into the target
(sin(progress * pi / 2) cubed); unpitched slides release early (1 - sin((1 - progress) * pi / 2)).

\param progress Position within the slide segment in [0, 1]; values outside clamp.
\param unpitched True for the unpitched (pressure-release) easing.
\return Interpolation weight in [0, 1].
*/
[[nodiscard]] double highwaySlideEaseWeight(double progress, bool unpitched) noexcept;

/*!
\brief Returns the vibrato wobble at a time from the note onset, as a signed unit factor.

Onset-phased on purpose (the reference phases by absolute time, which desynchronizes repeats);
callers scale by the bend lift distance and the taper envelope.

\param seconds_from_onset Time since the note onset.
\return Wobble factor in [-1, 1].
*/
[[nodiscard]] double highwayVibratoWobble(double seconds_from_onset) noexcept;

/*!
\brief Returns the tremolo wobble at a time from the note onset, as a signed factor.

The reference's triangle wave, onset-phased; callers scale by the tail half-width and the taper
envelope.

\param seconds_from_onset Time since the note onset.
\return Wobble factor in [-0.75, 0.75].
*/
[[nodiscard]] double highwayTremoloWobble(double seconds_from_onset) noexcept;

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
