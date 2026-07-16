/*!
\file fraction.h
\brief Exact rational value type for grid-relative musical positions and durations.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <type_traits>

namespace rock_hero::common::core
{

/*!
\brief Exact rational number used for grid-relative note offsets and beat durations.

Fraction stores a normalized rational (reduced to lowest terms, with a positive denominator) so that
musical subdivisions such as 1/3 or 3/16 are represented exactly rather than as lossy decimals. Note
offsets use a Fraction in [0, 1) for the position within a beat; durations use a Fraction measured in
beats that may meet or exceed 1. Because every value is reduced on construction, equal rationals
always share one representation, which makes equality and set keys exact.
*/
struct Fraction
{
    /*! \brief Signed numerator of the reduced rational. */
    int numerator{0};

    /*! \brief Positive denominator of the reduced rational. */
    int denominator{1};

    /*! \brief Creates a zero value (0/1). */
    constexpr Fraction() noexcept = default;

    /*!
    \brief Creates a normalized rational from a numerator and denominator.
    \param numerator_value Signed numerator.
    \param denominator_value Denominator; its sign moves onto the numerator and zero collapses to 0/1.
    */
    constexpr Fraction(int numerator_value, int denominator_value) noexcept
    {
        // A headless value type cannot signal an error, so a zero denominator collapses to 0/1
        // rather than carrying an undefined rational that would poison later comparisons.
        if (denominator_value == 0)
        {
            return;
        }

        // Keep the sign on the numerator so equal values always share one reduced representation.
        if (denominator_value < 0)
        {
            numerator_value = -numerator_value;
            denominator_value = -denominator_value;
        }

        const int divisor = std::gcd(numerator_value, denominator_value);
        numerator = numerator_value / divisor;
        denominator = denominator_value / divisor;
    }

    /*!
    \brief Creates a whole-number rational (value/1).
    \param whole Whole number stored as value/1.
    */
    explicit constexpr Fraction(int whole) noexcept
        : numerator{whole}
    {}

    /*!
    \brief Resolves the rational to a double for time interpolation.
    \return Numerator divided by denominator.
    */
    [[nodiscard]] constexpr double toDouble() const noexcept
    {
        return static_cast<double>(numerator) / static_cast<double>(denominator);
    }

    /*!
    \brief Adds two rationals exactly.
    \param lhs Left-hand rational.
    \param rhs Right-hand rational.
    \return Reduced exact sum.
    */
    friend constexpr Fraction operator+(const Fraction& lhs, const Fraction& rhs) noexcept
    {
        // 64-bit intermediates keep the cross-multiplied sum exact far past the bounded terms the
        // chart grammar produces (1/960 fine grid, note values ≤ 1/128), then the reduced result
        // narrows back to int through the normalizing constructor — the same headless-value
        // stance as the zero-denominator collapse above.
        const std::int64_t numerator_sum =
            (static_cast<std::int64_t>(lhs.numerator) * rhs.denominator) +
            (static_cast<std::int64_t>(rhs.numerator) * lhs.denominator);
        const std::int64_t denominator_product =
            static_cast<std::int64_t>(lhs.denominator) * rhs.denominator;
        // Denominators are positive after normalization, so the gcd is always positive too.
        const std::int64_t divisor = std::gcd(numerator_sum, denominator_product);
        return Fraction{
            static_cast<int>(numerator_sum / divisor),
            static_cast<int>(denominator_product / divisor)
        };
    }

    /*!
    \brief Subtracts one rational from another exactly.
    \param lhs Left-hand rational.
    \param rhs Rational subtracted from lhs.
    \return Reduced exact difference.
    */
    friend constexpr Fraction operator-(const Fraction& lhs, const Fraction& rhs) noexcept
    {
        return lhs + Fraction{-rhs.numerator, rhs.denominator};
    }

    /*!
    \brief Orders two rationals by value using cross-multiplication.
    \param lhs Left-hand rational.
    \param rhs Right-hand rational.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const Fraction& lhs, const Fraction& rhs) noexcept
    {
        // Both denominators are positive after normalization, so cross-multiplication preserves the
        // ordering. int64 products keep the comparison exact for the small denominators charts use.
        return (static_cast<std::int64_t>(lhs.numerator) * rhs.denominator) <=>
               (static_cast<std::int64_t>(rhs.numerator) * lhs.denominator);
    }

    /*!
    \brief Compares two rationals for equal value.
    \param lhs Left-hand rational.
    \param rhs Right-hand rational.
    \return True when both reduce to the same numerator and denominator.
    */
    friend constexpr bool operator==(const Fraction& lhs, const Fraction& rhs) noexcept
    {
        // Reduction makes the representation unique, so field equality is value equality.
        return lhs.numerator == rhs.numerator && lhs.denominator == rhs.denominator;
    }
};

static_assert(sizeof(Fraction) <= 16);
static_assert(std::is_trivially_copyable_v<Fraction>);

} // namespace rock_hero::common::core
