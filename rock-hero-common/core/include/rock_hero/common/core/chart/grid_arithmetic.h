/*!
\file grid_arithmetic.h
\brief Exact grid-position arithmetic over the tempo map's musical grid.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Denominator of the chart's tick lattice: the finest position any chart may store.

1/3840 of a whole note is 1/960 of a quarter note — the standard MIDI PPQ tick, far finer than
audible resolution and still an exact rational. 3840 rather than 4096 because triplet grids need
the factor of 3, so straight, triplet, and quintuplet subdivisions all land on the lattice.
*/
inline constexpr int g_tick_quantum_denominator = 3840;

/*!
\brief The tick lattice as a note value: the lattice every stored position lands on.

A note value like any other, so the measure-anchored lattice arithmetic walks it unchanged. It is
never a grid the user selects or the editor draws — only a lattice positions land on: the editor's
placement quantum falls back to it while grid snap is off, and the margin walk floors onto it
(\ref marginBefore).
*/
inline constexpr Fraction g_tick_quantum_note_value{1, g_tick_quantum_denominator};

/*!
\brief THE one rounding rule onto the tick lattice: the whole tick nearest an exact tick count,
a tie going to the earlier tick.

Every producer that turns an exact instant into a position rounds through this — the grid's own
lines (\ref snapGridPosition and \ref adjacentGridPosition), the editor's drawn grid, and the
Guitar Pro import — so a septuplet grid's lines and a note imported off a septuplet rhythm land on
the SAME tick: both start from the same exact instant and round it the same way. The tie rule is
the snap's own stable-click rule. Adding whole ticks commutes with it, so rounding from a downbeat
and rounding from the song's start agree, every downbeat being a whole tick.

\param numerator Numerator of the exact tick count.
\param denominator Denominator of the exact tick count; strictly positive.
\return The nearest whole tick.
*/
[[nodiscard]] constexpr std::int64_t nearestTick(
    const std::int64_t numerator, const std::int64_t denominator) noexcept
{
    // Round half down is the ceiling of x - 1/2, which is floor((2n + d - 1) / 2d).
    const std::int64_t shifted = (2 * numerator) + denominator - 1;
    const std::int64_t divisor = 2 * denominator;
    const std::int64_t quotient = shifted / divisor;
    return (shifted % divisor != 0 && shifted < 0) ? quotient - 1 : quotient;
}

/*!
\brief Reports whether a position lies on the chart's tick lattice.

The lattice is every multiple of \ref g_tick_quantum_note_value on the whole-note axis
(\ref wholeNotePosition), which every downbeat of a meter the package format admits lies on.

\param tempo_map Tempo map supplying the meter of every measure up to the position.
\param position Position to test.
\return True when the position is a whole number of ticks from the grid origin.
*/
[[nodiscard]] bool isOnTickLattice(const TempoMap& tempo_map, const GridPosition& position);

/*!
\brief The minimum sustain distance: the seconds every DRAWN element keeps before a following
event.

The one settled spacing sustain tails, slide glide ends, chord/arpeggio shape spans, and the
hand-window morph ramps all trim to — display's alone, since the stored chart holds the truth and a
statement at a ring's end may sit exactly on the next head of its own string.

A DURATION, not a note value, for the same reason \ref g_minimum_kept_sustain_seconds is one: a gap
is read on screen in TIME, so a note value would open a quarter-second hole at 60 BPM and close to
a barely visible gap at 200. A duration reads the same at every tempo, and the meter never enters —
seconds do not care about the signature's denominator. The value sits in the band rhythm-game
charting converged on for the gap before a following note. Scoring judges a sustain to its drawn
end, so the margin must be at least the early half of the hit window, or a legal early strike cuts
a sustain short of credit it could not have kept; roadmap plan 24 carries that invariant.

Beyond presentation it answers two questions founded on it deliberately, both asking what the
smallest READABLE interval is: a never-restruck landed span is emitted only where its tenure
STRICTLY EXCEEDS this distance at the closing head — the same quantum that makes two marks
distinguishable is what makes a landed grip statable — and the Guitar Pro import caps the scoop it
synthesizes for a bare slide-in at it, since with no duration to go on the quickest glide that
still reads as a glide is the honest guess. So retuning this re-shapes a re-import's scoops, which
is intended. The editor's duration verb does NOT clamp to it: growth stops at exact adjacency with
the next onset on the note's own string (\ref sustainBoundOf), because a stored ring has no reason
to stop short of anything, and a ring trimmed by this margin would leave the editor's reveal
nothing to show.

THIS INITIALIZER IS THE ONLY STATEMENT OF THE VALUE: every other comment, guide and rule text names
"the margin" or "the minimum sustain distance" and points here.
*/
inline constexpr double g_minimum_sustain_distance_seconds{0.075};

