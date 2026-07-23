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

// The pitched slide ease at half progress (sin(pi/4) cubed), the shared curve every window
// transition uses.
[[nodiscard]] double halfProgressWeight()
{
    return std::pow(std::sin(std::numbers::pi / 4.0), 3.0);
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

} // namespace

// Outside every ramp the window is a step function of the arrivals: the nut window before the
// first placement, each settled window from its (inclusive) arrival on, and a zero ramp steps
// exactly at its arrival instant.
TEST_CASE("Hand window holds settled extents outside ramps", "[core][highway][window]")
{
    const std::vector<HighwayFhpView> placements = makePlacements();

    CHECK(highwayHandWindowAt({}, 5.0) == HighwayHandWindow{.low_line = 0.0, .high_line = 4.0});
    CHECK(
        highwayHandWindowAt(placements, 1.9) ==
        HighwayHandWindow{.low_line = 0.0, .high_line = 4.0});
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

    // The first placement ramps from the nut window when it carries a ramp of its own.
    const std::vector<HighwayFhpView> from_nut{
        HighwayFhpView{.seconds = 1.0, .fret = 5, .width = 4, .ramp_seconds = 1.0},
    };
    const HighwayHandWindow nut_mid = highwayHandWindowAt(from_nut, 0.5);
    CHECK(nut_mid.low_line == Catch::Approx(4.0 * weight));
    CHECK(nut_mid.high_line == Catch::Approx(4.0 + (4.0 * weight)));
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
