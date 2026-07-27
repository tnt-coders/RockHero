#include "preview/preview_time_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::editor::ui
{

namespace
{

using common::audio::PlaybackClockSnapshot;
using common::core::TimePosition;

// A paused snapshot; the model ignores its position and reads the caller-supplied target instead.
[[nodiscard]] PlaybackClockSnapshot pausedSnapshot()
{
    return PlaybackClockSnapshot{.playing = false};
}

// A playing snapshot with a zero capture stamp: the extrapolator treats it as a plain current
// value (no extrapolation) and snaps on the playing transition, so the model returns this position.
[[nodiscard]] PlaybackClockSnapshot playingSnapshot(const double seconds)
{
    return PlaybackClockSnapshot{
        .position = TimePosition{seconds},
        .monotonic_capture_time = std::chrono::nanoseconds{0},
        .playback_rate = 1.0,
        .playing = true,
    };
}

constexpr std::chrono::nanoseconds g_any_now{1'000'000};
constexpr double g_frame_dt = 1.0 / 60.0;

} // namespace

// The first paused frame snaps straight to the target: there is no prior displayed time to glide
// from, so a fresh open lands exactly on the caret.
TEST_CASE("Preview time snaps to the paused target on the first frame", "[editor-ui][preview]")
{
    PreviewTimeModel model;
    CHECK(model.advance(pausedSnapshot(), 12.0, g_any_now, 0.0) == Catch::Approx(12.0));
}

// After the first frame establishes a displayed time, a new paused target is approached by an
// exponential glide (strictly partway on any one frame) that provably terminates on the target.
TEST_CASE("Preview time glides toward a new paused target and terminates", "[editor-ui][preview]")
{
    PreviewTimeModel model;
    CHECK(model.advance(pausedSnapshot(), 0.0, g_any_now, 0.0) == Catch::Approx(0.0));

    // One frame moves partway, never all the way, toward the new target.
    const double after_one = model.advance(pausedSnapshot(), 1.0, g_any_now, g_frame_dt);
    CHECK(after_one > 0.0);
    CHECK(after_one < 1.0);

    // The glide converges and pins to the target within a bounded number of frames.
    double displayed = after_one;
    for (int frame = 0; frame < 200; ++frame)
    {
        displayed = model.advance(pausedSnapshot(), 1.0, g_any_now, g_frame_dt);
    }
    CHECK(displayed == Catch::Approx(1.0));
}

// A zero frame delta makes no glide progress: 1 - exp(0) is zero, so the displayed time holds.
TEST_CASE("Preview time holds across a zero-length frame", "[editor-ui][preview]")
{
    PreviewTimeModel model;
    CHECK(model.advance(pausedSnapshot(), 4.0, g_any_now, 0.0) == Catch::Approx(4.0));
    // Target jumps, but dt is zero, so the displayed time does not move this frame.
    CHECK(model.advance(pausedSnapshot(), 9.0, g_any_now, 0.0) == Catch::Approx(4.0));
}

// Pausing resumes from the played stop point, not from the paused target: the playing frame pins
// the glide state, so the next paused frame glides FROM the played time rather than snapping.
TEST_CASE("Preview time resumes the glide from the played stop point", "[editor-ui][preview]")
{
    PreviewTimeModel model;
    CHECK(model.advance(playingSnapshot(5.0), 0.0, g_any_now, 0.0) == Catch::Approx(5.0));

    // First paused frame after playing: glides from 5.0 toward 20.0, landing strictly between.
    const double after_pause = model.advance(pausedSnapshot(), 20.0, g_any_now, g_frame_dt);
    CHECK(after_pause > 5.0);
    CHECK(after_pause < 20.0);
}

// resetForSnap forgets the glide state so the next paused frame snaps rather than eases — the
// resume-after-a-hidden-gap behavior that must never slew across the gap.
TEST_CASE("Preview time snaps again after resetForSnap", "[editor-ui][preview]")
{
    PreviewTimeModel model;
    CHECK(model.advance(pausedSnapshot(), 0.0, g_any_now, 0.0) == Catch::Approx(0.0));
    // Establish a mid-glide displayed time away from a far target.
    const double mid = model.advance(pausedSnapshot(), 30.0, g_any_now, g_frame_dt);
    CHECK(mid < 30.0);

    model.resetForSnap();
    // The next frame snaps to the target instead of continuing the glide.
    CHECK(model.advance(pausedSnapshot(), 30.0, g_any_now, g_frame_dt) == Catch::Approx(30.0));
}

} // namespace rock_hero::editor::ui
