#include "chart/chart.h"

#include <cmath>

namespace rock_hero::common::core
{

int fretFor(const ChartNote& note)
{
    // The has_value() guard is implied by the predicate but spelled out anyway: the CI-only
    // optional-access checker cannot see through a wrapper (chart.h documents the pattern), so the
    // dereference stays visibly paired with its own check.
    if (note.harmonic_node.has_value() && frettingFingerOnNode(note))
    {
        return static_cast<int>(std::ceil(*note.harmonic_node));
    }
    return note.fret;
}

ChartNote savedChartNote(const ChartNote& note)
{
    ChartNote saved = note;
    if (saved.attack == NoteAttack::PickSlide)
    {
        saved.mute = NoteMute::None;
        saved.harmonic_node.reset();
        saved.vibrato = false;
        saved.tremolo = false;
        saved.bend.clear();
    }
    return saved;
}

int releasedFret(const ChartNote& note)
{
    if (note.attack == NoteAttack::PickSlide && note.slide_out.has_value())
    {
        return note.slide_out->fret;
    }
    return note.slides.empty() ? note.fret : note.slides.back().fret;
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
