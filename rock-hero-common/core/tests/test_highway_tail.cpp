#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <vector>

namespace rock_hero::common::core
{

// Adaptive sampling (the per-millisecond-tessellation fix): density follows the projected
// screen length, bounded below by a drawable pair and above by the hard cap.
TEST_CASE("Highway tail sample count follows screen length under a cap", "[core][highway][tail]")
{
    CHECK(highwayTailSampleCount(0.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(-10.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(4.0, 4.0, 256) == 2);
    CHECK(highwayTailSampleCount(40.0, 4.0, 256) == 11);
    CHECK(highwayTailSampleCount(1.0e6, 4.0, 256) == 256);
    // Degenerate resolution never divides by zero.
    CHECK(highwayTailSampleCount(100.0, 0.0, 256) == 2);
}

// The taper envelope anchors modulated rails on the string line: zero at both ends, full
// amplitude through the middle, linear ramps over the taper fraction.
TEST_CASE("Highway tail taper anchors both ends", "[core][highway][tail]")
{
    CHECK(highwayTailTaper(0.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(1.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(0.05, 0.1) == Catch::Approx(0.5));
    CHECK(highwayTailTaper(0.1, 0.1) == Catch::Approx(1.0));
    CHECK(highwayTailTaper(0.5, 0.1) == Catch::Approx(1.0));
    CHECK(highwayTailTaper(0.95, 0.1) == Catch::Approx(0.5));
    // Out-of-range progress clamps instead of extrapolating.
    CHECK(highwayTailTaper(-1.0, 0.1) == Catch::Approx(0.0));
    CHECK(highwayTailTaper(2.0, 0.1) == Catch::Approx(0.0));
}

// Bend evaluation is smoothstep-eased between control points and hits every control point
// exactly (segment midpoints match the linear value by smoothstep symmetry); the ramp anchors
// at the onset unless the first point is a prebend at the onset itself.
TEST_CASE("Highway bend curve hits its control points exactly", "[core][highway][tail]")
{
    const std::vector<HighwayBendPointView> bend{
        HighwayBendPointView{.seconds = 11.0, .semitones = 2.0},
        HighwayBendPointView{.seconds = 12.0, .semitones = 1.0},
    };

    CHECK(highwayBendSemitonesAt(bend, 10.0, 10.0) == Catch::Approx(0.0));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 10.5) == Catch::Approx(1.0));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 11.0) == Catch::Approx(2.0));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 11.5) == Catch::Approx(1.5));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 12.0) == Catch::Approx(1.0));
    // Quarter points ease below/above the linear value (smoothstep(0.25) = 0.15625): the curve
    // leaves each control point flat instead of at the linear slope, so no corner is drawn.
    CHECK(highwayBendSemitonesAt(bend, 10.0, 10.25) == Catch::Approx(2.0 * 0.15625));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 10.75) == Catch::Approx(2.0 * (1.0 - 0.15625)));
    CHECK(highwayBendSemitonesAt(bend, 10.0, 11.25) == Catch::Approx(2.0 - 0.15625));
    // After the last point the final value holds.
    CHECK(highwayBendSemitonesAt(bend, 10.0, 20.0) == Catch::Approx(1.0));
    // An empty curve is a flat zero.
    CHECK(highwayBendSemitonesAt({}, 10.0, 11.0) == Catch::Approx(0.0));

    // A prebend (first point at the onset) anchors the start value instead of ramping from zero.
    const std::vector<HighwayBendPointView> prebend{
        HighwayBendPointView{.seconds = 10.0, .semitones = 1.0},
        HighwayBendPointView{.seconds = 12.0, .semitones = 1.0},
    };
    CHECK(highwayBendSemitonesAt(prebend, 10.0, 10.0) == Catch::Approx(1.0));
    CHECK(highwayBendSemitonesAt(prebend, 10.0, 11.0) == Catch::Approx(1.0));
}

// Bend display amounts snap one target to half-semitone steps so slightly off hand-edited
// values land on the honest bucket; the digit/half split is pure text layout at the call
// site. Callers apply this to the curve's first point for the head and to each later target
// change along the tail.
TEST_CASE("Highway bend display amounts snap to half semitones", "[core][highway][tail]")
{
    CHECK(highwayBendDisplayHalfSteps(0.0) == 0);
    CHECK(highwayBendDisplayHalfSteps(0.5) == 1);
    CHECK(highwayBendDisplayHalfSteps(1.0) == 2);
    CHECK(highwayBendDisplayHalfSteps(1.5) == 3);
    CHECK(highwayBendDisplayHalfSteps(2.0) == 4);
    CHECK(highwayBendDisplayHalfSteps(3.0) == 6);

    // Near-half values snap to the nearest half-semitone bucket; sub-quarter noise shows
    // nothing, and a release-below-zero target can never go negative.
    CHECK(highwayBendDisplayHalfSteps(0.4) == 1);
    CHECK(highwayBendDisplayHalfSteps(0.2) == 0);
    CHECK(highwayBendDisplayHalfSteps(-1.0) == 0);
}

