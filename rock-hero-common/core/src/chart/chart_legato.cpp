#include "chart/chart_legato.h"

#include "span_cover.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
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
    // scrape is picked. A dead predecessor is deliberately NOT disqualified: its finger is on the
    // stop, and the muted cluck that follows is a hammer or pull like any other; the hold test
    // below bounds it by the same ring every note carries. Disqualifying it outright would turn
    // every imported muted cluck into a picked note.
    if (predecessor == nullptr || isScrape(predecessor->attack) || fretHandHarmonic(*predecessor) ||
        !predecessorHoldReaches(
            predecessor->position, predecessor->sustain, note.position, tempo_map))
    {
        return LegatoMotion::Unjustified;
    }
    // Where the finger ENDS, which the arrival changes: a slide-out names a fret the hand never
    // reaches, while an arrival is a stop it glides onto and holds. This caller HOLDS the pair, so
    // it asks the relation directly rather than taking a resolved vector it has no index into.
    const int fret_at_end =
        fretAtRingEnd(*predecessor, arrivesIntoNextHead(*predecessor, note, tempo_map));
    if (fret_at_end > note.fret && !note.harmonic_node.has_value())
    {
        return LegatoMotion::Pull;
    }
    if (fret_at_end < note.fret)
    {
        return LegatoMotion::Hammer;
    }
    return LegatoMotion::Unjustified;
}

bool endsOnNextHead(
    const ChartNote& predecessor, const ChartNote& successor, const TempoMap& tempo_map)
{
    // A glide finishing early states a slide-out, and a ring running past the head is bounded by
    // the same-string clamp before any pair exists, so strict equality is the whole of "one
    // instant".
    return sustainEndPosition(tempo_map, predecessor) == successor.position;
}

