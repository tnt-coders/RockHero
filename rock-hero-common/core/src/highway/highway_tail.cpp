#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <span>
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
    const std::span<const BendPointViewState> bend, const double onset_seconds,
    const double seconds) noexcept
{
    if (bend.empty())
    {
        return 0.0;
    }

    // The control polyline is the bend points with a virtual onset point at zero semitones in
    // front — unless the first point IS the onset (prebend), which anchors the start value.
    const bool prebend = bend.front().seconds <= onset_seconds;
    const std::size_t count = bend.size() + (prebend ? 0 : 1);
    const auto point_seconds = [&](const std::size_t index) {
        return (!prebend && index == 0) ? onset_seconds : bend[index - (prebend ? 0 : 1)].seconds;
    };
    const auto point_semitones = [&](const std::size_t index) {
        return (!prebend && index == 0) ? 0.0 : bend[index - (prebend ? 0 : 1)].semitones;
    };
    if (seconds <= point_seconds(0))
    {
        return point_semitones(0);
    }
    if (seconds >= point_seconds(count - 1))
    {
        return point_semitones(count - 1);
    }

    // Secant slope of the segment starting at `index`; the curve is flat (at rest) before the
    // first point and after the last, so out-of-range segments report zero slope — which the
    // Fritsch–Carlson rule below turns into zero endpoint tangents for free.
    const auto secant = [&](const std::size_t index) {
        if (index + 1 >= count)
        {
            return 0.0;
        }
        const double span = point_seconds(index + 1) - point_seconds(index);
        return span > 0.0 ? (point_semitones(index + 1) - point_semitones(index)) / span : 0.0;
    };
    // Fritsch–Carlson tangent at a control point: zero when the neighboring secants disagree
    // in direction or either is flat (plateaus stay exactly flat, reversals turn at rest), else
    // the span-weighted harmonic mean — which keeps the tangent inside the monotonicity region,
    // so the cubic can never overshoot a control value.
    const auto tangent = [&](const std::size_t index) {
        const double before = index > 0 ? secant(index - 1) : 0.0;
        const double after = secant(index);
        if (before * after <= 0.0)
        {
            return 0.0;
        }
        const double span_before =
            index > 0 ? point_seconds(index) - point_seconds(index - 1) : 0.0;
        const double span_after =
            index + 1 < count ? point_seconds(index + 1) - point_seconds(index) : 0.0;
        return 3.0 * (span_before + span_after) /
               ((((2.0 * span_after) + span_before) / before) +
                ((span_after + (2.0 * span_before)) / after));
    };

    std::size_t segment = 0;
    while (segment + 2 < count && seconds > point_seconds(segment + 1))
    {
        ++segment;
    }
    const double span = point_seconds(segment + 1) - point_seconds(segment);
    if (span <= 0.0)
    {
        return point_semitones(segment + 1);
    }
    const double mix = std::clamp((seconds - point_seconds(segment)) / span, 0.0, 1.0);
    // Cubic Hermite basis over the segment with the Fritsch–Carlson endpoint tangents.
    const double mix2 = mix * mix;
    const double mix3 = mix2 * mix;
    return (point_semitones(segment) * ((2.0 * mix3) - (3.0 * mix2) + 1.0)) +
           (tangent(segment) * span * (mix3 - (2.0 * mix2) + mix)) +
           (point_semitones(segment + 1) * ((-2.0 * mix3) + (3.0 * mix2))) +
           (tangent(segment + 1) * span * (mix3 - mix2));
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

// Onset-phased sine at the caller-derived period.
double highwayVibratoWobble(const double seconds_from_onset, const double period_seconds) noexcept
{
    return std::sin(2.0 * std::numbers::pi * seconds_from_onset / period_seconds);
}

// A sine's extremes fall a quarter period in and every half period after, so the index counts half
// periods offset by that quarter: index zero is the first crest, and -0.5 is the region's start.
double highwayVibratoTurningIndex(const double seconds_from_start) noexcept
{
    return (seconds_from_start / (g_highway_vibrato_period_seconds / 2.0)) - 0.5;
}

// Exact inverse of the index above, so a caller walking whole indices lands on the wave's corners
// rather than near them, in the same phase highwayVibratoSemitonesAt reads.
double highwayVibratoSecondsAtTurningIndex(const double index) noexcept
{
    return (0.5 + index) * (g_highway_vibrato_period_seconds / 2.0);
}

double highwayVibratoSemitonesAt(
    const std::span<const VibratoSpanViewState> vibrato, const double seconds,
    const double depth_scale) noexcept
{
    for (const VibratoSpanViewState& span : vibrato)
    {
        const double duration = span.end_seconds - span.start_seconds;
        // A region a statement opened exactly at the ring's end has no time to wobble in, and its
        // progress would be a division by zero — the same guard the head's taper carried when the
        // whole channel was one note-long flag.
        if (!(duration > 0.0) || seconds < span.start_seconds || seconds > span.end_seconds)
        {
            continue;
        }
        const double from_start = seconds - span.start_seconds;
        const double taper = highwayTailTaper(from_start / duration, g_highway_tail_taper_fraction);
        return depth_scale * taper * g_highway_vibrato_depth_semitones *
               highwayVibratoWobble(from_start, g_highway_vibrato_period_seconds);
    }
    return 0.0;
}

double highwayTremoloTailCycles(const double seconds_from_onset) noexcept
{
    return seconds_from_onset / g_highway_tremolo_tooth_cycle_seconds;
}

double highwayTremoloTailSecondsAtCycle(const double cycles) noexcept
{
    return cycles * g_highway_tremolo_tooth_cycle_seconds;
}

double highwayTremoloWobble(const double cycles) noexcept
{
    const double phase = cycles - std::floor(cycles);
    // Unit triangle (peaks at plus-or-minus one, phase zero at the anchor) scaled by the depth.
    return (std::abs(phase - 0.5) - 0.25) * 4.0 * g_highway_tremolo_depth;
}

double highwayTremoloEnvelope(const double cycles, const double end_cycles) noexcept
{
    const double ramp = std::max(g_highway_tremolo_ramp_cycles, 1.0e-6);
    return std::clamp(std::min(cycles, end_cycles - cycles) / ramp, 0.0, 1.0);
}

std::vector<double> makeHighwayTailSampleTimes(
    const NoteViewState& note, const double from_seconds, const double to_seconds,
    const std::size_t uniform_count, const std::span<const double> extra_times,
    const std::size_t sample_cap)
{
    if (to_seconds <= from_seconds)
    {
        return {};
    }

    // The exact times first, so the uniform grid can be budgeted against what they leave.
    std::vector<double> times;
    times.reserve(sample_cap + note.bend.size() + note.slides.size() + extra_times.size());
    for (const BendPointViewState& point : note.bend)
    {
        if (point.seconds > from_seconds && point.seconds < to_seconds)
        {
            times.push_back(point.seconds);
        }
    }
    for (const SlideViewState& waypoint : note.slides)
    {
        if (waypoint.seconds > from_seconds && waypoint.seconds < to_seconds)
        {
            times.push_back(waypoint.seconds);
        }
    }
    for (const double seconds : extra_times)
    {
        if (seconds > from_seconds && seconds < to_seconds)
        {
            times.push_back(seconds);
        }
    }
    const std::size_t budget = sample_cap > times.size() ? sample_cap - times.size() : 0;
    const std::size_t count =
        std::clamp(uniform_count, std::size_t{2}, std::max(budget, std::size_t{2}));
    for (std::size_t index = 0; index < count; ++index)
    {
        const double mix = static_cast<double>(index) / static_cast<double>(count - 1);
        times.push_back(from_seconds + ((to_seconds - from_seconds) * mix));
    }

    std::ranges::sort(times);
    // Dedupe with a tolerance: a uniform sample landing on a control point must not produce a
    // zero-length segment. Asking the same question as every other same-instant test on the board —
    // are these two highway times the same moment — so it reads the same named tolerance rather
    // than repeating the number, which is how the two would drift apart.
    const auto [first_dup, last_dup] =
        std::ranges::unique(times, [](const double lhs, const double rhs) {
            return std::abs(rhs - lhs) < g_onset_match_epsilon;
        });
    times.erase(first_dup, last_dup);
    return times;
}

} // namespace rock_hero::common::core
