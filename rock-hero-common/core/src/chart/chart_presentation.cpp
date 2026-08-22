#include <algorithm>
#include <compare>
#include <cstddef>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Rule 3 ENDS a tail rather than shortening it, and a presented note must still keep the model's
// shape — payload offsets lie within the sustain — because the painters read it as an ordinary
// note. Nothing actually survives the clip: any payload at all earns the group its tail, so a
// dropped group has none left to clip. The clip is here so that invariant holds by construction
// rather than by that argument, which spans two rules and would quietly stop being true if either
// moved. Rule 4 owes the same clip and pays it beside its own call to trimMutedTail.
void dropPresentedTail(ChartNote& note)
{
    note.sustain = Fraction{};
    clipPayloadsTo(note, note.sustain);
}

// Rules 1 and 2 for one note whose ring reaches into the margin before the next binding onset: the
// tail trims to the margin, floored at the payload that still has information to present, and the
// gesture geometry rides the new end.
//
// Preconditions the caller owns: `gap` is the distance to the first binding onset, the sustain is
// strictly positive, and the ring does NOT run strictly past that onset — rule 1's deliberate-hold
// exemption has already claimed those notes, which is what lets the scrape leg rule below assume
// its leg starts inside the gap.
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
        const Fraction leg_start = note.slides.empty() ? Fraction{} : note.slides.back().offset;
        Fraction terminal = note.slide_out->offset;
        if (leg_start < limit)
        {
            terminal = limit;
        }
        else if (leg_start < gap)
        {
            // Half the distance to the ONSET, not half the notated length: a leg notated past the
            // onset would halve to something still past it. The deliberate-hold exemption already
            // claims those notes, so this is belt and braces against that guard ever moving.
            terminal = leg_start + ((gap - leg_start) * Fraction{1, 2});
        }
        // A leg starting at or beyond the onset has nothing to crunch against, so it keeps its end
        // and the assignment below only ever shortens.
        if (terminal < note.slide_out->offset)
        {
            note.slide_out->offset = terminal;
        }
        // The terminal becomes the presented sustain, which is what keeps a presented scrape in the
        // shape validateChartNoteAlone pins for a stored one: the gesture ends exactly at the end.
        target = note.slide_out->offset;
    }
    else
    {
        // Rule 2: the margin yields only to information, and only as far as the information
        // reaches — the tail extends to the last payload point that CHANGES something and stops
        // exactly there, never on to the actual end.
        const Fraction informative = lastChangingPayloadOffset(note);
        if (target < informative)
        {
            target = informative;
        }
        // Trailing points the target passed present nothing new (only non-changing ones can sit
        // past the last changing one), so they leave with the tail. Clipping here rather than after
        // the sustain assignment keeps the payload inside the sustain AND lets the slide-out
        // measure itself against the path that survives — a trailing hold waypoint must not hold
        // the gesture open through the margin.
        clipPayloadsTo(note, target);
    }
    // The unpitched slide-out is NOT protected payload: its end is gesture geometry derived from
    // the ring rather than a musical event, so it trims back with the tail to respect the margin.
    // The trimmed end must stay strictly positive and strictly after the last surviving waypoint,
    // so a crowding that would crush it compresses to the smallest legal end instead of keeping
    // its full length — a kept end runs the gesture through the next onset whenever a slide-in has
    // moved that onset's head into the gap.
    if (note.slide_out.has_value() && target < note.slide_out->offset)
    {
        const Fraction compressed =
            keptStrictlyAfterLastWaypoint(note, std::max(target, g_minimum_slide_window));
        if (compressed < note.slide_out->offset)
        {
            note.slide_out->offset = compressed;
        }
        target = note.slide_out->offset;
    }
    if (target < note.sustain)
    {
        note.sustain = target;
    }
}

} // namespace

bool hasSustainTechnique(const ChartNote& note)
{
    return !note.bend.empty() || !note.slides.empty() || note.slide_out.has_value() ||
           note.vibrato || note.tremolo;
}

