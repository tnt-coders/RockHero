#include "chart/chart.h"

#include <cmath>

namespace rock_hero::common::core
{

int fretFor(const ChartNote& note)
{
    if (note.harmonic_node.has_value() && nodeIsOnNeck(note.attack) &&
        note.attack != NoteAttack::Tap)
    {
        return static_cast<int>(std::ceil(*note.harmonic_node));
    }
    return note.fret;
}

double snapHarmonicNode(const double notated, const int max_partial)
{
    // The octave is the fallback as well as the commonest target, so `best` starts there rather
    // than unset: a cap below 2 would otherwise leave nothing to return.
    double best = 12.0;
    double best_distance = std::abs(best - notated);
    // Every node of every partial in range, not just the nut-side one: notation names bridge-side
    // nodes too (19 and 24 are the 3rd and 4th partials' second and third nodes).
    for (int partial = 3; partial <= max_partial; ++partial)
    {
        for (int index = 1; index < partial; ++index)
        {
            const double node =
                12.0 *
                std::log2(static_cast<double>(partial) / static_cast<double>(partial - index));
            const double distance = std::abs(node - notated);
            if (distance < best_distance)
            {
                best_distance = distance;
                best = node;
            }
        }
    }
    return best;
}

} // namespace rock_hero::common::core
