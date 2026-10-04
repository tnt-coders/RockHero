#include "timeline/tick_lattice.h"

#include <cmath>
#include <cstdint>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>

namespace rock_hero::common::core
{

// Round half down is the ceiling of x - 1/2, the rational form's own identity.
std::int64_t nearestTick(const double exact_ticks) noexcept
{
    return static_cast<std::int64_t>(std::ceil(exact_ticks - 0.5));
}

// A reduced fraction of a whole note is a whole number of ticks exactly when its denominator
// divides the tick's.
bool isOnTickLattice(const Fraction whole_note) noexcept
{
    return g_tick_quantum_denominator % whole_note.denominator == 0;
}

bool isOnTickLattice(const TempoMap& tempo_map, const GridPosition& position)
{
    return isOnTickLattice(tempo_map.wholeNotePositionAt(position));
}

Fraction nearestTickPlace(const Fraction whole_note) noexcept
{
    if (isOnTickLattice(whole_note))
    {
        return whole_note;
    }
    const std::int64_t tick = nearestTick(
        static_cast<std::int64_t>(whole_note.numerator) * g_tick_quantum_denominator,
        whole_note.denominator);
    return Fraction{static_cast<int>(tick), g_tick_quantum_denominator};
}

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1} &&
           isOnTickLattice(tempo_map, position);
}

} // namespace rock_hero::common::core
