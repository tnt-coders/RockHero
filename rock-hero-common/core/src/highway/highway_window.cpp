#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <span>
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

// THE LEG LOOKUP, given the first arrival after `seconds` (the one upper_bound both public readers
// take): the leg is that arrival's ramp when it has begun by then. The first arrival never has one
// — its window already holds before it arrives — and neither does a zero ramp.
[[nodiscard]] std::optional<HighwayHandLeg> legBefore(
    const std::span<const HighwayHandArrival> track,
    const std::span<const HighwayHandArrival>::iterator next, const double seconds) noexcept
{
    if (next == track.begin() || next == track.end() || next->ramp_seconds <= 0.0 ||
        seconds < next->seconds - next->ramp_seconds)
    {
        return std::nullopt;
    }
    return HighwayHandLeg{
        .from = &*std::prev(next),
        .to = &*next,
        .progress = (seconds - (next->seconds - next->ramp_seconds)) / next->ramp_seconds,
    };
}

// The first arrival strictly after `seconds`: arrivals are inclusive, so one AT `seconds` has
// already arrived.
[[nodiscard]] std::span<const HighwayHandArrival>::iterator nextArrival(
    const std::span<const HighwayHandArrival> track, const double seconds) noexcept
{
    return std::ranges::upper_bound(
        track, seconds, std::ranges::less{}, &HighwayHandArrival::seconds);
}

// THE ONE DENSITY POLICY every leg the sampler walks is sliced by, in slices per fret of travel.
// Six slices flat sufficed for a tapped glide's few-fret travel but faceted a scrape's dozen-fret
// leg into visible straights; four per fret keeps the eased curve under half a fret per slice at
// its steepest, which makes the density a property of the TRAVEL rather than of the segment. The
// floor keeps a travel of well under a fret reading as a curve, and the ceiling keeps a full-neck
// sweep inside a batch's budget.
constexpr double g_glide_slices_per_fret = 4.0;
constexpr int g_glide_slice_min = 6;
constexpr int g_glide_slice_max = 64;

// How many straight slices a leg is drawn as, for its travel in fret units.
[[nodiscard]] int glideSliceCount(const double sweep_frets) noexcept
{
    return std::clamp(
        static_cast<int>(std::ceil(sweep_frets * g_glide_slices_per_fret)),
        g_glide_slice_min,
        g_glide_slice_max);
}

} // namespace

// Rationale lives on the declaration in highway_window.h.
std::optional<HighwayHandLeg> highwayHandLegAt(
    const std::span<const HighwayHandArrival> track, const double seconds) noexcept
{
    return legBefore(track, nextArrival(track, seconds), seconds);
}

// Rationale lives on the declaration in highway_window.h. The walk runs from the first arrival
// after `from` through the first after `to`: an instant in between eases through the ramp of the
// first arrival after it, so no later leg can move the window inside the range. An arrival's own
// instant is the end of its leg's last slice, which is why the slices stop short of it.
void highwayTrackSampleTimes(
    const std::span<const HighwayHandArrival> track, const double from_seconds,
    const double to_seconds, std::vector<double>& times)
{
    times.push_back(from_seconds);
    times.push_back(to_seconds);
    const auto keep_inside = [&](const double seconds) {
        if (seconds > from_seconds && seconds < to_seconds)
        {
            times.push_back(seconds);
        }
    };
    const auto first = nextArrival(track, from_seconds);
    const auto past = nextArrival(track, to_seconds);
    const auto last = past == track.end() ? past : std::next(past);
    for (auto it = first; it != last; ++it)
    {
        const HighwayHandArrival& arrival = *it;
        keep_inside(arrival.seconds);
        if (it == track.begin() || arrival.ramp_seconds <= 0.0)
        {
            continue;
        }
        const HighwayHandArrival& previous = *std::prev(it);
        const double sweep = std::max(
            std::abs(arrival.low_line - previous.low_line),
            std::abs(arrival.high_line - previous.high_line));
        if (sweep <= 0.0)
        {
            continue;
        }
        const double ramp_start = arrival.seconds - arrival.ramp_seconds;
        const int slices = glideSliceCount(sweep);
        for (int slice = 0; slice < slices; ++slice)
        {
            keep_inside(
                ramp_start +
                (arrival.ramp_seconds * static_cast<double>(slice) / static_cast<double>(slices)));
        }
    }
}

// Rationale lives on the declaration in highway_window.h. Asks the same question as every other
// same-instant test on the board — are these two highway times one moment — so it reads the same
// named tolerance rather than repeating the number.
void highwaySortUniqueTimes(std::vector<double>& times)
{
    std::ranges::sort(times);
    const auto [first_duplicate, last_duplicate] =
        std::ranges::unique(times, [](const double lhs, const double rhs) {
            return std::abs(rhs - lhs) < g_onset_match_epsilon;
        });
    times.erase(first_duplicate, last_duplicate);
}

