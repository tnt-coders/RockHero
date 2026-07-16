#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/timeline/fraction.h>

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

// Verifies addition and subtraction stay exact and return normalized rationals, including the
// tuplet denominators the chart corpus uses.
TEST_CASE("Fraction adds and subtracts exactly", "[core][fraction]")
{
    CHECK(Fraction{1, 3} + Fraction{1, 6} == Fraction{1, 2});
    CHECK(Fraction{3, 5} + Fraction{2, 5} == Fraction{1});
    CHECK(Fraction{1, 7} + Fraction{1, 5} == Fraction{12, 35});
    CHECK(Fraction{5, 12} + Fraction{} == Fraction{5, 12});
    CHECK(Fraction{1, 2} - Fraction{3, 4} == Fraction{-1, 4});
    CHECK(Fraction{7, 9} - Fraction{7, 9} == Fraction{});
    CHECK(Fraction{-1, 4} + Fraction{-1, 4} == Fraction{-1, 2});

    // The result is normalized like every constructed Fraction: reduced, positive denominator.
    const Fraction sum = Fraction{1, 4} + Fraction{1, 4};
    CHECK(sum.numerator == 1);
    CHECK(sum.denominator == 2);
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
