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
    // The arrival names its curve: the pitched one, which comes to rest, for every fretting-hand
    // approach; a light path's leg along a scrape's unpitched travel takes the release curve its
    // rail is drawn with.
    const double weight = highwaySlideEaseWeight(progress, next->unpitched_ramp);
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
