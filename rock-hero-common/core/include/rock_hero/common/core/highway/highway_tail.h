/*!
\file highway_tail.h
\brief Pure sustain-tail math: adaptive sampling, taper envelopes, and technique modulation.
*/

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <span>
#include <type_traits>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Vibrato wobble period in seconds, FIXED for every song and tempo (6.0 Hz).

A vibrato's rate is a property of the player's hand, not of the song: the wrist oscillates at
its own frequency whether the piece is a ballad or a thrash number. The detection plan bands it
at 4—7 Hz (docs/plans/roadmap/22-note-detection.md), and the literature sharpens the centre —
measured production means cluster 5.2—6.6 Hz across voice, violin and double bass, and every
perception study peaks in the same place (preference 6.0—6.5, optimal ~6, widest-wobble
tolerance 5—7). Tempo dependence is near zero: the one direct experiment found rate scaling in
2 of 5 professionals, and the largest effect measured anywhere is 8.3%, smaller than the
within-performer spread and smaller than register or finger choice. Worth knowing, because it is
a real evidence gap: nobody has published an electric-guitar measurement at all, so the band is
a transfer from instruments whose vibrato is a comparable wrist rotation.

Locking the wobble to a grid subdivision is the alternative this constant rejects: the grid's
eighth note runs at BPM/30 Hz — 2.0 Hz at 60 BPM and 7.1 Hz at 213, the two ends of a real
library — which is both slower and faster than any hand produces.

6.0 Hz sits on the literature's own centre, and it is reachable only because RATE AND DEPTH ARE
JUDGED TOGETHER, not independently. A nearby 6.25 Hz reads *"frantic"* at a ±0.49-gap swing,
and the ordinary depth here is ±0.345 (see g_highway_vibrato_depth_gaps). Listeners read a
wobble's speed partly from its width; production couples the two inversely at r = -0.62. So 6 Hz
at this narrow swing is a different stimulus from 6.25 Hz at the wider one, and it reads calm
where that one reads frantic. Keep the pairing in mind before moving either number alone:
widening the depth without slowing the rate walks back toward the setting that failed.
*/
inline constexpr double g_highway_vibrato_period_seconds = 1.0 / 6.0;

/*!
\brief Vibrato wobble depth, in string-lane gaps each way from the note's drawn position.

A DISPLACEMENT, not a pitch: the wobble is the sideways motion the fretting hand makes, so it adds
to wherever the bend has put the note and never enters the tension curve that places a bend
(\ref highwayBendLiftY). That curve is a square root near the unbent pitch, so a sine fed through
it draws flat-topped crests with near-vertical crossings; only a bend rides it.

0.345 is the sighted swing; a quarter-gap swing is the next stop down if it still reads large.
*/
inline constexpr double g_highway_vibrato_depth_gaps = 0.345;

/*!
\brief How much wider the WIDE tier swings than the ordinary one.

\ref VibratoState::Narrow is the depth above; the wide tier multiplies it, so the two tiers cannot
drift apart the way a second hard-coded depth would and re-tuning the ordinary vibrato carries the
exaggeration with it. 1.42 is the sighted ratio, a wide swing of about ±0.49 gaps; it is tuned on
this surface, as the 2D lane tunes its own. The wide swing must stay under half a gap, the margin
between an outer lane and the string grid's edge, or it reaches the board's clamp. The pairing note
under \ref g_highway_vibrato_period_seconds applies here too — the wide tier reads FASTER than the
narrow one at the same rate because listeners judge a wobble's speed partly from its width.
*/
inline constexpr double g_highway_wide_vibrato_depth_multiplier = 1.42;

