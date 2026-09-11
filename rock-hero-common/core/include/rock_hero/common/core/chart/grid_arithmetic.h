/*!
\file grid_arithmetic.h
\brief Exact grid-position arithmetic over the tempo map's musical grid.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief The minimum sustain distance, as a fraction of a whole note.

The one settled spacing every DRAWN element keeps before a following event: sustain tails, slide
glide ends, chord/arpeggio shape spans, and the hand-window morph ramps all trim to this margin.
1/16 whole note; a 1/32 margin closes the gap too tightly to read on screen.

It binds presentation, plus ONE derivation question founded on it deliberately: a never-restruck
landed span is emitted only where its tenure STRICTLY EXCEEDS this distance at the closing head's
measure — the same notated quantum that makes two marks distinguishable is what makes a landed
grip statable, referenced as a note value and never a pixel. The editor's duration verb does NOT
clamp to it: growth stops at exact adjacency with the next onset on the note's own string
(\ref sustainBoundOf), because a stored ring has no reason to stop short of anything, and a ring
trimmed by this margin would leave the editor's reveal nothing to show.
*/
inline constexpr Fraction g_minimum_sustain_distance_whole_note{1, 16};

/*!
\brief Returns the minimum sustain distance in signature beats.

A whole note is `signature_denominator` beats, so the margin scales with the meter: a quarter
of a beat in x/4, half a beat in x/8.

\param signature_denominator Note value that represents one beat (the signature's denominator).
\return The margin as an exact beat fraction.
*/
[[nodiscard]] constexpr Fraction minimumSustainDistanceBeats(
    const int signature_denominator) noexcept
{
    return Fraction{
        signature_denominator * g_minimum_sustain_distance_whole_note.numerator,
        g_minimum_sustain_distance_whole_note.denominator
    };
}

/*!
\brief The latest offset a fret statement may stand at before the next strike on its string.

A statement that ends a gesture — a shift glide's arrival, a release — keeps clear of the head
that follows it by the minimum sustain distance, so the two marks never print on top of each
other and the one before the head stays reachable. Where the gap is not longer than that margin,
the statement takes half the gap instead: a gesture always has somewhere to end, strictly before
the strike. One answer for both, so an imported arrival and an edited release cannot disagree
about how close to a head a statement may stand.

\param gap Beats from the note's onset to the next strike on its string; strictly positive.
\param margin The minimum sustain distance in beats at the note (\ref minimumSustainDistanceBeats).
\return The latest offset from the onset, in beats, strictly inside the gap.
*/
[[nodiscard]] Fraction latestStatementBeforeStrike(Fraction gap, Fraction margin);

/*!
\brief The kept-sustain bound: only a ring that lasts LONGER than this many seconds earns a drawn
sustain tail.

The bound presentation rule 3 (\ref presentedChartNotes) drops a short effect-free tail against: a
ring no longer than this reads as a struck note, not a deliberate sustain, so no surface draws a
tail for it. It bounds only what is DRAWN — the legato hold test reads the stored ring and asks
strict adjacency, so a chug inside the bound justifies its hammer-on by ringing to the onset rather
than by any assumption about tails.

A DURATION, not a note value, because the player reads the highway in time: a note value lasts
twice as long at half the tempo, so a note-value bound shows tails too often in fast songs and too
rarely in slow ones. Rule 3 measures each ring through the tempo map, so the same written value
earns at a slow tempo and not at a fast one — at a quarter second an eighth earns below 120 BPM and
a quarter below 240. The meter never enters: seconds do not care about the signature's
denominator. The verdict can change only at a tempo anchor, never inside a run, because the map's
rate is constant between anchors.

THIS INITIALIZER IS THE ONLY STATEMENT OF THE VALUE, and deliberately so: the bound is headed for
a user-tunable option, so every other comment, guide and rule text names "the kept-sustain bound"
and points here rather than repeating a figure that would then be wrong in one of them.
*/
inline constexpr double g_minimum_kept_sustain_seconds{0.25};

/*!
\brief The depth of the 3D board's sliding tail-reveal window, as a fraction of a whole note.

The execution form's one display constant: a tail the tail law hides draws only inside a SLIDING
WINDOW rising this deep from the hit line — fully lit at the line, fading to nothing at the window's
outer edge, so the ink continuously materializes as it scrolls in. A window, never a whole-tail
fade. Resolved at each note's own meter and tempo, referenced as a note value and never a pixel, so
the window rides tempo exactly as every other distance in this family does. The 2D lane never reads
it: the lane draws the execution form always. THE TUNABLE the reveal's feel is sighted against — the
initializer below is the one statement of its value, and no prose restates it.
*/
inline constexpr Fraction g_tail_reveal_lead_whole_note{1, 4};