// Bends on the upper half of the displayed stack invert so the curve stays inside the board;
// the middle lane of an odd stack keeps the upward default.
TEST_CASE("Highway bend inversion splits the displayed stack", "[core][highway][tail]")
{
    CHECK_FALSE(highwayBendInverted(1, 6));
    CHECK_FALSE(highwayBendInverted(3, 6));
    CHECK(highwayBendInverted(4, 6));
    CHECK(highwayBendInverted(6, 6));
    CHECK_FALSE(highwayBendInverted(3, 5));
    CHECK(highwayBendInverted(4, 5));
}

// Slide easing endpoints are exact for both variants and the curves stay within [0, 1].
TEST_CASE("Highway slide easing spans its endpoints", "[core][highway][tail]")
{
    for (const bool unpitched : {false, true})
    {
        CHECK(highwaySlideEaseWeight(0.0, unpitched) == Catch::Approx(0.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(1.0, unpitched) == Catch::Approx(1.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(-1.0, unpitched) == Catch::Approx(0.0).margin(1.0e-12));
        CHECK(highwaySlideEaseWeight(2.0, unpitched) == Catch::Approx(1.0).margin(1.0e-12));
    }
    // The pitched curve accelerates late; the unpitched curve releases early.
    CHECK(highwaySlideEaseWeight(0.5, false) < 0.5);
    CHECK(highwaySlideEaseWeight(0.5, true) < 0.5);
    CHECK(
        highwaySlideEaseWeight(0.5, false) ==
        Catch::Approx(std::pow(std::sin(std::numbers::pi / 4.0), 3.0)));
}

// Wobbles are onset-phased pure functions: vibrato starts on the string line, tremolo is the
// reference triangle wave within its documented bounds.
TEST_CASE("Highway wobbles are onset-phased and bounded", "[core][highway][tail]")
{
    CHECK(highwayVibratoWobble(0.0) == Catch::Approx(0.0).margin(1.0e-12));
    CHECK(
        highwayVibratoWobble(g_highway_vibrato_period_seconds / 4.0) ==
        Catch::Approx(1.0).margin(1.0e-9));
    CHECK(
        highwayVibratoWobble(g_highway_vibrato_period_seconds) ==
        Catch::Approx(0.0).margin(1.0e-9));

    CHECK(highwayTremoloWobble(0.0) == Catch::Approx(0.75));
    CHECK(highwayTremoloWobble(g_highway_tremolo_period_seconds / 2.0) == Catch::Approx(-0.75));
    CHECK(highwayTremoloWobble(g_highway_tremolo_period_seconds) == Catch::Approx(0.75));
    for (int step = 0; step < 30; ++step)
    {
        const double wobble = highwayTremoloWobble(0.007 * step);
        CHECK(wobble >= -0.75);
        CHECK(wobble <= 0.75);
    }
}

// Sample times cover the span, include every technique control point inside it exactly, and
// stay sorted and deduplicated.
TEST_CASE("Highway tail sample times include control points", "[core][highway][tail]")
{
    HighwayNoteView note;
    note.start_seconds = 10.0;
    note.end_seconds = 14.0;
    note.bend = {
        HighwayBendPointView{.seconds = 11.3, .semitones = 1.0},
        HighwayBendPointView{.seconds = 9.0, .semitones = 0.5},  // outside: dropped
        HighwayBendPointView{.seconds = 15.0, .semitones = 0.0}, // outside: dropped
    };
    note.slides = {HighwaySlideView{.seconds = 12.7, .fret = 7, .unpitched = false}};

    const std::vector<double> times = makeHighwayTailSampleTimes(note, 10.0, 14.0, 5);

    REQUIRE(times.size() >= 5);
    CHECK(times.front() == Catch::Approx(10.0));
    CHECK(times.back() == Catch::Approx(14.0));
    CHECK(std::ranges::is_sorted(times));
    CHECK(std::ranges::count(times, 11.3) == 1);
    CHECK(std::ranges::count(times, 12.7) == 1);
    for (std::size_t index = 1; index < times.size(); ++index)
    {
        CHECK(times[index] - times[index - 1] > 0.0);
    }

    // An empty span yields no samples; a control point landing on a uniform sample dedupes.
    CHECK(makeHighwayTailSampleTimes(note, 12.0, 12.0, 5).empty());
    note.bend = {HighwayBendPointView{.seconds = 12.0, .semitones = 1.0}};
    note.slides.clear();
    const std::vector<double> deduped = makeHighwayTailSampleTimes(note, 10.0, 14.0, 5);
    CHECK(std::ranges::count(deduped, 12.0) == 1);
}

} // namespace rock_hero::common::core
