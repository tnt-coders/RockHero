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

The one settled spacing every element keeps before a following event: sustain tails, slide
glide ends, chord/arpeggio shape spans, and the hand-window morph ramps all trim to this margin,
and the editor's duration verb clamps to it. 1/16 whole note; a 1/32 margin closes the gap too
tightly to read on screen.
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
\brief The shortest notated ring that earns a kept sustain tail, in signature beats.

Two readers must agree on this bound, which is why it is named once. The import drop rule
removes the tail of any effect-free note notated shorter than this — a shorter ring reads as
noise in a chart, not a deliberate sustain. Consequently a note held through a gap of at least
this length necessarily carries a tail reaching the minimum-sustain-distance margin, which is
what lets \ref predecessorHoldReaches read a shorter (or absent) tail as a proven release.
Currently one beat.
*/
inline constexpr Fraction g_minimum_kept_sustain_beats{1};

/*!
\brief True unless the chart proves the predecessor was released before the onset.

The legato hold test: a hammer-on or pull-off is real only while its predecessor can still be
held when the new note starts. Under an onset gap shorter than
\ref g_minimum_kept_sustain_beats that is assumed — tails below the bound are legitimately
absent. At or beyond it, a held-through predecessor necessarily carries a tail reaching the
minimum-sustain-distance margin before the onset, so a sustain ending short of that margin —
evaluated at the predecessor's measure, the same margin every trim derives — proves the string
was released. A scrape's sustain is its gesture's end, so the same comparison covers it.

\param predecessor Previous note on the same string.
\param onset Onset of the note taking the hammer-on or pull-off.
\param tempo_map Tempo map supplying the signature-derived beat axis.
\return True when a hammer-on or pull-off from the predecessor is not disproven.
*/
[[nodiscard]] bool predecessorHoldReaches(
    const ChartNote& predecessor, const GridPosition& onset, const TempoMap& tempo_map);

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

} // namespace rock_hero::common::core
