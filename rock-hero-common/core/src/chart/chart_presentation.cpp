#include "span_cover.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Tail rule 1 for one note whose ring the onset `gap` whole notes ahead binds: the ink stops one
// margin before that onset and never before the note's own onset. The margin belongs to the
// BINDING ONSET, not to the ringing note: it is that head's spacing being kept, and the margin is a
// duration, so a tempo change between the two would otherwise measure it at the wrong rate. A ring
// that ends at or before the crop is free and keeps its whole length.
[[nodiscard]] Fraction croppedInkEnd(
    const ChartNote& note, const Fraction gap, const Fraction binding_onset,
    const HeadMargins& margins)
{
    return std::min(note.sustain, std::max(gap - margins.length(binding_onset), Fraction{}));
}

// Tail rule 1 for one note: the binding onset is the first later onset the ring does not run
// STRICTLY past, so a ring ending exactly on an onset binds there — the ordinary let-ring
// collision. The scan starts where the note's own onset group ends and stops at the first onset
// that binds, so an ordinary tail reads a single onset and only a ring reaching past a head walks
// further. Passing an onset at all — a tie merged across a neighbour, a cross-voice hold — is
// reported through `deliberate_hold`, which tail rule 2 reads as a statement without switching the
// crop off.
[[nodiscard]] Fraction inkEndOf(
    const ChartConnections& connections, const std::size_t index, const std::size_t group_end,
    const HeadMargins& margins, bool& deliberate_hold)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    const std::vector<Fraction>& places = connections.places;
    const ChartNote& note = notes[index];
    deliberate_hold = false;
    for (std::size_t ahead = group_end; ahead < notes.size(); ++ahead)
    {
        const Fraction gap = places[ahead] - places[index];
        if (!(gap < note.sustain))
        {
            return croppedInkEnd(note, gap, places[ahead], margins);
        }
        deliberate_hold = true;
    }
    return note.sustain;
}

// Tail rule 2's per-member earning: any keyframe at all, whichever channel it states — a mid-ring
// curl and delayed vibrato ride the tail exactly as a glide does — or a ring longer than the
// kept-sustain bound. The ring is measured in SECONDS through the tempo map, because the bound is
// a duration: the same written value earns at a slow tempo and not at a fast one. Whole-note
// techniques (muting, emphasis, harmonics) are deliberately absent: they say the same thing with or
// without a tail.
[[nodiscard]] bool earnsTail(const ChartNote& note, const Fraction place, const TempoMap& tempo_map)
{
    const double onset = tempo_map.secondsAtWholeNote(place);
    const double end = tempo_map.secondsAtWholeNote(place + note.sustain);
    return std::is_neq(note.bend <=> 0.0) || !note.keyframes.empty() || hasVibrato(note.vibrato) ||
           note.tremolo || end - onset > g_minimum_kept_sustain_seconds;
}

// Tail rule 3 (technique rule 25): a dead note rings nothing, so a plain tail on one is silence
// pretending to be sound. Repeated raking (a chug) or a dragged mute keeps a dead string making
// noise or travelling, and keeps its tail; a scrape always carries a slide-out, so a dead scrape
// keeps its gesture.
[[nodiscard]] bool drawsNoDeadTail(const ChartNote& note)
{
    return note.dead && !note.tremolo && !anyKeyframeStatesFret(note.keyframes);
}

