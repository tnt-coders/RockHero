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
// leg starts inside the gap. The bracket clip needs no second entry here: it re-reads a covered
// ring as ending ON its next head BEFORE these rules run, and rule 1 then binds that ring exactly
// as it binds any other.
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
// standing is still judged as a dead note's.
std::vector<ChartNote> presentedChartNotes(
    const std::vector<ChartNote>& saved_notes, const TempoMap& tempo_map)
{
    std::vector<ChartNote> presented = saved_notes;

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
                    if (!(gap < note.sustain))
                    {
                        trimToMargin(note, gap, tempo_map);
                        break;
                    }
                    // All that survives of the exemption this rule replaced. Passing an onset is
                    // still the statement it always was — a tie merged across a neighbour, a
                    // cross-voice hold — so it earns the group its tails under rule 3 below. What
                    // it no longer does is switch the trim off.
                    deliberate_hold = true;
                }
            }
            // Rule 3's per-member earning, asked of the note as rules 1 and 2 leave it (a trim can
            // clip away the last uninformative payload point) but of the note's ACTUAL ring, which
            // is the length the source or the charter stated and the only one that can say whether
            // a deliberate sustain was meant.
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
    return presented;
}

// The span convention IS the hold, and there is nothing else to compose it with. Only a TAIL-LESS
// member of a same-onset group of two or more covered by a span extends, and never when the whole
// group is dead (a dead chug is choked, not held); single notes and members that still present a
// tail already state their own hold. Coverage is positional only, with no posture matching.
//
// Asked of the PRESENTED stream, which is what makes it extend exactly the members presentation
// emptied — it skips any note still carrying a tail, and presentation touches nothing else it
// reads (positions, strings and dead flags come through untouched).
std::vector<Fraction> chartHolds(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const TempoMap& tempo_map)
{
    std::vector<Fraction> held;
    held.reserve(presented_notes.size());
    for (const ChartNote& note : presented_notes)
    {
        held.push_back(note.sustain);
    }
    // How far the covering furniture reaches, from the one authority both span-scoped display
    // rules ask (\ref SpanCover).
    SpanCover cover{shapes, tempo_map};
    for (std::size_t index = 0; index < presented_notes.size();)
    {
        const GridPosition onset = presented_notes[index].position;
        // Sounding members only, on both counts: the span convention extends the members of a
        // STRUM, and a silently-held finger neither is one nor can be dead. Counting one would
        // make a lone note beside a held finger read as a chord, and its presence would break the
        // all-dead unanimity of a chug that is entirely dead.
        std::size_t group_end = index;
        std::size_t sounding = 0;
        bool all_dead = true;
        while (group_end < presented_notes.size() && presented_notes[group_end].position == onset)
        {
            if (!silentHold(presented_notes[group_end].attack))
            {
                ++sounding;
                all_dead = all_dead && presented_notes[group_end].dead;
            }
            ++group_end;
        }
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<SpanCoverage> covering = cover.reaching(onset);
        if (sounding >= 2 && !all_dead && covering.has_value())
        {
            const Fraction span_hold = beatDistance(tempo_map, onset, covering->end);
            for (std::size_t member = index; member < group_end; ++member)
            {
                if (silentHold(presented_notes[member].attack) ||
                    presented_notes[member].sustain.numerator > 0 || !(held[member] < span_hold))
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

// The other face of the same span coverage: where a BRACKET already states how long the hand stays
// down, a member's ribbon has nothing to add about the hold — so it stops restating it and reads
// RHYTHM instead, running from its own head to the next onset and no further. That is the staircase
// a picked arpeggio draws, and it replaces C3, under which the bracket owned the ink outright and
// the ribbons drew nothing at all (user ruling 2026-09-01).
//
// A RE-READ OF THE RING, BEFORE THE PRESENTATION RULES RUN, not a fifth rule after them. Under a
// bracket a ring is read as ending on its next head — the rhythm the ribbon now states — and rules
// 1 through 4 then govern that ring exactly as they govern any other: rule 1 binds it at the head
// it now ends on and trims the margin, rule 2 floors the trim on payload, rule 3 drops it where an
// equal ring would never have earned a tail, and rule 4 keeps judging dead notes. In-span and
// out-of-span therefore CANNOT disagree about equal rings, because one pipeline draws both — the
// compose that a post-presentation clip broke twice, first crossing heads it never saw and then
// leaving stubs on sub-quarter figures that rule 3 would have dropped.
//
// KEYED ON THE HEAD BEING CROSSED, not on the span over the member's own onset (user sighting
// 2026-09-01): a real let-ring figure opens with a strummed pair whose own onset a small box span
// covers, and the growth split carries its rings into the arpeggio span that follows — so the
// covering-span-at-onset key left exactly those founding rings uncut across the bracket's heads.
// The offending ink is a ribbon crossing a head that stands UNDER a bracket, so the head's own
// coverage is what is asked. A ring that never reaches its next head has nothing to re-read, and
// one ending exactly on it is already rule 1's ordinary bind.
//
// THE PAST-SPAN-END EXCEPTION (user ruling 2026-09-01): a member whose ring outlives every span
// covering its end always shows its tail — the ring outliving the held shape IS the information,
// so the staircase never takes it and only the ordinary rules apply. Asked at the ring's END
// against the same coverage authority: a covered end is a ring some span still carries (the fold-in
// laws make every ring under a span a member of it), and an uncovered end has outrun the figure.
//
// Silent holds are skipped exactly as rule 1 skips them: a held finger draws no head, so a ribbon
// ending at one would end in empty space, and authoring a held shape would silently shorten every
// tail in front of it. Same-instant partners bind nothing either — one stroke, not two.
//
// Two exclusions and no exemptions besides. A right-hand onset is a member of nothing, so a tap
// over a held shape keeps the ring it stated, and a silent hold has no ring to re-read. What C3
// exempted besides — a technique-bearing tail, a span covering a glide — was answering INK
// OWNERSHIP, and there is none left to except from: the payload floor below keeps a marked ring
// exactly as long as its statement needs, which is rule 2's own authority applied at this bound.
//
// Membership needs no posture matching, and that is the growth law's doing rather than an omission:
// a fretting-hand stop the standing shape does not state SPLITS the span, so every fretting-hand
// sounding inside one is on a string it states, at the stop it states. Positional coverage is
// therefore exact here for the same reason it is in \ref chartHolds beside it.
//
// THE JUNCTION SKIP (user ruling 2026-09-03, LAW B) restores an invariant the staircase broke:
// equal figures may not draw differently in-span and out. A ring whose end is a legato JUNCTION
// hands its string over — the finger stays down and the next strike takes the sound off it —
// where a ring that simply DIES at the same instant closes. Both end at the same beat, so DURATION
// CANNOT TELL THEM APART, and the margin-probe history is the proof: probing the raw ring end
// exempted every ring whose own death closed a span and broke the staircase; probing one margin
// back fixed that and broke the ring exiting a junction, which the probe then read as inside the
// figure it was leaving. The discriminator is neither — it is the SUCCESSOR's stored intent.
void clipArpeggioTails(
    std::vector<ChartNote>& notes, const std::vector<ChartShape>& shapes,
    const std::vector<bool>& arrivals, const std::vector<std::size_t>& predecessors,
    const TempoMap& tempo_map)
{
    const SpanCover cover{shapes, tempo_map};
    // Every ring that ends in a junction, marked from the SUCCESSOR because that is where the
    // chart states it: intent is stored on the note taking the connection (\ref legatoClaimed) and
    // the DIRECTION is derived per read, so this asks the stored half and never the resolution.
    // The adjacency half is \ref predecessorHoldReaches — the resolver's own strict-adjacency test,
    // called rather than restated, so "the ring reaches the onset" cannot come to mean two things.
    // A note has at most one claiming successor on its string, since a sounding one displaces
    // every later note's predecessor and a silent hold claims nothing.
    //
    // Read once, before anything is clipped: the rings this judges are the stored ones, which is
    // also what \ref predecessorHoldReaches is defined against.
    std::vector<bool> hands_over(notes.size(), false);
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const std::size_t predecessor = predecessors[index];
        if (predecessor == g_no_chart_predecessor || !legatoClaimed(notes[index].attack))
        {
            continue;
        }
        hands_over[predecessor] = predecessorHoldReaches(
            notes[predecessor].position,
            notes[predecessor].sustain,
            notes[index].position,
            tempo_map);
    }
    for (std::size_t index = 0; index < notes.size();)
    {
        const GridPosition onset = notes[index].position;
        std::size_t group_end = index;
        while (group_end < notes.size() && notes[group_end].position == onset)
        {
            ++group_end;
        }
        // The group's next head: the first sounding onset at a LATER instant, on any string. The
        // scan starts at the group's end, so it never sees a partner, and steps over held stops
        // for the reason above.
        std::size_t ahead = group_end;
        while (ahead < notes.size() && silentHold(notes[ahead].attack))
        {
            ++ahead;
        }
        if (ahead < notes.size())
        {
            // Bound to a local so the presence test and the read are provably the same object.
            const std::optional<SpanCoverage> covering = cover.reaching(notes[ahead].position);
            // A bracket standing over the head is what forbids crossing it; a box there, or open
            // ground, leaves every ring to the ordinary rules.
            //
            // AND THE RING MUST BELONG TO THE FIGURE-CHAIN (user ruling 2026-09-03): the
            // staircase reads only rings whose own onset some span covers — a member of the
            // bracket itself, or one carried in from the span before it (the 2026-09-01
            // founding-pair ruling, which this keeps: those onsets stand under the preceding
            // box). A ring whose onset stands on OPEN GROUND enters the bracket from outside the
            // figure, and its persistence into the shape is exactly what its tail states — the
            // mirror of the past-span-end exception below, asked of the group's own onset against
            // the same coverage authority.
            if (covering.has_value() && arrivals[covering->span] &&
                cover.reaching(onset).has_value())
            {
                const Fraction gap = beatDistance(tempo_map, onset, notes[ahead].position);
                for (std::size_t member = index; member < group_end; ++member)
                {
                    ChartNote& note = notes[member];
                    // `gap < sustain` is also the zero-sustain exclusion: a ring that does not
                    // run strictly past the head is rule 1's ordinary case already.
                    if (silentHold(note.attack) || rightHandOnset(note.attack) ||
                        !(gap < note.sustain))
                    {
                        continue;
                    }
                    if (hands_over[member])
                    {
                        // THE JUNCTION SKIP: this ring is not restating the bracket's hold, it is
                        // stating a handover the bracket cannot state at all. Skipped and nothing
                        // more — the ring goes into rules 1 through 4 exactly as an out-of-span
                        // ring does, so rule 1 binds it at the successor's own head and trims the
                        // margin there, and rule 3 still drops it where a sub-threshold effect-free
                        // tail earns nothing. A junction buys the ring no length it would not have
                        // had outside a span; it only stops the staircase from re-reading it.
                        continue;
                    }
                    // The past-span-end exception — the ring OUTLIVING the held shape is the
                    // information (user ruling 2026-09-01) — asked one rule-12a margin before the
                    // ring's end rather than at the end itself. A span's stored extent ends one
                    // display margin before its closing onset (the derivation's rule-12a trim), so
                    // the rings whose own deaths CLOSE a span always end exactly one margin past
                    // its drawn rails; asked at the bare end, the cover query landed in that
                    // furniture gap and exempted precisely the rings that outlive nothing (sighted
                    // 2026-09-01: a texture cut at a span-founding-free contradiction drew every
                    // tail full length). Stepping one margin back restores the musical close: a
                    // ring ending at or before the close takes the staircase, and only one ringing
                    // strictly past the close keeps its tail.
                    const GridPosition ring_end =
                        advanceGridPosition(tempo_map, note.position, note.sustain);
                    // The margin at the END's measure, matching the trim's own convention: the
                    // derivation reduces a close by the margin at the closing onset's measure.
                    const Fraction margin = minimumSustainDistanceBeats(
                        tempo_map.timeSignatureAt(ring_end.measure).denominator);
                    const GridPosition close_probe = advanceGridPosition(
                        tempo_map,
                        note.position,
                        note.sustain < margin ? Fraction{} : note.sustain - margin);
                    if (!cover.reaching(close_probe).has_value())
                    {
                        continue;
                    }
                    // The re-read: the ring ends on the head, floored at the last offset the
                    // payload still has information to present (rule 2's authority, called at
                    // this bound) so the note stays well-formed for the rules that follow.
                    // Trailing non-changing keyframes leave with the tail exactly as they do
                    // under a margin trim.
                    note.sustain = std::max(gap, informativePayloadEnd(note));
                    clipPayloadsTo(note, note.sustain);
                }
            }
        }
        index = group_end;
    }
}

} // namespace rock_hero::common::core
