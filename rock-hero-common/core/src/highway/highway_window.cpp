#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Fret-line extent of one settled placement: the window covers frets [fret, fret + width - 1],
// so its edges sit on lines fret - 1 and fret + width - 1.
[[nodiscard]] HighwayHandWindow settledWindow(const HighwayFhpView& fhp) noexcept
{
    return HighwayHandWindow{
        .low_line = static_cast<double>(fhp.fret - 1),
        .high_line = static_cast<double>(fhp.fret + fhp.width - 1),
    };
}

} // namespace

// Binary-searches the arrival-sorted placements, then eases inside the next placement's ramp.
// Per-frame consumers call this per element (and tails per sample), so the logarithmic bound
// matters on long charts.
HighwayHandWindow highwayHandWindowAt(
    const std::vector<HighwayFhpView>& fret_hand_positions, const double seconds) noexcept
{
    // The reference nut window before any placement (and for chartless boards).
    HighwayHandWindow previous{.low_line = 0.0, .high_line = 4.0};
    const auto next = std::ranges::upper_bound(
        fret_hand_positions, seconds, std::ranges::less{}, [](const HighwayFhpView& fhp) {
            return fhp.seconds;
        });
    if (next != fret_hand_positions.begin())
    {
        previous = settledWindow(*std::prev(next));
    }
    if (next == fret_hand_positions.end() || next->ramp_seconds <= 0.0 ||
        seconds < next->seconds - next->ramp_seconds)
    {
        return previous;
    }
    const HighwayHandWindow target = settledWindow(*next);
    const double progress = (seconds - (next->seconds - next->ramp_seconds)) / next->ramp_seconds;
    // The pitched slide ease for every window move (signed 2026-07-23): zero slope at both
    // endpoints, so the border leaves and rejoins the settled edges tangentially.
    const double weight = highwaySlideEaseWeight(progress, false);
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
