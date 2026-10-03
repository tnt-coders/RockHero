/*!
\file grid_position.h
\brief Musical grid position: a measure, a beat, and an exact sub-beat offset.
*/

#pragma once

#include <compare>
#include <rock_hero/common/core/timeline/fraction.h>

namespace rock_hero::common::core
{

/*!
\brief Musical grid position with exact sub-beat resolution.

Serializes as the grid token every stored position and every tempo anchor is written in:
`"<measure>:<beat>"` for whole beats and `"<measure>:<beat>+<n>/<d>"` for sub-beat positions.
*/
struct GridPosition
{
    /*! \brief One-based measure on the song grid. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};

    /*! \brief Exact fraction from this beat toward the next beat, in [0, 1). */
    Fraction offset{};

    /*!
    \brief Orders two grid positions along the timeline.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const GridPosition& lhs, const GridPosition& rhs) noexcept
    {
        // std::is_neq instead of `!= 0`: GCC's -Wzero-as-null-pointer-constant misfires on
        // ordering-vs-literal-zero comparisons (the standard comparison operator takes an
        // unspecified pointer-constructible parameter), so the named query keeps -Werror builds
        // clean without changing meaning.
        if (const auto measure_order = lhs.measure <=> rhs.measure; std::is_neq(measure_order))
        {
            return measure_order;
        }
        if (const auto beat_order = lhs.beat <=> rhs.beat; std::is_neq(beat_order))
        {
            return beat_order;
        }
        return lhs.offset <=> rhs.offset;
    }

    /*!
    \brief Compares two grid positions for equal value.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return True when both positions store equal values.
    */
    friend constexpr bool operator==(const GridPosition& lhs, const GridPosition& rhs) noexcept =
        default;
};

} // namespace rock_hero::common::core
