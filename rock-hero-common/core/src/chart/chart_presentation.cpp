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

// MEMBERSHIP on the fretting-hand axis, spelled once because both span-scoped rules in this file
// take the same scope and two spellings of one scope are two scopes free to drift. A silent hold
// produces no onset at all, and a right-hand onset is the other hand's — it joins no posture and
// extends no ring (\ref deriveChartShapes) — so neither is a member of what a grip states: not of
// the figure the tail law judges, and not of the strum the span convention holds.
[[nodiscard]] bool frettingHandMember(const ChartNote& note)
{
    return !silentHold(note.attack) && !rightHandOnset(note.attack);
}

// Rules 3 and 4 END a tail rather than shortening it, and a presented note must still keep the
// model's shape — payload offsets lie within the sustain — because the painters read it as an
// ordinary note. Nothing actually survives the clip under either rule: any payload at all earns
// the group its tail, and a dead note carrying a slide keeps its own. The clip is here so that
// invariant holds by construction rather than by that argument, which spans two rules and would
// quietly stop being true if either moved.
void dropPresentedTail(ChartNote& note)
{
    note.sustain = Fraction{};
    clipPayloadsTo(note, note.sustain);
}

// Rule 4 (E25): a dead note rings nothing, so a plain tail on one is silence pretending to be
// sound. The two things that keep a dead string making noise or travelling are what keep its tail:
// repeated raking (a chug), or a dragged mute. A scrape always carries a slide-out, so a scrape
// with a dead flag keeps its gesture.
//
// Applied to the PRESENTED note only. The stored ring is untouched by design (plan ruling 5): it
// is the timing information the legato adjacency test reads, and pinning a dead note at zero
// re-broke every claim after a muted cluck once already.
[[nodiscard]] bool presentsNoDeadTail(const ChartNote& note)
{
    return note.dead && !note.tremolo && !anyKeyframeStatesFret(note.keyframes) &&
           !note.slide_out.has_value() && note.sustain.numerator > 0;
}

// Rule 1's own comparison: a ring PASSES a head when it runs strictly past it. Any overhang at
// all means the head does not bind, which is what makes a ring ending exactly on an onset the
// ordinary let-ring collision.
[[nodiscard]] bool ringPassesHead(const Fraction ring, const Fraction gap)
{
    return gap < ring;
}

// The offset of the last keyframe that states a POSITION, or zero when none does — where the
// note's path stops saying anything new about where the hand is. The two rules that need it are
// the ones a position statement bounds: a scrape's leg begins there, and a ring ending in a
// slide-out must end strictly after it.
[[nodiscard]] Fraction lastStatedFretOffset(const ChartNote& note)
{
    Fraction last{};
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.fret.has_value())
        {
            last = keyframe.offset;
        }
    }
    return last;
}