/*!
\brief Returns the minimum sustain distance in signature beats at the onset it protects.

The margin is the stretch of \ref g_minimum_sustain_distance_seconds immediately BEFORE an onset,
so how many BEATS it spans is a question about the tempo there and not about the meter: it is
measured back through the tempo map and floored onto the tick lattice (\ref marginBefore), which
makes a tempo change inside the margin exact and the answer never shorter than the duration.

\param tempo_map Tempo map supplying the time axis and the beat grid.
\param onset The onset being PROTECTED — the head the margin is kept before, never the position of
       the ring, span or gesture that keeps it.
\return The margin as an exact beat fraction; zero where the onset is the grid origin.
*/
[[nodiscard]] Fraction minimumSustainDistanceBeats(
    const TempoMap& tempo_map, const GridPosition& onset);

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

// The margin taken off a tail must leave the shortest tail that EARNS one some ink: a margin at or
// past the bound would trim every earned tail to nothing.
static_assert(g_minimum_sustain_distance_seconds < g_minimum_kept_sustain_seconds);

/*!
\brief The depth of the 3D board's sliding tail-reveal window, as a fraction of a whole note.

The execution form's one display constant: a tail the tail law hides draws only inside a SLIDING
WINDOW rising this deep from the hit line — fully lit at the line, fading to nothing at the window's
outer edge, so the ink continuously materializes as it scrolls in. A window, never a whole-tail
fade. Resolved at each note's own meter and tempo, referenced as a note value and never a pixel, so
a window in a fast song is the shorter wall-clock rise a fast song reads as. Deliberately NOT the
duration the minimum sustain distance became: a reveal is a MUSICAL lead-in the scrolling board
carries the ink through, not a gap between two marks that has to stay readable at any tempo. The 2D
lane never reads it: the lane draws the execution form always. THE TUNABLE the reveal's feel is
sighted against — the initializer below is the one statement of its value, and no prose restates
it.
*/
inline constexpr Fraction g_tail_reveal_lead_whole_note{1, 4};

/*!
\brief Returns the reveal lead in signature beats.

A whole note is `signature_denominator` beats, so the lead scales with the meter: a quarter of a
whole note is one beat in x/4, two in x/8.

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
presented tail's reach past a statement that leaves the string shaking (\ref presentedChartNotes
rule 2), and the editor's scrape defaults — and a window one of them measured differently would be
a gesture the rules refuse.
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
\brief The grid position one minimum-sustain-distance margin before an onset.

THE ONE AUTHORITY on where the margin begins, and the only place the margin is a length at all: the
stretch of \ref g_minimum_sustain_distance_seconds immediately before `onset`, resolved through the
tempo map's own time axis so a tempo anchor inside the margin is honoured exactly, then FLOORED
onto the chart's tick lattice (\ref g_tick_quantum_note_value) — floored, because a margin rounded
the other way would be shorter than the duration it names. \ref minimumSustainDistanceBeats is this
position measured back in beats, and every trim, clearance and ramp reads one of the two rather
than composing a margin of its own.

Its readers: where the fretting hand begins its morph toward a placement at `onset` when no glide
carries it there, where the picking hand's light begins its rise toward an onset there, and every
drawn element that must stay clear of the head that follows it. Clamped at the grid origin like
\ref advanceGridPosition, so an onset standing closer to the chart's start than the margin yields
the origin itself.

\param tempo_map Tempo map supplying the time axis and the beat grid.
\param onset Valid grid position the margin is measured back from.
\return The tick line one margin or more before the onset.
*/
[[nodiscard]] GridPosition marginBefore(const TempoMap& tempo_map, GridPosition onset);

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
\brief A grid position on the whole-note axis: whole notes from the grid origin.

