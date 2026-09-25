#include <algorithm>
#include <cassert>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/shared/visible_events.h>

namespace rock_hero::common::core
{

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

// Seeded from the earliest item, which always qualifies for the rise, so the fold needs no order
// from its caller.
HighwayLitStretch foldLitEvidence(const std::span<const HighwayLitStretch> items) noexcept
{
    assert(!items.empty() && "foldLitEvidence needs evidence to fold");
    const HighwayLitStretch& earliest =
        *std::ranges::min_element(items, {}, &HighwayLitStretch::start_seconds);
    HighwayLitStretch folded = earliest;
    for (const HighwayLitStretch& item : items)
    {
        folded.release_seconds = std::max(folded.release_seconds, item.release_seconds);
        if (item.start_seconds - earliest.start_seconds < g_onset_match_epsilon)
        {
            folded.rise_seconds = std::max(folded.rise_seconds, item.rise_seconds);
        }
    }
    return folded;
}

// At most the gap, and the gap is never negative: a stretch starting inside the previous one's
// hold (overlapping strikes) gets no rise at all.
HighwayLitStretch crowdedAfter(
    HighwayLitStretch stretch, const HighwayLitStretch& previous) noexcept
{
    stretch.rise_seconds = std::min(
        stretch.rise_seconds, std::max(0.0, stretch.start_seconds - previous.release_seconds));
    return stretch;
}

} // namespace rock_hero::common::core
