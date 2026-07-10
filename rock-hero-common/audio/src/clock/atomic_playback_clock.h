// Wait-free publish/read storage backing the engine's IPlaybackClock port. Library-private: the
// public surface is the port and the snapshot value; only the adapter publishes.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <rock_hero/common/audio/clock/i_playback_clock.h>

namespace rock_hero::common::audio
{

class AtomicPlaybackClock final : public IPlaybackClock
{
public:
    [[nodiscard]] PlaybackClockSnapshot snapshot() const noexcept override;

    // Publishes an audio-derived position together with the steady-clock stamp of its capture.
    void publishPosition(
        common::core::TimePosition position, std::chrono::nanoseconds captured_at) noexcept;

    // Publishes the coarse playing flag independently of position.
    void publishPlaying(bool playing) noexcept;

    // Publishes the playback speed factor; stored in parts per million (see member note).
    void publishRate(double rate) noexcept;

private:
    // Relaxed atomics carry telemetry semantics: a clock read publishes no ownership of other
    // data, and a reader may see position, playing, and rate from different publishes. Position
    // and capture stamps are integer nanoseconds (finer than sample resolution) so the stores
    // stay lock-free without relying on lock-free floating-point atomics; the rate is stored in
    // integer parts per million for the same reason.
    std::atomic<std::int64_t> m_position_nanoseconds{0};
    std::atomic<std::int64_t> m_capture_nanoseconds{0};
    std::atomic<std::int64_t> m_rate_parts_per_million{1'000'000};
    std::atomic<bool> m_playing{false};
};

} // namespace rock_hero::common::audio
