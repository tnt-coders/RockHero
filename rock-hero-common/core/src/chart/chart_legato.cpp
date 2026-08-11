#include "chart/chart_legato.h"

#include <array>
#include <cstddef>
#include <limits>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// The one index sentinel these walks share: "this string has no earlier note yet".
constexpr std::size_t g_no_predecessor{std::numeric_limits<std::size_t>::max()};

} // namespace

LegatoMotion resolveLegato(
    const ChartNote& note, const ChartNote* const predecessor,
    const Fraction predecessor_effective_sustain, const TempoMap& tempo_map)
{
    // The left-hand tap states the motion locally, so it is answered before any predecessor is
    // consulted: there is nothing for a neighbour to justify or withdraw.
    if (note.attack == NoteAttack::LeftTap)
    {
        return LegatoMotion::Hammer;
    }
    if (predecessor == nullptr || fretHandHarmonic(*predecessor) ||
        !predecessorHoldReaches(
            predecessor->position, predecessor_effective_sustain, note.position, tempo_map))
    {
        return LegatoMotion::Unjustified;
    }
    const int released = releasedFret(*predecessor);
    if (released > note.fret && !note.harmonic_node.has_value())
    {
        return LegatoMotion::Pull;
    }
    if (released < note.fret && (note.fret > 0 || note.harmonic_node.has_value()))
    {
        return LegatoMotion::Hammer;
    }
    return LegatoMotion::Unjustified;
}

ChartResolutions chartResolutions(
    const std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    ChartResolutions resolutions;
    // Everything downstream judges the SAVED form. A pick slide's latent mute is the difference
    // that matters: in memory an onset group can read as all-muted, and so choked, where the saved
    // chart reads it as held — which flips a span's hold extension and with it a following claim.
    // Building the stream once here is what keeps every consumer on the same side of that.
    resolutions.saved_notes.reserve(notes.size());
    for (const ChartNote& note : notes)
    {
        resolutions.saved_notes.push_back(savedChartNote(note));
    }
    resolutions.effective_sustains =
        chartEffectiveSustains(resolutions.saved_notes, shapes, tempo_map);

    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached.
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(g_no_predecessor);
    resolutions.legato.reserve(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = resolutions.saved_notes[index];
        const bool string_in_range = note.string >= 1 && note.string <= g_max_chart_strings;
        const std::size_t predecessor_index =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                            : g_no_predecessor;
        const ChartNote* predecessor = nullptr;
        Fraction predecessor_hold{};
        if (predecessor_index != g_no_predecessor)
        {
            predecessor = &resolutions.saved_notes[predecessor_index];
            predecessor_hold = resolutions.effective_sustains[predecessor_index];
        }
        // Only a note that actually CLAIMS a connection is resolved here. A plain pick's entry
        // stays `Unjustified` even where a claim would have resolved — which is exactly what lets
        // display code read this entry alone for the whole legatoClaimable family. The `H` toggle
        // asks resolveLegato directly for the hypothetical it needs.
        const bool claims = note.attack == NoteAttack::Legato || note.attack == NoteAttack::LeftTap;
        resolutions.legato.push_back(
            claims ? resolveLegato(note, predecessor, predecessor_hold, tempo_map)
                   : LegatoMotion::Unjustified);
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = index;
        }
    }
    return resolutions;
}

std::vector<std::string> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    const ChartResolutions resolutions = chartResolutions(notes, shapes, tempo_map);
    std::vector<std::string> conversions;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        if (note.attack != NoteAttack::Legato ||
            resolutions.legato[index] != LegatoMotion::Unjustified)
        {
            continue;
        }
        note.attack = NoteAttack::Pick;
        conversions.push_back(
            "legato at " + formatGridPositionToken(note.position) + " on string " +
            std::to_string(note.string) + " has nothing to connect to; recorded as a plain pick");
    }
    return conversions;
}

} // namespace rock_hero::common::core
