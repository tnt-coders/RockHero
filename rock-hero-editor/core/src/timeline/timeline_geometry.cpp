#include "timeline/timeline_geometry.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/common/core/timeline/timeline_x.h>

namespace rock_hero::editor::core
{

// Pure presentation math kept in editor-core so it is unit-testable without JUCE: places a
// timeline position through the one time-to-x mapping, then applies the outside-range policy.
std::optional<float> timelineXForPosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline, int width,
    TimelinePositionClamping clamping) noexcept
{
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const auto drawing_width = static_cast<double>(width);
    const double x =
        common::core::timelineXForSeconds(position.seconds, visible_timeline, drawing_width);
    switch (clamping)
    {
        case TimelinePositionClamping::ClampToVisibleRange:
        {
            return static_cast<float>(std::clamp(x, 0.0, drawing_width));
        }
        case TimelinePositionClamping::RejectOutsideVisibleRange:
        {
            if (x < 0.0 || x > drawing_width)
            {
                return std::nullopt;
            }
            return static_cast<float>(x);
        }
        case TimelinePositionClamping::ExtendPastVisibleRange:
        {
            return static_cast<float>(x);
        }
    }
    return std::nullopt;
}

// Inverse of the same mapping, used to turn click coordinates into timeline positions before any
// snapping. Clamping keeps out-of-bounds coordinates inside the visible range instead of
// extrapolating past it.
std::optional<common::core::TimePosition> timelinePositionForX(
    float x, common::core::TimeRange visible_timeline, int width) noexcept
{
    if (width <= 0 || visible_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const auto drawing_width = static_cast<double>(width);
    return common::core::TimePosition{common::core::timelineSecondsForX(
        std::clamp(static_cast<double>(x), 0.0, drawing_width), visible_timeline, drawing_width)};
}

// Rounds a subpixel x to its pixel column, holding the visible end inside the last column.
int timelineColumnForX(float x, int width) noexcept
{
    return std::min(static_cast<int>(std::round(x)), width - 1);
}

} // namespace rock_hero::editor::core