/*!
\brief Returns the reveal lead in signature beats.

A whole note is `signature_denominator` beats, so the lead scales with the meter exactly as
\ref minimumSustainDistanceBeats does.

\param signature_denominator Note value that represents one beat (the signature's denominator).
\return The lead as an exact beat fraction.
*/
[[nodiscard]] constexpr Fraction tailRevealLeadBeats(const int signature_denominator) noexcept
{
    return Fraction{
        signature_denominator * g_tail_reveal_lead_whole_note.numerator,
        g_tail_reveal_lead_whole_note.denominator
    };
}

/*!
\brief Sub-beat step keeping a degenerate gesture payload strictly after its predecessor.

The minimum span a glide, slide-out, or scrape leg may occupy: zero-length gestures have
nowhere to travel, so synthesis and compression floor on this window.

Unlike the two bounds above this is a plain BEAT quantity, not a whole-note-referenced one: it
bounds payload offsets, which are already stated in beats, rather than naming a note value. It
sits here because three producers floor on it — the Guitar Pro import's gesture synthesis, the
presentation trim's slide-out compression (\ref presentedChartNotes), and the editor's scrape
defaults — and a window one of them measured differently would be a gesture the rules refuse.
*/
inline constexpr Fraction g_minimum_slide_window{1, 8};

/*!
\brief True when the predecessor's ring reaches the onset: strict adjacency.

The legato hold test. A hammer-on or pull-off is real only while the finger that plays it is still
on the string, and the chart says exactly how long that is: `ChartNote::sustain` is the ACTUAL
duration the string rings, so the predecessor is still down at the onset precisely when its ring
reaches it. Nothing is assumed and nothing is inferred.

Neither compensation a trimmed encoding would need applies here. A kept-sustain assumption (any
gap under a quarter note justifying a claim, on the grounds that a shorter tail was legitimately
absent from the chart) would say nothing about the notes and everything about what an import had
destroyed. Margin slack (a tail one minimum-sustain-distance short still counting) would only make
sense if the stored tail WERE the drawn tail that must not crowd the next head — the margin is a
presentation rule (\ref presentedChartNotes), and the stored ring stops short of nothing.

Consequence, and the point: a chug chained to its restrike justifies its hammer-on (Guitar Pro tiles
durations, so the ring ends on the next onset), while a note followed by a REST does not — the
string stopped sounding, and the chart says so. The settle sweep (\ref sweepUnjustifiedLegato)
flattens such a claim at load and reports it.

With the same-string clamp (\ref sustainBoundOf) this is equality in practice: a predecessor's ring
may reach its successor's onset and never pass it, and the successor of a claim IS the next onset
on that string.

\param predecessor Onset of the previous note on the same string.
\param sustain That note's stored ring in beats.
\param onset Onset of the note taking the hammer-on or pull-off.
\param tempo_map Tempo map supplying the signature-derived beat axis.
\return True when the predecessor is still ringing at the onset.
*/
[[nodiscard]] bool predecessorHoldReaches(
    const GridPosition& predecessor, Fraction sustain, const GridPosition& onset,
    const TempoMap& tempo_map);

/*!
\brief Converts a grid position onto the tempo map's fractional global-beat axis.

The whole-beat index of the position's (measure, beat) plus its sub-beat offset. This is the one
GridPosition-to-beat contract the 2D tab and 3D highway projections both resolve seconds through,
so defining it once keeps their timing from silently diverging.

\param tempo_map Tempo map supplying the global-beat index.
\param position Grid position to convert.
\return The position on the fractional global-beat axis.
*/
[[nodiscard]] inline double globalBeatPosition(
    const TempoMap& tempo_map, const GridPosition& position)
{
    return static_cast<double>(tempo_map.globalBeatIndex(position.measure, position.beat)) +
           position.offset.toDouble();
}

/*!
\brief The grid position of the tempo map's terminal anchor: the chart's closing barline.

Named once because every consumer of the chart's end needs the same answer — package read closing
the last tone region, tone-track normalization materializing the whole-song region, tone-track
validation bounding every region, and the editor's end-of-chart navigation and selection bounds.
The offset is zero: the terminal anchor sits exactly on a beat.

\param tempo_map Tempo map whose terminal anchor is being addressed.
\return Grid position of the terminal anchor.
*/
[[nodiscard]] inline GridPosition terminalGridPosition(const TempoMap& tempo_map)
{
    const auto [measure, beat] = tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
    return GridPosition{.measure = measure, .beat = beat, .offset = {}};
}

