/*!
\file tempo_grid_geometry.h
\brief Pure tempo-grid line geometry and snap lookup for the editor timeline.
*/

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Inclusive upper bound for tempo-grid spacing numerator and denominator values. */
inline constexpr int g_max_tempo_grid_spacing_term = 128;

/*!
\brief Reports whether a fraction is usable as a tempo-grid step measured in beats.

Valid spacing is a positive fraction whose numerator and denominator each fall in
[1, g_max_tempo_grid_spacing_term]. The default-constructed Fraction value of 0/1 is invalid, so
every owner of a spacing value must initialize it explicitly; the whole-beat grid is
Fraction{1, 1}.

\param spacing Grid step measured in tempo-map beats.
\return True when the spacing can drive grid generation and snapping.
*/
[[nodiscard]] constexpr bool isValidTempoGridSpacing(common::core::Fraction spacing) noexcept
{
    return spacing.numerator >= 1 && spacing.numerator <= g_max_tempo_grid_spacing_term &&
           spacing.denominator >= 1 && spacing.denominator <= g_max_tempo_grid_spacing_term;
}

/*!
\brief Converts a beat-relative grid step into its user-facing note value.

Display and entry share note-value units, expressed as fractions of a whole note: a half-beat step
in 4/4 displays as 1/8. The conversion assumes the tempo-map beat corresponds to the
time-signature denominator, and clamps a non-positive denominator to one so a malformed map cannot
collapse the result to Fraction's 0/1 error value.

\param grid_spacing_beats Grid step measured in tempo-map beats.
\param time_signature_denominator Note value that represents one beat.
\return Note value as a reduced fraction of a whole note.
*/
[[nodiscard]] constexpr common::core::Fraction displayedTempoGridNoteValue(
    common::core::Fraction grid_spacing_beats, int time_signature_denominator) noexcept
{
    const int denominator = time_signature_denominator < 1 ? 1 : time_signature_denominator;
    return common::core::Fraction{
        grid_spacing_beats.numerator, grid_spacing_beats.denominator * denominator
    };
}

/*!
\brief Converts a user-entered note value into a beat-relative grid step.

Inverse of displayedTempoGridNoteValue: entering 1/16 in 4/4 yields a quarter-beat step. Callers
must validate the result with isValidTempoGridSpacing before using it, because out-of-bounds
entries can convert to steps outside the supported spacing range. Products that cannot round-trip
through int collapse to Fraction's 0/1 invalid value instead of overflowing.

\param note_value Note value expressed as a fraction of a whole note.
\param time_signature_denominator Note value that represents one beat.
\return Grid step measured in tempo-map beats, as a reduced fraction.
*/
[[nodiscard]] constexpr common::core::Fraction tempoGridSpacingFromNoteValue(
    common::core::Fraction note_value, int time_signature_denominator) noexcept
{
    const int denominator = time_signature_denominator < 1 ? 1 : time_signature_denominator;

    // The product runs on unvalidated user entry, so it widens to 64 bits; any product outside
    // int is far outside the valid spacing bounds anyway, so it becomes the invalid 0/1 value for
    // isValidTempoGridSpacing to reject rather than signed-overflow undefined behavior. Products
    // below one are equally invalid and would otherwise wrap through the narrowing cast.
    const std::int64_t numerator = static_cast<std::int64_t>(note_value.numerator) * denominator;
    if (numerator < 1 || numerator > std::numeric_limits<int>::max())
    {
        return common::core::Fraction{0, 1};
    }

    return common::core::Fraction{static_cast<int>(numerator), note_value.denominator};
}

/*! \brief Musical rank of a tempo-grid line, ordered weakest to strongest. */
enum class TempoGridLineRank : std::uint8_t
{
    /*! \brief Fractional position between beats produced by sub-beat grid spacing. */
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

Grid lines sit every grid_spacing_beats along the tempo map's beat axis, so a spacing of 1/2
places a subdivision line halfway through every beat while whole beats and downbeats keep their
stronger rank. Each line maps onto the drawing width with timelineXForPosition and only lines
landing in [visible_x_begin, visible_x_end) are returned, in ascending column order. Because line
times increase monotonically with the grid-line index, the visible lines form a contiguous run:
the scan binary-searches the first line that can reach the span and stops once it passes the right
edge, so cost scales with the visible line count rather than the whole song. Lines that collapse
onto a single column when zoomed far out are merged, with the strongest rank keeping the column's
color and label identity.

\param tempo_map Song tempo map supplying the beat grid and absolute beat times.
\param grid_spacing_beats Grid step measured in tempo-map beats; invalid spacing falls back to the
       whole-beat grid so rendering and snapping can never diverge.
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
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
    common::core::TimeRange visible_timeline, int width, int visible_x_begin, int visible_x_end);

/*!
\brief Finds the tempo-grid time nearest to a target timeline position.

This is a pure musical-time query used for snap-to-grid timeline seek gestures: it never sees
pixels, so the snapped time is exact and independent of zoom or drawing width. Targets exactly
halfway between two grid lines resolve to the earlier line so repeated clicks snap stably. Targets
outside the authored beat range resolve to the first or last grid line. The result may lie outside
any particular visible range; callers bound the seek themselves.

\param tempo_map Song tempo map supplying the beat grid and absolute beat times.
\param grid_spacing_beats Grid step measured in tempo-map beats; invalid spacing falls back to the
       whole-beat grid so rendering and snapping can never diverge.
\param target Timeline position to snap.
\return Timeline position of the nearest tempo-grid line.
*/
[[nodiscard]] common::core::TimePosition nearestTempoGridTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
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
\param grid_spacing_beats Grid step measured in tempo-map beats shared with grid rendering.
\param visible_timeline Timeline range represented by the full timeline width.
\param timeline_width Full timeline content width in pixels.
\param timeline_x X coordinate in timeline-content coordinates.
\param mode Whether placement should snap to the grid or stay at the click point.
\return Timeline seek position, or empty for invalid timeline geometry.
*/
[[nodiscard]] std::optional<common::core::TimePosition> timelineCursorPlacementTime(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats,
    common::core::TimeRange visible_timeline, int timeline_width, float timeline_x,
    TimelineCursorPlacementMode mode);

} // namespace rock_hero::editor::core
