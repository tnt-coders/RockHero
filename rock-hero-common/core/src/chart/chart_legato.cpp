#include "chart/chart_legato.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

LegatoMotion resolveLegato(
    const ChartNote& note, const ChartNote* const predecessor, const TempoMap& tempo_map)
{
    // The left-hand tap states the motion locally, so it is answered before any predecessor is
    // consulted: there is nothing for a neighbour to justify or withdraw.
    if (note.attack == NoteAttack::LeftTap)
    {
        return LegatoMotion::Hammer;
    }
    // A scrape is disqualified by its attack: its travel is the PICK's position on the string, so
    // there is no fretting finger at its end to release or to continue from — the note after a
    // scrape is picked. A dead predecessor is deliberately NOT disqualified (ruled and reversed the
    // same day, 2026-08-20): its finger is on the stop, and the muted cluck that follows is a
    // hammer or pull like any other; the hold test below bounds it by the same ring every note
    // carries.
    if (predecessor == nullptr || isScrape(predecessor->attack) || fretHandHarmonic(*predecessor) ||
        !predecessorHoldReaches(
            predecessor->position, predecessor->sustain, note.position, tempo_map))
    {
        return LegatoMotion::Unjustified;
    }
    const int released = releasedFret(*predecessor);
    if (released > note.fret && !note.harmonic_node.has_value())
    {
        return LegatoMotion::Pull;
    }
    if (released < note.fret)
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
    // What the surfaces draw, and how long the hand stays down: derived here so a chart revision
    // pays for them once, and so no consumer can derive a different picture of the same chart.
    resolutions.presented_notes = presentedChartNotes(resolutions.saved_notes, tempo_map);
    resolutions.holds =
        chartHolds(resolutions.saved_notes, resolutions.presented_notes, shapes, tempo_map);

    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached.
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(g_no_chart_predecessor);
    resolutions.legato.reserve(notes.size());
    resolutions.predecessors.reserve(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = resolutions.saved_notes[index];
        const bool string_in_range = note.string >= 1 && note.string <= g_max_chart_strings;
        const std::size_t predecessor_index =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                            : g_no_chart_predecessor;
        resolutions.predecessors.push_back(predecessor_index);
        const ChartNote* const predecessor = predecessor_index == g_no_chart_predecessor
                                                 ? nullptr
                                                 : &resolutions.saved_notes[predecessor_index];
        // Only a note that actually CLAIMS a connection is resolved here. A plain pick's entry
        // stays `Unjustified` even where a claim would have resolved — which is exactly what lets
        // display code read this entry alone for the whole legatoClaimable family. The `H` toggle
        // asks resolveLegato directly for the hypothetical it needs.
        const bool claims = note.attack == NoteAttack::Legato || note.attack == NoteAttack::LeftTap;
        resolutions.legato.push_back(
            claims ? resolveLegato(note, predecessor, tempo_map) : LegatoMotion::Unjustified);
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = index;
        }
    }
    return resolutions;
}

std::vector<ChartConversion> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    // Nothing can flatten unless some note actually claims a connection, and this runs at EVERY
    // settle event — every caret move, seek, selection change and playback start. The scan is a
    // bare read over the stream; the resolutions pass below copies the whole stream twice before it
    // can answer the same question. `LeftTap` is deliberately not counted: its claim is local, so
    // the sweep never touches one.
    if (std::ranges::none_of(
            notes, [](const ChartNote& note) { return note.attack == NoteAttack::Legato; }))
    {
        return {};
    }
    const ChartResolutions resolutions = chartResolutions(notes, shapes, tempo_map);
    std::vector<ChartConversion> conversions;
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
            ChartConversion{
                .repair = ChartRepair::UnjustifiedLegato,
                .where = formatGridPositionToken(note.position) + " string " +
                         std::to_string(note.string),
            });
    }
    return conversions;
}

} // namespace rock_hero::common::core
