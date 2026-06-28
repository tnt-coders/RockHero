/*!
\file grid_position.h
\brief Grid-relative musical address: a measure, beat, and exact sub-beat offset.
*/

#pragma once

#include <rock_hero/common/core/fraction.h>
#include <type_traits>

namespace rock_hero::common::core
{

/*!
\brief Grid-relative address of a point on the beat grid.

A GridPosition names a musical position as a one-based measure and beat plus an exact fractional
offset into that beat. It is resolved to absolute seconds through the song TempoMap and is never
stored as seconds. The persisted form is a single token, "<measure>:<beat>" or
"<measure>:<beat>+<fraction>" (for example "12:1" or "12:2+1/2"), with a zero offset omitted. Both an
event's start and its end use this one address type, so they resolve through the same path.
*/
struct GridPosition
{
    /*! \brief One-based measure. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};

    /*! \brief Exact sub-beat offset in [0, 1); zero is on the beat. */
    Fraction offset{};

    /*!
    \brief Compares two grid positions by their stored fields.
    \param lhs Left-hand grid position.
    \param rhs Right-hand grid position.
    \return True when both positions store equal measure, beat, and offset.
    */
    friend constexpr bool operator==(const GridPosition& lhs, const GridPosition& rhs) noexcept =
        default;
};

static_assert(sizeof(GridPosition) <= 16);
static_assert(std::is_trivially_copyable_v<GridPosition>);

} // namespace rock_hero::common::core
