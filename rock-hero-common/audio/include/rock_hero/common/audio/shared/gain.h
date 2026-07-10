/*!
\file gain.h
\brief Project-owned gain value type with shared clamping policy.
*/

#pragma once

#include <algorithm>
#include <compare>

namespace rock_hero::common::audio
{

/*!
\brief Gain value carried through the live rig boundary and persisted in tone documents.

Pass by value; the type is trivially copyable. Use clampGain() to enforce the accepted range and
minimumGainDb() / maximumGainDb() to read the limits for UI slider ranges.

\see clampGain
\see minimumGainDb
\see maximumGainDb
*/
struct Gain
{
    /*! \brief Gain in decibels. */
    double db{0.0};

    /*!
    \brief Compares two gain values by their decibel fields.
    \param lhs Left-hand gain value.
    \param rhs Right-hand gain value.
    \return True when both gain values store equal decibel values.
    */
    friend constexpr bool operator==(Gain lhs, Gain rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.db <=> rhs.db);
    }
};

/*!
\brief Returns the lowest gain the system accepts.
\return Minimum accepted gain in decibels.
*/
[[nodiscard]] constexpr double minimumGainDb() noexcept
{
    return -24.0;
}

/*!
\brief Returns the highest gain the system accepts.
\return Maximum accepted gain in decibels.
*/
[[nodiscard]] constexpr double maximumGainDb() noexcept
{
    return 24.0;
}

/*!
\brief Returns the default gain used for new tones and missing tone document fields.
\return Default gain in decibels.
*/
[[nodiscard]] constexpr double defaultGainDb() noexcept
{
    return 0.0;
}

/*!
\brief Returns the gain clamped to the accepted range.
\param gain Input gain value.
\return Gain with its decibel value clamped to [minimumGainDb(), maximumGainDb()].
*/
[[nodiscard]] constexpr Gain clampGain(Gain gain) noexcept
{
    return Gain{std::clamp(gain.db, minimumGainDb(), maximumGainDb())};
}

} // namespace rock_hero::common::audio
