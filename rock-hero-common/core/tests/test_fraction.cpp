#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/domain/fraction.h>

namespace rock_hero::common::core
{

// Verifies construction reduces to lowest terms and pins the sign to the numerator.
TEST_CASE("Fraction normalizes on construction", "[core][fraction]")
{
    CHECK(Fraction{2, 4} == Fraction{1, 2});
    CHECK(Fraction{6, 8} == Fraction{3, 4});
    CHECK(Fraction{0, 5} == Fraction{});
    CHECK(Fraction{4}.numerator == 4);
    CHECK(Fraction{4}.denominator == 1);

    // A negative denominator moves its sign onto the numerator so equal values share one form.
    const Fraction negative{1, -2};
    CHECK(negative.numerator == -1);
    CHECK(negative.denominator == 2);
}

// Verifies a zero denominator collapses to 0/1 instead of an undefined rational.
TEST_CASE("Fraction collapses a zero denominator", "[core][fraction]")
{
    CHECK(Fraction{3, 0} == Fraction{});
    CHECK(Fraction{3, 0}.denominator == 1);
}

// Verifies ordering compares by value, not by stored fields.
TEST_CASE("Fraction orders by value", "[core][fraction]")
{
    CHECK(Fraction{1, 3} < Fraction{1, 2});
    CHECK(Fraction{1, 2} < Fraction{2, 3});
    CHECK(Fraction{3, 4} > Fraction{1, 2});
    CHECK(Fraction{} <= Fraction{0, 9});
    CHECK(Fraction{1, 2} >= Fraction{2, 4});
}

// Verifies the rational resolves to the expected double for time interpolation.
TEST_CASE("Fraction resolves to a double", "[core][fraction]")
{
    CHECK(Fraction{1, 4}.toDouble() == Catch::Approx(0.25));
    CHECK(Fraction{3, 2}.toDouble() == Catch::Approx(1.5));
    CHECK(Fraction{}.toDouble() == Catch::Approx(0.0));
}

} // namespace rock_hero::common::core