/*!
\brief The head's vibrato swing as a fraction of the tail's.

Exactly half of the tail's displacement at the same instant, so it retunes with the tail. A pinned
head reads as odd against the wobbling tail and a full-depth head bounces; breathing at half keeps
the head visibly alive while the tail carries the motion.
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

Because a tooth is a fixed length of TIME (see \ref highwayTremoloTailCycles), a tail shorter than
about two teeth is ramp the whole way through and reads as a ripple rather than a saw. That affects
very short tremolo sustains only, and because the ramp is counted in teeth the eased length does not
vary with viewing distance.
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

The value is a sighted density, the count that reads as a saw at the hit line. The alternative —
spacing the teeth by the tail's drawn half-width, so each tooth's on-screen SHAPE stays constant
instead of its length — buys aspect stability at the cost of a count that grows about fourfold over
a note's approach as the wave slides through the ribbon. The two cannot both hold under
perspective, and rigidity on the note wins.

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
\brief Fraction of a vibrato span's duration over which its wobble ramps in and out.

Modulating at full amplitude to the span's very ends would start and end the wobble off the
string line; the taper is this project's deliberate fix so it always anchors on it. It applies
only at a span's two true ends, never at a change of width inside one. Tremolo
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

Each segment uses the same cosine ease as a pitched slide, run on the DISPLACEMENT the board draws
(\ref bendTravel) rather than on pitch, so the drawn bend leaves one stated value and arrives at the
next one tangentially — and therefore comes to REST at every authored point, a bend starting from
or releasing to the unbent string included: the travel law is a square root there, so a pitch
eased flat into zero would still meet the string line at an angle. That
rest is the point's meaning: a two-step bend that goes straight to two has no point at one, so a
point at one says the bend stops there, and the drawn shelf must show it (user ruling, 2026-09-24,
replacing a monotone cubic that flowed through same-direction points). The curve hits every
authored point exactly, never overshoots a segment's endpoints, and holds the last value after the
last point. A curve whose first point is not at the onset eases from zero at the onset; a prebend
whose first point is at the onset anchors that start value instead.

\param bend Bend curve points in ascending time order.
\param onset_seconds The note's onset time (the zero anchor for the pre-first-point ramp).
\param seconds Absolute time to evaluate at.
\return Bend amount in semitones; zero when the curve is empty.
*/
[[nodiscard]] double highwayBendSemitonesAt(
    std::span<const BendPointViewState> bend, double onset_seconds, double seconds) noexcept;

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
\brief Returns whether an onset group's bend lift points downward.

The direction belongs to the simultaneous onset rather than each member: a majority vote over the
group's displayed members chooses one side for every bend and vibrato in that strike. Ties resolve
to the upper displayed side, matching the majority-upper case.

\param notes Full note stream the group indexes.
\param group Onset group whose members are being drawn.
\param extra_lanes Displayed lanes beyond the chart's own count.
\param string_count Number of displayed lanes.
\param invert_string_order True when the displayed string stack is inverted.
\return True when the group's bend lift is inverted (downward).
*/
[[nodiscard]] bool highwayBendInverted(
    std::span<const NoteViewState> notes, const HighwayChordGroupViewState& group, int extra_lanes,
    int string_count, bool invert_string_order) noexcept;

/*!
\brief Returns the eased interpolation weight of a slide at a segment progress.

Pitched slides ease symmetrically, leaving and arriving tangentially (\ref raisedCosineEase);
unpitched slides release early (1 - sin((1 - progress) * pi / 2)).

\param progress Position within the slide segment in [0, 1]; values outside clamp.
\param unpitched True for the unpitched (pressure-release) easing.
\return Interpolation weight in [0, 1].
*/
[[nodiscard]] double highwaySlideEaseWeight(double progress, bool unpitched) noexcept;

/*!
\brief Returns the slope of \ref highwaySlideEaseWeight at a segment progress, per unit progress.

The one derivative of the one curve, for the approach that must leave the curve without a kink:
the window's settle over a crop zone joins the curve at its value AND its slope here
(\ref highwayHandWindowAt).

\param progress Position within the slide segment in [0, 1]; values outside clamp.
\param unpitched True for the unpitched (pressure-release) easing.
\return The curve's slope at the progress.
*/
[[nodiscard]] double highwaySlideEaseSlope(double progress, bool unpitched) noexcept;

/*!
\brief The cubic Hermite curve from one value and slope to another, at a unit progress.

The one spelling of the basis, for the bend curve's segments and the hand window's settle: the
curve passes through `from` with slope `from_slope` at 0 and `to` with slope `to_slope` at 1,
slopes measured per unit of `t`.

\param from Value at progress 0.
\param from_slope Slope at progress 0, per unit progress.
\param to Value at progress 1.
\param to_slope Slope at progress 1, per unit progress.
\param t Progress in [0, 1].
\return The curve's value at `t`.
*/
[[nodiscard]] double cubicHermite(
    double from, double from_slope, double to, double to_slope, double t) noexcept;

