/*!
\file grid_arithmetic.h
\brief Exact grid-position arithmetic over the tempo map's musical grid.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

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
\brief Resolves each note's effective held length: its sustain, span-extended for chord strums.

The chart convention the hold test must judge against, and the musical twin of the display's
\ref HighwayViewState::display_hold_ends, which resolves this answer into seconds rather than
restating it: a strum under a hand-shape span is held for the whole span even when
its notes carry no sustain, because the span is what tells the player how long to keep the shape
fretted. Each SUSTAINLESS note in a same-onset group of two or more covered by a span therefore
holds to the span's end. Groups whose notes are all fully muted stay unextended (a dead chug is
choked, not held), as do single notes and notes carrying an explicit sustain, whose tails already
state their hold. Coverage is positional only, with no posture matching.

The span-implied hold is capped at the next onset on the note's OWN string, the same bound 40-Q2-B
imposes on a stored sustain: a derived hold running past a later head would draw a tail through and
beyond it, which no storable chart can express. The cap cannot change \ref predecessorHoldReaches —
the onset it measures to IS the successor whose claim reads this hold, and a hold reaching exactly
that onset still reaches — so it is a display bound resolved in the one authority rather than a
second rule on the surfaces.

The two readers ask simultaneity in the terms their domains offer — exact `GridPosition` equality
here, resolved seconds against a rounding tolerance on the display side — and agree. Exact positions
always resolve to equal seconds, and the display's tolerance is a nanosecond, six orders below the
finest grid the editor offers, so it can absorb arithmetic noise but never join two notes a chart
can tell apart. There is no set of notes one groups and the other does not.

Callers must pass notes in their SAVED form (`savedChartNote`). A pick slide's latent mute is the
difference that matters: in memory an onset group can read as all-muted, and so choked, where the
saved chart reads it as held. Judging the saved form is what keeps the `H` verb, the legato repair
and the validation gate from disagreeing about whether a shape is held — each had to learn this
separately, so it is a contract here now rather than a habit at three call sites.

\param notes Note stream in saved form, sorted by (position, string).
\param shapes Hand-posture spans sorted by position.
\param tempo_map Tempo map supplying the signature-derived beat axis.
\return Per-note effective held length in beats, sized like notes; never shorter than the
        note's own sustain.
*/
[[nodiscard]] std::vector<Fraction> chartEffectiveSustains(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map);

/*!
\brief True unless the chart proves the predecessor was released before the onset.

The legato hold test: a hammer-on or pull-off is real only while its predecessor can still be
held when the new note starts. Under an onset gap shorter than
\ref g_minimum_kept_sustain_beats that is assumed — tails below the bound are legitimately
absent. At or beyond it, a held-through predecessor necessarily carries a tail reaching the
minimum-sustain-distance margin before the onset, so a hold ending short of that margin —
evaluated at the predecessor's measure, the same margin every trim derives — proves the string
was released.

The held length is the EFFECTIVE one (\ref chartEffectiveSustains), never the stored sustain: a
sustainless member of a strum under a hand-shape span is held by the span, and reading its zero
would call a held shape released. A scrape's sustain is its gesture's end, so the same
comparison covers it.

\param predecessor Onset of the previous note on the same string.
\param effective_sustain That note's effective held length from \ref chartEffectiveSustains.
\param onset Onset of the note taking the hammer-on or pull-off.
\param tempo_map Tempo map supplying the signature-derived beat axis.
\return True when a hammer-on or pull-off from the predecessor is not disproven.
*/
[[nodiscard]] bool predecessorHoldReaches(
    const GridPosition& predecessor, Fraction effective_sustain, const GridPosition& onset,
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
