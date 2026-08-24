/*!
\file tempo_grid_geometry.h
\brief Pure tempo-grid line geometry and snap lookup for the editor timeline.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Inclusive upper bound for the terms of a grid note value a user may select. */
inline constexpr int g_max_tempo_grid_note_value_term = 128;

/*!
\brief Denominator of the tick lattice: the finest note value any editor verb places on.

1/3840 of a whole note is 1/960 of a quarter note — the standard MIDI PPQ tick, far finer than
audible resolution and still an exact rational. 3840 rather than 4096 because triplet grids need
the factor of 3, so straight, triplet, and quintuplet subdivisions all land on the lattice.
*/
inline constexpr int g_tick_quantum_denominator = 3840;

/*!
\brief The tick lattice as a note value: what placement quantizes to while grid snap is off.

A note value like any other, so the measure-anchored lattice arithmetic walks it unchanged. It is
never a grid the user selects or the editor draws — only a lattice positions land on.
*/
inline constexpr common::core::Fraction g_tick_quantum_note_value{1, g_tick_quantum_denominator};

/*!
\brief The editor's default grid note value: the sixteenth-note grid.

The single source for every owner of a grid note value (the default-constructed Fraction of 0/1
is a degenerate step, so owners must initialize explicitly) and the fallback whenever a stored or
supplied value is invalid, so rendering and snapping can never diverge.
*/
inline constexpr common::core::Fraction g_default_tempo_grid_note_value{1, 16};

/*!
\brief Reports whether a fraction is a lattice the measure walk and the snap lookup can use.

The grid's authoritative unit is a note value expressed as a fraction of a whole note (1/8 means
eighth notes in every meter). A usable lattice is a positive fraction whose numerator falls in
[1, g_max_tempo_grid_note_value_term] — past that the walk's integer step arithmetic stops being
worth trusting — and whose denominator falls in [1, g_tick_quantum_denominator], the finest lattice
any verb places on. The default-constructed Fraction value of 0/1 is invalid, so every owner of a
grid note value must initialize it explicitly; the editor default is
g_default_tempo_grid_note_value.

This is deliberately NOT the question the grid box asks, which is why there are two predicates and
not one bound doing double duty. "Can the walk walk this" must admit the tick, because the
placement quantum becomes exactly that note value while grid snap is off
(\ref placementQuantumNoteValue). "May a user pick this as the drawn grid" must not
(\ref isSelectableTempoGridNoteValue).

\param note_value Grid step expressed as a fraction of a whole note.
\return True when the note value can drive grid generation and snapping.
*/
[[nodiscard]] constexpr bool isValidTempoGridNoteValue(common::core::Fraction note_value) noexcept
{
    return note_value.numerator >= 1 && note_value.numerator <= g_max_tempo_grid_note_value_term &&
           note_value.denominator >= 1 && note_value.denominator <= g_tick_quantum_denominator;
}

/*!
\brief Reports whether a note value may be applied as the session's own grid.

Every walkable lattice, minus the ones nobody means as a grid: nothing finer than one
g_max_tempo_grid_note_value_term-th of a whole note. The session grid is the DRAWN one, generated
and painted line by line across the visible span (\ref visibleTempoGridLines), which at full
zoom-out is the whole song — so this bound is what keeps a typed 1/3840 out of the free-text grid
box, where snapping alone (a binary search) would never have noticed it. The tick lattice placement
falls back on is never drawn, which is exactly why it passes \ref isValidTempoGridNoteValue and
fails here.

\param note_value Grid step expressed as a fraction of a whole note.
\return True when the note value may be applied as the session grid.
*/
[[nodiscard]] constexpr bool isSelectableTempoGridNoteValue(
    common::core::Fraction note_value) noexcept
{
    return isValidTempoGridNoteValue(note_value) &&
           note_value.denominator <= g_max_tempo_grid_note_value_term;
}