// Binary-searches the arrival-sorted track once, then eases through the leg in progress there.
// Per-frame consumers call this per element (and tails per sample), so the logarithmic bound
// matters on long charts.
HighwayHandWindow highwayHandWindowAt(
    const std::span<const HighwayHandArrival> track, const double seconds) noexcept
{
    // The reference nut window applies only to chartless boards. With arrivals, the first one's
    // window already holds before its arrival: the song's opening scroll shows where the hand
    // belongs before the first note reaches the hit line, and the first "ramp" degenerates to no
    // motion, which also zeroes the light's motion dim.
    if (track.empty())
    {
        return HighwayHandWindow{
            .low_line = 0.0,
            .high_line = static_cast<double>(g_min_fret_hand_width),
        };
    }
    const auto next = nextArrival(track, seconds);
    const std::optional<HighwayHandLeg> leg = legBefore(track, next, seconds);
    if (!leg.has_value())
    {
        return settledWindow(next == track.begin() ? *next : *std::prev(next));
    }
    const HighwayHandWindow previous = settledWindow(*leg->from);
    const HighwayHandWindow target = settledWindow(*leg->to);
    // The window eases with the SAME curve the drawn rail uses for this move: the pitched glide's
    // curve for a pitched ramp, the unpitched release curve for a slide-out. Easing every move with
    // the pitched one left the window and the rail sharing only their endpoints, which read as the
    // window not moving with the slide.
    const double weight = approachWeight(*leg->to, leg->progress);
    return HighwayHandWindow{
        .low_line = previous.low_line + ((target.low_line - previous.low_line) * weight),
        .high_line = previous.high_line + ((target.high_line - previous.high_line) * weight),
    };
}

// Rationale lives on the declaration in highway_window.h. After the release the light follows only
// the leg in progress AT the release, to that leg's end.
double highwayLitTrackTime(
    const std::span<const HighwayHandArrival> track, const HighwayLitStretch& stretch,
    const double seconds) noexcept
{
    if (seconds < stretch.start_seconds)
    {
        return stretch.start_seconds;
    }
    if (seconds <= stretch.release_seconds)
    {
        return seconds;
    }
    const std::optional<HighwayHandLeg> leg = highwayHandLegAt(track, stretch.release_seconds);
    return std::min(seconds, leg.has_value() ? leg->to->seconds : stretch.release_seconds);
}

// Rationale lives on the declaration in highway_window.h.
HighwayHandWindow highwayLitWindowAt(
    const std::span<const HighwayHandArrival> track, const HighwayLitStretch& stretch,
    const double seconds) noexcept
{
    return highwayHandWindowAt(track, highwayLitTrackTime(track, stretch, seconds));
}

// Rationale lives on the declaration in highway_window.h. Walks back from the window standing at
// the held instant, one settled window at a time, until one covers the line. Each window stood
// until the next leg's ramp began, and the walk stops at the light's start or once a window's end
// lies a whole decay back, so it visits only the few windows a decay spans.
double highwayLitLineAfterglowAt(
    const std::span<const HighwayHandArrival> track, const HighwayLitStretch& stretch,
    const double line, const double seconds, const double decay_seconds) noexcept
{
    if (track.empty() || seconds < stretch.start_seconds)
    {
        return 0.0;
    }
    const double held = std::min(seconds, stretch.release_seconds);
    const auto next = nextArrival(track, held);
    auto settled = next == track.begin() ? next : std::prev(next);
    double stood_until =
        legBefore(track, next, held).has_value() ? next->seconds - next->ramp_seconds : held;
    while (stood_until >= stretch.start_seconds && stood_until > seconds - decay_seconds)
    {
        if (highwayHandWindowLineCoverage(settledWindow(*settled), line) >= 1.0)
        {
            return 1.0 - ((seconds - stood_until) / decay_seconds);
        }
        if (settled == track.begin())
        {
            break;
        }
        stood_until = settled->seconds - settled->ramp_seconds;
        settled = std::prev(settled);
    }
    return 0.0;
}

// Rationale lives on the declaration in highway_window.h.
HighwayHandWindow highwayBoxSidesAt(
    const std::span<const HighwayHandArrival> track, const double onset_seconds,
    const double now_seconds) noexcept
{
    return highwayHandWindowAt(track, std::max(onset_seconds, now_seconds));
}

// Distance-to-edge coverage: saturates one lane inside either edge, so a settled integer window
// scores exactly 1 on its own lines and 0 one line outside.
double highwayHandWindowLineCoverage(const HighwayHandWindow& window, const double line) noexcept
{
    return std::clamp(1.0 + std::min(line - window.low_line, window.high_line - line), 0.0, 1.0);
}

} // namespace rock_hero::common::core
