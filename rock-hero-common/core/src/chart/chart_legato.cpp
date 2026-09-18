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
    connections.hands_over.assign(notes.size(), false);
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
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = index;
        }
    }
    return connections;
}

std::vector<std::optional<int>> chartPlantedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // THE HOLD-UNDER DERIVATION, read off the connections this walk already resolved: a PULL-OFF
    // states the stop planted beneath its source, because a finger has to be waiting on a fret to
    // be pulled off onto — whichever hand made the source's onset. The planted stop is a fact about
    // the source for the whole of its ring, and it is written NOWHERE: the notation already states
    // it, in the pull-off itself.
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
        // THE TRAVELED RANGE REFUSES IT, through the very predicate that refuses an AUTHORED one
        // (\ref travelsThroughFret): the planted finger is on the string for the whole of the
        // onset's path, so a stop the source starts on, ends on or sweeps through is not a stop any
        // finger could have been waiting on. A source keyframed up past the fret its pull-off lands
        // on is the figure, and there the connection states nothing about a second finger. One
        // predicate for the derivation and the rule, so the resolution can never state a stop the
        // document would refuse.
        const ChartNote& onset_note = notes[onset];
        if (!travelsThroughFret(onset_note, stop))
        {
            planted[onset] = stop;
        }
    }
    return planted;
}

std::vector<std::optional<int>> chartDerivedStops(const ChartConnections& connections)
{
    // THE FIELD'S SCOPE, stated HERE and nowhere else. What a derivation can supersede is a stop
    // the `held` FIELD states, and only a note the picking hand stops the string for carries that
    // field at all (pickingHandStopsString). Under a FRETTING-hand onset the same planted finger
    // rides BESIDE the note's own fret: it states nothing the charter could have typed, supersedes
    // no field and leaves no residue. Under a TAPPED HARMONIC the fretting hand is on the pressed
    // stop the note itself states, and the model gives that hand no second finger, so a pull-off
    // from one derives nothing here either — its claim stays the pressed fret. Every FIELD-scoped
    // reader takes this narrowing, so a plant under either can never reach the claim column or the
    // writer's residue sweep. Who reads the WIDE table instead is stated once, on
    // \ref chartPlantedStops.
    std::vector<std::optional<int>> derived = chartPlantedStops(connections);
    for (std::size_t index = 0; index < derived.size(); ++index)
    {
        const ChartNote& note = connections.saved_notes[index];
        if (!pickingHandStopsString(note.attack, note.harmonic_node))
        {
            derived[index].reset();
        }
    }
    return derived;
}

std::vector<std::optional<int>> chartClaimedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // The fold, and the direction is the rule: the derivation SUPERSEDES the stored field rather
    // than agreeing with it, which is the whole point — one statement of the fact, and the notation
    // itself is where it is written.
    std::vector<std::optional<int>> claimed = chartDerivedStops(connections);
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        // Bound to a local so the presence test and the write are provably the same object.
        std::optional<int>& stop = claimed[index];
        if (!stop.has_value())
        {
            stop = claimedStop(notes[index]);
        }
    }
    return claimed;
}

