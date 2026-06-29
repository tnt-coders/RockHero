#include "timeline_geometry.h"

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

} // namespace rock_hero::editor::core
