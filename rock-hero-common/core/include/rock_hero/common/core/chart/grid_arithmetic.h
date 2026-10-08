/*!
\file grid_arithmetic.h
\brief Exact grid-position arithmetic over the tempo map's musical grid.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief The minimum sustain distance: the seconds every DRAWN element keeps before a following
event.

The one settled spacing a tail's ink stops at and chord/arpeggio shape spans and the hand-window
morph ramps trim to — display's alone, since the stored chart holds the truth and a statement at a
ring's end may sit exactly on the next head of its own string, where it stays.

A DURATION, not a note value, for the same reason \ref g_minimum_kept_sustain_seconds is one: a gap
is read on screen in TIME, so a note value would open a quarter-second hole at 60 BPM and close to
a barely visible gap at 200. A duration reads the same at every tempo, and the meter never enters —
seconds do not care about the signature's denominator. The value was sighted, not derived: with
the highway's tip fade dissolving the last stretch of every tail, a shorter crop keeps more of a
glide's arrival bend and ends where the fade does, and the 2D lane still reads at it. Scoring
judges a sustain to its drawn end, so a legal early strike inside the hit window's early half can
cut a sustain short of credit it could not have kept; roadmap plan 24 carries the sustain-drop
leniency that covers the difference.

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
inline constexpr double g_minimum_sustain_distance_seconds{0.05};

/*!
\brief The lattice place a stretch of seconds reaches from a place: THE one conversion of a
duration in time into a length on the chart's axis.

The stretch is walked on the tempo map's own time axis, so every tempo anchor between the two
instants is part of it and a stretch spanning a tempo change is exact rather than an average. The
instant reached is then rounded onto the tick lattice (\ref g_tick_quantum_note_value) AWAY from
`from`: floored going back and ceiled going forward, because a length rounded the other way would
be shorter than the duration it names. A count within a millionth of a tick of a line is taken as
on it, so a stretch that lands squarely on a tick is not given a whole tick by the round trip's
last-bit error.

Going back, the answer never passes the grid origin and never stands later than `from`: the time
axis clamps at the first anchor, so a place there reaches no earlier. Going forward it is always
at least one tick past `from`: the time axis also clamps at the terminal anchor, and a place at or
past it would otherwise reach nothing.

\param tempo_map Tempo map supplying the time axis.
\param from A place on the tick lattice the stretch is measured from.
\param seconds The signed stretch: negative reaches back, positive forward.
\return The lattice place reached.
*/
[[nodiscard]] Fraction tickPlaceReached(const TempoMap& tempo_map, Fraction from, double seconds);

/*!
\brief The minimum sustain distance kept before an onset, as a whole-note length.

The stretch of \ref g_minimum_sustain_distance_seconds immediately BEFORE the onset, so how long it
is on the chart's axis is a question about the tempo there: \ref tickPlaceReached measures it back
and floors it onto the lattice, which makes a tempo change inside the margin exact and the answer
never shorter than the duration. Every ink crop, trim and ramp that keeps the margin reads this
length (through \ref HeadMargins inside a scene build) rather than composing a margin of its own.

\param tempo_map Tempo map supplying the time axis.
\param onset The whole-note place of the onset being PROTECTED — the head the margin is kept
       before, never the place of the ring, span or gesture that keeps it.
\return The margin in whole notes; zero where the onset is the grid origin.
*/
[[nodiscard]] Fraction minimumSustainDistance(const TempoMap& tempo_map, Fraction onset);

