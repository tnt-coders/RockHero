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

/*!
\brief Tremolo tooth pitch, in natural-log units of eye depth.

The teeth are spaced by DEPTH RATIO rather than by time, so one tooth spans a constant
fraction of its own distance from the camera and therefore keeps the same shape on screen the
whole way down the tail. A fixed time (or world-distance) pitch cannot: perspective shrinks a
tooth's along-tail advance as one over depth squared but its lateral swing only as one over
depth, so the near teeth read wide and shallow while the far ones collapse into needles — a
1.53x drift in tooth aspect across one tail, measured through the real projection, which is
exactly the "starts spaced out, gets compressed" the sighted pass showed. At this pitch the
aspect holds to within two percent end to end.

The value is small because it sets DENSITY as well as shape, and density is most of what reads
as intensity: teeth per tail go as the log of the tail's depth ratio over this pitch, so a
tooth count is bought here and nowhere else. Sighted up from a first uniform pass whose teeth
were correct in shape but far too few. With the depth constant below, a tooth here is very
nearly as wide as it is long (aspect ~0.94), which is a hard saw rather than a ripple.
*/
inline constexpr double g_highway_tremolo_log_pitch = 0.05;

/*!
\brief Tremolo wobble depth as a multiple of the tail's half-width.

Above one on purpose: the centerline swings wider than the ribbon is thick, so consecutive
teeth clear each other and the zigzag reads as a hard saw instead of a wobbling bar.
*/
inline constexpr double g_highway_tremolo_depth = 1.25;

/*!
\brief Teeth over which the tremolo envelope ramps in and out at the tail's ends.

The teeth ease off the string line rather than starting mid-swing, but the ramp is measured in
TEETH, not in a fraction of the tail's duration. A duration fraction cannot work here: teeth
are spaced by depth, so the same fraction damps a dozen of them near the head on one sustain
and less than one on another — read on sight as the teeth being narrower at the start (user
catch 2026-08-04). One tooth in and one tooth out is fast enough to leave the run uniform at
any sustain length while keeping the eased entry.
*/
inline constexpr double g_highway_tremolo_ramp_cycles = 1.0;

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
\brief Returns the tremolo tooth phase at a point, in cycles from the tail's anchor.

Depth-ratio spacing (see \ref g_highway_tremolo_log_pitch): a tooth spans a constant fraction
of its own eye depth, which is what holds its on-screen shape constant down the whole tail.
Phase is zero at the anchor and grows toward the horizon; callers anchor at the tail's visible
start so the teeth belong to the note rather than sitting at fixed screen positions the tail
slides through.

Depth is the perspective divisor, which the caller owns because it is projection state; this
module only holds the wave.

\param eye_depth Camera-relative depth of the point; non-positive depths (behind the camera)
       return zero rather than a domain error.
\param anchor_eye_depth Depth the phase is zero at; non-positive anchors return zero.
\return Phase in cycles, zero at the anchor.
*/
[[nodiscard]] double highwayTremoloCycles(double eye_depth, double anchor_eye_depth) noexcept;

/*!
\brief Returns the eye depth a tremolo tooth phase lands at — the inverse of
       \ref highwayTremoloCycles.

Callers walk the half-cycles to place the wave's turning points exactly (see
\ref makeHighwayTailSampleTimes).

\param cycles Phase in cycles from the anchor.
\param anchor_eye_depth Depth the phase is zero at.
\return Eye depth at that phase.
*/
[[nodiscard]] double highwayTremoloEyeDepthAtCycle(double cycles, double anchor_eye_depth) noexcept;

/*!
\brief Returns the tremolo wobble at a tooth phase, as a signed factor.

A triangle wave, peaking at the anchor; callers scale by the tail half-width and the envelope.
The teeth mean UNMEASURED noise picking (the charting standard spells out measured repetition
as discrete notes), so pick-slide tails ride this same wave outright — a scrape is that noise
dragged along the string.

\param cycles Phase in cycles, from \ref highwayTremoloCycles.
\return Wobble factor within plus-or-minus \ref g_highway_tremolo_depth.
*/
[[nodiscard]] double highwayTremoloWobble(double cycles) noexcept;

/*!
\brief Returns the tremolo amplitude envelope at a tooth phase.

Ramps over \ref g_highway_tremolo_ramp_cycles teeth at each end so the wave leaves and meets
the string line instead of starting and stopping mid-swing, and holds full depth everywhere
between — the ramp is in teeth, so a long sustain damps no more of them than a short one.

\param cycles Phase in cycles from the tail's anchor.
\param end_cycles Phase at the tail's far end; ends at or before the anchor give zero.
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