// Rules 1 and 2 for one note whose ring reaches into the margin before the next binding onset: the
// tail trims to the margin, floored at the payload that still has information to present, and the
// gesture geometry rides the new end.
//
// Preconditions the caller owns: `gap` is the distance to the BINDING onset — the first sounding
// onset the ring does not run strictly past — and the sustain is strictly positive. The ring
// therefore ends at or before that onset, which is what lets the scrape leg rule below assume its
// leg starts inside the gap. The tail law needs no second entry here and never will: it can only
// DROP a tail this trim already sized, so no span-scoped rule ever hands a length to these rules.
void trimToMargin(ChartNote& note, const Fraction gap, const TempoMap& tempo_map)
{
    const Fraction margin =
        minimumSustainDistanceBeats(tempo_map.timeSignatureAt(note.position.measure).denominator);
    const Fraction limit = gap - margin;
    if (!(limit < note.sustain))
    {
        // The ring already clears the margin: nothing to trim, and no gesture to move with it.
        return;
    }

    Fraction target = limit.numerator < 0 ? Fraction{} : limit;
    // A scrape's path is DERIVED gesture geometry, synthesized from the notated duration rather
    // than authored, so moving its endpoint loses no information — which is why the trim squishes
    // the gesture instead of flooring the tail on it the way an authored bend point does (rule 2).
    // The slide-out IS that gesture's terminal and a saved scrape carries no other payload, so
    // this branch is the whole payload story for a scrape.
    //
    // Where the leg sits relative to the margin decides everything, and the two cases are the whole
    // rule. A leg that STARTS before the margin line has room to end on it, so it does: the gap is
    // the margin exactly, and no spacing is given up. A leg that starts ON OR AFTER that line
    // cannot yield the margin at all — it is already inside the window — so it halves the distance
    // to the onset, which is the one split that always leaves some gap whatever the crowding. This
    // is the sanctioned exception: the gesture is LITERALLY defined inside the margin, which is
    // exactly when the spacing rule steps aside.
    //
    // No compression floor. Both cases land strictly after the leg's start by construction — the
    // first by its own branch condition, the second because half of a positive room is positive —
    // so the payload stays ascending without one, and a floor here could only buy leg length by
    // spending the spacing the rule exists to protect. g_minimum_slide_window keeps its other job,
    // which is SYNTHESIS: a gesture built from nothing needs a default span. That is not this
    // decision.
    if (isScrape(note.attack) && note.slide_out.has_value())
    {
        const Fraction leg_start = lastStatedFretOffset(note);
        Fraction terminal = note.sustain;
        if (leg_start < limit)
        {
            terminal = limit;
        }
        else if (leg_start < gap)
        {
            // Half the distance to the ONSET, not half the notated length: a leg notated past the
            // onset would halve to something still past it. A binding onset is by definition one
            // the ring does not pass, and a leg lies inside the ring, so no leg can reach it —
            // this is belt and braces against that definition ever moving.
            terminal = leg_start + ((gap - leg_start) * Fraction{1, 2});
        }
        // A leg starting at or beyond the onset has nothing to crunch against, so it keeps its end
        // and the assignment below only ever shortens. The terminal IS the presented sustain,
        // which is what keeps a presented scrape in the shape validateChartNoteAlone pins for a
        // stored one: the gesture ends exactly at the end.
        target = std::min(terminal, note.sustain);
    }
    else
    {
        // Rule 2: the margin yields only to information, and only as far as the information
        // reaches — the tail extends to the last instant the payload still has something to
        // present and stops exactly there, never on to the actual end.
        const Fraction informative = informativePayloadEnd(note);
        if (target < informative)
        {
            target = informative;
        }
        // Trailing statements the target passed present nothing new (only non-changing ones can
        // sit past the last changing one), so they leave with the tail. Clipping here rather than
        // after the sustain assignment keeps the payload inside the sustain AND lets the slide-out
        // measure itself against the path that survives — a trailing hold keyframe must not hold
        // the gesture open through the margin.
        clipPayloadsTo(note, target);
        // The unpitched slide-out is NOT protected payload: it ends wherever the RING ends, so it
        // trims back with the tail to respect the margin. What the trim owes it is a ring still
        // long enough to be a gesture at all and still strictly past the last stated fret, so a
        // crowding that would crush it compresses to the smallest legal end instead of keeping its
        // full length — a kept end runs the gesture through the next onset whenever a slide-in has
        // moved that onset's head into the gap.
        if (note.slide_out.has_value())
        {
            target = keptAfterLastStatedFret(note, std::max(target, g_minimum_slide_window));
        }
    }
    if (target < note.sustain)
    {
        note.sustain = target;
    }
}

