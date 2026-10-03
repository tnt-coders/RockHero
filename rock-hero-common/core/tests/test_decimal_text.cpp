#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>
#include <rock_hero/common/core/shared/decimal_text.h>

namespace rock_hero::common::core
{

// One whole finite number, surrounding whitespace aside, reads as that number.
TEST_CASE("parseFiniteDecimal reads one whole finite number", "[core][decimal-text]")
{
    const std::optional<double> whole = parseFiniteDecimal("137");
    REQUIRE(whole.has_value());
    if (whole.has_value())
    {
        CHECK_THAT(*whole, Catch::Matchers::WithinAbs(137.0, 1e-12));
    }

    const std::optional<double> padded = parseFiniteDecimal(" \t-12.5 \n");
    REQUIRE(padded.has_value());
    if (padded.has_value())
    {
        CHECK_THAT(*padded, Catch::Matchers::WithinAbs(-12.5, 1e-12));
    }
}

// Empty text, leftover text and anything that is not a number all read as absent.
TEST_CASE("parseFiniteDecimal refuses anything but one number", "[core][decimal-text]")
{
    CHECK_FALSE(parseFiniteDecimal("").has_value());
    CHECK_FALSE(parseFiniteDecimal("   ").has_value());
    CHECK_FALSE(parseFiniteDecimal("fast").has_value());
    CHECK_FALSE(parseFiniteDecimal("137x").has_value());
    CHECK_FALSE(parseFiniteDecimal("1 2").has_value());
}

} // namespace rock_hero::common::core
