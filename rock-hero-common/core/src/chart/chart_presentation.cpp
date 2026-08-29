#include <algorithm>
#include <compare>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
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
// leg starts inside the gap.
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
    // Both streams ascend, so one cursor consumes each span exactly once. What it has to remember
    // is the FURTHEST point any already-started span reaches — not which span started last. Spans
    // may overlap, and an earlier one running longer holds the same strum just as well; tracking
    // the latest STARTING span let a long shape be shadowed by a short one that began inside it,
    // so a held chord silently lost its extension and the legato that extension justified was
    // repaired away. Advancing each span once here is also less work than re-advancing the
    // remembered span at every onset group.
    std::size_t next_shape = 0;
    std::optional<GridPosition> covering_end;
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
        while (next_shape < shapes.size() && !(onset < shapes[next_shape].position))
        {
            const GridPosition span_end = advanceGridPosition(
                tempo_map, shapes[next_shape].position, shapes[next_shape].sustain);
            if (!covering_end.has_value() || *covering_end < span_end)
            {
                covering_end = span_end;
            }
            ++next_shape;
        }
        if (sounding >= 2 && !all_dead && covering_end.has_value() && !(*covering_end < onset))
        {
            const Fraction span_hold = beatDistance(tempo_map, onset, *covering_end);
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

} // namespace rock_hero::common::core