// THE TAIL LAW's statement landmark, for one member's stored ring (the grip-tenure law,
// user-signed 2026-09-04; the split generalized 2026-09-06): where the ring stops stating anything
// of its own, which is the earliest offset any curtained stretch may start at — the cases are
// stated once, at \ref ChartPresentation::rested_stretches — or nothing for a tail that never
// rests at all. This answers the STATEMENT half only; the caller asks which stretches of the
// ribbon a span stands over and floors each of them on this.
//
// What never rests is a ring still STATING at its own end — a bend held to the end, a shake
// that never stops, tremolo, a slide-out's travel: the span states where the hand IS, with no
// vocabulary for a statement in progress. A statement that FINISHES is the split the user asked
// for: the stated portion stays always visible, and the plain remainder joins the curtain where
// the statement ended (\ref informativePayloadEnd — the same landmark rule 2 floors the
// presented tail at, so the offset always lies at or inside the drawn ribbon's end).
//
// A ring whose string a later strike takes over (\ref ChartConnections::hands_over) is a
// TRANSFER of the sound — a statement the span has no vocabulary for either, but one that
// FINISHES: it completes at the takeover, where the successor picks the sound up. So it is the
// finished-statement split with an EMPTY remainder — the whole drawn ribbon is the stated
// portion, and the landmark is the ribbon's own end (the co-struck source sighting, 2026-09-06).
// THE HANDOVER IS ASKED FIRST, deliberately: the takeover terminates whatever the ring was still
// stating — a shake or a bend into a pull-off ends where the successor takes the string — so a
// handed-over ring is a finished statement whether or not its channels were quiet at its end,
// and its landmark is the ribbon's end either way. Its ink is identical under both readings; only
// the stroke's verdict differs, and that verdict is the whole sighting. Read as a statement still
// in progress it refused the verdict outright, and the stroke's conjunction then made a
// co-struck partner draw its whole ring in front of the curtain that owned it.
[[nodiscard]] std::optional<Fraction> restedOffsetOf(
    const ChartConnections& connections, const std::size_t index, const ChartNote& presented)
{
    const ChartNote& stored = connections.saved_notes[index];
    if (connections.hands_over[index])
    {
        return presented.sustain;
    }
    // Still stating at the ring's end: tremolo and a slide-out run to the end by construction,
    // and the state in force at the ring's own end says whether the bend and vibrato channels
    // ever go quiet.
    if (stored.tremolo || stored.slide_out.has_value())
    {
        return std::nullopt;
    }
    const RingState state = ringStateAt(stored, stored.sustain);
    if (std::is_neq(state.bend <=> 0.0) || isShaking(state.vibrato))
    {
        return std::nullopt;
    }
    return informativePayloadEnd(stored);
}

// Rule 3's one asker is the whole audience since the tail law moved to the finished-statement
// split, so this is a file-local classifier. Any keyframe at all, whichever channel it states: a
// mid-ring curl and a delayed shake ride the tail exactly as a glide does, and dropping the tail
// would drop the statement with it. Whole-note techniques (muting, emphasis, harmonics) are
// deliberately absent: they say the same thing with or without a tail.
[[nodiscard]] bool hasSustainTechnique(const ChartNote& note)
{
    return std::is_neq(note.bend <=> 0.0) || !note.keyframes.empty() ||
           note.slide_out.has_value() || isShaking(note.vibrato) || note.tremolo;
}

} // namespace

// A CHANGE is what a channel has to state to say anything, so this is the one question the
// per-instant authority cannot answer alone: it folds the ring itself (ringStateAtOnset plus
// RingState::advance, one pass) and compares each keyframe's state against the one it replaced.
// Measuring against where the note STARTS falls out of that — the fold opens at the onset bend,
// the onset fret and the onset vibrato, and carries each channel forward through keyframes that
// state nothing about it.
Fraction informativePayloadEnd(const ChartNote& note)
{
    Fraction last{};
    const auto reaches = [&last](const Fraction offset) {
        if (last < offset)
        {
            last = offset;
        }
    };
    RingState state = ringStateAtOnset(note);
    for (const Keyframe& keyframe : note.keyframes)
    {
        const RingState previous = state;
        state.advance(keyframe);
        if (std::is_neq(state.bend <=> previous.bend))
        {
            reaches(keyframe.offset);
        }
        if (state.fret != previous.fret)
        {
            reaches(keyframe.offset);
        }
        if (state.vibrato != previous.vibrato)
        {
            // A bend value and a fret are POINTS — their information is complete at the instant
            // they are reached, so the tail may stop exactly there. A statement that leaves the
            // string SHAKING is an interval STATE — a start, or a step to the other width: a tail
            // ending on it would show the new shake for no time at all and read as the old one, so
            // the information reaches one minimum gesture window past the statement. A statement
            // that ends the shake is a point again — the interval before it already showed
            // everything.
            reaches(
                isShaking(state.vibrato) ? keyframe.offset + g_minimum_slide_window
                                         : keyframe.offset);
        }
    }
    return last;
}

