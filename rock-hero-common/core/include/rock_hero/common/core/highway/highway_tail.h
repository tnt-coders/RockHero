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
fixed wall-clock rate (the prior 160 ms sine read too frantic). This constant only covers
grids that yield no beat interval around the onset.
*/
inline constexpr double g_highway_vibrato_period_seconds = 0.125;

/*!
\brief Vibrato wobble depth in semitones of bend lift — an eighth of a step each way.

A quarter of a semitone. Drawing the wobble at the unit factor's full swing (±1 semitone of
lift) reads as a whammy dive, not a vibrato. Callers multiply this into the wobble factor
when converting it to bend-lift semitones.
*/
inline constexpr double g_highway_vibrato_depth_semitones = 0.25;

/*!
\brief The head's vibrato swing as a fraction of the tail's depth.

Half the tail's eighth-step swing — a sixteenth of a step each way. A fully pinned head
looked odd against the wobbling tail and a full-depth head bounced; the head breathing at
half depth keeps it visibly alive while the tail carries the motion.
*/
inline constexpr double g_highway_vibrato_head_depth_fraction = 0.5;

/*!
\brief Tremolo wobble depth as a multiple of the tail's half-width.

Above one on purpose: the centerline swings wider than the ribbon is thick, so consecutive
teeth clear each other and the zigzag reads as a hard saw instead of a wobbling bar.
*/
inline constexpr double g_highway_tremolo_depth = 1.25;

/*!
\brief Teeth over which the tremolo envelope ramps in and out at the tail's ends.

The teeth ease off the string line rather than starting mid-swing, and the ramp is measured in
TEETH rather than as a fraction of the tail's duration, so the eased entry always occupies the
same number of ridges instead of a dozen on one sustain and less than one on another. One tooth
in and one tooth out keeps the run uniform at any length.

Because a tooth is a fixed length of tail (see \ref g_highway_tremolo_apexes_per_note_height), a
tail shorter than about two teeth is ramp the whole way through and reads as a ripple rather than
a saw. That affects very short tremolo sustains only, and unlike the depth-ratio spacing this
replaced, it no longer varies with viewing distance.
*/
inline constexpr double g_highway_tremolo_ramp_cycles = 1.0;

/*!
\brief Seconds of tail per tremolo tooth cycle — forty cycles, eighty apexes, per second.

The whole tooth law: one cycle per fixed span of the note's own duration, so the count is the
tail's length over that span and nothing else. A longer note carries more ridges, a shorter one
fewer, and neither the camera, the viewing distance, nor the player's scroll-speed setting appears
in it. The teeth belong to the note.

Measured in TIME rather than world distance on purpose, even though the two are proportional at a
fixed scroll speed. \ref highwayTimeToZ divides world distance by the scroll setting, so a
world-space pitch would hand a note FEWER ridges as a player raised their scroll speed — a display
preference silently editing how a note is notated. In time the ridge count is a property of the
note, and the spacing compresses with scroll exactly as every other board feature does.

Deliberately NOT a musical subdivision (a 1/64 note, say). The teeth mean UNMEASURED noise picking
— the charting standard spells out measured repetition as discrete notes, which is why a scrape
rides this same wave — so tying their rate to tempo would assert a subdivision the notation
declines to specify, and would swing the density threefold between a slow song and a fast one.

The value is the density sighted and approved on 2026-08-06, which also happens to be the count the
previous depth-ratio spacing reached at the hit line. That spacing held each tooth's on-screen SHAPE
constant instead of its length, which bought aspect stability at the cost of a count that grew about
fourfold over a note's approach while the wave slid through the ribbon. The two cannot both hold
under perspective; rigidity on the note was chosen after seeing all three candidates in motion.

The turning-point count needs no ceiling: the drawn tail is clamped to the visible window, so that
window's length over this span bounds what any tail can emit.
*/
inline constexpr double g_highway_tremolo_tooth_cycle_seconds = 0.025;

/*!
\brief Returns the tremolo tooth phase at a point on the tail, in cycles from the note onset.

\param seconds_from_onset Tail time since the note onset.
\return Phase in cycles, zero at the onset and growing along the tail.
*/
[[nodiscard]] double highwayTremoloTailCycles(double seconds_from_onset) noexcept;

/*!
\brief Returns the tail time a tooth phase lands at — the inverse of
       \ref highwayTremoloTailCycles.

Callers walk the half-cycles to place the wave's turning points exactly (see
\ref makeHighwayTailSampleTimes).

\param cycles Phase in cycles from the note onset.
\return Tail time since the onset, in seconds.
*/
[[nodiscard]] double highwayTremoloTailSecondsAtCycle(double cycles) noexcept;

