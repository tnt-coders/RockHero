#include "timeline/timeline_geometry.h"

#include <algorithm>

namespace rock_hero::editor::core
{

// Pure presentation math kept in editor-core so it is unit-testable without JUCE: maps a timeline
// position onto a drawing width's [0, width - 1] pixel span, honoring the outside-range policy.
std::optional<float> timelineXForPosition(
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

// Inverse of timelineXForPosition's interior mapping, used to turn click coordinates into timeline
// positions before any snapping. Clamping keeps out-of-bounds coordinates inside the visible range
// instead of extrapolating past it.
std::optional<common::core::TimePosition> timelinePositionForX(
    float x, common::core::TimeRange visible_timeline, int width) noexcept
{
    const common::core::TimeDuration visible_duration = visible_timeline.duration();
    if (width <= 0 || visible_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    const auto max_x = static_cast<double>(width - 1);
    if (max_x <= 0.0)
    {
        // A single-pixel canvas has no interior span; its only column is the visible start.
        return visible_timeline.start;
    }

    const double relative_position = std::clamp(static_cast<double>(x), 0.0, max_x) / max_x;
    return common::core::TimePosition{
        visible_timeline.start.seconds + relative_position * visible_duration.seconds
    };
}

} // namespace rock_hero::editor::core
