#include "timeline/tick_lattice.h"

#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>

namespace rock_hero::common::core
{

// A reduced fraction of a whole note is a whole number of ticks exactly when its denominator
// divides the tick's.
bool isOnTickLattice(const TempoMap& tempo_map, const GridPosition& position)
{
    return g_tick_quantum_denominator % tempo_map.wholeNotePositionAt(position).denominator == 0;
}

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1} &&
           isOnTickLattice(tempo_map, position);
}

} // namespace rock_hero::common::core