bool arrivesIntoNextHead(
    const ChartNote& predecessor, const ChartNote& successor, const TempoMap& tempo_map)
{
    // (3) A scrape on either side, first, because it is the cheapest and disqualifies outright: a
    // scrape's travel is the pick's, its terminal is required at its end, and nothing glides into
    // its onset.
    if (isScrape(predecessor.attack) || isScrape(successor.attack))
    {
        return false;
    }
    // (4) A next head the PICKING hand stops the string for is not arrived into — a fretting hand
    // sliding into a fret a different hand then stops is not one gesture. A tapped harmonic passes
    // here by construction: the predicate is already false for it, because the fretting hand holds
    // the stop its node rides.
    if (pickingHandStopsString(successor.attack, successor.harmonic_node))
    {
        return false;
    }
    // (1) The end must NAME A FRET, asked through the one note-local accessor.
    const int* const stated = endStatedFretOrNull(predecessor);
    if (stated == nullptr)
    {
        return false;
    }
    // (2) EXACT ADJACENCY, asked of the clause's own function so the walk and this cannot measure
    // it two ways.
    if (!endsOnNextHead(predecessor, successor, tempo_map))
    {
        return false;
    }
    // (5) The fret named IS the stop the next head is struck at, node-aware — one query for both
    // sides, so a harmonic node and the fret beneath it can never read as the same place
    // (frettingStopAt). `ChartStop`'s defaulted `==` compares an `std::optional<double>` node, a
    // float compare made inside a standard library header, which coding-conventions names safe.
    return frettingStopAt(predecessor, *stated) == frettingStopAt(successor, successor.fret);
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
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings)> last_per_string{};
    last_per_string.fill(g_no_chart_predecessor);
    connections.legato.reserve(notes.size());
    connections.predecessors.reserve(notes.size());
    connections.hands_over.assign(notes.size(), false);
    connections.arrives_into.assign(notes.size(), false);
    connections.end_heads.assign(notes.size(), std::nullopt);
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        // Bound once so the presence test and both reads are provably the same object.
        const std::optional<std::size_t> string_index = chartStringIndex(note);
        const std::size_t predecessor_index =
            string_index.has_value() ? last_per_string.at(*string_index) : g_no_chart_predecessor;
        connections.predecessors.push_back(predecessor_index);
        const ChartNote* const predecessor = predecessor_index == g_no_chart_predecessor
                                                 ? nullptr
                                                 : &connections.saved_notes[predecessor_index];
        // THE SHIFT SLIDE, marked from the SUCCESSOR onto its predecessor because the relation is
        // about the pair (\ref arrivesIntoNextHead). Resolved BEFORE the claim below, which reads
        // the predecessor's fret at its ring's end and therefore this very answer — asked here once
        // and through the predicate there, one producer either way.
        if (predecessor != nullptr)
        {
            connections.arrives_into[predecessor_index] =
                arrivesIntoNextHead(*predecessor, note, tempo_map);
            // The arrival's EXACT ADJACENCY clause on its own: what an arrival and an abutting
            // slide-out share, and all the surfaces need to know that two marks stand at one x.
            if (endsOnNextHead(*predecessor, note, tempo_map))
            {
                connections.end_heads[predecessor_index] = index;
            }
        }
        // Only a note that actually CLAIMS a connection is resolved here. A plain pick's entry
        // stays `Unjustified` even where a claim would have resolved — which is exactly what lets
        // display code read this entry alone for the whole legatoClaimable family. The `H` toggle
        // asks resolveLegato directly for the hypothetical it needs.
        connections.legato.push_back(
            legatoClaimed(note.attack) ? resolveLegato(note, predecessor, tempo_map)
                                       : LegatoMotion::Unjustified);
        // WHICH RINGS HAND THEIR STRING OVER, marked from the SUCCESSOR because that is where the
        // chart states it: intent is stored on the note taking the connection (\ref legatoClaimed)
        // and the DIRECTION is derived per read, so this asks the STORED half and never the
        // resolution beside it — an equal-fret tie claim resolves `Unjustified` and still hands the
        // string over. The adjacency half is \ref predecessorHoldReaches, the resolver's own strict
        // test called rather than restated, so "the ring reaches the onset" cannot come to mean two
        // things.
        //
        // Filled here rather than by the one display rule that reads it, because the same-string
        // relation this needs is exactly the one this walk establishes, and two producers of one
        // relation is how a chart comes to be described two ways.
        if (predecessor != nullptr && legatoClaimed(note.attack))
        {
            connections.hands_over[predecessor_index] = predecessorHoldReaches(
                predecessor->position, predecessor->sustain, note.position, tempo_map);
        }
        // A PREDECESSOR is the last note on the string: a connection continues a ringing string.
        if (string_index.has_value())
        {
            last_per_string.at(*string_index) = index;
        }
    }
    return connections;
}

std::vector<std::optional<int>> chartPlantedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // THE HOLD-UNDER DERIVATION, read off the connections this walk already resolved: a PULL-OFF
    // states the stop planted beneath its source, because a finger has to be waiting on a fret to
    // be pulled off onto — whichever hand made the source's onset. It is a fact about the SLIDE-OUT
    // and no earlier instant, so what it may state to a grip is \ref gripStatement's to decide; it
    // is written NOWHERE, because the notation already states it, in the pull-off itself.
    std::vector<std::optional<int>> planted(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        if (connections.legato[index] != LegatoMotion::Pull)
        {
            continue;
        }
        // A Pull is resolved AGAINST a predecessor by construction (\ref resolveLegato answers
        // Unjustified without one), so this index is real and needs no second test.
        const std::size_t onset = connections.predecessors[index];
        const int stop = notes[index].fret;
        // EVERY fret derives alike, the open string included: what the pull-off states beneath its
        // source is the STOP the string falls to when the finger lifts, and for fret zero that stop
        // is the open string — always waiting, no finger needed. Only a destination the chart never
        // defines derives nothing.
        //
        // THE PLANT IS BOUND BY THE RELEASE ALONE, whatever path the source's finger travelled: the
        // finger it proves is on the string AT THE RELEASE, and a finger arriving behind a sliding
        // one and waiting there when it lifts is the ordinary two-finger landing of a slid
        // pull-off. The Pull resolution already puts the stop strictly below the fret released
        // from; no other bound exists (RULED 2026-09-29). The onset's traveled range bounds the
        // RIDE instead (\ref gripStatement).
        planted[onset] = stop;
    }
    return planted;
}

