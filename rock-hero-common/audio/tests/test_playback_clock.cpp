#include "clock/atomic_playback_clock.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstdint>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::audio
{

// The clock contract promises wait-free any-thread reads, which requires every atomic member
// type to be lock-free on this platform. The members are private, so the guarantee is pinned on
// the member types the storage class documents.
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

// The storage class must be usable through the public port surface.
static_assert(std::derived_from<AtomicPlaybackClock, IPlaybackClock>);

// Before any publish, consumers must see a well-defined "nothing published yet" value: zero
// position, zero capture stamp (the documented sentinel), unit rate, stopped.
TEST_CASE("AtomicPlaybackClock default snapshot is stopped at zero", "[audio][clock]")
{
    const AtomicPlaybackClock clock;
    const PlaybackClockSnapshot snapshot = clock.snapshot();

    CHECK(snapshot.position == common::core::TimePosition{});
    CHECK(snapshot.monotonic_capture_time == std::chrono::nanoseconds{0});
    CHECK(std::is_eq(snapshot.playback_rate <=> 1.0));
    CHECK_FALSE(snapshot.playing);
}

// Positions cross the port as integer nanoseconds; representative values (zero, sub-millisecond,
// sub-second, multi-minute) must survive the double -> ns -> double conversion exactly.
TEST_CASE("AtomicPlaybackClock round-trips positions through nanoseconds", "[audio][clock]")
{
    AtomicPlaybackClock clock;

    for (const double seconds : {0.0, 0.0005, 0.128, 90.0, 754.25})
    {
        clock.publishPosition(common::core::TimePosition{seconds}, std::chrono::nanoseconds{1});
        CHECK(clock.snapshot().position == common::core::TimePosition{seconds});
    }
}

// publishPosition carries the capture stamp of the same publish so consumers can extrapolate
// from the pair.
TEST_CASE("AtomicPlaybackClock stores the capture stamp with the position", "[audio][clock]")
{
    AtomicPlaybackClock clock;

    clock.publishPosition(common::core::TimePosition{2.5}, std::chrono::nanoseconds{123'456'789});

    const PlaybackClockSnapshot snapshot = clock.snapshot();
    CHECK(snapshot.position == common::core::TimePosition{2.5});
    CHECK(snapshot.monotonic_capture_time == std::chrono::nanoseconds{123'456'789});
}

// Position, playing, and rate publish independently — updating one field must leave the last
// published values of the others visible (the documented per-field incoherence contract).
TEST_CASE("AtomicPlaybackClock fields update independently", "[audio][clock]")
{
    AtomicPlaybackClock clock;

    clock.publishPosition(common::core::TimePosition{1.0}, std::chrono::nanoseconds{10});
    clock.publishPlaying(true);
    CHECK(clock.snapshot().position == common::core::TimePosition{1.0});
    CHECK(clock.snapshot().playing);

    clock.publishRate(0.75);
    const PlaybackClockSnapshot snapshot = clock.snapshot();
    CHECK(snapshot.position == common::core::TimePosition{1.0});
    CHECK(snapshot.monotonic_capture_time == std::chrono::nanoseconds{10});
    CHECK(std::is_eq(snapshot.playback_rate <=> 0.75));
    CHECK(snapshot.playing);

    clock.publishPlaying(false);
    CHECK_FALSE(clock.snapshot().playing);
    CHECK(clock.snapshot().position == common::core::TimePosition{1.0});
}

} // namespace rock_hero::common::audio