/*!
\brief Returns the vibrato wobble at a time from its span's start, as a signed unit factor.

Span-phased on purpose: absolute-time phasing desynchronizes repeated notes, and phasing from the
onset would cut a mid-ring vibrato in at whatever phase the onset reached. Runs at
\ref g_highway_vibrato_period_seconds; callers scale by the swing and the taper envelope.

\param seconds_from_start Time since the span's start.
\return Wobble factor in [-1, 1].
*/
[[nodiscard]] double highwayVibratoWobble(double seconds_from_start) noexcept;

/*!
\brief Returns the vibrato turning-point index at a time from a span's start.

Counts the sine's extremes: they fall a quarter period into the span and every half period
after that, so index zero is the first crest, one the trough behind it, and the index grows by
one per half period, fractional in between. Whole values ARE the turning points, which is what
lets a sampler walk them (see \ref makeHighwayTailSampleTimes).

\param seconds_from_start Time since the span's start — the anchor
       \ref highwayVibratoDisplacementAt phases from.
\return Turning-point index, -0.5 at the span's start and growing along it.
*/
[[nodiscard]] double highwayVibratoTurningIndex(double seconds_from_start) noexcept;

/*!
\brief Returns the time a vibrato turning point lands at — the inverse of
       \ref highwayVibratoTurningIndex.

The pair states the span-anchored phase ONCE, so a caller placing samples on the wave's extremes
cannot drift out of step with the reading \ref highwayVibratoDisplacementAt produces.

\param index Turning-point index from the span's start.
\return Time since the span's start, in seconds.
*/
[[nodiscard]] double highwayVibratoSecondsAtTurningIndex(double index) noexcept;

/*!
\brief The board's vibrato width ease: half a wobble (see \ref vibratoWideWeightAt).
*/
inline constexpr double g_highway_vibrato_width_ease_seconds =
    g_highway_vibrato_period_seconds / 2.0;

/*!
\brief Returns the vibrato displacement a note's stated spans contribute at an absolute time, in
string-lane gaps.

The board's whole vibrato reading, in one place: which span is in force, the envelope that anchors
its wobble on the string line at the span's own two ends, the fixed rate, and the swing its stated
widths set (\ref vibratoWideWeightAt between the board's two swings). A DISPLACEMENT added to the
note's drawn position, never a pitch fed through the bend's tension curve (see
\ref g_highway_vibrato_depth_gaps).

Each span carries one phase and one envelope, measured from where the vibrato STARTS: the wobble
leaves the string line at every span's start instead of jumping in at whatever phase the onset
happens to reach, keeps that phase across a change of width, and a span covering the whole tail
reproduces the note-anchored arithmetic exactly, because its start IS the onset.

\param vibrato The note's stated spans (\ref NoteViewState::vibrato), ascending and disjoint.
\param seconds Absolute time to evaluate at.
\param depth_scale Fraction of the full swing this reader shows; the tail shows all of it, a
       sounding head breathes at \ref g_highway_vibrato_head_depth_fraction of it.
\return Displacement in string-lane gaps, signed; zero outside every span and at each span's ends.
*/
[[nodiscard]] double highwayVibratoDisplacementAt(
    std::span<const VibratoSpanViewState> vibrato, double seconds, double depth_scale) noexcept;

/*!
\brief Returns the tremolo wobble at a tooth phase, as a signed factor.

A triangle wave, peaking at the note onset; callers scale by the tail half-width and the envelope.
The teeth mean UNMEASURED noise picking (the charting standard spells out measured repetition
as discrete notes), so pick-slide tails ride this same wave outright — a scrape is that noise
dragged along the string.

\param cycles Phase in cycles from the onset (see \ref highwayTremoloTailCycles).
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
\brief Builds the times a tail's visible span MUST be sampled at: its two ends, every bend point
and slide keyframe inside it, and the caller's extra times inside it.

The skeleton \ref makeHighwayTailSampleTimes fills in: the eased curve's segments end at each of
these, and a teethed or wobbling tail's turning points arrive through \p extra_times, so every
stretch between two of them is one monotone piece of the drawn curve. They arrive as times rather
than being derived here because their placement is projection state (the onset time each phase in
\ref highwayTremoloTailCycles is measured from), which this module does not hold.

\param note The note whose bend and slide times are folded in.
\param from_seconds Visible span start (already clamped to the hit line by the caller).
\param to_seconds Visible span end.
\param extra_times Additional times the shape needs sampled exactly; those outside the span are
       dropped.
\param capacity Room to reserve for the samples the caller adds between these.
\return Ascending, deduplicated times spanning [from_seconds, to_seconds]; empty when the span is
        empty.
*/
[[nodiscard]] std::vector<double> highwayTailExactTimes(
    const NoteViewState& note, double from_seconds, double to_seconds,
    std::span<const double> extra_times, std::size_t capacity);

