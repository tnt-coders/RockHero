#include "chart/chart_legato.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <utility>
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

ChartConnections chartConnections(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    ChartConnections connections;
    // Everything downstream judges the SAVED form. A pick slide's latent mute is the difference
    // that matters: in memory an onset group can read as all-muted, and so choked, where the saved
    // chart reads it as held — which flips a span's hold extension and with it a following claim.
    // Building the stream once here is what keeps every consumer on the same side of that.
    connections.saved_notes.reserve(notes.size());
    for (const ChartNote& note : notes)
    {
        connections.saved_notes.push_back(savedChartNote(note));
    }

    // The last note seen per string: the stream is sorted, so this IS each note's same-string
    // predecessor when it is reached.
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(g_no_chart_predecessor);
    connections.legato.reserve(notes.size());
    connections.predecessors.reserve(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        const bool string_in_range = note.string >= 1 && note.string <= g_max_chart_strings;
        const std::size_t predecessor_index =
            string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                            : g_no_chart_predecessor;
        connections.predecessors.push_back(predecessor_index);
        const ChartNote* const predecessor = predecessor_index == g_no_chart_predecessor
                                                 ? nullptr
                                                 : &connections.saved_notes[predecessor_index];
        // Only a note that actually CLAIMS a connection is resolved here. A plain pick's entry
        // stays `Unjustified` even where a claim would have resolved — which is exactly what lets
        // display code read this entry alone for the whole legatoClaimable family. The `H` toggle
        // asks resolveLegato directly for the hypothetical it needs.
        const bool claims = note.attack == NoteAttack::Legato || note.attack == NoteAttack::LeftTap;
        connections.legato.push_back(
            claims ? resolveLegato(note, predecessor, tempo_map) : LegatoMotion::Unjustified);
        // A PREDECESSOR is the last note that SOUNDED on the string: a connection continues a
        // ringing string, and a silently-held finger neither rings nor can be released from. Left
        // in the walk it would shadow the real predecessor, so a claim the chart justifies would
        // go quiet the moment a held shape was authored between the two notes.
        if (string_in_range && !silentHold(note.attack))
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = index;
        }
    }
    return connections;
}

ChartResolutions chartResolutions(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    ChartResolutions resolutions;
    resolutions.connections = chartConnections(notes, tempo_map);
    const std::vector<ChartNote>& saved_notes = resolutions.connections.saved_notes;
    // What the surfaces draw, which postures the hand holds, and how long it stays down: all
    // derived here so a chart revision pays for them once, and so no consumer can derive a
    // different picture of the same chart. The order is the dependency order — the spans are read
    // from the presented articulation, and the holds are answered against the spans.
    resolutions.presented_notes = presentedChartNotes(saved_notes, tempo_map);
    ChartShapes derived = deriveChartShapes(saved_notes, resolutions.presented_notes, tempo_map);
    resolutions.shapes = std::move(derived.shapes);
    resolutions.postures = std::move(derived.postures);
    resolutions.claim_shapes = std::move(derived.claim_shapes);
    resolutions.holds =
        chartHolds(saved_notes, resolutions.presented_notes, resolutions.shapes, tempo_map);
    return resolutions;
}

std::vector<ChartConversion> sweepUnjustifiedLegato(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    // Nothing can flatten unless some note actually claims a connection, and this runs at EVERY
    // settle event — every caret move, seek, selection change and playback start. The scan is a
    // bare read over the stream; the connections pass below copies the whole stream once before it
    // can answer the same question. `LeftTap` is deliberately not counted: its claim is local, so
    // the sweep never touches one.
    if (std::ranges::none_of(
            notes, [](const ChartNote& note) { return note.attack == NoteAttack::Legato; }))
    {
        return {};
    }
    const ChartConnections connections = chartConnections(notes, tempo_map);
    std::vector<ChartConversion> conversions;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        if (note.attack != NoteAttack::Legato ||
            connections.legato[index] != LegatoMotion::Unjustified)
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

std::vector<ChartConversion> sweepInertClaimedStops(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    // A stream that claims no stop at all has nothing to sweep, and this runs on every plan the
    // editor gates. The scan is a bare read; the derivation below presents and walks the whole
    // stream before it could answer the same question.
    if (std::ranges::none_of(
            notes, [](const ChartNote& note) { return claimedStop(note).has_value(); }))
    {
        return {};
    }
    std::vector<ChartConversion> conversions;
    // The fixpoint: taking one claim can leave a span with one member, which states nothing and
    // takes its remaining claims with it. Each round takes at least one claim, so this ends.
    for (bool swept = true; swept;)
    {
        const ChartShapes derived =
            deriveChartShapes(notes, presentedChartNotes(notes, tempo_map), tempo_map);
        // What this round took, counted off the one list both kinds report through — so the
        // fixpoint's "did anything change" cannot drift from what was actually reported.
        const std::size_t before = conversions.size();
        // Only the whole-note removals are collected: clearing a field leaves every index in place,
        // so it is done as the scan finds it and the erase list stays the one thing that must be
        // applied back to front.
        std::vector<std::size_t> removed;
        for (std::size_t index = 0; index < notes.size(); ++index)
        {
            ChartNote& note = notes[index];
            if (!claimedStop(note).has_value() || derived.claim_shapes[index].has_value())
            {
                continue;
            }
            const std::string where =
                formatGridPositionToken(note.position) + " string " + std::to_string(note.string);
            if (silentHold(note.attack))
            {
                removed.push_back(index);
                conversions.push_back(
                    ChartConversion{.repair = ChartRepair::InertSilentHold, .where = where});
                continue;
            }
            // The note still states its own onset, so only the statement that reached nothing
            // goes: the sound the charter wrote stays exactly as authored.
            note.held.reset();
            conversions.push_back(
                ChartConversion{.repair = ChartRepair::InertHeldStop, .where = where});
        }
        swept = conversions.size() != before;
        // Erased from the back, so every index still names the note it was derived against.
        for (const std::size_t index : std::views::reverse(removed))
        {
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    return conversions;
}

} // namespace rock_hero::common::core