The one axis a meter change does not bend (\ref TempoMap::wholeNotePositionAt), and the axis the
tick lattice is ruled on: a tick is \ref g_tick_quantum_note_value of a whole note, so two lattice
positions are a whole number of ticks apart here whatever meters lie between them, where their beat
distance re-reads in each measure's own denominator.

\param tempo_map Tempo map supplying the meter of every measure up to the position.
\param position Valid grid position.
\return Whole notes from measure 1 beat 1, exact.
*/
[[nodiscard]] Fraction wholeNotePosition(const TempoMap& tempo_map, GridPosition position);

/*!
\brief Measures the signed exact whole-note distance from one grid position to another.

The inverse of \ref advanceGridPositionByWholeNotes, as \ref beatDistance is of
\ref advanceGridPosition: advancing `from` by the returned distance reaches `to` exactly.

\param tempo_map Tempo map supplying the meter of every measure crossed.
\param from Position the distance is measured from.
\param to Position the distance is measured to.
\return Signed distance in whole notes as an exact rational.
*/
[[nodiscard]] Fraction wholeNoteDistance(
    const TempoMap& tempo_map, GridPosition from, GridPosition to);

/*!
\brief The grid position a whole-note duration after another, exact across every meter change
between them.

\ref advanceGridPosition carries a BEAT count, the right walk for a ring or a keyframe offset
measured from its own onset. This carries MUSICAL TIME: the move verb steps every instant it moves
by one whole-note delta, so a note keeps each instant it holds the same distance apart, and a
lattice position stepped by whole ticks lands on the lattice in whichever meter it arrives in — the
same beat count re-read in a larger denominator would not. Clamped at the grid origin like
\ref advanceGridPosition, so a step back past the song's start falls short of the duration asked.

\param tempo_map Tempo map supplying the meter of every measure crossed.
\param position Valid grid position.
\param whole_notes Signed whole-note duration.
\return The position that far along the whole-note axis.
*/
[[nodiscard]] GridPosition advanceGridPositionByWholeNotes(
    const TempoMap& tempo_map, GridPosition position, Fraction whole_notes);

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
multiple of the step, and ties resolve to the earlier line. Every line is ROUNDED onto the tick
lattice (\ref nearestTick), so a grid no tick divides — a septuplet's — still yields only positions
a chart may store. Callers own note-value validity policy (the editor validates with
`isValidTempoGridNoteValue` and falls back to 1/4); a note value under a tick, which has no lattice
of distinct lines, or a degenerate signature returns the position unchanged.

\param tempo_map Tempo map supplying signatures and the beat grid.
\param position Valid grid position to snap (offset in [0, 1)).
\param note_value Grid step as a fraction of a whole note, at least a tick.
\return The position of the nearest grid line, on the tick lattice.
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
caller as for \ref snapGridPosition: a note value under a tick or a degenerate signature also
returns the position unchanged.

\param tempo_map Tempo map supplying signatures and the beat grid.
\param position Valid grid position to step from (offset in [0, 1)), on- or off-grid.
\param note_value Grid step as a fraction of a whole note, at least a tick.
\param later True to step later in time, false earlier.
\return The position of the adjacent grid line in the step direction, on the tick lattice.
*/
[[nodiscard]] GridPosition adjacentGridPosition(
    const TempoMap& tempo_map, GridPosition position, Fraction note_value, bool later);

} // namespace rock_hero::common::core
