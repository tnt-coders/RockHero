#include "chart/chart.h"

#include <cmath>

namespace rock_hero::common::core
{

std::optional<double> harmonicPartialOffset(const int partial)
{
    if (partial < 2)
    {
        return std::nullopt;
    }
    // The nut-side node of the nth partial divides the speaking length at 1/n, and a length ratio r
    // maps to fret units as 12*log2(1/(1-r)) — so 1/n gives 12*log2(n/(n-1)).
    return 12.0 * std::log2(static_cast<double>(partial) / static_cast<double>(partial - 1));
}

int handFret(const int fret, const std::optional<double>& node, const NoteAttack attack)
{
    if (!node.has_value() || attack == NoteAttack::Pinch || attack == NoteAttack::Tap)
    {
        return fret;
    }
    return static_cast<int>(std::lround(*node));
}

double snapHarmonicNode(const double notated, const int fret, const int max_partial)
{
    const auto fret_offset = static_cast<double>(fret);
    // The octave is the fallback as well as the commonest target, so `best` starts there rather than
    // unset: a cap below 2 would otherwise leave nothing to return.
    double best = fret_offset + 12.0;
    double best_distance = std::abs(best - notated);
    // Every node of every partial in range, not just the nut-side one: notation names bridge-side
    // nodes too (19 and 24 are the 3rd and 4th partials' second and third nodes).
    for (int partial = 3; partial <= max_partial; ++partial)
    {
        for (int index = 1; index < partial; ++index)
        {
            const double node =
                fret_offset +
                (12.0 *
                 std::log2(static_cast<double>(partial) / static_cast<double>(partial - index)));
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