/*!
\brief Advances a grid position by an exact number of beats.

Whole beats carry across beat and measure boundaries through the tempo map's time-signature
segments (a beat is one signature beat, so crossing a meter change re-slices exactly the way the
map's beat axis does); the fractional remainder becomes the resulting sub-beat offset. Negative
deltas move earlier; a result that would land before the grid origin clamps to measure 1 beat 1
with a zero offset. Positions past the terminal anchor keep extending — signatures carry forward.

\param tempo_map Tempo map supplying the signature-derived beat axis.
\param position Valid grid position to advance (offset in [0, 1)).
\param beats Signed exact beat delta.
\return The advanced position, clamped at the grid origin.
*/
[[nodiscard]] GridPosition advanceGridPosition(
    const TempoMap& tempo_map, GridPosition position, Fraction beats);

/*!
\brief The grid position one minimum-sustain-distance margin before a position, at its meter.

The shared arrival rule: where the fretting hand begins its morph toward a placement at
`position` when no glide carries it there, and where the picking hand's light begins its rise
toward an onset there. Both ramps read this one function rather than composing the margin
themselves, so they cannot drift apart. Clamped at the grid origin like \ref advanceGridPosition.

\param tempo_map Tempo map supplying the meter at `position` and the beat axis.
\param position Valid grid position the margin is measured back from.
\return The position one margin earlier.
*/
[[nodiscard]] GridPosition marginBefore(const TempoMap& tempo_map, GridPosition position);

/*!
\brief Measures the signed exact beat distance from one grid position to another.

The inverse of advanceGridPosition: advancing `from` by the returned distance reaches `to`
exactly. Positive when `to` is later than `from`.

\param tempo_map Tempo map supplying the signature-derived beat axis.
\param from Position the distance is measured from.
\param to Position the distance is measured to.
\return Signed distance in beats as an exact rational.
*/
[[nodiscard]] Fraction beatDistance(const TempoMap& tempo_map, GridPosition from, GridPosition to);

/*!
\brief Resolves the grid position where a note's sustain ends.

A zero sustain ends at the onset itself. Sustains may cross beat, measure, and signature
boundaries; the endpoint is exact.

\param tempo_map Tempo map supplying the signature-derived beat axis.
\param note Chart note whose sustain endpoint is wanted.
\return The onset advanced by the note's sustain.
*/
[[nodiscard]] GridPosition sustainEndPosition(const TempoMap& tempo_map, const ChartNote& note);

/*!
\brief Snaps a grid position to the nearest line of the measure-anchored note-value grid.

Same grid semantics as the editor timeline's rendered grid and time-space snap
(`nearestTempoGridPosition`): the note value is a fraction of a whole note (1/8 means eighth
notes in every meter), lines sit every step from each measure's downbeat with the count
restarting at the next downbeat, every downbeat is a line even when the measure length is not a
multiple of the step, ties resolve to the earlier line, and the result stores the line's exact
rational position. Callers own note-value validity policy (the editor validates with
`isValidTempoGridNoteValue` and falls back to 1/4); a non-positive note value or degenerate
signature returns the position unchanged.

\param tempo_map Tempo map supplying signatures and the beat grid.
\param position Valid grid position to snap (offset in [0, 1)).
\param note_value Grid step as a fraction of a whole note; must be positive.
\return The exact position of the nearest grid line.
*/
[[nodiscard]] GridPosition snapGridPosition(
    const TempoMap& tempo_map, GridPosition position, Fraction note_value);

/*!
\brief The adjacent line of the measure-anchored note-value grid strictly beyond a position.

The same lattice \ref snapGridPosition reads, asked a different question: not the nearest line but
the neighbouring one in a direction. From a position between lines the result is the nearer line in
the step direction, so a step never jumps past the adjacent line; from a line it is the next line
of the lattice — across a downbeat, the neighbouring measure's line on THAT measure's meter, with
every downbeat a line of its own. The answer is read off the lattice directly rather than by
stepping and re-snapping, which is what makes the walk exactly reversible on every meter: stepping
back from any line returns to the line it was stepped from, even where the measure length leaves
the last line an odd half-step short of the next downbeat (a 1/4 grid in 7/8).

The grid origin has no earlier line, so stepping earlier from it returns the position unchanged;
callers treat a result equal to the input as a refusal. Note-value validity policy stays with the
caller as for \ref snapGridPosition: a non-positive note value or a degenerate signature also
returns the position unchanged.

\param tempo_map Tempo map supplying signatures and the beat grid.
\param position Valid grid position to step from (offset in [0, 1)), on- or off-grid.
\param note_value Grid step as a fraction of a whole note; must be positive.
\param later True to step later in time, false earlier.
\return The exact position of the adjacent grid line in the step direction.
*/
[[nodiscard]] GridPosition adjacentGridPosition(
    const TempoMap& tempo_map, GridPosition position, Fraction note_value, bool later);

} // namespace rock_hero::common::core