// Each payload is measured against where the note STARTS — unbent, at its own fret — so the first
// point of each curve is a change exactly when it differs from that starting state.
Fraction lastChangingPayloadOffset(const ChartNote& note)
{
    Fraction last{};
    double previous_semitones = 0.0;
    for (const BendPoint& point : note.bend)
    {
        if (std::is_neq(point.semitones <=> previous_semitones) && last < point.offset)
        {
            last = point.offset;
        }
        previous_semitones = point.semitones;
    }
    int previous_fret = note.fret;
    for (const SlideWaypoint& waypoint : note.slides)
    {
        if (waypoint.fret != previous_fret && last < waypoint.offset)
        {
            last = waypoint.offset;
        }
        previous_fret = waypoint.fret;
    }
    return last;
}

void clipPayloadsTo(ChartNote& note, const Fraction target)
{
    std::erase_if(note.bend, [target](const BendPoint& point) { return target < point.offset; });
    std::erase_if(
        note.slides, [target](const SlideWaypoint& waypoint) { return target < waypoint.offset; });
}

Fraction keptStrictlyAfterLastWaypoint(const ChartNote& note, const Fraction window)
{
    if (!note.slides.empty() && window <= note.slides.back().offset)
    {
        return note.slides.back().offset + g_minimum_slide_window;
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
        // The stream is sorted by (position, string), so notes sharing an onset are contiguous and
        // the first note past the group is the next BINDING onset by construction: the first later
        // note at a different grid position, on any string.
        std::size_t group_end = group_begin + 1;
        while (group_end < presented.size() &&
               presented[group_end].position == presented[group_begin].position)
        {
            ++group_end;
        }
        const bool has_binding = group_end < presented.size();

        bool group_earned = false;
        for (std::size_t index = group_begin; index < group_end; ++index)
        {
            ChartNote& note = presented[index];
            bool deliberate_hold = false;
            if (has_binding)
            {
                const Fraction gap =
                    beatDistance(tempo_map, note.position, presented[group_end].position);
                if (gap < note.sustain)
                {
                    // Rule 1's exemption: a ring running strictly PAST the first onset that binds
                    // it is a statement — a tie merged across a neighbour, a cross-voice hold — so
                    // it presents whole however many later onsets it crosses.
                    deliberate_hold = true;
                }
                else if (note.sustain.numerator > 0)
                {
                    trimToMargin(note, gap, tempo_map);
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

    // Rule 4 (E25), asked of the one authority that states it rather than restating its condition:
    // a dead note rings nothing, so a plain tail on one is silence pretending to be sound, while
    // tremolo (a chug) or a slide payload (a dragged mute) keeps it making noise or travelling.
    // Applying it to the PRESENTED note rather than the stored one is the whole of the model's
    // change here: a dead note keeps its actual duration, which is what a legato claim after a
    // muted cluck reads.
    for (ChartNote& note : presented)
    {
        if (trimMutedTail(note))
        {
            clipPayloadsTo(note, note.sustain);
        }
    }
    return presented;
}

// The span rule is asked, never restated: chartEffectiveSustains is the one authority for which
// members a hand-shape span extends and how far, and the model's whole change to the answer is a
// cap. Handing it the PRESENTED stream is what makes it extend exactly the members presentation
// emptied — it skips any note still carrying a tail, and presentation touches nothing else it
// reads (positions, strings and dead flags come through untouched). Restating its walk here would
// duplicate the furthest-reaching-span subtlety that already cost one silently dropped chord
// extension, in a copy no build could keep in step.
std::vector<Fraction> chartHolds(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    std::vector<Fraction> held = chartEffectiveSustains(presented_notes, shapes, tempo_map);
    for (std::size_t index = 0; index < held.size(); ++index)
    {
        // The actual ring is the cap the model adds: the span says how long the SHAPE is held, but
        // a string the source notated as ringing for an eighth is not held for the bar just
        // because a box is drawn around it. Capping every note rather than only the extended ones
        // costs nothing and needs no second copy of the extension test — an unextended hold is its
        // presented tail, and no presentation rule ever lengthens a tail past its stored ring.
        held[index] = std::min(saved_notes[index].sustain, held[index]);
    }
    return held;
}

} // namespace rock_hero::common::core
