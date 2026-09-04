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

// THE TAIL LAW's one comparison, for one member's stored ring (the grip-tenure law, user-signed
// 2026-09-04): a tail hides exactly when its own span COVERS the whole ring and the ring states
// nothing of its own — the three-exception form verbatim: entering before the span, LEAVING
// after it, or carrying information are the only outs.
//
// "Its own span" is the span standing at the note's ONSET, nothing else — the user's own-span
// ruling: the junction survivor draws (its ring outlives its span — "leaving", the news the user
// wants inked), the figure concept is gone from this law, and the seam question is unaskable
// because no cross-span lookup exists to ask it. COVERED means AT OR BEFORE the close, not
// exactly at it: under grip-tenure derivation a same-grip restrike RENEWS the span, so a
// replaced ring — a chug interior, a re-picked step — dies strictly inside the merged span, and
// those rings hide with the rest of the covered set (the repeat boxes and the rails state them).
// The CROSSING conjunct is DELETED BY RULING, not omission: the 2026-09-01 closer-shows fixture
// was reversed by the user on 2026-09-04 ("the last note in the span shouldn't get treated
// special"), so plain sustained chords, chug chains, and co-terminating let-ring figures go
// ribbonless — rails, boxes, and the 3D hold-pinning carry the duration, and Alt or the caret
// reveals the close. STRING and END died as PROOFS, not rulings: growth-in-place makes every
// sounded string a posture member, and a MEMBER's un-renewed death breaks the span — both proofs
// conditional on no non-bounding member class ever returning.
[[nodiscard]] bool ownSpanAccountsForRing(
    const ChartConnections& connections, const SpanCover& cover, const TempoMap& tempo_map,
    const std::size_t index)
{
    const ChartNote& stored = connections.saved_notes[index];
    // PRESENCE first: a span states where the hand IS, and has no vocabulary for what the string
    // is DOING (a bend, a glide, a shake, a tremolo) nor for a TRANSFER of the sound to the next
    // strike — a ring saying either keeps the mark that says it.
    if (hasSustainTechnique(stored) || connections.hands_over[index])
    {
        return false;
    }
    // Bound to a local and guarded on its own line, so the presence test and the reads below are
    // provably about the same object.
    const std::optional<SpanCoverage> own = cover.reaching(stored.position);
    if (!own.has_value())
    {
        return false;
    }
    const GridPosition ring_end = advanceGridPosition(tempo_map, stored.position, stored.sustain);
    return !(own->end < ring_end);
}

} // namespace

