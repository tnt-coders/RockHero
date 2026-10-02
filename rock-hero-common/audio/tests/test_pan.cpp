#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <optional>
#include <rock_hero/common/audio/shared/pan.h>
#include <rock_hero/common/audio/shared/tone_output.h>

namespace rock_hero::common::audio
{

// Verifies that the default pan is centre and that clamping follows the shared range.
TEST_CASE("Pan defaults to centre and clamps to the accepted range", "[audio][pan]")
{
    constexpr Pan pan{};
    CHECK_THAT(pan.position, Catch::Matchers::WithinULP(0.0, 0));
    CHECK(std::is_eq(clampPan(Pan{-0.5}).position <=> -0.5));
    CHECK(std::is_eq(clampPan(Pan{-3.0}).position <=> minimumPanPosition()));
    CHECK(std::is_eq(clampPan(Pan{3.0}).position <=> maximumPanPosition()));
}

// Verifies the one display form: C at centre, L/R plus the whole percent elsewhere.
TEST_CASE("formatPan renders C, L<percent> and R<percent>", "[audio][pan]")
{
    CHECK(formatPan(Pan{}) == "C");
    CHECK(formatPan(Pan{0.004}) == "C");
    CHECK(formatPan(Pan{-0.35}) == "L35");
    CHECK(formatPan(Pan{0.355}) == "R36");
    CHECK(formatPan(Pan{1.0}) == "R100");
    CHECK(formatPan(Pan{-7.0}) == "L100");
}

// Verifies parsing is the exact inverse of formatting and tolerates case and spaces only.
TEST_CASE("parsePan reads the display form back and refuses anything else", "[audio][pan]")
{
    CHECK(parsePan("C") == std::optional{Pan{}});
    CHECK(parsePan(" c ") == std::optional{Pan{}});
    CHECK(parsePan("L35") == std::optional{Pan{-0.35}});
    CHECK(parsePan("r 20") == std::optional{Pan{0.2}});
    CHECK(parsePan("R250") == std::optional{Pan{1.0}});
    CHECK_FALSE(parsePan("").has_value());
    CHECK_FALSE(parsePan("centre").has_value());
    CHECK_FALSE(parsePan("-35").has_value());
    CHECK_FALSE(parsePan("L").has_value());
    CHECK_FALSE(parsePan("L35.5").has_value());
    CHECK_FALSE(parsePan("L-5").has_value());

    for (const Pan& pan : {Pan{-1.0}, Pan{-0.35}, Pan{}, Pan{0.5}, Pan{1.0}})
    {
        CHECK(parsePan(formatPan(pan)) == std::optional{pan});
    }
}

// Verifies the output stage compares and clamps as one value.
TEST_CASE("ToneOutput clamps level and pan together", "[audio][pan]")
{
    const ToneOutput clamped = clampToneOutput(ToneOutput{.level = Gain{40.0}, .pan = Pan{-2.0}});
    CHECK(clamped == ToneOutput{.level = Gain{maximumGainDb()}, .pan = Pan{minimumPanPosition()}});
    CHECK(ToneOutput{} == ToneOutput{.level = Gain{defaultGainDb()}, .pan = Pan{}});
}

} // namespace rock_hero::common::audio
