#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/highway/highway_window.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The pitched slide ease at half progress (sin(pi/4) cubed), the curve a glide-locked or ordinary
// window transition uses.
[[nodiscard]] double halfProgressWeight()
{
    return std::pow(std::sin(std::numbers::pi / 4.0), 3.0);
}

// The unpitched release ease at half progress (1 - sin(pi/4)), the curve a trail-off's window
// transition uses instead.
[[nodiscard]] double unpitchedHalfProgressWeight()
{
    return 1.0 - std::sin(std::numbers::pi / 4.0);
}

// Two placements: an instant arrival at fret 3, then a ramped move to a wider fret-8 window
// (lines 7-13) whose two-second ramp starts at 4.0.
[[nodiscard]] std::vector<HighwayFhpView> makePlacements()
{
    return {
        HighwayFhpView{.seconds = 2.0, .fret = 3, .width = 4, .ramp_seconds = 0.0},
        HighwayFhpView{.seconds = 6.0, .fret = 8, .width = 6, .ramp_seconds = 2.0},
    };
}

// The same move with the arriving placement's ramp marked unpitched, so the two ease families can
// be compared over identical geometry rather than against a second hand-written fixture.
[[nodiscard]] std::vector<HighwayFhpView> makeUnpitchedPlacements()
{
    std::vector<HighwayFhpView> placements = makePlacements();
    placements.back().unpitched_ramp = true;
    return placements;
}

} // namespace

// Outside every ramp the window is a step function of the arrivals: the first placement's
// window already holds before its arrival (the opening scroll shows where the hand belongs), each
// settled window from its (inclusive) arrival on, and a zero ramp steps exactly at its arrival
// instant. The nut window applies only to chartless boards.
TEST_CASE("Hand window holds settled extents outside ramps", "[core][highway][window]")
{
    const std::vector<HighwayFhpView> placements = makePlacements();

    CHECK(highwayHandWindowAt({}, 5.0) == HighwayHandWindow{.low_line = 0.0, .high_line = 4.0});
    CHECK(
        highwayHandWindowAt(placements, 1.9) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 2.0) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 3.9) ==
        HighwayHandWindow{.low_line = 2.0, .high_line = 6.0});
    CHECK(
        highwayHandWindowAt(placements, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
    CHECK(
        highwayHandWindowAt(placements, 9.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});
}

// Inside a ramp both edges ease independently from the previous settled window toward the
// arriving one with the pitched slide curve, so a position move and a width morph are one
// mechanism and the border leaves and rejoins the settled edges tangentially.
TEST_CASE("Hand window eases both edges through a ramp", "[core][highway][window]")
{
    const std::vector<HighwayFhpView> placements = makePlacements();

    // Ramp start is exact: at 4.0 the window has not yet moved.
    const HighwayHandWindow at_start = highwayHandWindowAt(placements, 4.0);
    CHECK(at_start.low_line == Catch::Approx(2.0));
    CHECK(at_start.high_line == Catch::Approx(6.0));

    // Half progress uses the pitched ease weight on each edge's own travel.
    const double weight = halfProgressWeight();
    const HighwayHandWindow mid = highwayHandWindowAt(placements, 5.0);
    CHECK(mid.low_line == Catch::Approx(2.0 + (5.0 * weight)));
    CHECK(mid.high_line == Catch::Approx(6.0 + (7.0 * weight)));

    // The first placement never sweeps in from the nut window: its own window pre-holds, so a
    // ramp on the first placement degenerates to no motion.
    const std::vector<HighwayFhpView> opening{
        HighwayFhpView{.seconds = 1.0, .fret = 5, .width = 4, .ramp_seconds = 1.0},
    };
    const HighwayHandWindow opening_mid = highwayHandWindowAt(opening, 0.5);
    CHECK(opening_mid.low_line == Catch::Approx(4.0));
    CHECK(opening_mid.high_line == Catch::Approx(8.0));
}

// A placement arriving on an unpitched trail-off eases with the release curve, not the pitched
// one: same settled windows at both ends of the ramp, a different path between them. That
// difference is the flag's entire purpose, and the projection setting it is not evidence the
// window reads it.
TEST_CASE("Hand window eases an unpitched ramp with the release curve", "[core][highway][window]")
{
    const std::vector<HighwayFhpView> placements = makeUnpitchedPlacements();

    // Ramp start: the release curve is zero at zero progress, so the previous window still holds.
    const HighwayHandWindow at_start = highwayHandWindowAt(placements, 4.0);
    CHECK(at_start.low_line == Catch::Approx(2.0));
    CHECK(at_start.high_line == Catch::Approx(6.0));

    // Arrival: full travel, so the arriving placement's own settled extent.
    CHECK(
        highwayHandWindowAt(placements, 6.0) ==
        HighwayHandWindow{.low_line = 7.0, .high_line = 13.0});

    // Half progress rides 1 - sin(pi/4) of each edge's own travel.
    const double weight = unpitchedHalfProgressWeight();
    const HighwayHandWindow mid = highwayHandWindowAt(placements, 5.0);
    CHECK(mid.low_line == Catch::Approx(2.0 + (5.0 * weight)));
    CHECK(mid.high_line == Catch::Approx(6.0 + (7.0 * weight)));

    // And it is genuinely the other family rather than the pitched curve relabeled: over the
    // identical move, the release has given up less of its travel by half progress.
    const HighwayHandWindow pitched_mid = highwayHandWindowAt(makePlacements(), 5.0);
    CHECK(mid.low_line < pitched_mid.low_line);
    CHECK(mid.high_line < pitched_mid.high_line);
}

// Coverage is the shared hit-line signal: full one lane inside either edge, zero one lane
// outside, ramping linearly across each moving edge so brightness crossfades and number fades
// track the sweeping border exactly.
TEST_CASE("Hand window line coverage ramps across the edges", "[core][highway][window]")
{
    const HighwayHandWindow settled{.low_line = 2.0, .high_line = 6.0};
    CHECK(highwayHandWindowLineCoverage(settled, 2.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(settled, 6.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(settled, 1.0) == Catch::Approx(0.0));
    CHECK(highwayHandWindowLineCoverage(settled, 7.0) == Catch::Approx(0.0));

    // Mid-sweep fractional edges: the line being exited fades, interior lines stay saturated.
    const HighwayHandWindow sweeping{.low_line = 2.5, .high_line = 6.5};
    CHECK(highwayHandWindowLineCoverage(sweeping, 2.0) == Catch::Approx(0.5));
    CHECK(highwayHandWindowLineCoverage(sweeping, 3.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(sweeping, 6.0) == Catch::Approx(1.0));
    CHECK(highwayHandWindowLineCoverage(sweeping, 7.0) == Catch::Approx(0.5));
}

} // namespace rock_hero::common::core
