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
    return note.dead && !note.tremolo && !anyWaypointStatesFret(note.waypoints) &&
           !note.slide_out.has_value() && note.sustain.numerator > 0;
}

// The offset of the last waypoint that states a POSITION, or zero when none does — where the
// note's path stops saying anything new about where the hand is. The two rules that need it are
// the ones a position statement bounds: a scrape's leg begins there, and a ring ending in a
// slide-out must end strictly after it.
[[nodiscard]] Fraction lastStatedFretOffset(const ChartNote& note)
{
    Fraction last{};
    for (const Waypoint& waypoint : note.waypoints)
    {
        if (waypoint.fret.has_value())
        {
            last = waypoint.offset;
        }
    }
    return last;
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
        const Fraction leg_start = lastStatedFretOffset(note);
        Fraction terminal = note.sustain;
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
        // measure itself against the path that survives — a trailing hold waypoint must not hold
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

// The span convention, and \ref chartHolds's whole engine: a strum under a hand-shape span is held
// for the span even where its members present no tail, because the span is what tells the player
// how long to keep the shape fretted. Only a TAIL-LESS member of a same-onset group of two or more
// covered by a span extends, and never when the whole group is dead (a dead chug is choked, not
// held); single notes and members that still present a tail already state their own hold. Coverage
// is positional only, with no posture matching.
//
// Asked of the PRESENTED stream, which is what makes it extend exactly the members presentation
// emptied. It was public while readers still resolved holds from a trimmed stored form; nothing
// does now, so it is this composition's engine rather than a rule of its own.
//
// It states no bound of its own. 40-Q2-B — no tail past the next onset on its own string — reaches
// the answer through chartHolds's ring cap instead, because normalizeSustainOverlaps already holds
// every stored ring inside that same bound; restating it here was a second statement of the rule
// that could only ever agree with the first.
[[nodiscard]] std::vector<Fraction> spanExtendedHolds(
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
        std::size_t group_end = index + 1;
        bool all_dead = presented_notes[index].dead;
        while (group_end < presented_notes.size() && presented_notes[group_end].position == onset)
        {
            all_dead = all_dead && presented_notes[group_end].dead;
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
        if (group_end - index >= 2 && !all_dead && covering_end.has_value() &&
            !(*covering_end < onset))
        {
            const Fraction span_hold = beatDistance(tempo_map, onset, *covering_end);
            for (std::size_t member = index; member < group_end; ++member)
            {
                if (presented_notes[member].sustain.numerator > 0 || !(held[member] < span_hold))
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

} // namespace

bool hasSustainTechnique(const ChartNote& note)
{
    // Any waypoint at all, whichever channel it states: a mid-ring curl and a delayed shake ride
    // the tail exactly as a glide does, and dropping the tail would drop the statement with it.
    return std::is_neq(note.bend <=> 0.0) || !note.waypoints.empty() ||
           note.slide_out.has_value() || note.vibrato || note.tremolo;
}

// Each channel is measured against where the note STARTS — at its onset bend, its own fret, its
// onset vibrato — so a first statement is a change exactly when it differs from that opening
// value, and the running value carries forward through waypoints that state nothing about it.
Fraction informativePayloadEnd(const ChartNote& note)
{
    Fraction last{};
    const auto reaches = [&last](const Fraction offset) {
        if (last < offset)
        {
            last = offset;
        }
    };
    double previous_bend = note.bend;
    int previous_fret = note.fret;
    bool previous_vibrato = note.vibrato;
    for (const Waypoint& waypoint : note.waypoints)
    {
        const double bend = waypoint.bend.value_or(previous_bend);
        if (std::is_neq(bend <=> previous_bend))
        {
            reaches(waypoint.offset);
        }
        previous_bend = bend;

        const int fret = waypoint.fret.value_or(previous_fret);
        if (fret != previous_fret)
        {
            reaches(waypoint.offset);
        }
        previous_fret = fret;

        const bool vibrato = waypoint.vibrato.value_or(previous_vibrato);
        if (vibrato != previous_vibrato)
        {
            // A bend value and a fret are POINTS — their information is complete at the instant
            // they are reached, so the tail may stop exactly there. A vibrato START is an interval
            // STATE: a tail ending on it would show the shake for no time at all and read as no
            // shake, so the information reaches one minimum gesture window past the statement.
            // Its END is a point again — the interval before it already showed everything.
            reaches(vibrato ? waypoint.offset + g_minimum_slide_window : waypoint.offset);
        }
        previous_vibrato = vibrato;
    }
    return last;
}

void clipPayloadsTo(ChartNote& note, const Fraction target)
{
    std::erase_if(
        note.waypoints, [target](const Waypoint& waypoint) { return target < waypoint.offset; });
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

// The span convention is asked of the presented stream (spanExtendedHolds above), and the model's
// whole change to the answer is a cap. Handing it the PRESENTED stream is what makes it extend
// exactly the members presentation emptied — it skips any note still carrying a tail, and
// presentation touches nothing else it reads (positions, strings and dead flags come through
// untouched).
std::vector<Fraction> chartHolds(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const std::vector<ChartShape>& shapes, const TempoMap& tempo_map)
{
    std::vector<Fraction> held = spanExtendedHolds(presented_notes, shapes, tempo_map);
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