/*!
\brief The kept-sustain bound: only a ring that lasts LONGER than this many seconds earns a drawn
sustain tail.

The bound tail rule 2 (\ref chartPresentation) drops a short effect-free tail against: a ring no
longer than this reads as a struck note, not a deliberate sustain, so no surface draws a tail for
it. It bounds only what is DRAWN — the legato hold test reads the stored ring and asks strict
adjacency, so a chug inside the bound justifies its hammer-on by ringing to the onset rather than
by any assumption about tails.

A DURATION, not a note value, because the player reads the highway in time: a note value lasts
twice as long at half the tempo, so a note-value bound shows tails too often in fast songs and too
rarely in slow ones. Tail rule 2 measures each ring through the tempo map, so the same written
value earns at a slow tempo and not at a fast one — at a quarter second an eighth earns below 120
BPM and a quarter below 240. The meter never enters: seconds do not care about the signature's
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
fade. Measured back from each onset on the whole-note axis and resolved through the tempo map,
referenced as a note value and never a pixel, so a window in a fast song is the shorter wall-clock
rise a fast song reads as. Deliberately NOT a
duration like the minimum sustain distance: a reveal is a MUSICAL lead-in the scrolling board
carries the ink through, not a gap between two marks that has to stay readable at any tempo. The 2D
lane never reads it: the lane draws the execution form always. THE TUNABLE the reveal's feel is
sighted against — the initializer below is the one statement of its value, and no prose restates
it.
*/
inline constexpr Fraction g_tail_reveal_lead_whole_note{1, 4};

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
sense if the stored tail stopped where the ink does — the margin is where presentation stops the
ink (\ref chartPresentation), and the stored ring stops short of nothing.

Consequence, and the point: a chug chained to its restrike justifies its hammer-on (Guitar Pro tiles
durations, so the ring ends on the next onset), while a note followed by a REST does not — the
string stopped sounding, and the chart says so. The settle sweep (\ref sweepUnjustifiedLegato)
flattens such a claim at load and reports it.

With the same-string clamp (\ref sustainBoundOf) this is equality in practice: a predecessor's ring
may reach its successor's onset and never pass it, and the successor of a claim IS the next onset
on that string.

\param sustain The previous note's stored ring on the same string, in whole notes.
\param onset_gap Whole notes from that note's onset to the onset of the note taking the hammer-on
       or pull-off.
\return True when the predecessor is still ringing at the onset.
*/
[[nodiscard]] constexpr bool predecessorHoldReaches(
    const Fraction sustain, const Fraction onset_gap) noexcept
{
    return sustain >= onset_gap;
}

/*!
\brief The grid position of the tempo map's terminal anchor: the chart's closing barline.

Named once because every consumer of the chart's end needs the same answer — package read closing
the last tone region, tone-track normalization materializing the whole-song region, tone-track
validation bounding every region, and the editor's end-of-chart navigation and selection bounds.
The tempo-map rules keep the terminal anchor on a downbeat with no offset.

\param tempo_map Tempo map whose terminal anchor is being addressed.
\return Grid position of the terminal anchor, or the grid origin for a map with no anchors.
*/
[[nodiscard]] inline GridPosition terminalGridPosition(const TempoMap& tempo_map)
{
    return tempo_map.anchors().empty() ? GridPosition{} : tempo_map.anchors().back().position;
}

/*!
\brief Measures the signed exact whole-note distance from one grid position to another.

The inverse of \ref advanceGridPositionByWholeNotes: advancing `from` by the returned distance
reaches `to` exactly. For a single-shot question about one pair; a pass over a note stream measures
its distances by subtracting the places it computed once (\ref chartNotePlaces).

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

Every duration the chart stores is a whole-note value, so this is how a ring or a keyframe offset
reaches its instant and how the move verb steps a note: a lattice position stepped by whole ticks
lands on the lattice in whichever meter it arrives in. Clamped at the grid origin, so a step back
past the song's start falls short of the duration asked.

\param tempo_map Tempo map supplying the meter of every measure crossed.
\param position Valid grid position.
\param whole_notes Signed whole-note duration.
\return The position that far along the whole-note axis.
*/
[[nodiscard]] GridPosition advanceGridPositionByWholeNotes(
    const TempoMap& tempo_map, GridPosition position, Fraction whole_notes);

/*!
\brief Every note's onset as a place on the whole-note axis, computed once for a pass.

THE coordinate every derivation pass reasons in. A duration is a difference of two places and an
instant inside a note is its onset's place plus an offset, so a pass that asks for the places once
measures every gap, ring end and keyframe instant by exact addition and subtraction — never by
re-addressing a pair through the tempo map, which costs two segment searches per question.

\param notes Note stream whose onsets are wanted.
\param tempo_map Tempo map supplying the meter of every measure.
\return Index-parallel to `notes`: each onset's whole notes from measure 1 beat 1.
*/
[[nodiscard]] std::vector<Fraction> chartNotePlaces(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map);

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
