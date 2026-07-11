#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <vector>

namespace rock_hero::common::core
{

std::size_t highwayTailSampleCount(
    const double projected_length_pixels, const double pixels_per_sample,
    const std::size_t sample_cap) noexcept
{
    const std::size_t floor_count = 2;
    if (pixels_per_sample <= 0.0 || projected_length_pixels <= 0.0)
    {
        return std::min(floor_count, std::max(sample_cap, std::size_t{1}));
    }
    const double raw = std::ceil(projected_length_pixels / pixels_per_sample) + 1.0;
    const auto count = static_cast<std::size_t>(std::min(raw, 1.0e9));
    return std::clamp(count, floor_count, std::max(sample_cap, floor_count));
}

double highwayTailTaper(const double progress, const double taper_fraction) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    const double fraction = std::clamp(taper_fraction, 1.0e-6, 0.5);
    return std::min(1.0, std::min(p, 1.0 - p) / fraction);
}

double highwayBendSemitonesAt(
    const std::span<const HighwayBendPointView> bend, const double onset_seconds,
    const double seconds) noexcept
{
    if (bend.empty())
    {
        return 0.0;
    }

    // Segment start: the onset at zero semitones, unless the first point IS the onset (prebend).
    double previous_seconds = onset_seconds;
    double previous_semitones =
        bend.front().seconds <= onset_seconds ? bend.front().semitones : 0.0;
    for (const HighwayBendPointView& point : bend)
    {
        if (seconds <= point.seconds)
        {
            const double span = point.seconds - previous_seconds;
            if (span <= 0.0)
            {
                return point.semitones;
            }
            const double mix = std::clamp((seconds - previous_seconds) / span, 0.0, 1.0);
            return previous_semitones + ((point.semitones - previous_semitones) * mix);
        }
        previous_seconds = point.seconds;
        previous_semitones = point.semitones;
    }
    return bend.back().semitones;
}

bool highwayBendInverted(const int displayed_lane, const int string_count) noexcept
{
    // Strictly-upper-half lanes invert; the middle lane of an odd stack lifts upward.
    return 2 * displayed_lane > string_count + 1;
}

double highwaySlideEaseWeight(const double progress, const bool unpitched) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (unpitched)
    {
        return 1.0 - std::sin((1.0 - p) * std::numbers::pi / 2.0);
    }
    const double eased = std::sin(p * std::numbers::pi / 2.0);
    return eased * eased * eased;
}

double highwayVibratoWobble(const double seconds_from_onset) noexcept
{
    return std::sin(2.0 * std::numbers::pi * seconds_from_onset / g_highway_vibrato_period_seconds);
}

double highwayTremoloWobble(const double seconds_from_onset) noexcept
{
    const double cycles = seconds_from_onset / g_highway_tremolo_period_seconds;
    const double phase = cycles - std::floor(cycles);
    return (std::abs(phase - 0.5) - 0.25) * 3.0;
}

std::vector<double> makeHighwayTailSampleTimes(
    const HighwayNoteView& note, const double from_seconds, const double to_seconds,
    const std::size_t uniform_count)
{
    if (to_seconds <= from_seconds)
    {
        return {};
    }

    std::vector<double> times;
    const std::size_t count = std::max(uniform_count, std::size_t{2});
    times.reserve(count + note.bend.size() + note.slides.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const double mix = static_cast<double>(index) / static_cast<double>(count - 1);
        times.push_back(from_seconds + ((to_seconds - from_seconds) * mix));
    }
    for (const HighwayBendPointView& point : note.bend)
    {
        if (point.seconds > from_seconds && point.seconds < to_seconds)
        {
            times.push_back(point.seconds);
        }
    }
    for (const HighwaySlideView& waypoint : note.slides)
    {
        if (waypoint.seconds > from_seconds && waypoint.seconds < to_seconds)
        {
            times.push_back(waypoint.seconds);
        }
    }

    std::ranges::sort(times);
    // Dedupe with a tolerance: a uniform sample landing on a control point must not produce a
    // zero-length segment.
    constexpr double g_epsilon = 1.0e-9;
    const auto [first_dup, last_dup] = std::ranges::unique(
        times, [](const double lhs, const double rhs) { return std::abs(rhs - lhs) < g_epsilon; });
    times.erase(first_dup, last_dup);
    return times;
}

} // namespace rock_hero::common::core
