#include "span_cover.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Rules 3 and 4 END a tail rather than shortening it, and a presented note must still keep the
// model's shape — payload offsets lie within the sustain — because the painters read it as an
// ordinary note. Nothing actually survives the clip under either rule: any payload at all earns
// the group its tail, and a dead note carrying a slide keeps its own. The clip is here so that
// invariant holds by construction rather than by that argument, which spans two rules and would
// quietly stop being true if either moved.
void dropPresentedTail(ChartNote& note)
{
    clipPayloadsToSustain(note, Fraction{});
}

// Rule 4 (E25): a dead note rings nothing, so a plain tail on one is silence pretending to be
// sound. The two things that keep a dead string making noise or travelling are what keep its tail:
// repeated raking (a chug), or a dragged mute. A scrape always carries a slide-out, so a scrape
// with a dead flag keeps its gesture.
//
// Applied to the PRESENTED note only. The stored ring is untouched by design (plan ruling 5): it
// is the timing information the legato adjacency test reads, and pinning a dead note at zero
// breaks every claim after a muted cluck.
[[nodiscard]] bool presentsNoDeadTail(const ChartNote& note)
{
    return note.dead && !note.tremolo && !anyKeyframeStatesFret(note.keyframes) &&
           note.sustain.numerator > 0;
}

// Rule 1's own comparison: a ring PASSES a head when it runs strictly past it. Any overhang at
// all means the head does not bind, which is what makes a ring ending exactly on an onset the
// ordinary let-ring collision.
[[nodiscard]] bool ringPassesHead(const Fraction ring, const Fraction gap)
{
    return gap < ring;
}

// The furthest offset the tail still has a statement to show: its last keyframe, or zero for a
// plain ring. A stored note's last keyframe always says something — the keyframe commit law
// (keyframeSaysNothingNew) sheds one that does not — so no change detection is asked here; the
// commit law is the one authority on silence. A fret and a bend value are POINTS, complete at the
// instant they are reached, so the tail may stop exactly there; a release is the ring's end
// itself. A statement that leaves the string SHAKING is an interval STATE: a tail ending on it
// would show the shake for no time at all and read as no shake, so it reaches one minimum gesture
// window past the statement (a tail is never lengthened past its ring by it — the ring is the
// ceiling of every rule here). A statement that ends the shake is a point again.
[[nodiscard]] Fraction lastStatementEnd(const ChartNote& note)
{
    if (note.keyframes.empty())
    {
        return Fraction{};
    }
    const Keyframe& last = note.keyframes.back();
    return isShaking(last.vibrato.value_or(VibratoState::Off))
               ? last.offset + g_minimum_slide_window
               : last.offset;
}

// How long a note's ACTUAL ring lasts, in seconds, read through the tempo map from the onset to the
// ring's end so a ring crossing a tempo anchor is measured exactly. Rule 3's input: the
// kept-sustain bound is a duration, so this is the one number it compares.
[[nodiscard]] double ringSeconds(const ChartNote& note, const TempoMap& tempo_map)
{
    const double onset =
        tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, note.position));
    const double end = tempo_map.secondsAtGlobalBeatPosition(
        globalBeatPosition(tempo_map, sustainEndPosition(tempo_map, note)));
    return end - onset;
}

// Rules 1 and 2 for one note whose ring reaches into the margin before the next binding onset: the
// tail trims to the margin, and never past the note's last statement.
//
// Preconditions the caller owns: `gap` is the distance to the BINDING onset — the first sounding
// onset the ring does not run strictly past — and the sustain is strictly positive. The tail law
// needs no second entry here and never will: it MARKS where a tail this trim already sized rests
// and never resizes one, so no span-scoped rule ever hands a length to these rules.
void trimToMargin(ChartNote& note, const Fraction gap, const TempoMap& tempo_map)
{
    const Fraction margin =
        minimumSustainDistanceBeats(tempo_map.timeSignatureAt(note.position.measure).denominator);
    const Fraction limit = gap - margin;
    // Rule 2: the tail always reaches the last keyframe. A released ring therefore never trims —
    // the release is its last keyframe, at the ring's end, and the stored ring already keeps it
    // clear of the next head on its string (keyframeClearanceOf); a head on another string may
    // sit inside it.
    const Fraction target =
        std::max(limit.numerator < 0 ? Fraction{} : limit, lastStatementEnd(note));
    if (target < note.sustain)
    {
        clipPayloadsToSustain(note, target);
    }
}

