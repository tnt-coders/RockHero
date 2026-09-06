#include "chart/chart_legato.h"

#include "span_cover.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
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

std::vector<std::optional<int>> chartPlantedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // THE HOLD-UNDER DERIVATION (user ruling 2026-09-06), read off the connections this walk
    // already resolved: a PULL-OFF states the stop planted beneath its source, because a finger
    // has to be waiting on a fret to be pulled off onto — whichever hand made the source's onset.
    // The planted stop is a fact about the source for the whole of its ring, and it is written
    // NOWHERE: the notation already states it, in the pull-off itself.
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
        // An open string asserts no finger, so a pull onto one plants nothing.
        //
        // AND THE TRAVELED RANGE REFUSES IT, through the very predicate that refuses an AUTHORED
        // one (\ref travelsThroughFret, user ruling 2026-08-27): the planted finger is on the
        // string for the whole of the onset's path, so a stop the source starts on, ends on
        // or sweeps through is not a stop any finger could have been waiting on. A source
        // keyframed up past the fret its pull-off lands on is the figure, and there the
        // connection states nothing about a second finger. One predicate for the derivation and
        // the rule, so the resolution can never state a stop the document would refuse.
        const ChartNote& onset_note = notes[onset];
        if (stop > 0 && !travelsThroughFret(onset_note, stop))
        {
            planted[onset] = stop;
        }
    }
    return planted;
}

std::vector<std::optional<int>> chartDerivedStops(const ChartConnections& connections)
{
    // THE FIELD'S SCOPE, stated HERE and nowhere else. A claim is a statement the `held` FIELD
    // makes (\ref claimedStop), and only a right-hand onset carries one — its own fret belongs to
    // the other hand. Under a FRETTING-hand onset the same planted finger rides BESIDE the note's
    // own fret: it states nothing the charter could have typed, supersedes no field, leaves no
    // residue, and is read only by the seam machinery (\ref chartPlantedStops names both
    // readers). Every field-scoped reader takes this, so the wide table can never reach the
    // claim column.
    std::vector<std::optional<int>> derived = chartPlantedStops(connections);
    for (std::size_t index = 0; index < derived.size(); ++index)
    {
        if (!rightHandOnset(connections.saved_notes[index].attack))
        {
            derived[index].reset();
        }
    }
    return derived;
}

