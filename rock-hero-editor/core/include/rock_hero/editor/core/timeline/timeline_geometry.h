/*!
\file timeline_geometry.h
\brief Pure timeline-to-pixel mapping helpers for editor timeline views.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::editor::core
{

/*! \brief Selects how timeline positions outside the visible range are handled. */
enum class TimelinePositionClamping : std::uint8_t
{
    /*! \brief Clamp outside positions to the nearest visible pixel. */
    ClampToVisibleRange,

    /*! \brief Return no coordinate for outside positions. */
    RejectOutsideVisibleRange,

    /*!
    \brief Map outside positions past the drawing's edges like any other, for a mark that is
           partly scrolled off or a segment that runs to a point beyond the visible range.
    */
    ExtendPastVisibleRange,
};

/*!
\brief Maps a timeline position into a subpixel x coordinate.

The editor's entry to THE time-to-x mapping, \ref common::core::timelineXForSeconds: the visible
start lies at x 0 and the visible end at x == width, so every editor row, the ruler, the grid and
the shared tab lane place a time on the same pixel. This adds only the outside-range policy and
the guard against geometry that cannot be mapped.

\param position Timeline position to map.
\param visible_timeline Timeline range represented by the drawing width.
\param width Drawing width in pixels.
\param clamping Outside-range handling policy.
\return Subpixel x coordinate (in [0, width] unless extended past the range), or empty when no
        coordinate can be mapped.
*/
[[nodiscard]] std::optional<float> timelineXForPosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline, int width,
    TimelinePositionClamping clamping) noexcept;

/*!
\brief Maps a subpixel x coordinate back to its timeline position.

Inverse of timelineXForPosition through \ref common::core::timelineSecondsForX: x 0 is the visible
start and x == width is the visible end. Coordinates outside [0, width] clamp to the visible
range.

\param x Subpixel x coordinate in drawing-width coordinates.
\param visible_timeline Timeline range represented by the drawing width.
\param width Drawing width in pixels.
\return Timeline position for the coordinate, or empty when the geometry cannot be mapped.
*/
[[nodiscard]] std::optional<common::core::TimePosition> timelinePositionForX(
    float x, common::core::TimeRange visible_timeline, int width) noexcept;

/*!
\brief The pixel column a one-pixel point mark at x draws in.

The mapping puts the visible end at x == width, the right edge of the last column, where no column
is left to draw in: a point mark there (the cursor at the song's end, the grid's terminal line)
holds the last column instead. This is the one place that end is held, so the mapping itself stays
exact and every point mark that reaches the end lands on the same column. A mark short of the end
takes the column its x rounds into.

\param x Subpixel x coordinate in drawing-width coordinates.
\param width Drawing width in pixels.
\return The column, at most width - 1.
*/
[[nodiscard]] int timelineColumnForX(float x, int width) noexcept;

} // namespace rock_hero::editor::core