/*!
\brief The one placement quantum: the note value every position-quantizing verb snaps onto.

The editor has exactly one answer to "what lattice does a position land on", and this is it —
note insert, note move, the sustain gesture's steps, marker placement, tone-region endpoints, and
every seek or caret click read it, with no per-verb opt-out. With snap on that is the session's
own grid; with snap off it is the tick lattice, which is why turning snap off does not lose the
precision the retired Ctrl tier used to reach (one step is one tick).

It is a POSITION rule and only a position rule. A verb needing a musical DURATION default — a
placed note's ring is the standing case — keeps reading the grid note value, because that is the
unit the user is authoring in; a 1/3840 default ring would be absurd. When a site is ambiguous,
ask whether the number is a place on the timeline or a length.

\param grid_note_value The session's grid step as a fraction of a whole note.
\param grid_snap True while grid snap is on.
\return The note value positions quantize to.
*/
[[nodiscard]] constexpr common::core::Fraction placementQuantumNoteValue(
    common::core::Fraction grid_note_value, bool grid_snap) noexcept
{
    return grid_snap ? grid_note_value : g_tick_quantum_note_value;
}

/*! \brief Musical rank of a tempo-grid line, ordered weakest to strongest. */
enum class TempoGridLineRank : std::uint8_t
{
    /*! \brief Fractional position between beats produced by a grid step finer than a beat. */
    Subdivision,

    /*! \brief Whole tempo-map beat that is not the first beat of its measure. */
    Beat,

    /*! \brief First beat of a measure (a downbeat). */
    Measure,
};

/*! \brief One vertical tempo-grid line resolved to a drawing column. */
struct TempoGridLine
{
    /*! \brief Zero-based pixel column within the drawing width. */
    int x{0};

    /*! \brief One-based measure number of the beat containing this line. */
    int measure{1};

    /*! \brief Musical rank used for styling and merged-column promotion. */
    TempoGridLineRank rank{TempoGridLineRank::Beat};

    /*!
    \brief Compares two grid lines by their stored fields.
    \param lhs Left-hand grid line.
    \param rhs Right-hand grid line.
    \return True when both lines store the same column, measure, and rank.
    */
    friend bool operator==(const TempoGridLine& lhs, const TempoGridLine& rhs) = default;
};

/*!
\brief Resolves the tempo-grid lines whose columns fall inside a visible pixel span.

The grid is measure-anchored in note-value units: within each measure, lines sit every
grid_note_value (as a fraction of a whole note) from the measure's downbeat, and the count
restarts at the next downbeat. A 1/8 grid therefore means eighth notes in every meter, every
downbeat carries a Measure-rank line even when the measure length is not a multiple of the step
(a 7/8 measure with a 1/4 grid), and lines landing on whole beats keep the stronger Beat rank.
Each line maps onto the drawing width with timelineXForPosition and only lines landing in
[visible_x_begin, visible_x_end) are returned, in ascending column order. Because line times
increase monotonically along the measure walk, the visible lines form a contiguous run: the scan
binary-searches the first line that can reach the span and stops once it passes the right edge,
so cost scales with the visible line count rather than the whole song. Lines that collapse onto a
single column when zoomed far out are merged, with the strongest rank keeping the column's color
and label identity.

\param tempo_map Song tempo map supplying signatures, the beat grid, and absolute beat times.
\param grid_note_value Grid step as a fraction of a whole note; an invalid value falls back to
       the default grid so rendering and snapping can never diverge.
\param visible_timeline Timeline range represented by the full drawing width.
\param width Full drawing width in pixels.
\param visible_x_begin Inclusive left pixel of the visible span, in drawing-width coordinates.
\param visible_x_end Exclusive right pixel of the visible span, in drawing-width coordinates.
\return Visible grid lines in ascending column order; empty when nothing is visible or the inputs
        are degenerate.
\note Relies on the tempo-map invariant that authored anchors give monotonically non-decreasing beat
      times; a malformed map can only misplace lines, never crash.
*/
[[nodiscard]] std::vector<TempoGridLine> visibleTempoGridLines(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int width, int visible_x_begin, int visible_x_end);