// THE TAIL LAW's landmark, for one member's stored ring: the curtain owns everything past a
// note's last always-visible landmark. The verdict is the OFFSET that landmark sits at — the
// cases are stated once, at \ref ChartPresentation::rested_from — or nothing for a tail that
// never rests. This is the WHOLE verdict: the curtain being universal, there is no coverage half
// to take the later of, so what this returns is what the law marks.
//
// What never rests is a ring still STATING at its own end — a bend held to the end, a shake
// that never stops, tremolo, a slide-out's travel: the curtain owns only what the ribbon has
// stopped saying anything with, with no
// vocabulary for a statement in progress. A statement that FINISHES is the split the user asked
// for: the stated portion stays always visible, and the plain remainder joins the curtain where
// the statement ended (lastStatementEnd — the same landmark rule 2 floors the presented tail at,
// so the offset always lies at or inside the drawn ribbon's end).
//
// A ring whose string a later strike takes over (\ref ChartConnections::hands_over) is a
// TRANSFER of the sound — a statement with no vocabulary of its own either, but one that
// FINISHES: it completes at the takeover, where the successor picks the sound up. So it is the
// finished-statement split with an EMPTY remainder — the whole drawn ribbon is the stated
// portion, and the landmark is the ribbon's own end.
// THE HANDOVER IS ASKED FIRST, deliberately: the takeover terminates whatever the ring was still
// stating — a shake or a bend into a pull-off ends where the successor takes the string — so a
// handed-over ring is a finished statement whether or not its channels were quiet at its end,
// and its landmark is the ribbon's end either way. Read as a statement still in progress it would
// refuse to rest at all and draw its whole ribbon in front of the curtain that owns it. The
// branch is what keeps a plain handed-over ring resting from its own end.
[[nodiscard]] std::optional<Fraction> restedOffsetOf(
    const ChartConnections& connections, const std::size_t index, const ChartNote& presented)
{
    const ChartNote& stored = connections.saved_notes[index];
    if (connections.hands_over[index])
    {
        return presented.sustain;
    }
    // Still stating at the ring's end: tremolo and a release run to the end by construction,
    // and the state in force at the ring's own end says whether the bend and vibrato channels
    // ever go quiet.
    if (stored.tremolo || slideOutFretOrNull(stored) != nullptr)
    {
        return std::nullopt;
    }
    const RingState state = ringStateAt(stored, stored.sustain);
    if (std::is_neq(state.bend <=> 0.0) || isShaking(state.vibrato))
    {
        return std::nullopt;
    }
    return lastStatementEnd(stored);
}

// Rule 3 is its one asker — the tail law keys on the finished-statement split instead — so this is
// a file-local classifier. Any keyframe at all, whichever channel it states: a mid-ring curl and a
// delayed shake ride the tail exactly as a glide does, and dropping the tail would drop the
// statement with it. Whole-note techniques (muting, emphasis, harmonics) are deliberately absent:
// they say the same thing with or without a tail.
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note)
{
    return std::is_neq(note.bend <=> 0.0) || !note.keyframes.empty() || isShaking(note.vibrato) ||
           note.tremolo;
}

} // namespace