void clipPayloadsTo(ChartNote& note, const Fraction target)
{
    std::erase_if(
        note.keyframes, [target](const Keyframe& keyframe) { return target < keyframe.offset; });
}

Fraction keptAfterLastStatedFret(const ChartNote& note, const Fraction window)
{
    const Fraction last_fret = lastStatedFretOffset(note);
    if (window <= last_fret)
    {
        return last_fret + g_minimum_slide_window;
    }
    return window;
}

// One walk over the onset groups carries rules 1 through 3, because they share a partition — every
// note at one grid position — and rule 3's verdict needs its members already trimmed. Rule 4 runs
// last over the whole stream, as the plan's ordering states, so a tail that rules 1 to 3 left
// standing is still judged as a dead note's. THE TAIL LAW runs after all four, over the same
// partition, and only ever EMPTIES what they left standing.
ChartPresentation presentedChartNotes(
    const ChartConnections& connections, const ChartShapes& shapes, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& saved_notes = connections.saved_notes;
    ChartPresentation presentation;
    presentation.notes = saved_notes;
    presentation.rested_stretches.resize(saved_notes.size());
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
            // Rule 1: the onset that binds the trim is the first SOUNDING onset the ring does not
            // PASS, where passing means running strictly past it. A ring ending exactly ON an
            // onset binds there and trims — the common let-ring collision, because a notated ring
            // ends on a musical boundary and the next note starts from one, so a ring left whole
            // there would die on a later head with no gap at all. A ring no onset binds presents
            // whole, which is what a last note has always done.
            //
            // Silent holds draw no head, so a slot that only holds fingers binds nothing and the
            // scan steps over it: letting one bind would make authoring a held shape silently
            // shorten every tail in front of it.
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
                    if (silentHold(presented[ahead].attack))
                    {
                        continue;
                    }
                    const Fraction gap =
                        beatDistance(tempo_map, note.position, presented[ahead].position);
                    if (!ringPassesHead(note.sustain, gap))
                    {
                        trimToMargin(note, gap, tempo_map);
                        break;
                    }
                    // All that survives of the exemption this rule replaced. Passing an onset is
                    // still the statement it always was — a tie merged across a neighbour, a
                    // cross-voice hold — so it earns the group its tails under rule 3 below. What
                    // it no longer does is switch the trim off. Deliberately the STRICT reading,
                    // so a near-miss still trims and a tail rule 3 has always earned still earns.
                    deliberate_hold = true;
                }
            }
            // Rule 3's per-member earning, asked of the note as rules 1 and 2 leave it (a trim can
            // clip away the last uninformative payload point) but of the note's ACTUAL ring, which
            // is the length the source or the charter stated and the only one that can say whether
            // a deliberate sustain was meant. That read is exact now rather than nearly so: while a
            // span-scoped rule ran BEFORE this one, `saved_notes` was a rewritten copy under its
            // own name and this comment was a standing falsehood. The tail law runs last and only
            // empties, so nothing reaches here but the chart's own rings.
            const Fraction kept_bound = minimumKeptSustainBeats(
                tempo_map.timeSignatureAt(note.position.measure).denominator);
            group_earned = group_earned || deliberate_hold || hasSustainTechnique(note) ||
                           saved_notes[index].sustain >= kept_bound;
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

    // THE TAIL LAW (\ref presentedChartNotes rule 5, the one authority): span furniture may
    // REST a tail, never shorten one. VERDICT-ONLY, AND LAST: it reads the STORED rings, judges,
    // and MARKS which stretches of each tail rest, inventing and erasing no length. Running last
    // is what makes three things true by construction rather than by argument: rules 1 to 3 see
    // the chart's real rings, so a resting member cannot reach through rule 3's group earning and
    // delete a partner's ribbon; a tail rule 3 or rule 4 already emptied never RESTS, so the
    // hold channel's floor (a resting member's stored ring) never claims a length those rules
    // judged away; and the verdict still exists when the holds are answered.
    //
    // A chart with no furniture has no figures, so the whole law is vacuous there — which is
    // exactly the promise it makes about every tail it does not take.
    if (!shapes.shapes.empty())
    {
        const SpanCover cover{shapes.shapes, tempo_map};
        // One buffer for the whole stream: the coverage walk fills a caller's vector rather than
        // returning one, so a song's worth of notes costs no per-note allocation.
        std::vector<SpanStretch> covered;
        for (std::size_t index = 0; index < presented.size(); ++index)
        {
            const ChartNote& note = presented[index];
            // SCOPE, and it is scope rather than an exception list: the law is judged of
            // fretting-hand members alone (\ref frettingHandMember), and a hold has no ring to
            // rest in any case.
            if (!frettingHandMember(note) || note.sustain.numerator <= 0)
            {
                continue;
            }
            // THE ATOM IS THE MEMBER (user ruling 2026-09-07: "the curtain should apply to
            // everything in the span that doesn't carry technique info"). The stroke conjunction
            // this law shipped with — one rest-or-draw verdict per stroke, so a partner still
            // stating at its end drew its plain stackmates whole beside it — is gone: a member
            // still stating draws, a member no span ever stands over is never reached, and each
            // plain member rests on its own. Per member was always the shape of the landmark,
            // since the stated portion of a technique is a mark and not a duration; now the
            // verdict is per member too.
            //
            // VERDICT ONLY (the execution-form amendment, user ruling 2026-09-03): the tail is
            // judged and marked, never emptied — the presented stream carries every member's
            // rules-1-to-4 tail, and the hold extension keys on the verdict rather than on tail
            // emptiness.
            //
            // Bound to a local and guarded on its own line, so the presence test and the read are
            // provably the same object.
            const std::optional<Fraction> finished = restedOffsetOf(connections, index, note);
            if (!finished.has_value())
            {
                continue;
            }
            const Fraction landmark = *finished;
            // THE CURTAIN BELONGS TO THE SPAN, so it falls where the ribbon runs under one and
            // LIFTS where that span closes (user ruling 2026-09-07, the symmetric half of the
            // coverage ruling the same day): a ring struck under a span is taken at its own head,
            // a ring struck on open board that rings into a later bracket is taken at that
            // bracket's front, and a ring outliving its span draws its overrun at full — which
            // replaces the 2026-09-06 spill clause, where the curtain owned everything past the
            // landmark whatever stood over it. A drone under a melody of brackets is answered
            // once per bracket and shows whole in the gaps; a junction is one uninterrupted
            // stretch, because \ref SpanCover::covering has already merged the abutment.
            cover.covering(
                note.position,
                advanceGridPosition(tempo_map, note.position, note.sustain),
                covered);
            std::vector<RestedStretch>& stretches = presentation.rested_stretches[index];
            stretches.reserve(covered.size());
            for (const SpanStretch& stretch : covered)
            {
                // THE STATEMENT LANDMARK FLOORS THE CURTAIN: the stated portion of a technique
                // rides at full wherever its payload ends, so a stretch starting before that is
                // clipped forward to it — and a stretch lying wholly before it is not stored at
                // all, which is how a handed-over member (landmark: its own ribbon's end) ends up
                // with no stretch to its name.
                const Fraction from =
                    std::max(landmark, beatDistance(tempo_map, note.position, stretch.from));
                const Fraction to = beatDistance(tempo_map, note.position, stretch.to);
                if (from < to)
                {
                    stretches.push_back(RestedStretch{.from = from, .to = to});
                }
            }
        }
    }
    return presentation;
}

