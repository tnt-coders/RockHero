#include "frame_clock/frame_clock.h"

#include <algorithm>
#include <utility>

namespace rock_hero::game::core
{

FrameClock::FrameClock(common::audio::PlaybackClockExtrapolationPolicy policy)
    : m_extrapolator(policy)
{}

FrameClockSample FrameClock::sample(
    const common::audio::PlaybackClockSnapshot& snapshot,
    const std::chrono::nanoseconds monotonic_now)
{
    FrameClockSample result;
    result.song_time = m_extrapolator.advance(snapshot, monotonic_now);
    result.playing = snapshot.playing;

    if (m_has_previous)
    {
        result.frame_delta = monotonic_now - m_previous_now;
    }
    m_has_previous = true;
    m_previous_now = monotonic_now;

    // A zero capture stamp means the clock has never published (see PlaybackClockSnapshot), so
    // age stays empty rather than reading as "published one epoch ago".
    if (snapshot.monotonic_capture_time != std::chrono::nanoseconds{0})
    {
        result.snapshot_age = monotonic_now - snapshot.monotonic_capture_time;
    }

    return result;
}

void FrameClock::reset() noexcept
{
    m_extrapolator.reset();
    m_has_previous = false;
    m_previous_now = std::chrono::nanoseconds{0};
}

FramePacingStats::FramePacingStats(const std::chrono::nanoseconds summary_window)
    : m_summary_window(summary_window)
{}

std::optional<FramePacingSummary> FramePacingStats::record(
    const std::chrono::nanoseconds frame_delta)
{
    if (frame_delta <= std::chrono::nanoseconds{0})
    {
        return std::nullopt;
    }

    if (m_frame_count == 0)
    {
        m_min_delta = frame_delta;
        m_max_delta = frame_delta;
    }
    else
    {
        m_min_delta = std::min(m_min_delta, frame_delta);
        m_max_delta = std::max(m_max_delta, frame_delta);
    }
    ++m_frame_count;
    m_accumulated += frame_delta;

    if (m_accumulated < m_summary_window)
    {
        return std::nullopt;
    }

    const FramePacingSummary summary{
        .frame_count = m_frame_count,
        .window_duration = m_accumulated,
        .min_delta = m_min_delta,
        .max_delta = m_max_delta,
        .average_delta = m_accumulated / m_frame_count,
    };

    m_frame_count = 0;
    m_accumulated = std::chrono::nanoseconds{0};
    m_min_delta = std::chrono::nanoseconds{0};
    m_max_delta = std::chrono::nanoseconds{0};

    return summary;
}

} // namespace rock_hero::game::core