/*!
\brief Fraction of the tail duration over which the vibrato lift ramps in and out.

Modulating at full amplitude to the tail's very ends would start and end the lift off the
string line; the taper is this project's deliberate fix so it always anchors on it. Tremolo
teeth ramp in their own phase units instead (see \ref g_highway_tremolo_ramp_cycles), because
a fraction of duration is the wrong measure for a depth-spaced wave.
*/
inline constexpr double g_highway_tail_taper_fraction = 0.1;

/*!
\brief Returns the number of centerline samples a tail needs at a screen-space resolution.

Sampling density follows the tail's projected on-screen length rather than its duration, so a
long sustain costs vertices proportional to its visible size — a per-millisecond tessellation
would pay for time the viewer cannot see.

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
mechanical on multi-stage bends). Plateaus and direction reversals still get an exactly flat
tangent, and the Fritsch–Carlson limits guarantee no overshoot past any control value. The
curve starts and settles at rest: zero tangent at the first point (easing from zero at the
onset — unless the first point sits at the onset itself, a prebend, which anchors the start
value) and at the last point, whose value then holds.

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
the board — stated in display space so it holds for any string count and stacking order.

\param displayed_lane One-based displayed lane, 1 at the bottom of the stack.
\param string_count Number of displayed lanes.
\return True when the bend lift is inverted (downward).
*/
[[nodiscard]] bool highwayBendInverted(int displayed_lane, int string_count) noexcept;

/*!
\brief Returns the eased interpolation weight of a slide at a segment progress.

Pitched slides accelerate into the target (sin(progress * pi / 2) cubed); unpitched slides
release early (1 - sin((1 - progress) * pi / 2)).

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
\brief Returns the tremolo wobble at a tooth phase, as a signed factor.

A triangle wave, peaking at the note onset; callers scale by the tail half-width and the envelope.
The teeth mean UNMEASURED noise picking (the charting standard spells out measured repetition
as discrete notes), so pick-slide tails ride this same wave outright — a scrape is that noise
dragged along the string.

\param cycles Phase in cycles of tail length from the onset (see
       \ref highwayTremoloToothCycleWorld).
\return Wobble factor within plus-or-minus \ref g_highway_tremolo_depth.
*/
[[nodiscard]] double highwayTremoloWobble(double cycles) noexcept;

/*!
\brief Returns the tremolo amplitude envelope at a tooth phase.

Ramps over \ref g_highway_tremolo_ramp_cycles teeth at each end so the wave leaves and meets
the string line instead of starting and stopping mid-swing, and holds full depth everywhere
between — the ramp is in teeth, so a long sustain damps no more of them than a short one.

\param cycles Phase in cycles from the tail's visible start.
\param end_cycles Phase at the tail's far end; ends at or before the start give zero.
\return Amplitude scale in [0, 1].
*/
[[nodiscard]] double highwayTremoloEnvelope(double cycles, double end_cycles) noexcept;

/*!
\brief Builds the ascending sample times for one tail's visible span.

Uniform samples cover the span at the requested count, and every bend point and slide waypoint
inside the span is included exactly, so piecewise-linear technique curves hit their control
points instead of aliasing across them. A teethed tail's wobble turning points come in through
\p extra_times for the same reason: a triangle is piecewise linear, so its turning points are
the only samples its shape actually needs, and without them the uniform grid rounds every apex
by up to half its spacing — unevenly, tooth to tooth — and aliases the wave outright once the
teeth crowd that spacing. They arrive as times rather than being derived here because their
placement is projection state (see \ref highwayTremoloCycles), which this module does not hold.

\param note The note whose bend and slide times are folded in.
\param from_seconds Visible span start (already clamped to the hit line by the caller).
\param to_seconds Visible span end.
\param uniform_count Uniform sample count from highwayTailSampleCount.
\param extra_times Additional times the shape needs sampled exactly; those outside the span are
       dropped. The uniform count's cap never evicts them.
\return Ascending, deduplicated sample times spanning [from_seconds, to_seconds]; empty when
        the span is empty.
*/
[[nodiscard]] std::vector<double> makeHighwayTailSampleTimes(
    const HighwayNoteView& note, double from_seconds, double to_seconds, std::size_t uniform_count,
    std::span<const double> extra_times);

} // namespace rock_hero::common::core