bool hasSustainTechnique(const ChartNote& note)
{
    // Any keyframe at all, whichever channel it states: a mid-ring curl and a delayed shake ride
    // the tail exactly as a glide does, and dropping the tail would drop the statement with it.
    return std::is_neq(note.bend <=> 0.0) || !note.keyframes.empty() ||
           note.slide_out.has_value() || isShaking(note.vibrato) || note.tremolo;
}

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
    presentation.hidden.assign(saved_notes.size(), false);
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

    // THE TAIL LAW (user ruling 2026-09-04): span furniture may HIDE a tail, never shorten one.
    //
    // DROP-ONLY, AND LAST. It reads the STORED rings, judges, and empties the tails rules 1 through
    // 4 left standing — so it invents no length, and the whole staircase argument about WHICH
    // fabricated length to draw has no place left to attach. Running last is what makes three
    // things true by construction rather than by argument: rules 1 to 3 see the chart's real rings,
    // so a hidden member cannot reach through rule 3's group earning and delete a partner's ribbon;
    // a tail rule 3 or rule 4 already emptied is never HIDDEN, so the hold channel's floor (a
    // hidden member's stored ring) never claims a length those rules judged away; and the verdict
    // still exists when the holds are answered.
    //
    // A chart with no furniture has no figures, so the whole law is vacuous there — which is
    // exactly the promise it makes about every tail it does not take.
    if (!shapes.shapes.empty())
    {
        const SpanCover cover{shapes.shapes, tempo_map};
        std::size_t stroke_begin = 0;
        while (stroke_begin < presented.size())
        {
            std::size_t stroke_end = stroke_begin + 1;
            while (stroke_end < presented.size() &&
                   presented[stroke_end].position == presented[stroke_begin].position)
            {
                ++stroke_end;
            }
            // THE ATOM IS THE STROKE, exactly as rule 3's is: every string of a chord rings from
            // one stroke, so one stroke gets one tail verdict. A CONJUNCTION over the members whose
            // tails are still standing — a chord showing a ribbon on the string that stopped and
            // none on the string still sounding is a picture no strum makes, and it is reachable
            // the moment two members of one stroke disagree about a conjunct.
            //
            // SCOPE, and it is scope rather than an exception list: the figure is judged of its
            // fretting-hand members alone (\ref frettingHandMember), and a hold has no ring to
            // hide in any case.
            bool any_member = false;
            bool accounted = true;
            for (std::size_t index = stroke_begin; index < stroke_end && accounted; ++index)
            {
                const ChartNote& note = presented[index];
                if (!frettingHandMember(note) || note.sustain.numerator <= 0)
                {
                    continue;
                }
                any_member = true;
                accounted = ownSpanAccountsForRing(connections, cover, tempo_map, index);
            }
            if (any_member && accounted)
            {
                for (std::size_t index = stroke_begin; index < stroke_end; ++index)
                {
                    ChartNote& note = presented[index];
                    if (!frettingHandMember(note) || note.sustain.numerator <= 0)
                    {
                        continue;
                    }
                    presentation.hidden[index] = true;
                    // The same drop rules 3 and 4 spend, so a hidden note stays a well-formed
                    // presented note: payload offsets lie within the sustain. Nothing actually
                    // survives the clip here — a ring carrying any statement is never hidden — and
                    // the call is what keeps that an invariant of the code rather than of that
                    // argument.
                    dropPresentedTail(note);
                }
            }
            stroke_begin = stroke_end;
        }
    }
    return presentation;
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
// Dead members (a dead chug is choked, not held), the other hand's onsets, and members still
// DRAWING a tail state their own hold. Coverage is positional only, with no posture matching.
//
// Asked of the PRESENTED stream, which is what makes it extend exactly the members whose tails
// no surface draws — presentation touches nothing else it reads (positions, strings, attacks and
// dead flags come through untouched).
std::vector<Fraction> chartHolds(
    const ChartPresentation& presentation, const std::vector<ChartNote>& saved_notes,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& presented_notes = presentation.notes;
    std::vector<Fraction> held;
    held.reserve(presented_notes.size());
    for (std::size_t index = 0; index < presented_notes.size(); ++index)
    {
        // The FLOOR: a hidden member starts from its own stored ring (never the presented zero —
        // the ring is what was hidden, and it is owed back even where no span survives to cover
        // the onset in this reading); everyone else starts from the tail they draw. The span
        // extension below then raises every covered no-tail member to the reach, and a hidden
        // ring never exceeds it (covered MEANS at or inside the close), so the floor is exactly
        // the fallback and never a competing answer.
        held.push_back(
            presentation.hidden[index] ? saved_notes[index].sustain
                                       : presented_notes[index].sustain);
    }
    // How far the covering furniture reaches, from the one authority both span-scoped display
    // rules ask (\ref SpanCover).
    SpanCover cover{shapes, tempo_map};
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
                // A member still DRAWING a tail states its own hold — the leaving member's ribbon
                // already says where its ring ends. A HIDDEN member's presented tail is zero, so
                // the drawn-tail test below passes it straight into the extension: presentation
                // drops the ribbon, and the hold pins the grip.
                if (!frettingHandMember(note) || note.dead || note.sustain.numerator > 0 ||
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