// THE TAIL LAW's landmark, for one member: the curtain owns everything past a note's last
// always-visible landmark. The verdict is the OFFSET that landmark sits at — the cases are stated
// once, at ChartPresentation::rested_from — or nothing for a tail that never rests.
//
// What never rests is a ring still STATING at its own end — a bend held to the end, vibrato that
// never stops, tremolo, a slide-out's travel: the curtain owns only what the ribbon has stopped
// saying anything with. A statement that FINISHES is the split: the stated portion stays always
// visible, and the plain remainder joins the curtain where the statement ended — held to the ink
// end, since a statement standing in the ending zone is never drawn and the curtain starts where
// the ink is. A ring whose string a later strike takes over, or whose end arrives into the next
// head, is a transfer that FINISHES at the takeover: the whole drawn ribbon is the stated portion
// and the landmark is the ink end. Both are asked first, because either event terminates whatever
// the ring was still stating.
[[nodiscard]] std::optional<Fraction> restedOffsetOf(
    const ChartConnections& connections, const std::size_t index, const Fraction ink_end)
{
    const ChartNote& note = connections.saved_notes[index];
    if (connections.hands_over[index] || connections.arrives_into[index])
    {
        return ink_end;
    }
    // Still stating at the ring's end: tremolo and a SLIDE-OUT run to the end by construction, and
    // with the arrival taken above an end fret statement here IS the slide-out, while the state in
    // force at the ring's own end says whether the bend and vibrato channels ever go quiet.
    if (note.tremolo || endStatedFretOrNull(note) != nullptr)
    {
        return std::nullopt;
    }
    const RingState state = ringStateAt(note, note.sustain);
    if (std::is_neq(state.bend <=> 0.0) || hasVibrato(state.vibrato))
    {
        return std::nullopt;
    }
    const Fraction last_statement =
        note.keyframes.empty() ? Fraction{} : note.keyframes.back().offset;
    return std::min(last_statement, ink_end);
}

// The next strike on a note's string after its own onset — whichever hand strikes and whatever it
// sounds — as a place on the whole-note axis; nothing where the string is never struck again. Read
// off the revision's one per-string index (\ref ChartConnections::string_notes).
[[nodiscard]] std::optional<Fraction> nextStrikeOnString(
    const ChartConnections& connections, const std::size_t index)
{
    const std::optional<std::size_t> string_index =
        chartStringIndex(connections.saved_notes[index]);
    if (!string_index.has_value())
    {
        return std::nullopt;
    }
    const std::vector<std::size_t>& string_notes = connections.string_notes[*string_index];
    const auto place_of = [&connections](const std::size_t note) {
        return connections.places[note];
    };
    const auto next = std::ranges::upper_bound(
        string_notes, connections.places[index], std::ranges::less{}, place_of);
    if (next == string_notes.end())
    {
        return std::nullopt;
    }
    return place_of(*next);
}

} // namespace

// One walk over the onset groups — every note at one grid position, contiguous in the sorted
// stream — because tail rule 2's verdict is the GROUP's: every string of a chord rings from one
// stroke, so a tail any member earned keeps every member's and a group that earned none draws none.
// Tail rules 1 and 3 are per member and need no order; THE TAIL LAW (tail rule 4) reads each
// member's final ink end and only ever MARKS what the first three left standing. THE CURTAIN IS
// UNIVERSAL: a chart with no furniture at all rests exactly the same tails as one full of it.
ChartPresentation chartPresentation(
    const ChartConnections& connections, const HeadMargins& margins, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    const std::vector<Fraction>& places = connections.places;
    ChartPresentation presentation;
    presentation.ink_end.reserve(notes.size());
    presentation.rested_from.reserve(notes.size());
    std::size_t group_begin = 0;
    while (group_begin < notes.size())
    {
        std::size_t group_end = group_begin + 1;
        while (group_end < notes.size() && places[group_end] == places[group_begin])
        {
            ++group_end;
        }
        bool group_earned = false;
        for (std::size_t index = group_begin; index < group_end; ++index)
        {
            bool deliberate_hold = false;
            const Fraction ink_end =
                inkEndOf(connections, index, group_end, margins, deliberate_hold);
            presentation.ink_end.push_back(drawsNoDeadTail(notes[index]) ? Fraction{} : ink_end);
            group_earned = group_earned || deliberate_hold ||
                           earnsTail(notes[index], places[index], tempo_map);
        }
        for (std::size_t index = group_begin; index < group_end; ++index)
        {
            if (!group_earned)
            {
                presentation.ink_end[index] = Fraction{};
            }
            // The tail law's SCOPE: judged of fretting-hand members alone — a right-hand onset
            // joins no posture and extends no ring (\ref deriveChartShapes), so it is no member of
            // what a grip states — and of tails that still draw, so a tail that tail rules 2 and 3
            // emptied never rests and the note-length channel never claims a length those rules
            // judged away.
            const Fraction ink_end = presentation.ink_end[index];
            presentation.rested_from.push_back(
                rightHandOnset(notes[index].attack) || ink_end.numerator <= 0
                    ? std::nullopt
                    : restedOffsetOf(connections, index, ink_end));
        }
        group_begin = group_end;
    }
    return presentation;
}

