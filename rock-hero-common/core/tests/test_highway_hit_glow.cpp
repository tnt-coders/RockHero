#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <rock_hero/common/core/highway/highway_hit_glow.h>

namespace rock_hero::common::core
{

// Strict zero pre-glow with a full-intensity crossing: the envelope is zero for any negative
// offset, exactly 1.0 at the crossing, decays monotonically, and is dark again from the release
// end on. The smoothstep shape leaves half the light at the halfway point.
TEST_CASE("Hit glow peaks at the crossing and decays to zero", "[core][highway][hit-glow]")
{
    constexpr double release = 0.09;

    CHECK(highwayHitGlowIntensity(-0.001, release) == Catch::Approx(0.0));
    CHECK(highwayHitGlowIntensity(0.0, release) == Catch::Approx(1.0));

    const double early = highwayHitGlowIntensity(0.01, release);
    const double mid = highwayHitGlowIntensity(release / 2.0, release);
    const double late = highwayHitGlowIntensity(0.08, release);
    CHECK(early < 1.0);
    CHECK(mid < early);
    CHECK(late < mid);
    CHECK(late > 0.0);
    CHECK(mid == Catch::Approx(0.5));

    CHECK(highwayHitGlowIntensity(release, release) == Catch::Approx(0.0));
    CHECK(highwayHitGlowIntensity(0.2, release) == Catch::Approx(0.0));

    // A degenerate or negative release never lights.
    CHECK(highwayHitGlowIntensity(0.0, 0.0) == Catch::Approx(0.0));
    CHECK(highwayHitGlowIntensity(0.0, -1.0) == Catch::Approx(0.0));
}

// The inter-onset clamp guarantees a dark trough before the next pop on the same geometry at any
// tempo: wide gaps keep the nominal release, moderate gaps end a full guard early, and
// ultra-dense gaps floor at half the spacing so both a pop and a trough survive.
TEST_CASE("Hit glow release clamps to the next onset's spacing", "[core][highway][hit-glow]")
{
    constexpr double nominal = 0.09;
    constexpr double guard = 0.03;

    CHECK(
        highwayHitGlowRelease(nominal, guard, std::numeric_limits<double>::infinity()) ==
        Catch::Approx(nominal));
    CHECK(highwayHitGlowRelease(nominal, guard, 1.0) == Catch::Approx(nominal));

    // A 150 BPM sixteenth (0.1 s gap): the release ends a full guard before the next pop —
    // exactly the density where a fixed release used to fuse into a continuous shimmer.
    CHECK(highwayHitGlowRelease(nominal, guard, 0.1) == Catch::Approx(0.07));

    // Denser than the guard can carve from: split the gap evenly instead of going dark.
    CHECK(highwayHitGlowRelease(nominal, guard, 0.04) == Catch::Approx(0.02));

    // Coincident onsets are one strike, not a clamp pair.
    CHECK(highwayHitGlowRelease(nominal, guard, 0.0) == Catch::Approx(nominal));
}

} // namespace rock_hero::common::core
