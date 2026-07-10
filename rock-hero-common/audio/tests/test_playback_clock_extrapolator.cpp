#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <rock_hero/common/audio/clock/playback_clock_extrapolator.h>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::audio
{

namespace
{

// Converts synthetic test seconds into the steady-clock nanosecond domain the API consumes. The
// base offset keeps a t=0 capture away from the zero-stamp "never published" sentinel — real
// steady-clock stamps are never zero, and the extrapolator's target math only uses differences,
// so the offset cancels everywhere.
[[nodiscard]] std::chrono::nanoseconds nanosecondsAt(double seconds)
{
    constexpr double g_base_seconds = 3600.0;
    return std::chrono::nanoseconds{std::llround((g_base_seconds + seconds) * 1.0e9)};
}

// Builds a playing/paused snapshot without repeating designated-init boilerplate per test.
[[nodiscard]] PlaybackClockSnapshot makeSnapshot(
    double position_seconds, double capture_seconds, bool playing, double rate = 1.0)
{
    return PlaybackClockSnapshot{
        .position = common::core::TimePosition{position_seconds},
        .monotonic_capture_time = nanosecondsAt(capture_seconds),
        .playback_rate = rate,
        .playing = playing,
    };
}

constexpr double g_frame_seconds = 1.0 / 60.0;

} // namespace

// 60Hz frames against 20Hz publishes with exact capture stamps: the extrapolated output must be
// monotonic and track ideal playback time to within numerical noise.
TEST_CASE("Extrapolator tracks ideal time across sparse publishes", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    double last_output = -1.0;
    for (int frame = 1; frame <= 120; ++frame)
    {
        const double now = frame * g_frame_seconds;
        // Latest 20Hz publish at or before this frame; the publisher stamps exact audio time.
        const double publish_time = std::floor(now / 0.05) * 0.05;
        const auto output = extrapolator.advance(
            makeSnapshot(publish_time, publish_time, true), nanosecondsAt(now));

        CHECK(output.seconds > last_output);
        CHECK(std::abs(output.seconds - now) < 1.0e-6);
        last_output = output.seconds;
    }
}

// Jittered capture stamps must never show up as output regressions, and the error stays bounded
// by the jitter amplitude rather than accumulating.
TEST_CASE("Extrapolator stays monotonic under publish jitter", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    double last_output = -1.0;
    for (int frame = 1; frame <= 240; ++frame)
    {
        const double now = frame * g_frame_seconds;
        const double publish_time = std::floor(now / 0.05) * 0.05;
        // Deterministic +/-2ms stamp jitter patterned on the publish index.
        const auto publish_index = static_cast<std::int64_t>(publish_time / 0.05);
        const double jitter = ((publish_index % 2) == 0) ? 0.002 : -0.002;
        const auto output = extrapolator.advance(
            makeSnapshot(publish_time, publish_time + jitter, true), nanosecondsAt(now));

        CHECK(output.seconds >= last_output);
        CHECK(std::abs(output.seconds - now) < 0.01);
        last_output = output.seconds;
    }
}

// Paused output holds the published position exactly (a paused seek lands exactly), and resuming
// snaps to the resumed target through the playing-state transition rule.
TEST_CASE("Extrapolator holds exactly while paused and snaps on resume", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    (void)extrapolator.advance(makeSnapshot(1.0, 0.0, true), nanosecondsAt(0.1));

    const auto paused = extrapolator.advance(makeSnapshot(1.05, 0.2, false), nanosecondsAt(0.2));
    CHECK(paused == common::core::TimePosition{1.05});
    const auto still_paused =
        extrapolator.advance(makeSnapshot(1.05, 0.2, false), nanosecondsAt(0.4));
    CHECK(still_paused == common::core::TimePosition{1.05});

    // Seek while paused: output lands on the new position exactly.
    const auto paused_seek =
        extrapolator.advance(makeSnapshot(5.0, 0.5, false), nanosecondsAt(0.5));
    CHECK(paused_seek == common::core::TimePosition{5.0});

    // Resume: fresh capture stamp means the target is the resumed position itself.
    const auto resumed = extrapolator.advance(makeSnapshot(5.0, 0.6, true), nanosecondsAt(0.6));
    CHECK(resumed == common::core::TimePosition{5.0});
}

// Forward seeks and backward loop wraps beyond the snap threshold land exactly on the target
// instead of slewing toward it.
TEST_CASE("Extrapolator snaps on seeks and loop wraps", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    (void)extrapolator.advance(makeSnapshot(10.0, 10.0, true), nanosecondsAt(10.0));

    // Backward jump (practice-mode loop wrap): far beyond the 120ms threshold.
    const auto wrapped = extrapolator.advance(makeSnapshot(2.0, 10.1, true), nanosecondsAt(10.1));
    CHECK(wrapped == common::core::TimePosition{2.0});

    // Forward seek, also beyond threshold.
    const auto sought = extrapolator.advance(makeSnapshot(7.5, 10.2, true), nanosecondsAt(10.2));
    CHECK(sought == common::core::TimePosition{7.5});
}

// Non-unit playback rates advance the output at the published rate (plan 28's day-one plumbing).
TEST_CASE("Extrapolator honors published playback rates", "[audio][clock]")
{
    for (const double rate : {0.75, 1.25})
    {
        PlaybackClockExtrapolator extrapolator;
        double last_output = -1.0;
        for (int frame = 1; frame <= 120; ++frame)
        {
            const double now = frame * g_frame_seconds;
            const auto output =
                extrapolator.advance(makeSnapshot(0.0, 0.0, true, rate), nanosecondsAt(now));
            CHECK(output.seconds > last_output);
            CHECK(std::abs(output.seconds - now * rate) < 1.0e-6);
            last_output = output.seconds;
        }
    }
}

// Within-threshold drift corrects by at most max_correction_rate x frame_delta per frame, so
// publisher offsets read as gentle convergence rather than a visible speed change.
TEST_CASE("Extrapolator clamps drift correction per frame", "[audio][clock]")
{
    const PlaybackClockExtrapolationPolicy policy{};
    PlaybackClockExtrapolator extrapolator{policy};

    const auto seeded = extrapolator.advance(makeSnapshot(0.0, 0.0, true), nanosecondsAt(0.001));
    double previous = seeded.seconds;

    // A snapshot whose target runs 20ms ahead (inside the 120ms snap threshold).
    for (int frame = 1; frame <= 60; ++frame)
    {
        const double now = 0.001 + frame * g_frame_seconds;
        const auto output =
            extrapolator.advance(makeSnapshot(previous + 0.020, now, true), nanosecondsAt(now));

        const double delta = output.seconds - previous;
        CHECK(delta >= 0.0);
        CHECK(delta <= g_frame_seconds * (1.0 + policy.max_correction_rate) + 1.0e-9);
        previous = output.seconds;
    }
}

// With no fresh publishes for 500ms the output keeps advancing smoothly from the stale capture
// stamp instead of stalling or snapping.
TEST_CASE("Extrapolator rides through publisher stalls", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    const PlaybackClockSnapshot stale = makeSnapshot(0.0, 0.0, true);
    double last_output = -1.0;
    for (int frame = 1; frame <= 30; ++frame)
    {
        const double now = frame * g_frame_seconds;
        const auto output = extrapolator.advance(stale, nanosecondsAt(now));
        CHECK(output.seconds > last_output);
        CHECK(std::abs(output.seconds - now) < 1.0e-6);
        last_output = output.seconds;
    }
}

// reset() forgets smoothing state: the next call snaps like a first call even to a small
// backward target that continuous playback would otherwise clamp to a hold.
TEST_CASE("Extrapolator reset snaps like a first call", "[audio][clock]")
{
    PlaybackClockExtrapolator extrapolator;

    (void)extrapolator.advance(makeSnapshot(3.0, 3.0, true), nanosecondsAt(3.0));
    extrapolator.reset();

    const auto output = extrapolator.advance(makeSnapshot(2.95, 3.1, true), nanosecondsAt(3.1));
    CHECK(output == common::core::TimePosition{2.95});
}

} // namespace rock_hero::common::audio