bool hasRestingRemainder(const std::optional<Fraction>& rested_from, const Fraction& ink_end)
{
    return rested_from.has_value() && *rested_from < ink_end;
}

// The span convention IS the hold, and there is one rule: a LIVE fretting-hand member whose tail
// draws nothing or RESTS, covered by a span, is held to the span's reach, cut where its string is
// struck again. The reach is the HAND's: under grip tenure a covered member's un-renewed death
// would have BROKEN the span, so coverage past a member's ring IS the record that the finger never
// lifted (the restrike replaced the sound, not the hand). The cut is the SOUND's: a pinned head
// states what the string sounds, and the next strike on the string takes that whatever it sounds —
// a repeat lands in the head's place and takes it over, a different stop lands beside it as it
// dissolves. The finger may well stay planted under that strike (a pull-off's source does); the
// span's rails state the hold, the head states the sound.
//
// Dead members (a dead chug is choked, not held), the other hand's onsets, and members whose tails
// are still stating at their end state their own hold — their ribbon already says where the ring
// ends. "At rest" is the VERDICT's question, not tail emptiness: a resting member still draws its
// tail, and keying on the tail would release its pin.
//
// UNDER THE UNIVERSAL CURTAIN resting says nothing about a span, so the tenure floor is keyed on
// COVERAGE and not on the verdict: a covered resting member raises to its stored ring and then to
// the span's reach, while a lone resting note — every plain note on open board is one — holds for
// the tail it draws.
std::vector<Fraction> chartNoteLengths(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    const std::vector<Fraction>& places = connections.places;

    // Everyone starts from the tail they draw. THE RESTING member deliberately does NOT floor here
    // on its stored ring: under the universal curtain every plain note rests, so such a floor would
    // run a LONE note's head pin out to its stored ring and into the next note's margin. The floor
    // a resting member needs is SPAN COVERAGE, so it lives in the covered-group loop below.
    std::vector<Fraction> held = presentation.ink_end;
    // How far the covering furniture reaches, from the one authority both span-scoped display
    // rules ask (\ref SpanCover).
    const SpanCover cover{shapes};
    for (std::size_t index = 0; index < notes.size();)
    {
        std::size_t group_end = index;
        while (group_end < notes.size() && places[group_end] == places[index])
        {
            ++group_end;
        }
        // Bound to a local so the presence test and the read are provably the same object. There is
        // no strum-size gate on the extension: the lone covered chug between two strikes is a grip
        // member exactly as a strummed one is, and in a DERIVED chart a lone tail-less note a span
        // covers past was necessarily renewed — an un-renewed death breaks the grip, so the span
        // could not reach past it at all.
        const std::optional<SpanCoverage> covering = cover.reaching(places[index]);
        if (covering.has_value())
        {
            for (std::size_t member = index; member < group_end; ++member)
            {
                const ChartNote& note = notes[member];
                const bool rests = presentation.rested_from[member].has_value();
                if (rightHandOnset(note.attack) || note.dead ||
                    (presentation.ink_end[member].numerator > 0 && !rests))
                {
                    continue;
                }
                // THE RESTING member's own floor, and it lives HERE because span coverage is what
                // earns it: a covered resting member holds at least its own stored ring, which
                // MAY exceed the span's reach — the honest hold, because the string genuinely
                // rings there. A member tail rule 2 emptied carries no verdict and takes the reach
                // alone; a dead one was skipped above.
                if (rests && held[member] < note.sustain)
                {
                    held[member] = note.sustain;
                }
                // The reach: the span's close, or the string's next strike where that comes first.
                // A handed-over member needs no case of its own: the adjacency its claim required
                // puts that strike exactly at its stored ring.
                const Fraction reach =
                    std::min(
                        covering->end,
                        nextStrikeOnString(connections, member).value_or(covering->end)) -
                    places[member];
                if (held[member] < reach)
                {
                    held[member] = reach;
                }
            }
        }
        index = group_end;
    }
    return held;
}

} // namespace rock_hero::common::core
