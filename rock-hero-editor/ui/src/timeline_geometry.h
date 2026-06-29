/*!
\file timeline_geometry.h
\brief Private timeline-to-pixel mapping helpers shared by editor UI components.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/timeline.h>

namespace rock_hero::editor::ui
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
[[nodiscard]] inline std::optional<float> timelineXForPosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline, int width,
    TimelinePositionClamping clamping) noexcept
{
    const common::core::TimeDuration visible_duration = visible_timeline.duration();
    if (width <= 0 || visible_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    const double relative_position =
        (position.seconds - visible_timeline.start.seconds) / visible_duration.seconds;
    if (clamping == TimelinePositionClamping::RejectOutsideVisibleRange &&
        (relative_position < 0.0 || relative_position > 1.0))
    {
        return std::nullopt;
    }

    const double clamped_position = std::clamp(relative_position, 0.0, 1.0);
    const auto max_x = static_cast<double>(width - 1);
    return static_cast<float>(clamped_position * max_x);
}

} // namespace rock_hero::editor::ui
