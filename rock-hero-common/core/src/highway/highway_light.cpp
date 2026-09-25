#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// THE fold, one item at a time over a run sorted by start and seeded from its first item: the run
// keeps its earliest start, takes the latest release, and takes the widest rise only from items
// starting at that earliest start — a later item's rise ends inside the light, which is already
// full there. The seed is the earliest item because the run is sorted.
void foldLitEvidence(HighwayLitStretch& run, const HighwayLitStretch& item) noexcept
{
    run.release_seconds = std::max(run.release_seconds, item.release_seconds);
    if (item.start_seconds - run.start_seconds < g_onset_match_epsilon)
    {
        run.rise_seconds = std::max(run.rise_seconds, item.rise_seconds);
    }
}

// THE crowding clamp: a rise never reaches back past the previous stretch's release. The gap
// cannot be negative here, because the merge only splits a run at a gap of at least the tolerance,
// which is itself never negative.
[[nodiscard]] HighwayLitStretch crowdedAfter(
    HighwayLitStretch stretch, const HighwayLitStretch& previous) noexcept
{
    const double gap = stretch.start_seconds - previous.release_seconds;
    assert(gap >= 0.0 && "the merge splits only at a gap of at least the rest tolerance");
    stretch.rise_seconds = std::min(stretch.rise_seconds, gap);
    return stretch;
}

} // namespace

// Rationale lives on the declaration in highway_light.h.
HighwayLitInterval highwayLitInterval(
    const HighwayLitStretch& stretch, const double decay_seconds) noexcept
{
    return HighwayLitInterval{
        .from_seconds = stretch.start_seconds - stretch.rise_seconds,
        .to_seconds = stretch.release_seconds + decay_seconds,
    };
}

// A zero rise lights nothing before the start, stated rather than divided by: division by zero
// is undefined in C++, whatever the hardware would return.
double highwayLightLevel(
    const HighwayLitStretch& stretch, const double seconds, const double decay_seconds) noexcept
{
    if (seconds < stretch.start_seconds)
    {
        return stretch.rise_seconds > 0.0
                   ? std::clamp(
                         (seconds - highwayLitInterval(stretch, decay_seconds).from_seconds) /
                             stretch.rise_seconds,
                         0.0,
                         1.0)
                   : 0.0;
    }
    if (seconds > stretch.release_seconds)
    {
        return std::clamp(1.0 - ((seconds - stretch.release_seconds) / decay_seconds), 0.0, 1.0);
    }
    return 1.0;
}

// Rationale lives on the declaration in highway_light.h. The runs are folded into the front of the
// sorted evidence itself — a run never starts before the slot its stretch is written to — so the
// merge allocates nothing beyond the vector it was handed.
std::vector<HighwayLitStretch> mergeLitEvidence(
    std::vector<HighwayLitStretch> items, const double rest_seconds)
{
    assert(rest_seconds >= 0.0 && "the rest tolerance is a duration");
    // A gap narrower than the onset tolerance is no gap at all, whatever the rest tolerance: the
    // members of one sustainless chord start and release at one instant, and under a zero tolerance
    // they would otherwise split into one stretch each for a single strike.
    const double split_gap = std::max(rest_seconds, g_onset_match_epsilon);
    std::ranges::sort(items, std::ranges::less{}, &HighwayLitStretch::start_seconds);
    std::size_t kept = 0;
    for (std::size_t next = 0; next < items.size();)
    {
        HighwayLitStretch run = items[next];
        ++next;
        while (next < items.size() && items[next].start_seconds - run.release_seconds < split_gap)
        {
            foldLitEvidence(run, items[next]);
            ++next;
        }
        if (kept > 0)
        {
            run = crowdedAfter(run, items[kept - 1]);
        }
        items[kept] = run;
        ++kept;
    }
    items.resize(kept);
    return items;
}

} // namespace rock_hero::common::core