std::vector<std::optional<int>> chartClaimedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // The fold, and the direction is the ruling: the derivation SUPERSEDES the stored field rather
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
    const ChartShapes& shapes, const TempoMap& tempo_map)
{
    // WHICH span covers an instant, from the one authority every span-scoped rule asks
    // (\ref SpanCover) — the same coverage the held extension and the bracket clip are measured
    // against, so the default can never sit under a span those two say is not there.
    const SpanCover cover{shapes.shapes, tempo_map};
    std::vector<std::optional<int>> held(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        // The question only arises under a RIGHT-HAND onset: a fretting-hand onset IS the hand,
        // and a silently-held stop is its own fret, so neither has a second stop beneath it.
        if (!rightHandOnset(note.attack))
        {
            continue;
        }
        // Bound to a local so the presence test and the read are provably the same object. The
        // resolved claim already carries the first two tiers folded in their ruled order — the
        // pull-off derivation over the authored field — so a note that states one is done here.
        const std::optional<int>& claimed = claimed_stops[index];
        if (claimed.has_value())
        {
            held[index] = *claimed;
            continue;
        }
        // THE DEFAULT FACT (user ruling 2026-09-02): the hand is holding whatever grip it holds,
        // so a tap that states nothing releases onto the covering span's posture. Zero — the open
        // string, nothing held — where no span covers the tap, and equally where the covering
        // posture says nothing about THIS string: a posture is a per-string statement, and a
        // string it never names is a string no finger was on.
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
                const std::vector<std::optional<int>>& frets = shapes.postures[span.posture].frets;
                const auto string_index = static_cast<std::size_t>(note.string - 1);
                if (string_index < frets.size())
                {
                    // Bound to a local so the presence test and the read are one object.
                    const std::optional<int>& posture_fret = frets[string_index];
                    stop = posture_fret.value_or(0);
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
    resolutions.derived_stops = chartDerivedStops(resolutions.connections);
    resolutions.claimed_stops = chartClaimedStops(resolutions.connections);
    // The SPANS are independent of presentation entirely: they read the stored stream alone, since
    // every stop they compare comes off a stored fret channel. The wide planted table rides
    // beside the claims for the hold-under law's verdicts alone — it is derived here and handed
    // in, never published on the resolutions, because no surface but the span machine may read it
    // (\ref chartPlantedStops).
    const std::vector<std::optional<int>> planted_stops =
        chartPlantedStops(resolutions.connections);
    ChartShapes derived =
        deriveChartShapes(saved_notes, resolutions.claimed_stops, planted_stops, tempo_map);
    // THE COMPLETE HELD TABLE, and its place in the pipeline is the ruling (user, 2026-09-02): a
    // bare tap's DEFAULT held stop is the grip the covering span holds, so it reads the postures
    // the claims above just produced. It therefore runs AFTER the derivation and feeds nothing
    // that runs before it — a default folded into the claims would be an input to the very spans
    // it is read out of. Handed the whole derivation rather than its two vectors apart, because
    // `shapes` indexes `postures` and passing them separately is a mismatch waiting to happen.
    resolutions.held_stops =
        chartHeldStops(saved_notes, resolutions.claimed_stops, derived, tempo_map);
    // The CLASS every span arrives as, answered once for the revision because both surfaces draw
    // it. Asked of the stored stream, which presentation cannot move: the rule reads positions and
    // attacks and nothing else, and both come through presentation untouched. NO TAIL RULE READS
    // IT any more — the tail law is class-blind, which is what let the re-read that needed it go.
    resolutions.arrivals = chartShapeArrivals(saved_notes, derived.shapes, tempo_map);
    // What the surfaces draw, derived here so a chart revision pays for it once and no consumer can
    // derive a different picture of the same chart. ONE PASS OWNS EVERY TAIL DECISION: the spans go
    // IN, and the presentation rules and the tail law come out together, so there is no ordering
    // contract between two rules and no rewritten copy of the stream under the saved stream's name
    // (\ref presentedChartNotes). The connections go in whole because the law reads the same-string
    // relation this walk established — the handover a figure cannot state.
    ChartPresentation presentation =
        presentedChartNotes(resolutions.connections, derived, tempo_map);
    resolutions.shapes = std::move(derived.shapes);
    resolutions.postures = std::move(derived.postures);
    resolutions.claim_shapes = std::move(derived.claim_shapes);
    // The holds read the presented picture AND the law's verdict, which is what makes the two
    // complementary by construction: presentation drops a hidden member's ribbon, and the hold
    // hands that member back its own stored ring.
    resolutions.holds = chartHolds(presentation, saved_notes, resolutions.shapes, tempo_map);
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
    // A stream that claims no stop at all has nothing to sweep, and this runs on every plan the
    // editor gates. The scan is a bare read; the derivation below presents and walks the whole
    // stream before it could answer the same question.
    if (std::ranges::none_of(
            notes, [](const ChartNote& note) { return claimedStop(note).has_value(); }))
    {
        return {};
    }
    std::vector<ChartConversion> conversions;
    // ONE PASS (user ruling 2026-08-31, review #15). What this takes is a claim that reached NO
    // span, so it was a member of nothing and no span's membership moves when it goes — the
    // cascade the fixpoint that stood here iterated for cannot arise. The spans read the stored
    // stream alone, so this no longer pays for a presentation pass it only ever handed back the
    // frets it started with.
    const ChartConnections connections = chartConnections(notes, tempo_map);
    const ChartShapes derived = deriveChartShapes(
        notes, chartClaimedStops(connections), chartPlantedStops(connections), tempo_map);
    // Only the whole-note removals are collected: clearing a field leaves every index in place, so
    // it is done as the scan finds it and the erase list stays the one thing that must be applied
    // back to front.
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
        // A stop the note's own PITCH is measured from is never inert, whatever the shapes made of
        // it. A harmonic speaks from the STOPPED length (\ref physicalStopFret), so on a tapped
        // harmonic the held fret is not a claim about the hand that happens to ride a note — it is
        // where the note sounds from, and clearing it would retune the record and could leave its
        // node at or behind its own stop, which the validator refuses. The settle takes statements
        // that reach nothing; it never takes the sound the charter wrote.
        //
        // Asked of the SAVED form, like every other judgment here: a node the writer strips — a
        // scrape's latent one — describes no sound this record will ever have, so it cannot hold a
        // stop in place either.
        if (savedChartNote(note).harmonic_node.has_value())
        {
            continue;
        }
        // The note still states its own onset, so only the statement that reached nothing goes:
        // the sound the charter wrote stays exactly as authored.
        note.held.reset();
        conversions.push_back(
            ChartConversion{.repair = ChartRepair::InertHeldStop, .where = where});
    }
    // Erased from the back, so every index still names the note it was derived against.
    for (const std::size_t index : std::views::reverse(removed))
    {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
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
