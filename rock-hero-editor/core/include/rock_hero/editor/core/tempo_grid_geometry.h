/*!
\file tempo_grid_geometry.h
\brief Pure tempo-grid line geometry for editor timeline rendering.
*/

#pragma once

#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One vertical tempo-grid line resolved to a drawing column. */
struct TempoGridLine
{
    /*! \brief Zero-based pixel column within the drawing width. */
    int x{0};

    /*! \brief One-based measure number for the beat represented by this line. */
    int measure{1};

    /*! \brief One-based beat number within the represented measure. */
    int beat{1};

    /*! \brief True when the line marks the first beat of a measure (a downbeat). */
    bool measure_start{false};

    /*!
    \brief Compares two grid lines by their stored fields.
    \param lhs Left-hand grid line.
    \param rhs Right-hand grid line.
    \return True when both lines store the same column, musical position, and measure flag.
    */
    friend bool operator==(const TempoGridLine& lhs, const TempoGridLine& rhs) = default;
};

/*!
\brief Resolves the tempo-grid lines whose columns fall inside a visible pixel span.

Maps each song beat onto the drawing width with timelineXForPosition and returns only the lines
landing in [visible_x_begin, visible_x_end), in ascending column order. Because beat times increase
monotonically with the global beat index, the visible beats form a contiguous run: the scan
binary-searches the first beat that can reach the span and stops once it passes the right edge, so
cost scales with the visible beat count rather than the whole song. Beats that collapse onto a
single column when zoomed far out are merged, with downbeats taking colour and label priority.

\param tempo_map Song tempo map supplying the beat grid and absolute beat times.
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
    const common::core::TempoMap& tempo_map, common::core::TimeRange visible_timeline, int width,
    int visible_x_begin, int visible_x_end);

} // namespace rock_hero::editor::core