std::vector<std::optional<int>> chartHeldStops(
    const std::vector<ChartNote>& notes, const std::vector<std::optional<int>>& claimed_stops,
    const std::vector<std::optional<int>>& planted_stops, const ChartShapes& shapes,
    const TempoMap& tempo_map)
{
    // WHICH span covers an instant, from the one authority every span-scoped rule asks
    // (\ref SpanCover) — the same coverage the hold extension is measured against, the only other
    // reader left since the curtain became universal, so the default can never sit under a span
    // that walk says is not there.
    const SpanCover cover{shapes.shapes, tempo_map};
    std::vector<std::optional<int>> held(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        // THE PRESSED STOP, first of the fretting hand's two tiers: a harmonic sounded over a
        // pressed stop prints the NODE at its head while the hand is on the stop below it
        // (\ref harmonicOverPressedStop), so that stop is what this note holds — and it OUTRANKS
        // the plant, because the pressed fret is pitch-critical and nothing else states it, while a
        // plant is a span fact the bracket prints.
        //
        // THE PLANT'S FACE: every other fretting-hand onset IS the hand, so the one second stop it
        // can hold is the one a pull-off PLANTS beneath it — the wide table the hold-under law
        // derives whichever hand made the onset. Every tier below is the RIGHT-HAND onset's, whose
        // fretting-hand stop the claim query names (\ref claimedStop): the planted finger beside a
        // plain tap or a scrape, the pressed fret under a tapped harmonic.
        if (!rightHandOnset(note.attack))
        {
            held[index] =
                harmonicOverPressedStop(note) ? std::optional{note.fret} : planted_stops[index];
            continue;
        }
        // Bound to a local so the presence test and the read are provably the same object. The
        // resolved claim already carries the first two tiers folded in that order — the
        // pull-off derivation over the authored field — so a note that states one is done here.
        const std::optional<int>& claimed = claimed_stops[index];
        if (claimed.has_value())
        {
            held[index] = claimed;
            continue;
        }
        // THE DEFAULT FACT: the hand is holding whatever grip it holds, so a tap that states
        // nothing releases onto the covering span's posture. Zero — the open string, nothing held —
        // where no span covers the tap, and equally where the covering posture says nothing about
        // THIS string: a posture is a per-string statement, and a string it never names is a string
        // no finger was on.
        //
        // Read live off the derived postures rather than stored anywhere, which is the whole of
        // why an edit reflowing the spans moves the default with them.
        int stop = 0;
        // Bound to a local so the presence test and the reads are provably the same object.
        if (const std::optional<SpanCoverage> covering = cover.reaching(note.position);
            covering.has_value() && note.string >= 1)
        {
            const ChartShape& span = shapes.shapes[covering->span];
            if (span.posture < shapes.postures.size())
            {
                // Posture array index 0 is the lowest string, exactly as the projection reads it.
                const std::vector<std::optional<ChartStop>>& stops =
                    shapes.postures[span.posture].stops;
                const auto string_index = static_cast<std::size_t>(note.string - 1);
                if (string_index < stops.size())
                {
                    // Bound to a local so the presence test and the read are one object. The
                    // PRESSED fret, which a node grip states as 0 by construction: a node presses
                    // nothing, so a tap under one releases onto the open string.
                    const std::optional<ChartStop>& posture_stop = stops[string_index];
                    stop = posture_stop.has_value() ? posture_stop->fret : 0;
                }
            }
        }
        held[index] = stop;
    }
    return held;
}

