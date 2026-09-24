#include <algorithm>
#include <functional>
#include <iterator>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

[[nodiscard]] HighwayHandWindow settledWindow(const HighwayHandArrival& arrival) noexcept
{
    return HighwayHandWindow{.low_line = arrival.low_line, .high_line = arrival.high_line};
}

// The approach's weight at `progress` through a ramp of positive length: the ramp's own curve,
// and inside the settle stretch (\ref HighwayHandArrival::settle_seconds, never more than the
// ramp) the cubic from that curve's value and slope where the stretch begins to full travel with
// zero slope where it ends. The slope is scaled into the stretch's own unit so the join is
// continuous in time, not only in value.
[[nodiscard]] double approachWeight(
    const HighwayHandArrival& arrival, const double progress) noexcept
{
    const double settle =
        std::min(arrival.settle_seconds, arrival.ramp_seconds) / arrival.ramp_seconds;
    const double knee = 1.0 - settle;
    if (settle <= 0.0 || progress <= knee)
    {
        return highwaySlideEaseWeight(progress, arrival.unpitched_ramp);
    }
    return cubicHermite(
        highwaySlideEaseWeight(knee, arrival.unpitched_ramp),
        highwaySlideEaseSlope(knee, arrival.unpitched_ramp) * settle,
        1.0,
        0.0,
        std::clamp((progress - knee) / settle, 0.0, 1.0));
}

} // namespace

// Binary-searches the arrival-sorted track, then eases inside the next arrival's ramp. Per-frame
// consumers call this per element (and tails per sample), so the logarithmic bound matters on
// long charts.
HighwayHandWindow highwayHandWindowAt(
    const std::vector<HighwayHandArrival>& track, const double seconds) noexcept
{
    // The reference nut window applies only to chartless boards. With arrivals, the first one's
    // window already holds before its arrival: the song's opening scroll shows where the hand
    // belongs before the first note reaches the hit line, and the first "ramp" degenerates to no
    // motion, which also zeroes the light's motion dim.
    if (track.empty())
    {
        return HighwayHandWindow{.low_line = 0.0, .high_line = 4.0};
    }
    const auto next =
        std::ranges::upper_bound(track, seconds, std::ranges::less{}, &HighwayHandArrival::seconds);
    const HighwayHandWindow previous =
        settledWindow(next == track.begin() ? *next : *std::prev(next));
    if (next == track.end() || next->ramp_seconds <= 0.0 ||
        seconds < next->seconds - next->ramp_seconds)
    {
        return previous;
    }
    const HighwayHandWindow target = settledWindow(*next);
    const double progress = (seconds - (next->seconds - next->ramp_seconds)) / next->ramp_seconds;
    // The window eases with the SAME curve the drawn rail uses for this move: the pitched glide's
    // curve for a pitched ramp, the unpitched release curve for a slide-out. Easing every move with
    // the pitched one left the window and the rail sharing only their endpoints, which read as the
    // window not moving with the slide.
    const double weight = approachWeight(*next, progress);
    return HighwayHandWindow{
        .low_line = previous.low_line + ((target.low_line - previous.low_line) * weight),
        .high_line = previous.high_line + ((target.high_line - previous.high_line) * weight),
    };
}

// Distance-to-edge coverage: saturates one lane inside either edge, so a settled integer window
// scores exactly 1 on its own lines and 0 one line outside.
double highwayHandWindowLineCoverage(const HighwayHandWindow& window, const double line) noexcept
{
    return std::clamp(1.0 + std::min(line - window.low_line, window.high_line - line), 0.0, 1.0);
}

} // namespace rock_hero::common::core