bool hasRestingRemainder(const std::vector<RestedStretch>& rested)
{
    return !rested.empty();
}

// The span convention IS the hold, and there is one rule (user sighting 2026-09-03, the repeated
// chord's released pin): a LIVE fretting-hand member with no DRAWN tail, covered by a span, is
// held to the span's reach — while the grip is held, the board pins what is held. The hidden and
// the rule-3-emptied member take the same extension because they are the same physical fact:
// under grip tenure a covered member's un-renewed death would have BROKEN the span, so coverage
// past a member's ring IS the record that the finger never lifted (the restrike replaced the
// sound, not the hand). Two arms used to answer this — the strum extension for emptied tails and
// the stored ring for hidden ones — agreeing only while a hidden ring provably died at the close;
// the covered tail form broke that accident, and the repeated chord's pin released at every
// restrike while the faster chug's held, which is the split the sighting caught.
//
// Dead members (a dead chug is choked, not held), the other hand's onsets, and members whose
// tails stand AT REST state their own hold. Since the execution-form amendment restored hidden
// members' presented tails, "at rest" is the VERDICT's question, not tail emptiness: a hidden
// member's ribbon is the board's near-line reveal, so its hold is still the tenure — keying on
// the tail again would re-release the pins the sighting fixed. It asks that question through
// \ref hasRestingRemainder, the same reading the projection and the census take, because since
// the curtain became a set of stretches (user ruling 2026-09-07) "rests" and "the curtain owns
// part of this ribbon" are one question and no longer two. Coverage is positional only, with no
// posture matching.
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
        // The FLOOR. A HANDED-OVER member pins for exactly its stored ring (user law 2026-09-06,
        // "pinned heads reflect the current SOUNDING state"): the next strike on its string takes
        // the sound, and the same-string clamp (\ref sustainBoundOf) plus the adjacency the claim
        // itself required (\ref predecessorHoldReaches) make the stored ring end exactly on that
        // takeover — the ring IS the takeover instant, stated once. A RESTING member likewise
        // starts from its own stored ring — the span extension below raises it to the reach where
        // the ring falls short, and a resting ring MAY exceed the reach, which is the honest hold.
        // Everyone else starts from the tail they present.
        held.push_back(
            connections.hands_over[index] ||
                    hasRestingRemainder(presentation.rested_stretches[index])
                ? saved_notes[index].sustain
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
        // Bound to a local so the presence test and the read are provably the same object. There
        // is no strum-size gate on the extension any more (the 2026-09-03 one-rule collapse): the
        // lone covered chug between two strikes is a grip member exactly as a strummed one is,
        // and in a DERIVED chart a lone tail-less note a span covers past was necessarily renewed
        // — an un-renewed death breaks the grip, so the span could not reach past it at all.
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
                // already says where its ring ends. A RESTING member is keyed by the VERDICT,
                // not by tail emptiness: the execution-form amendment restored its presented
                // tail, but that ribbon is the board's near-line reveal, and the pin states the
                // grip for the whole tenure regardless. A HANDED-OVER member is excluded whole:
                // its sound ends at its own stored ring, where the next strike on its string
                // takes over (floored above), so the grip's tenure is not its to inherit — that
                // strike owns the display from there. Its tail verdict now says the same thing
                // from the other side (it carries no curtained stretch at all), but only where it
                // still HAS a tail: a handover rules 3 or 4 emptied passes the tail clause below,
                // so the exclusion reads the handover itself rather than a verdict that would let
                // that one through.
                if (!frettingHandMember(note) || note.dead || connections.hands_over[member] ||
                    (note.sustain.numerator > 0 &&
                     !hasRestingRemainder(presentation.rested_stretches[member])) ||
                    !(held[member] < span_hold))
                {
                    continue;
                }
                held[member] = span_hold;
            }
        }
        index = group_end;
    }
    return held;
}

} // namespace rock_hero::common::core
