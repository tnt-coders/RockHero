#include "shared/ratio_text.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>

namespace rock_hero::editor::core
{

// A positive whole number, spaces and tabs around it allowed; nothing else.
TEST_CASE("Positive whole text reads one positive whole number", "[core][ratio-text]")
{
    CHECK(parsePositiveWhole("7") == std::optional{7});
    CHECK(parsePositiveWhole(" \t12 ") == std::optional{12});
    for (const char* const refused : {"", " ", "0", "-3", "+3", "1.5", "3x", "x3", "3 3"})
    {
        INFO(refused);
        CHECK_FALSE(parsePositiveWhole(refused).has_value());
    }
}

// Two positive whole numbers around one slash, kept as written: 6/8 is not reduced to 3/4.
TEST_CASE("Typed ratio text keeps both terms as written", "[core][ratio-text]")
{
    const std::optional<TypedRatio> meter = parseTypedRatio(" 6 / 8 ");
    REQUIRE(meter.has_value());
    if (meter.has_value())
    {
        CHECK(meter->numerator == 6);
        CHECK(meter->denominator == 8);
    }
    for (const char* const refused : {"", "7", "7/", "/8", "0/4", "4/0", "1/2/3", "a/b", "-1/2"})
    {
        INFO(refused);
        CHECK_FALSE(parseTypedRatio(refused).has_value());
    }
}

} // namespace rock_hero::editor::core