/*!
\brief Finds the tempo-grid time nearest to a target timeline position.

This is a pure musical-time query used for snap-to-grid timeline seek gestures: it never sees
pixels, so the snapped time is exact and independent of zoom or drawing width. The candidate
lines are the same measure-anchored note-value grid visibleTempoGridLines renders. Targets
exactly halfway between two grid lines resolve to the earlier line so repeated clicks snap
stably. Targets outside the authored beat range resolve to the first or last grid line. The
result may lie outside any particular visible range; callers bound the seek themselves.

\param tempo_map Song tempo map supplying signatures, the beat grid, and absolute beat times.
\param grid_note_value Grid step as a fraction of a whole note; an invalid value falls back to
       the default grid so rendering and snapping can never diverge.
\param target Timeline position to snap.
\return Timeline position of the nearest tempo-grid line.
*/
[[nodiscard]] common::core::TimePosition nearestTempoGridTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target);

/*!
\brief Finds the musical address of the tempo-grid line nearest to a target position.

Same candidate lines and tie-breaking as nearestTempoGridTime, but the result is the line's exact
musical position: the within-measure offset is an exact rational in the grid's note denominator,
so snapped placements of any grid — including odd values like 1/13 — store the grid line itself
rather than an approximation in some fixed fine grid.

\param tempo_map Song tempo map supplying signatures, the beat grid, and absolute beat times.
\param grid_note_value Grid step as a fraction of a whole note; an invalid value falls back to
       the default grid so rendering and snapping can never diverge.
\param target Timeline position to snap.
\return Exact musical position of the nearest tempo-grid line.
*/
[[nodiscard]] common::core::GridPosition nearestTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target);

/*!
\brief Converts a timeline-content x coordinate into a timeline seek position.

Placement always resolves the click to the exact time of the nearest quantum line, so the seek
target stays on the lattice at any zoom level instead of being quantized to the clicked pixel.
A caller wanting the raw click time asks \ref timelinePositionForX directly.

\param tempo_map Song tempo map supplying the snap grid.
\param placement_quantum Note value positions quantize to (\ref placementQuantumNoteValue).
\param visible_timeline Timeline range represented by the full timeline width.
\param timeline_width Full timeline content width in pixels.
\param timeline_x X coordinate in timeline-content coordinates.
\return Timeline seek position, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x);

/*!
\brief One grid step in beats at a measure: the note value scaled by the local meter's unit.

The note value is a fraction of a whole note and a beat is one signature-denominator unit, so
step_beats = note_value x denominator (a 1/8 grid in 6/8 steps one beat; in 4/4, half a beat).

\param tempo_map Song tempo map supplying the local time signature.
\param grid_note_value Grid step as a fraction of a whole note.
\param measure One-based measure whose meter scales the step.
\return The grid step as an exact beat fraction.
*/
[[nodiscard]] common::core::Fraction gridStepBeats(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value, int measure);

/*!
\brief The adjacent tempo-grid line strictly beyond a position, walked in exact rationals.

The one keyboard time-step primitive shared by the marker's caret stepping, the automation point
nudge, and the duration verb's grid step, so no two surfaces can land on different slots for the
same verb. It is \ref common::core::adjacentGridPosition under the editor's note-value validity
policy (an invalid value falls back to the default grid, exactly as rendering and snapping do):
from an off-grid position the result is the nearer line in the step direction (a step never jumps
past the adjacent line); from the lattice, the neighbouring line, read off the lattice directly.
The walk is exact-rational end to end — no seconds round-trip — and precisely reversible on any
grid and any meter, odd values and odd measure lengths included. At the grid origin stepping
earlier collapses onto \p from; callers treat that as a refusal.

\param tempo_map Song tempo map supplying signatures, the beat grid, and absolute beat times.
\param grid_note_value Grid step as a fraction of a whole note.
\param from Position to step from (any exact position, on- or off-grid).
\param later True to step later in time, false earlier.
\return Exact musical position of the adjacent grid line in the step direction.
*/
[[nodiscard]] common::core::GridPosition adjacentTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    const common::core::GridPosition& from, bool later);

/*!
\brief Converts an exact musical grid position to absolute song seconds.
\param tempo_map Song tempo map defining the grid.
\param position Musical position to convert.
\return The position's absolute time in seconds.
*/
[[nodiscard]] double secondsAtGridPosition(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& position);

} // namespace rock_hero::editor::core