// One walk over the onset groups carries rules 1 through 3, because they share a partition — every
// note at one grid position — and rule 3's verdict needs its members already trimmed. Rule 4 runs
// last over the whole stream, as the plan's ordering states, so a tail that rules 1 to 3 left
// standing is still judged as a dead note's. THE TAIL LAW runs after all four, per NOTE rather than
// per group since the atom became the member, and only ever MARKS what they left standing.
ChartPresentation presentedChartNotes(
    const ChartConnections& connections, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& saved_notes = connections.saved_notes;
    ChartPresentation presentation;
    presentation.notes = saved_notes;
    presentation.rested_from.assign(saved_notes.size(), std::nullopt);
    std::vector<ChartNote>& presented = presentation.notes;
    std::size_t group_begin = 0;
    while (group_begin < presented.size())
    {
        // The stream is sorted by (position, string), so notes sharing an onset are contiguous.
        std::size_t group_end = group_begin + 1;
        while (group_end < presented.size() &&
               presented[group_end].position == presented[group_begin].position)
        {
            ++group_end;
        }
        bool group_earned = false;
        for (std::size_t index = group_begin; index < group_end; ++index)
        {
            ChartNote& note = presented[index];
            // Rule 1: the onset that binds the trim is the first onset the ring does not PASS,
            // where passing means running strictly past it. A ring ending exactly ON an onset binds
            // there and trims — the common let-ring collision, because a notated ring ends on a
            // musical boundary and the next note starts from one, so a ring left whole there would
            // die on a later head with no gap at all. A ring no onset binds presents whole, which
            // is what a last note has always done.
            //
            // The scan is the note's own and starts where the group ends, never a cursor shared
            // across the walk — one member's ring must not move where the next member starts
            // looking. It stops at the first onset that binds, so an ordinary tail reads a single
            // onset and only a ring reaching past a head walks any further.
            bool deliberate_hold = false;
            if (note.sustain.numerator > 0)
            {
                for (std::size_t ahead = group_end; ahead < presented.size(); ++ahead)
                {
                    const Fraction gap =
                        beatDistance(tempo_map, note.position, presented[ahead].position);
                    if (!ringPassesHead(note.sustain, gap))
                    {
                        trimToMargin(note, gap, tempo_map);
                        break;
                    }
                    // Passing an onset is a statement — a tie merged across a neighbour, a
                    // cross-voice hold — so it earns the group its tails under rule 3 below, but
                    // it does not switch the trim off. Deliberately the STRICT reading, so a
                    // near-miss still trims while a tail rule 3 earns still earns.
                    deliberate_hold = true;
                }
            }
            // Rule 3's per-member earning, asked of the note as rules 1 and 2 leave it (its
            // payload is untouched, since a tail always reaches the last keyframe) but of the
            // note's ACTUAL ring, which
            // is the length the source or the charter stated and the only one that can say whether
            // a deliberate sustain was meant. That read is exact because the tail law runs LAST
            // and only marks: `saved_notes` is the stored stream itself, never a rewritten copy
            // under its own name, so nothing reaches here but the chart's own rings.
            //
            // The ring is measured in SECONDS: the bound is a duration, stated once at
            // g_minimum_kept_sustain_seconds, so the same written value earns at a slow tempo and
            // not at a fast one.
            group_earned =
                group_earned || deliberate_hold || hasSustainTechnique(note) ||
                ringSeconds(saved_notes[index], tempo_map) > g_minimum_kept_sustain_seconds;
        }

        // Rule 3's verdict is the GROUP's: every string of a chord rings from one stroke, so a tail
        // any member earned keeps every member's, and a group that earned none presents none.
        if (!group_earned)
        {
            for (std::size_t index = group_begin; index < group_end; ++index)
            {
                if (presented[index].sustain.numerator > 0)
                {
                    dropPresentedTail(presented[index]);
                }
            }
        }
        group_begin = group_end;
    }

    // Rule 4 (E25), over the whole stream last, so a tail rules 1 to 3 left standing is still
    // judged as a dead note's.
    for (ChartNote& note : presented)
    {
        if (presentsNoDeadTail(note))
        {
            dropPresentedTail(note);
        }
    }

    // THE TAIL LAW (\ref presentedChartNotes rule 5, the one authority): a tail that shows no
    // technique information RESTS, and the curtain owns it from its last always-visible landmark.
    // VERDICT-ONLY, AND LAST: it reads the STORED rings, judges, and MARKS where each tail rests,
    // inventing and erasing no length. Running last is what makes three things true by
    // construction rather than by argument: rules 1 to 3 see the chart's real rings, so a resting
    // member cannot reach through rule 3's group earning and delete a partner's ribbon; a tail
    // rule 3 or rule 4 already emptied never RESTS, so the hold channel never claims a length
    // those rules judged away; and the verdict still exists when the holds are answered.
    //
    // THE CURTAIN IS UNIVERSAL. There is no coverage question — neither whether a span stands at
    // the onset, nor where the ribbon first runs under one — so a chart with no furniture at all
    // rests exactly the same tails as one full of it: the span is not what makes a plain ribbon
    // uninformative. The per-member landmark is the whole verdict.
    for (std::size_t index = 0; index < presented.size(); ++index)
    {
        const ChartNote& note = presented[index];
        // SCOPE, and it is scope rather than an exception list: the law is judged of fretting-hand
        // members alone. A right-hand onset is the other hand's — it joins no posture and extends
        // no ring (\ref deriveChartShapes) — so it is no member of what a grip states, and the hold
        // rule below takes exactly the same scope for exactly this reason.
        if (rightHandOnset(note.attack) || note.sustain.numerator <= 0)
        {
            continue;
        }
        // THE ATOM IS THE MEMBER. Each member is judged on its own: a member still stating at its
        // end draws, and every other tail rests from wherever its last statement ends — zero for
        // a plain ring.
        //
        // VERDICT ONLY: the tail is judged and marked, never emptied — the presented stream carries
        // every member's rules-1-to-4 tail, and the hold extension keys on the verdict rather than
        // on tail emptiness.
        presentation.rested_from[index] = restedOffsetOf(connections, index, note);
    }
    return presentation;
}

