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

/*! \brief Inclusive upper bound for grid note-value numerator and denominator values. */
inline constexpr int g_max_tempo_grid_note_value_term = 128;

/*!
\brief Reports whether a fraction is usable as the grid note value.

The grid's authoritative unit is a note value expressed as a fraction of a whole note (1/8 means
eighth notes in every meter). A valid note value is a positive fraction whose numerator and
denominator each fall in [1, g_max_tempo_grid_note_value_term]. The default-constructed Fraction
value of 0/1 is invalid, so every owner of a grid note value must initialize it explicitly; the
editor default is the quarter-note grid Fraction{1, 4}.

\param note_value Grid step expressed as a fraction of a whole note.
\return True when the note value can drive grid generation and snapping.
*/
[[nodiscard]] constexpr bool isValidTempoGridNoteValue(common::core::Fraction note_value) noexcept
{
    return note_value.numerator >= 1 && note_value.numerator <= g_max_tempo_grid_note_value_term &&
           note_value.denominator >= 1 &&
           note_value.denominator <= g_max_tempo_grid_note_value_term;
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
       the quarter-note grid so rendering and snapping can never diverge.
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
       the quarter-note grid so rendering and snapping can never diverge.
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
       the quarter-note grid so rendering and snapping can never diverge.
\param target Timeline position to snap.
\return Exact musical position of the nearest tempo-grid line.
*/
[[nodiscard]] common::core::GridPosition nearestTempoGridPosition(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimePosition target);

/*! \brief Controls whether cursor placement snaps to the tempo grid or keeps the click point. */
enum class TimelineCursorPlacementMode : std::uint8_t
{
    /*! \brief Resolve the click to the exact time of the nearest tempo-grid line. */
    SnapToGrid,

    /*! \brief Keep the sub-pixel click point's own time. */
    Free,
};

/*!
\brief Converts a timeline-content x coordinate into a timeline seek position.

Snap placement resolves the click to the exact time of the nearest tempo-grid line, so the seek
target stays on the beat at any zoom level instead of being quantized to the clicked pixel. Free
placement keeps the sub-pixel click point's time.

\param tempo_map Song tempo map supplying the snap grid.
\param grid_note_value Grid step as a fraction of a whole note, shared with grid rendering.
\param visible_timeline Timeline range represented by the full timeline width.
\param timeline_width Full timeline content width in pixels.
\param timeline_x X coordinate in timeline-content coordinates.
\param mode Whether placement should snap to the grid or stay at the click point.
\return Timeline seek position, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x,
    TimelineCursorPlacementMode mode);

/*!
\brief Denominator of the fine placement grid used when precision bypasses the visible grid.

1/960 of a beat keeps every stored position an exact rational at far finer than audible
resolution: 960 is the standard MIDI PPQ resolution and divides every practical straight,
triplet, and quintuplet subdivision. Ctrl composes it onto placements and moves on every
surface — automation points, tone boundaries, ruler seeks, and chart-note verbs alike (the
off-grid unification: snap default follows the data, the precision capability is uniform).
*/
inline constexpr int g_fine_grid_denominator = 960;

/*!
\brief Quantizes a fractional global-beat position to the exact rational 1/960-beat fine grid.

The seam shared by the Ctrl placement gestures (tone boundaries, automation points, the lane
caret arm) and playhead-anchored tone-marker insertion: it turns a seconds- or beat-derived
double into a storable position that stays an exact rational (never a raw double) at
sub-millisecond resolution. A caller that already holds an exact musical position should keep
it verbatim rather than round-tripping through this — keyboard fine MOVES advance exact
rationals relatively and have no caller here.

\param tempo_map Song tempo map supplying the beat-to-measure mapping.
\param global_beat Global beat position (whole beats plus fractional beat) to quantize.
\return Exact musical position on the fine grid.
*/
[[nodiscard]] common::core::GridPosition fineGridPositionForBeat(
    const common::core::TempoMap& tempo_map, double global_beat);

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

The one keyboard time-step primitive shared by the marker's caret stepping and the automation
point nudge, so the two surfaces can never land on different slots for the same verb. From an
off-grid position whose nearest line lies in the step direction, the result is that line (a
step never jumps past the adjacent line); from the lattice, the position advances one grid
step and re-snaps, with a second push when a measure-anchored grid restart would otherwise
swallow the step. The walk is exact-rational end to end — no seconds round-trip — so stepping
is precisely reversible on any grid, odd values included. At the map edge the result can
collapse onto \p from; callers treat that as a refusal.

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