/*!
\brief Builds the ascending sample times for one tail's visible span.

Every exact time (\ref highwayTailExactTimes) is kept, so the technique curves hit their control
points and a triangle wave its turning points instead of aliasing across them; between each pair of
exact times the samples fall where the drawn curve MOVES. Each stretch takes its own count from how
far the centerline travels on screen across it (\ref highwayTailSampleCount), spread evenly in time
within it. A glide lasting a sliver of a long tail but crossing many frets is therefore sampled as
densely as its travel needs, where one grid over the whole tail spent the samples by duration and
drew a quick slide's eased S-curve as two or three straight legs with corners.

The cap is ONE budget for the whole list, and the in-between samples are what yield to it: the
exact times carry the shape's correctness (a turning point the grid rounds is a visible error), so
they are never evicted, and every stretch's share shrinks in proportion instead — down to none when
the exact times alone fill the budget. A cap bounding only the grid, with every exact time appended
past it, lets a long teethed open tail reach 477 samples against a cap of 256, and the accent batch
it feeds can then exceed the 16-bit index budget and drop the whole group's light.

A template on the projection so the per-frame render path hands its camera in without allocating,
and two passes over the exact times — one to count, one to place — rather than a scratch list per
tail.

\tparam ScreenAt Callable answering where the tail's drawn centerline stands on screen at an
        instant, as `{x, y}` in pixels. The camera is the renderer's, so the renderer answers it and
        this module stays headless.
\param note The note whose bend and slide times are folded in.
\param from_seconds Visible span start (already clamped to the hit line by the caller).
\param to_seconds Visible span end.
\param screen_at The centerline's on-screen position at an instant.
\param pixels_per_sample Target screen-space distance between samples.
\param extra_times Additional times the shape needs sampled exactly; those outside the span are
       dropped, and the budget never evicts the ones inside.
\param sample_cap The whole list's budget; the in-between samples share what the exact times leave
       of it.
\return Ascending, deduplicated sample times spanning [from_seconds, to_seconds]; empty when
        the span is empty.
*/
template <typename ScreenAt>
    requires std::is_invocable_r_v<std::array<double, 2>, const ScreenAt&, double>
[[nodiscard]] std::vector<double> makeHighwayTailSampleTimes(
    const NoteViewState& note, const double from_seconds, const double to_seconds,
    const ScreenAt& screen_at, const double pixels_per_sample,
    const std::span<const double> extra_times, const std::size_t sample_cap)
{
    std::vector<double> times =
        highwayTailExactTimes(note, from_seconds, to_seconds, extra_times, sample_cap);
    if (times.size() < 2)
    {
        return times;
    }
    const std::size_t exact_count = times.size();
    // The in-between samples one stretch asks for: its on-screen travel's count, less the two ends
    // the exact times already are.
    const auto wanted_between = [&](const std::size_t stretch, std::array<double, 2>& from_point) {
        const std::array<double, 2> to_point = screen_at(times[stretch + 1]);
        const double pixels = std::hypot(to_point[0] - from_point[0], to_point[1] - from_point[1]);
        from_point = to_point;
        return highwayTailSampleCount(pixels, pixels_per_sample, sample_cap) - 2;
    };
    std::size_t wanted = 0;
    std::array<double, 2> point = screen_at(times.front());
    for (std::size_t stretch = 0; stretch + 1 < exact_count; ++stretch)
    {
        wanted += wanted_between(stretch, point);
    }
    const std::size_t budget = sample_cap > exact_count ? sample_cap - exact_count : 0;
    point = screen_at(times.front());
    for (std::size_t stretch = 0; stretch + 1 < exact_count; ++stretch)
    {
        std::size_t count = wanted_between(stretch, point);
        if (wanted > budget)
        {
            count = count * budget / wanted;
        }
        const double start = times[stretch];
        const double span = times[stretch + 1] - start;
        for (std::size_t index = 1; index <= count; ++index)
        {
            times.push_back(
                start + (span * static_cast<double>(index) / static_cast<double>(count + 1)));
        }
    }
    highwaySortUniqueTimes(times);
    return times;
}

} // namespace rock_hero::common::core