bool hasRestingRemainder(const std::optional<Fraction>& rested_from, const ChartNote& presented)
{
    return rested_from.has_value() && *rested_from < presented.sustain;
}

// The span convention IS the hold, and there is one rule: a LIVE fretting-hand member with no DRAWN
// tail, covered by a span, is held to the span's reach — while the grip is held, the board pins
// what is held. The hidden and the rule-3-emptied member take the same extension because they are
// the same physical fact: under grip tenure a covered member's un-renewed death would have BROKEN
// the span, so coverage past a member's ring IS the record that the finger never lifted (the
// restrike replaced the sound, not the hand). Two arms — a strum extension for emptied tails and
// the stored ring for hidden ones — would agree only while a hidden ring provably died at the
// close, and under the covered tail form they part: a repeated chord's pin would release at every
// restrike while a faster chug's held.
//
// Dead members (a dead chug is choked, not held), the other hand's onsets, and members whose
// tails stand AT REST state their own hold. "At rest" is the VERDICT's question, not tail
// emptiness, because a hidden member carries its presented tail as the board's near-line reveal:
// its hold is still the tenure, and keying on the tail would release those pins. Coverage is
// positional only, with no posture matching.
//
// UNDER THE UNIVERSAL CURTAIN resting says nothing about a span, so the tenure floor is keyed on
// COVERAGE and not on the verdict: a covered resting member raises to its stored ring and then to
// the span's reach, while a lone resting note — every plain note on open board is one — holds for
// the tail it presents.
std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const ChartConnections& connections,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& saved_notes = connections.saved_notes;
    const std::vector<ChartNote>& presented_notes = presentation.notes;

    std::vector<Fraction> held;
    held.reserve(presented_notes.size());
    for (std::size_t index = 0; index < presented_notes.size(); ++index)
    {
        // The FLOOR, keyed on the HANDOVER alone. A handed-over member pins for exactly its stored
        // ring, because a pinned head reflects the current SOUNDING state: the next strike on its
        // string takes the sound, and the same-string clamp (\ref sustainBoundOf) plus the
        // adjacency the claim itself required (\ref predecessorHoldReaches) make the stored ring
        // end exactly on that takeover — the ring IS the takeover instant, stated once. Everyone
        // else starts from the tail they present.
        //
        // THE RESTING member deliberately does NOT floor here on its stored ring: under the
        // universal curtain every plain note rests, so such a floor would run a LONE note's head
        // pin out to its untrimmed stored ring and into the next note's margin. The floor a
        // resting member needs is SPAN COVERAGE, so it lives in the covered-group loop below,
        // where a covered member raises to its stored ring before the span's reach and a lone one
        // is never reached — a lone resting note holds for its presented tail.
        held.push_back(
            connections.hands_over[index] ? saved_notes[index].sustain
                                          : presented_notes[index].sustain);
    }
    // How far the covering furniture reaches, from the one authority both span-scoped display
    // rules ask (\ref SpanCover).
    const SpanCover cover{shapes, tempo_map};
    for (std::size_t index = 0; index < presented_notes.size();)
    {
        const GridPosition onset = presented_notes[index].position;
        std::size_t group_end = index;
        while (group_end < presented_notes.size() && presented_notes[group_end].position == onset)
        {
            ++group_end;
        }
        // Bound to a local so the presence test and the read are provably the same object. There is
        // no strum-size gate on the extension: the lone covered chug between two strikes is a grip
        // member exactly as a strummed one is, and in a DERIVED chart a lone tail-less note a span
        // covers past was necessarily renewed — an un-renewed death breaks the grip, so the span
        // could not reach past it at all.
        const std::optional<SpanCoverage> covering = cover.reaching(onset);
        if (covering.has_value())
        {
            const Fraction span_hold = beatDistance(tempo_map, onset, covering->end);
            for (std::size_t member = index; member < group_end; ++member)
            {
                const ChartNote& note = presented_notes[member];
                // A DEAD member is choked, never held. Its fate is decided by the mute at either
                // end of rule 4: a plain dead tail is emptied there, and one rule 4 spares (a raked
                // or dragged mute) is still standing, so the empty-tail gate below would take the
                // first and pass over the second — a percussive choke pinned as if the finger
                // stayed down.
                //
                // A RIGHT-HAND onset is skipped: its head is no part of what the grip states, so
                // the span's reach is not its to inherit.
                //
                // A member whose tail stands and never rests states its own hold — its ribbon
                // already says where its ring ends. A RESTING member is keyed by the VERDICT, not
                // by tail emptiness: it carries its presented tail, but that ribbon is the board's
                // near-line reveal, and the pin states the grip for the whole tenure regardless. A
                // HANDED-OVER member is excluded whole: its sound ends at its own stored ring,
                // where the next strike on its string takes over (floored above), so the grip's
                // tenure is not its to inherit — that strike owns the display from there. Its tail
                // verdict says the same thing from the other side (it rests from its ribbon's end),
                // so the exclusion reads the handover itself rather than a verdict that would pass
                // it through.
                const bool rests = presentation.rested_from[member].has_value();
                if (rightHandOnset(note.attack) || note.dead || connections.hands_over[member] ||
                    (note.sustain.numerator > 0 && !rests))
                {
                    continue;
                }
                // THE RESTING member's own floor, and it lives HERE because span coverage is what
                // earns it: a covered resting member holds at least its own stored ring, which
                // MAY exceed the span's reach — the honest hold, because the string genuinely
                // rings there. A rule-3 or rule-4 emptied member carries no verdict and takes the
                // reach alone.
                if (rests && held[member] < saved_notes[member].sustain)
                {
                    held[member] = saved_notes[member].sustain;
                }
                if (held[member] < span_hold)
                {
                    held[member] = span_hold;
                }
            }
        }
        index = group_end;
    }
    return held;
}

} // namespace rock_hero::common::core
