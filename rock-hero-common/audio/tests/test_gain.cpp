#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/gain.h>

namespace rock_hero::common::audio
{

// Verifies that the default gain is 0 dB.
TEST_CASE("Gain defaults to 0 dB", "[audio][gain]")
{
    constexpr Gain gain{};
    CHECK(gain.db == 0.0);
    CHECK(gain.db == defaultGainDb());
}

// Verifies that clampGain passes through values inside the accepted range.
TEST_CASE("clampGain passes through in-range values", "[audio][gain]")
{
    CHECK(clampGain(Gain{0.0}).db == 0.0);
    CHECK(clampGain(Gain{-12.0}).db == -12.0);
    CHECK(clampGain(Gain{3.0}).db == 3.0);
    CHECK(clampGain(Gain{-24.0}).db == -24.0);
    CHECK(clampGain(Gain{24.0}).db == 24.0);
    CHECK(clampGain(Gain{minimumGainDb()}).db == minimumGainDb());
    CHECK(clampGain(Gain{maximumGainDb()}).db == maximumGainDb());
}

// Verifies that clampGain clamps values below the minimum.
TEST_CASE("clampGain clamps below minimum", "[audio][gain]")
{
    CHECK(clampGain(Gain{-100.0}).db == minimumGainDb());
    CHECK(clampGain(Gain{-25.0}).db == minimumGainDb());
}

// Verifies that clampGain clamps values above the maximum.
TEST_CASE("clampGain clamps above maximum", "[audio][gain]")
{
    CHECK(clampGain(Gain{100.0}).db == maximumGainDb());
    CHECK(clampGain(Gain{25.0}).db == maximumGainDb());
}

// Verifies that gain equality compares by dB value.
TEST_CASE("Gain equality compares dB values", "[audio][gain]")
{
    CHECK(Gain{0.0} == Gain{0.0});
    CHECK(Gain{-6.0} == Gain{-6.0});
    CHECK_FALSE(Gain{0.0} == Gain{1.0});
}

} // namespace rock_hero::common::audio
