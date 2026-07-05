/*!
\file timeline_geometry.h
\brief Pure timeline-to-pixel mapping helpers for editor timeline views.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/domain/timeline.h>

namespace rock_hero::editor::core
{

/*! \brief Selects how timeline positions outside the visible range are handled. */
enum class TimelinePositionClamping : std::uint8_t
{
    /*! \brief Clamp outside positions to the nearest visible pixel. */
    ClampToVisibleRange,

    /*! \brief Return no coordinate for outside positions. */
    RejectOutsideVisibleRange,
};

/*!
\brief Maps a timeline position into a subpixel x coordinate.
\param position Timeline position to map.
\param visible_timeline Timeline range represented by the drawing width.
\param width Drawing width in pixels.
\param clamping Outside-range handling policy.
\return Subpixel x coordinate in [0, width - 1], or empty when no coordinate can be mapped.
*/
[[nodiscard]] std::optional<float> timelineXForPosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline, int width,
    TimelinePositionClamping clamping) noexcept;

/*!
\brief Maps a subpixel x coordinate back to its timeline position.

Inverse of timelineXForPosition's interior mapping: column 0 is the visible start and column
width - 1 is the visible end. Coordinates outside [0, width - 1] clamp to the visible range, and a
single-pixel width resolves to the visible start.

\param x Subpixel x coordinate in drawing-width coordinates.
\param visible_timeline Timeline range represented by the drawing width.
\param width Drawing width in pixels.
\return Timeline position for the coordinate, or empty when the geometry cannot be mapped.
*/
[[nodiscard]] std::optional<common::core::TimePosition> timelinePositionForX(
    float x, common::core::TimeRange visible_timeline, int width) noexcept;

} // namespace rock_hero::editor::core
