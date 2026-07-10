#include "clock/atomic_playback_clock.h"

#include <cmath>

namespace rock_hero::common::audio
{

namespace
{

constexpr double g_nanoseconds_per_second = 1.0e9;
constexpr double g_parts_per_million = 1.0e6;

} // namespace

// Assembles a snapshot from the relaxed atomics. Fields may originate from different publishes;
// that per-field incoherence is part of the documented snapshot contract.
PlaybackClockSnapshot AtomicPlaybackClock::snapshot() const noexcept
{
    const std::int64_t position_nanoseconds =
        m_position_nanoseconds.load(std::memory_order_relaxed);
    const std::int64_t capture_nanoseconds = m_capture_nanoseconds.load(std::memory_order_relaxed);
    const std::int64_t rate_parts_per_million =
        m_rate_parts_per_million.load(std::memory_order_relaxed);
    const bool playing = m_playing.load(std::memory_order_relaxed);

    return PlaybackClockSnapshot{
        .position =
            common::core::TimePosition{
                static_cast<double>(position_nanoseconds) / g_nanoseconds_per_second
            },
        .monotonic_capture_time = std::chrono::nanoseconds{capture_nanoseconds},
        .playback_rate = static_cast<double>(rate_parts_per_million) / g_parts_per_million,
        .playing = playing,
    };
}

// Converts seconds to integer nanoseconds; llround keeps sub-nanosecond rounding symmetric
// around zero so negative boundary values do not truncate toward zero.
void AtomicPlaybackClock::publishPosition(
    common::core::TimePosition position, std::chrono::nanoseconds captured_at) noexcept
{
    m_position_nanoseconds.store(
        std::llround(position.seconds * g_nanoseconds_per_second), std::memory_order_relaxed);
    m_capture_nanoseconds.store(captured_at.count(), std::memory_order_relaxed);
}

// Publishes only the playing flag; the last published position and stamp stay visible.
void AtomicPlaybackClock::publishPlaying(bool playing) noexcept
{
    m_playing.store(playing, std::memory_order_relaxed);
}

// Stores the rate in parts per million; llround keeps the quantization symmetric around zero.
void AtomicPlaybackClock::publishRate(double rate) noexcept
{
    m_rate_parts_per_million.store(
        std::llround(rate * g_parts_per_million), std::memory_order_relaxed);
}

} // namespace rock_hero::common::audio