ChartResolutions chartResolutions(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    ChartResolutions resolutions;
    resolutions.connections = chartConnections(notes, tempo_map);
    const std::vector<ChartNote>& saved_notes = resolutions.connections.saved_notes;
    // The claims the spans are derived against, resolved once for the revision: a right-hand
    // onset's held stop is DERIVED where a pull-off states it, and every surface downstream reads
    // this rather than the raw field (\ref chartClaimedStops).
    //
    // Both halves of that one derivation are carried, because two different questions are asked of
    // it: WHAT the stop is, which the fold below answers, and WHO states it, which only the
    // derivation alone can — a stop the notation owns is read-only and shows its face on the
    // reveal's terms, and a consumer comparing values could not tell the two apart. The fold walks
    // the derivation again rather than being restated here over this vector: one linear pass per
    // chart revision is cheaper than a second copy of the fold free to disagree with the first.
    resolutions.claimed_stops = chartClaimedStops(resolutions.connections);
    // The SPANS are independent of presentation entirely: they read the stored stream alone, since
    // every stop they compare comes off a stored fret channel. The wide planted table rides
    // beside the claims for the hold-under law's verdicts, and is published for exactly two more
    // readers — the held table's fretting-hand tier below and the editor's retype refusal (THE
    // PLANT'S FACE); the claim column never sees it (\ref chartPlantedStops).
    resolutions.planted_stops = chartPlantedStops(resolutions.connections);
    ChartShapes derived = deriveChartShapes(
        saved_notes, resolutions.claimed_stops, resolutions.planted_stops, tempo_map);
    // THE COMPLETE HELD TABLE, and its place in the pipeline is part of the rule: a bare tap's
    // DEFAULT held stop is the grip the covering span holds, so it reads the postures the claims
    // above just produced. It therefore runs AFTER the derivation and feeds nothing that runs
    // before it — a default folded into the claims would be an input to the very spans it is read
    // out of. Handed the whole derivation rather than its two vectors apart, because `shapes`
    // indexes `postures` and passing them separately is a mismatch waiting to happen.
    resolutions.held_stops = chartHeldStops(
        saved_notes, resolutions.claimed_stops, resolutions.planted_stops, derived, tempo_map);
    // The CLASS every span arrives as, answered once for the revision because both surfaces draw
    // it. Asked of the stored stream, which presentation cannot move: the rule reads positions and
    // attacks and nothing else, and both come through presentation untouched. NO TAIL RULE READS
    // IT: the tail law is class-blind, so nothing downstream has to re-read the class.
    resolutions.arrivals = chartShapeArrivals(saved_notes, derived.shapes, tempo_map);
    // What the surfaces draw, derived here so a chart revision pays for it once and no consumer can
    // derive a different picture of the same chart. ONE PASS OWNS EVERY TAIL DECISION: the
    // presentation rules and the tail law come out together, so there is no ordering contract
    // between two rules and no rewritten copy of the stream under the saved stream's name (\ref
    // presentedChartNotes). The spans do not go in at all — the curtain is universal — but the
    // connections go in whole, because the law reads the same-string relation this walk
    // established: the handover a figure cannot state.
    ChartPresentation presentation = presentedChartNotes(resolutions.connections, tempo_map);
    resolutions.shapes = std::move(derived.shapes);
    resolutions.postures = std::move(derived.postures);
    resolutions.claim_shapes = std::move(derived.claim_shapes);
    // The holds read the presented picture AND the law's verdict, which is what makes the two
    // complementary by construction: presentation only RESTS a member's ribbon, and the hold
    // hands a resting member its own stored ring.
    resolutions.holds =
        chartHolds(presentation, resolutions.connections, resolutions.shapes, tempo_map);
    resolutions.presented_notes = std::move(presentation.notes);
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

std::vector<ChartConversion> sweepInertClaimedStops(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    // A stream stating no field claim at all has nothing to sweep, and this runs on every plan the
    // editor gates. The scan is a bare read; the derivation below presents and walks the whole
    // stream before it could answer the same question.
    if (std::ranges::none_of(notes, [](const ChartNote& note) {
            return pickingHandStopsString(note.attack, note.harmonic_node) &&
                   claimedStop(note).has_value();
        }))
    {
        return {};
    }
    std::vector<ChartConversion> conversions;
    // ONE PASS. What this takes is a claim that reached NO span, so it was a member of nothing and
    // no span's membership moves when it goes — the cascade a fixpoint would iterate for cannot
    // arise. The spans read the stored stream alone, so no presentation pass is paid for here:
    // one would only hand back the frets it started with.
    const ChartConnections connections = chartConnections(notes, tempo_map);
    const ChartShapes derived = deriveChartShapes(
        notes, chartClaimedStops(connections), chartPlantedStops(connections), tempo_map);
    // Clearing a field leaves every index in place, so each repair is applied as the scan finds it.
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        // FIELD SCOPE, the same narrowing the residue sweep takes: what this clears is the `held`
        // FIELD, so the only claim it can take is one that field states. A tapped harmonic claims
        // the stop it SPEAKS from — its own fret (\ref claimedStop) — which is the sound the
        // charter wrote, carries no field to clear, and is no more inert than a head is.
        if (!pickingHandStopsString(note.attack, note.harmonic_node) ||
            !claimedStop(note).has_value() || derived.claim_shapes[index].has_value())
        {
            continue;
        }
        const std::string where =
            formatGridPositionToken(note.position) + " string " + std::to_string(note.string);
        // The note still states its own onset, so only the statement that reached nothing goes:
        // the sound the charter wrote stays exactly as authored.
        note.held.reset();
        conversions.push_back(
            ChartConversion{.repair = ChartRepair::InertHeldStop, .where = where});
    }
    return conversions;
}

std::vector<ChartConversion> sweepDerivedHeldStops(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    // Nothing stores a held stop, so nothing can be residue — and this runs on every plan the
    // editor gates, where the connection walk below is the expensive half.
    if (std::ranges::none_of(notes, [](const ChartNote& note) { return note.held.has_value(); }))
    {
        return {};
    }
    std::vector<ChartConversion> conversions;
    const std::vector<std::optional<int>> derived =
        chartDerivedStops(chartConnections(notes, tempo_map));
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        // Neither optional is ever READ here, only asked whether it is there: what the derivation
        // states is already the answer every consumer gets, and what the field held is what this
        // takes away.
        if (!derived[index].has_value() || !note.held.has_value())
        {
            continue;
        }
        // Unconditional: agreeing or contradicting, the notation is where this stop is written.
        note.held.reset();
        conversions.push_back(
            ChartConversion{
                .repair = ChartRepair::DerivedHeldStop,
                .where = formatGridPositionToken(note.position) + " string " +
                         std::to_string(note.string),
            });
    }
    return conversions;
}

} // namespace rock_hero::common::core
