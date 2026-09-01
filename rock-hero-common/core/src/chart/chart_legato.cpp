#include "chart/chart_legato.h"

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

std::vector<std::optional<int>> chartDerivedStops(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    // THE DERIVATION, read off the connections this walk already resolved: a PULL-OFF states the
    // stop its predecessor's other hand was holding, because a finger has to be waiting on a fret
    // to be pulled off onto.
    std::vector<std::optional<int>> derived(notes.size());
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
        // Only a right-hand onset holds a stop apart from the one it sounds; and an open string
        // asserts no finger, so a pull onto one derives nothing.
        //
        // AND THE TRAVELED RANGE REFUSES IT, through the very predicate that refuses an AUTHORED
        // one (\ref travelsThroughFret, user ruling 2026-08-27): the planted finger is on the
        // string for the whole of the onset's path, so a stop the picking hand starts on, ends on
        // or sweeps through is not a stop any finger could have been waiting on. A tap keyframed
        // up past the fret its pull-off lands on is the figure, and there the connection states
        // nothing about a second finger. One predicate for the derivation and the rule, so the
        // resolution can never state a stop the document would refuse.
        const ChartNote& onset_note = notes[onset];
        if (stop > 0 && rightHandOnset(onset_note.attack) && !travelsThroughFret(onset_note, stop))
        {
            derived[onset] = stop;
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

ChartResolutions chartResolutions(const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    ChartResolutions resolutions;
    resolutions.connections = chartConnections(notes, tempo_map);
    const std::vector<ChartNote>& saved_notes = resolutions.connections.saved_notes;
    // What the surfaces draw, which postures the hand holds, and how long it stays down: all
    // derived here so a chart revision pays for them once, and so no consumer can derive a
    // different picture of the same chart. The order is the dependency order — the class and the
    // holds are answered against the spans, and the absorption reads the class. The SPANS are
    // independent of presentation entirely: they read the stored stream alone, since every stop
    // they compare comes off a stored fret channel.
    resolutions.presented_notes = presentedChartNotes(saved_notes, tempo_map);
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
    ChartShapes derived = deriveChartShapes(saved_notes, resolutions.claimed_stops, tempo_map);
    resolutions.shapes = std::move(derived.shapes);
    resolutions.postures = std::move(derived.postures);
    resolutions.claim_shapes = std::move(derived.claim_shapes);
    // The CLASS every span arrives as, answered once for the revision: both surfaces draw it, and
    // the absorption rule below keys on it — a bracket stands where its members' ribbons would be
    // and owns them, a box is drawn at an instant and owns nothing.
    resolutions.arrivals =
        chartShapeArrivals(resolutions.presented_notes, resolutions.shapes, tempo_map);
    resolutions.holds = chartHolds(resolutions.presented_notes, resolutions.shapes, tempo_map);
    resolutions.suppressed_tails = chartSuppressedTails(
        resolutions.presented_notes, resolutions.shapes, resolutions.arrivals, tempo_map);
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
    const ChartShapes derived =
        deriveChartShapes(notes, chartClaimedStops(chartConnections(notes, tempo_map)), tempo_map);
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
