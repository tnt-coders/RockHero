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
\brief Exact rational number used for grid-relative note offsets and note-value durations.

Fraction stores a normalized rational (reduced to lowest terms, with a positive denominator) so that
musical subdivisions such as 1/3 or 3/16 are represented exactly rather than as lossy decimals. A
grid position's offset is a Fraction in [0, 1) of its beat; a duration is a note value in whole
notes (1/4 a quarter, 3/8 a dotted quarter) whatever the meter, and may meet or exceed 1. Because
every value is reduced on construction, equal rationals always share one representation, which
makes equality and set keys exact.
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
    \param denominator_value Denominator; its sign moves onto the
                             numerator and zero collapses to 0/1.
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
        // chart grammar produces (the 1/3840-whole-note tick lattice every position lands on,
        // selectable grids no finer than 1/128), then the reduced result narrows back to int.
        const std::int64_t numerator_sum =
            (static_cast<std::int64_t>(lhs.numerator) * rhs.denominator) +
            (static_cast<std::int64_t>(rhs.numerator) * lhs.denominator);
        const std::int64_t denominator_product =
            static_cast<std::int64_t>(lhs.denominator) * rhs.denominator;
        return reducedOf(numerator_sum, denominator_product);
    }

    /*!
    \brief Subtracts one rational from another exactly.
    \param lhs Left-hand rational.
    \param rhs Rational subtracted from lhs.
    \return Reduced exact difference.
    */
    friend constexpr Fraction operator-(const Fraction& lhs, const Fraction& rhs) noexcept
    {
        // Negating the numerator keeps a reduced rational reduced, so nothing re-reduces it.
        Fraction negated = rhs;
        negated.numerator = -negated.numerator;
        return lhs + negated;
    }

    /*!
    \brief Multiplies two rationals exactly.
    \param lhs Left-hand rational.
    \param rhs Right-hand rational.
    \return Reduced exact product.
    */
    friend constexpr Fraction operator*(const Fraction& lhs, const Fraction& rhs) noexcept
    {
        // Same 64-bit-intermediates-then-reduce stance as operator+: the unreduced products can
        // leave int range on song-scale terms even when the reduced result is tiny.
        const std::int64_t numerator_product =
            static_cast<std::int64_t>(lhs.numerator) * rhs.numerator;
        const std::int64_t denominator_product =
            static_cast<std::int64_t>(lhs.denominator) * rhs.denominator;
        return reducedOf(numerator_product, denominator_product);
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

private:
    // Reduces 64-bit terms with a positive denominator once and narrows them to int. Arithmetic
    // runs per note on every derivation pass, so it sets the fields directly rather than through
    // the normalizing constructor, whose second gcd on already-coprime terms roughly doubled its
    // cost.
    static constexpr Fraction reducedOf(
        const std::int64_t numerator_value, const std::int64_t denominator_value) noexcept
    {
        // The denominator is positive, so the gcd is too.
        const std::int64_t divisor = std::gcd(numerator_value, denominator_value);
        Fraction reduced;
        reduced.numerator = static_cast<int>(numerator_value / divisor);
        reduced.denominator = static_cast<int>(denominator_value / divisor);
        return reduced;
    }
};

static_assert(sizeof(Fraction) <= 16);
static_assert(std::is_trivially_copyable_v<Fraction>);

} // namespace rock_hero::common::core
