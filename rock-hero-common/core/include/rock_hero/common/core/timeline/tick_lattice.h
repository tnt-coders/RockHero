/*!
\file tick_lattice.h
\brief The tick lattice every stored position lands on, and the rule for a valid grid position.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Denominator of the timeline's tick lattice: the finest position the chart may land on.

1/3840 of a whole note is 1/960 of a quarter note — the standard MIDI PPQ tick, far finer than
audible resolution and still an exact rational. 3840 rather than 4096 because triplet grids need
the factor of 3, so straight, triplet, and quintuplet subdivisions all land on the lattice.
*/
inline constexpr int g_tick_quantum_denominator = 3840;

/*!
\brief The tick lattice as a note value: the lattice every stored position lands on.

A note value like any other, so the measure-anchored lattice arithmetic walks it unchanged. It is
never a grid the user selects or the editor draws — only a lattice positions land on: the editor's
placement quantum falls back to it while grid snap is off.
*/
inline constexpr Fraction g_tick_quantum_note_value{1, g_tick_quantum_denominator};

/*!
\brief THE one rounding rule onto the tick lattice: the whole tick nearest an exact tick count,
a tie going to the earlier tick.

Every producer that turns an instant into a position rounds through this, so a septuplet grid's
lines and a note imported off a septuplet rhythm land on the SAME tick: both start from the same
exact instant and round it the same way. The tie rule is
the snap's own stable-click rule. Adding whole ticks commutes with it, so rounding from a downbeat
and rounding from the song's start agree, every downbeat being a whole tick.

\param numerator Numerator of the exact tick count.
\param denominator Denominator of the exact tick count; strictly positive.
\return The nearest whole tick.
*/
[[nodiscard]] constexpr std::int64_t nearestTick(
    const std::int64_t numerator, const std::int64_t denominator) noexcept
{
    // Round half down is the ceiling of x - 1/2, which is floor((2n + d - 1) / 2d).
    const std::int64_t shifted = (2 * numerator) + denominator - 1;
    const std::int64_t divisor = 2 * denominator;
    const std::int64_t quotient = shifted / divisor;
    return (shifted % divisor != 0 && shifted < 0) ? quotient - 1 : quotient;
}

/*!
\brief The same rounding rule onto the tick lattice for a tick count read as a double.

A producer whose source states a position as a decimal (a Guitar Pro sync point's fraction of a
bar) has no exact rational to hand the rational form, so it asks this one: the nearest whole tick,
a tie going to the earlier tick, exactly as \ref nearestTick(std::int64_t, std::int64_t) rounds.

\param exact_ticks Tick count to round; finite and within the range of a 64-bit integer.
\return The nearest whole tick.
*/
[[nodiscard]] std::int64_t nearestTick(double exact_ticks) noexcept;

/*!
\brief Reports whether a place on the whole-note axis lies on the timeline's tick lattice.

THE lattice rule: the lattice is every multiple of \ref g_tick_quantum_note_value on the
whole-note axis (\ref TempoMap::wholeNotePositionAt), which every downbeat of a meter the package
format admits lies on.

\param whole_note Whole notes from the grid origin.
\return True when the place is a whole number of ticks from the grid origin.
*/
[[nodiscard]] bool isOnTickLattice(Fraction whole_note) noexcept;

/*!
\brief Reports whether a position lies on the timeline's tick lattice: its whole-note place does.
\param tempo_map Tempo map supplying the meter of every measure up to the position.
\param position Position to test.
\return True when the position is a whole number of ticks from the grid origin.
*/
[[nodiscard]] bool isOnTickLattice(const TempoMap& tempo_map, const GridPosition& position);

/*!
\brief Returns the lattice place nearest a place on the whole-note axis.

A place on the lattice comes back unchanged, so an exact producer never rounds; any other rounds
through \ref nearestTick(std::int64_t, std::int64_t), a tie going to the earlier tick.

\param whole_note Whole notes from the grid origin.
\return The nearest whole number of ticks, as whole notes.
*/
[[nodiscard]] Fraction nearestTickPlace(Fraction whole_note) noexcept;

/*!
\brief Reports whether a grid position names a real place on the tempo map's grid.

Shared by every document that stores a grid position (chart notes, section markers, tone changes),
so the on-grid rule cannot drift between them. A usable position lies on the tick lattice
(\ref isOnTickLattice): the chart can state no instant between two ticks.

\param position Grid position to test.
\param tempo_map Song tempo map defining the grid.
\return True when the position's measure, beat, and sub-beat offset are all usable.
*/
[[nodiscard]] bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
