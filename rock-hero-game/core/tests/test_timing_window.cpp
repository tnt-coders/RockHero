#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/game/core/scoring/scoring_ruleset.h>
#include <rock_hero/game/core/scoring/timing_window.h>

namespace rock_hero::game::core
{

namespace
{

// 4/4 at 120 BPM: one beat is 0.5 s, one measure is 2.0 s, so measure 2 beat 1 sits at 2.0 s.
[[nodiscard]] common::core::TempoMap defaultTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{10.0});
}

constexpr common::core::GridPosition g_note_position{.measure = 2, .beat = 1, .offset = {}};

} // namespace

// The rh-score-1 onset window is ±100 ms of real time around the tempo-map-expected note time.
TEST_CASE("Onset window spans the ruleset half-width around the expected time", "[core][scoring]")
{
    const HitWindow window =
        makeOnsetWindow(defaultTempoMap(), g_note_position, ScoringRuleset{}, 1.0);
    CHECK(window.open_seconds == Catch::Approx(1.9));
    CHECK(window.close_seconds == Catch::Approx(2.1));
}

// The half-width is a real-time constant, so at half speed it spans half as much song time and
// at double speed twice as much — the speed factor passes through the window math instead of
// changing the player's real window.
TEST_CASE("Onset window keeps its real-time width across playback speeds", "[core][scoring]")
{
    const HitWindow half_speed =
        makeOnsetWindow(defaultTempoMap(), g_note_position, ScoringRuleset{}, 0.5);
    CHECK(half_speed.open_seconds == Catch::Approx(1.95));
    CHECK(half_speed.close_seconds == Catch::Approx(2.05));

    const HitWindow double_speed =
        makeOnsetWindow(defaultTempoMap(), g_note_position, ScoringRuleset{}, 2.0);
    CHECK(double_speed.open_seconds == Catch::Approx(1.8));
    CHECK(double_speed.close_seconds == Catch::Approx(2.2));
}

// Window bounds are inclusive: an edge hit is a hit. A NaN observation is never in any window.
TEST_CASE("Onset window contains its inclusive bounds", "[core][scoring]")
{
    const HitWindow window{.open_seconds = 1.9, .close_seconds = 2.1};
    CHECK(window.contains(1.9));
    CHECK(window.contains(2.0));
    CHECK(window.contains(2.1));
    CHECK_FALSE(window.contains(1.89));
    CHECK_FALSE(window.contains(2.11));
    CHECK_FALSE(window.contains(std::numeric_limits<double>::quiet_NaN()));
}

// The plan-13 effective-offset contract: the player's intent is the observed time minus the
// audio offset, with the real-millisecond offset converted through the speed factor.
TEST_CASE("Played song time subtracts the calibration offset", "[core][scoring]")
{
    CHECK(playedSongTime(2.05, 100.0, 1.0) == Catch::Approx(1.95));
    CHECK(playedSongTime(2.05, 100.0, 0.5) == Catch::Approx(2.0));
    CHECK(playedSongTime(2.0, 0.0, 1.0) == Catch::Approx(2.0));
}

// Timing deltas are recorded in signed real milliseconds, negative when the player was early,
// recovering the real-time error at any playback speed.
TEST_CASE("Timing delta reports signed real milliseconds", "[core][scoring]")
{
    CHECK(timingDeltaMs(2.0, 1.95, 1.0) == Catch::Approx(-50.0));
    CHECK(timingDeltaMs(2.0, 2.025, 1.0) == Catch::Approx(25.0));
    CHECK(timingDeltaMs(2.0, 2.025, 0.5) == Catch::Approx(50.0));
    CHECK(timingDeltaMs(2.0, 2.0, 1.0) == Catch::Approx(0.0).margin(1e-9));
}

// End-to-end shift: after calibration a raw out-of-window observation lands inside the window
// and records the player's genuine +50 ms lateness — verdict and delta agree on the same play.
TEST_CASE("Calibration shift maps a late observation into the window", "[core][scoring]")
{
    const HitWindow window =
        makeOnsetWindow(defaultTempoMap(), g_note_position, ScoringRuleset{}, 1.0);
    const double played = playedSongTime(2.15, 100.0, 1.0);
    CHECK(window.contains(played));
    CHECK(timingDeltaMs(2.0, played, 1.0) == Catch::Approx(50.0));
}

} // namespace rock_hero::game::core
