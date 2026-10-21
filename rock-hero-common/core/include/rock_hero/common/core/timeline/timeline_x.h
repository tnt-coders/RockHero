/*!
\file timeline_x.h
\brief The one mapping between timeline time and a drawing's horizontal pixel axis.
*/

#pragma once

#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::core
{

/*!
\brief THE time-to-x mapping: places a timeline time on a drawing that represents a visible range.

Every surface that draws or hit-tests the timeline maps through this one affine rule — the
editor's ruler, grid, cursor, tone rows and automation lanes (through editor core's
timelineXForPosition) and the shared tab lane (TabLaneGeometry::x) — so a mark never lands a pixel
apart from the rail, grid line or cursor it stands on. The visible range spans the drawing's whole
width: its start lies at x 0 and its end at x == width, the right edge of the last column, so
width / duration is exactly the drawing's pixels per second. A time outside the range extrapolates
past the edges; a point mark that must stay visible at the very end holds its own drawn column.

\param seconds Timeline time to place.
\param visible_timeline Timeline range the drawing width represents; its duration must be
       positive.
\param width Drawing width in pixels; must be positive.
\return X coordinate measured from the drawing's left edge.
*/
[[nodiscard]] constexpr double timelineXForSeconds(
    double seconds, TimeRange visible_timeline, double width) noexcept
{
    return (seconds - visible_timeline.start.seconds) / visible_timeline.duration().seconds * width;
}

/*!
\brief The inverse of \ref timelineXForSeconds: the timeline time under a drawing's x coordinate.

\param x X coordinate measured from the drawing's left edge; one outside [0, width] extrapolates
       past the visible range.
\param visible_timeline Timeline range the drawing width represents; its duration must be
       positive.
\param width Drawing width in pixels; must be positive.
\return Timeline time in seconds.
*/
[[nodiscard]] constexpr double timelineSecondsForX(
    double x, TimeRange visible_timeline, double width) noexcept
{
    return visible_timeline.start.seconds + ((x / width) * visible_timeline.duration().seconds);
}

} // namespace rock_hero::common::core
