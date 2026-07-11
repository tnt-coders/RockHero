#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <rock_hero/game/core/frame_clock/frame_clock.h>

namespace rock_hero::game::core
{

namespace
{

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""ns;

// Builds a published, playing snapshot at the given position and capture stamp.
[[nodiscard]] common::audio::PlaybackClockSnapshot playingSnapshot(
    const double position_seconds, const std::chrono::nanoseconds capture_time)
{
    return common::audio::PlaybackClockSnapshot{
        .position = common::core::TimePosition{position_seconds},
        .monotonic_capture_time = capture_time,
        .playback_rate = 1.0,
        .playing = true,
    };
}

} // namespace

TEST_CASE("FrameClock first sample has no frame delta and no age before publish", "[core][frame]")
{
    FrameClock clock;

    const FrameClockSample sample = clock.sample(common::audio::PlaybackClockSnapshot{}, 10ms);

    CHECK(sample.frame_delta == 0ns);
    CHECK_FALSE(sample.snapshot_age.has_value());
    CHECK_FALSE(sample.playing);
    // An unpublished snapshot reads as a plain current value: exact position zero.
    CHECK_THAT(sample.song_time.seconds, Catch::Matchers::WithinULP(0.0, 0));
}

TEST_CASE("FrameClock reports frame deltas between successive samples", "[core][frame]")
{
    FrameClock clock;

    (void)clock.sample(common::audio::PlaybackClockSnapshot{}, 10ms);
    const FrameClockSample second = clock.sample(common::audio::PlaybackClockSnapshot{}, 26ms);
    const FrameClockSample third = clock.sample(common::audio::PlaybackClockSnapshot{}, 43ms);

    CHECK(second.frame_delta == 16ms);
    CHECK(third.frame_delta == 17ms);
}

TEST_CASE("FrameClock reports the consumed snapshot's age", "[core][frame]")
{
    FrameClock clock;

    const FrameClockSample sample = clock.sample(playingSnapshot(1.0, 50ms), 80ms);

    REQUIRE(sample.snapshot_age.has_value());
    if (sample.snapshot_age.has_value())
    {
        CHECK(*sample.snapshot_age == 30ms);
    }
    CHECK(sample.playing);
}

TEST_CASE("FrameClock extrapolates playing song time from the capture stamp", "[core][frame]")
{
    FrameClock clock;

    // First sample snaps to the extrapolated target: 1.0s position + 100ms elapsed at rate 1.0.
    const FrameClockSample sample = clock.sample(playingSnapshot(1.0, 100ms), 200ms);

    CHECK_THAT(sample.song_time.seconds, Catch::Matchers::WithinAbs(1.1, 1e-9));
}

TEST_CASE("FrameClock returns paused positions exactly", "[core][frame]")
{
    FrameClock clock;
    const common::audio::PlaybackClockSnapshot paused{
        .position = common::core::TimePosition{2.5},
        .monotonic_capture_time = 40ms,
        .playback_rate = 1.0,
        .playing = false,
    };

    const FrameClockSample sample = clock.sample(paused, 90ms);

    // The extrapolator's contract: paused output is the published position exactly.
    CHECK_THAT(sample.song_time.seconds, Catch::Matchers::WithinULP(2.5, 0));
}

TEST_CASE("FrameClock reset forgets the previous frame", "[core][frame]")
{
    FrameClock clock;
    (void)clock.sample(common::audio::PlaybackClockSnapshot{}, 10ms);

    clock.reset();
    const FrameClockSample sample = clock.sample(common::audio::PlaybackClockSnapshot{}, 30ms);

    CHECK(sample.frame_delta == 0ns);
}

TEST_CASE("FramePacingStats aggregates a window into one summary", "[core][frame]")
{
    FramePacingStats stats{std::chrono::nanoseconds{50ms}};

    CHECK_FALSE(stats.record(16ms).has_value());
    CHECK_FALSE(stats.record(18ms).has_value());
    const std::optional<FramePacingSummary> summary = stats.record(20ms);

    REQUIRE(summary.has_value());
    if (summary.has_value())
    {
        CHECK(summary->frame_count == 3);
        CHECK(summary->window_duration == 54ms);
        CHECK(summary->min_delta == 16ms);
        CHECK(summary->max_delta == 20ms);
        CHECK(summary->average_delta == 18ms);
    }
}

TEST_CASE("FramePacingStats starts each window fresh", "[core][frame]")
{
    FramePacingStats stats{std::chrono::nanoseconds{30ms}};

    REQUIRE(stats.record(35ms).has_value());

    // The second window must not inherit the first window's extremes.
    CHECK_FALSE(stats.record(10ms).has_value());
    const std::optional<FramePacingSummary> summary = stats.record(25ms);
    REQUIRE(summary.has_value());
    if (summary.has_value())
    {
        CHECK(summary->frame_count == 2);
        CHECK(summary->min_delta == 10ms);
        CHECK(summary->max_delta == 25ms);
    }
}

TEST_CASE("FramePacingStats ignores non-positive deltas", "[core][frame]")
{
    FramePacingStats stats{std::chrono::nanoseconds{20ms}};

    CHECK_FALSE(stats.record(0ns).has_value());
    CHECK_FALSE(stats.record(-5ms).has_value());
    const std::optional<FramePacingSummary> summary = stats.record(25ms);

    REQUIRE(summary.has_value());
    if (summary.has_value())
    {
        CHECK(summary->frame_count == 1);
        CHECK(summary->min_delta == 25ms);
    }
}

} // namespace rock_hero::game::core