std::vector<std::optional<int>> chartHeldStops(
    const ChartConnections& connections, const ChartShapes& shapes, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    const std::vector<std::optional<int>> planted = chartPlantedStops(connections);
    std::vector<std::optional<int>> held(notes.size());
    // WHICH span covers an instant, from the one authority every span-scoped rule asks
    // (\ref SpanCover) — the same coverage the hold extension is measured against, so the default
    // can never sit under a span that walk says is not there.
    const SpanCover cover{shapes.shapes, tempo_map};
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        // The source is chosen on the RAW plant: a pull-off onto the open string states that no
        // finger is there, so the default must not answer beneath it.
        std::optional<int> stated = planted[index];
        if (!stated.has_value() && pickingHandStopsString(note.attack, note.harmonic_node))
        {
            // THE DEFAULT FACT: the hand is holding whatever grip it holds, so a tap the notation
            // says nothing under releases onto the covering span's posture — its PRESSED fret,
            // which a node grip states as 0 by construction. Nothing where no span covers the tap,
            // or where the covering posture says nothing about THIS string.
            // Each bound to a local so the presence test and the reads are provably one object.
            const std::optional<SpanCoverage> covering = cover.reaching(note.position);
            const std::optional<std::size_t> string_index = chartStringIndex(note);
            if (covering.has_value() && string_index.has_value())
            {
                const std::optional<ChartStop>& posture_stop =
                    shapes.postures[shapes.shapes[covering->span].posture].stops[*string_index];
                if (posture_stop.has_value())
                {
                    stated = posture_stop->fret;
                }
            }
        }
        // The open string is no finger, so it holds nothing.
        held[index] = stated.and_then(pressedFret);
    }
    return held;
}

ChartResolutions chartResolutions(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    ChartResolutions resolutions;
    resolutions.connections = chartConnections(notes, tempo_map);
    const std::vector<ChartNote>& saved_notes = resolutions.connections.saved_notes;
    // The SPANS are independent of presentation entirely: they read the stored stream alone, since
    // every stop they compare comes off a stored fret channel.
    ChartShapes derived = deriveChartShapes(resolutions.connections, tempo_map);
    // THE HELD TABLE, and its place in the pipeline is part of the rule: a bare tap's
    // DEFAULT held stop is the grip the covering span holds, so it reads the postures just derived.
    // It therefore runs AFTER the derivation and feeds nothing that runs before it. Handed the
    // whole derivation rather than its two vectors apart, because `shapes` indexes `postures` and
    // passing them separately is a mismatch waiting to happen.
    resolutions.held_stops = chartHeldStops(resolutions.connections, derived, tempo_map);
    // The CLASS every span arrives as, answered once for the revision because both surfaces draw
    // it. Asked of the stored stream, which presentation cannot move: the rule reads positions and
    // attacks and nothing else, and both come through presentation untouched. NO TAIL RULE READS
    // IT: the tail law is class-blind, so nothing downstream has to re-read the class.
    resolutions.arrivals = chartShapeArrivals(saved_notes, derived.shapes, tempo_map);
    // Where each ring's ink stops, derived here so a chart revision pays for it once and no
    // consumer can derive a different picture of the same chart. ONE PASS OWNS EVERY TAIL DECISION:
    // the presentation rules and the tail law come out together, so there is no ordering contract
    // between two rules (\ref chartPresentation). The spans do not go in at all — the curtain is
    // universal — but the connections go in whole, because the law reads the same-string relation
    // this walk established: the handover a figure cannot state.
    ChartPresentation presentation = chartPresentation(resolutions.connections, tempo_map);
    resolutions.shapes = std::move(derived.shapes);
    resolutions.postures = std::move(derived.postures);
    // The holds read the drawn lengths AND the law's verdict, which is what makes the two
    // complementary by construction: presentation only RESTS a member's ribbon, and the hold
    // hands a resting member its own stored ring.
    resolutions.holds =
        chartHolds(presentation, resolutions.connections, resolutions.shapes, tempo_map);
    resolutions.ink_end = std::move(presentation.ink_end);
    resolutions.rested_from = std::move(presentation.rested_from);
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

} // namespace rock_hero::common::core
