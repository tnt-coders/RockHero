#include "chart/chart_edits.h"

#include "chart/legato_normalize.h"
#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/session/session.h>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] ChartNoteKey keyOf(const common::core::ChartNote& note)
{
    return ChartNoteKey{.position = note.position, .string = note.string};
}

[[nodiscard]] bool keyLess(const common::core::ChartNote& lhs, const common::core::ChartNote& rhs)
{
    return keyOf(lhs) < keyOf(rhs);
}

// Drops bend and slide points past the note's (possibly shortened) sustain so the payload rule
// "offsets within the sustain" keeps holding after a 40-Q2-B truncation. A slide-out past the
// sustain drops like any payload. Latent payloads on a scrape clip too — they must still fit
// the sustain when a toggle-back makes them real again.
//
// `end_lands_on_onset` says the new sustain end IS a following same-string onset, which the
// 40-Q2-B truncation always makes it. A pitched waypoint may not sit on a later onset of its own
// string (the glide-into-a-real-note case stores no coordinates, which is what keeps that
// encoding undesyncable), so there the last point must go too: keeping it turned an ordinary
// note placement into a silent refusal of the whole plan, because the truncation left behind
// exactly the payload the gate then rejected. A sustain that merely ends where the user put it
// keeps a point at its end, which is the normal shift-slide glide-end.
void clipPayloadsToSustain(common::core::ChartNote& note, const bool end_lands_on_onset = false)
{
    std::erase_if(note.bend, [&note](const common::core::BendPoint& point) {
        return note.sustain < point.offset;
    });
    if (note.attack == common::core::NoteAttack::PickSlide && note.slide_out.has_value())
    {
        // A scrape's gesture ends exactly at the sustain, so a sustain change RE-TERMINATES the
        // slide-out instead of dropping it (the import trim's compress twist under the editor's
        // exact-adjacency bound): turnaround waypoints strictly before the new end survive, and
        // the terminal rides to the sustain itself. When compression makes the terminal fret
        // meet its new predecessor, the nearest earlier differing fret takes over so the path
        // never sits still.
        const std::vector<common::core::SlideWaypoint> path = std::move(note.slides);
        note.slides = {};
        for (const common::core::SlideWaypoint& waypoint : path)
        {
            if (waypoint.offset < note.sustain)
            {
                note.slides.push_back(waypoint);
            }
        }
        const int previous_fret = note.slides.empty() ? note.fret : note.slides.back().fret;
        int terminal_fret = note.slide_out->fret;
        std::size_t candidate = path.size();
        while (terminal_fret == previous_fret && candidate > 0)
        {
            --candidate;
            terminal_fret = path[candidate].fret;
        }
        note.slide_out = common::core::SlideOut{.offset = note.sustain, .fret = terminal_fret};
        return;
    }
    std::erase_if(
        note.slides, [&note, end_lands_on_onset](const common::core::SlideWaypoint& waypoint) {
            return end_lands_on_onset ? !(waypoint.offset < note.sustain)
                                      : note.sustain < waypoint.offset;
        });
    if (note.slide_out.has_value() && note.sustain < note.slide_out->offset)
    {
        note.slide_out.reset();
    }
}

// True when a note's harmonic node cannot follow it into `target`, so the verb changing the attack
// must send the node away with it.
//
// Two facts decide it, and nothing else. A pull-off releases onto a plain stopped pitch and sounds
// no harmonic at any fret, so a node can never survive one. Otherwise the node survives exactly
// while the same HAND still owns it: the stored number means a place on the neck under a fretting
// touch and a place the picking hand damps otherwise, so a change that flips the owner silently
// re-reads it as a different technique. On a stopped note nothing flips — an artificial harmonic's
// fretting hand is on the stop and the picking hand on the node under every attack — which is why a
// hammer-on onto a stopped harmonic keeps it and a tap on an open string does not.
//
// Both attack verbs ask this. They used to answer it separately and disagreed: for one pinch at a
// real stop becoming a hammer-on, the legato verb kept the node (correctly, as the tapped-harmonic
// gesture) while the attack verb dropped it, so two keystrokes specified as the same conversion
// would have produced two different notes.
[[nodiscard]] bool nodeLeavesWithAttack(
    const common::core::ChartNote& note, const common::core::NoteAttack target)
{
    if (!note.harmonic_node.has_value())
    {
        return false;
    }
    if (target == common::core::NoteAttack::Pull)
    {
        return true;
    }
    common::core::ChartNote retyped = note;
    retyped.attack = target;
    return common::core::frettingFingerOnNode(note) != common::core::frettingFingerOnNode(retyped);
}

// 40-Q2-B normalization: walking each string's sorted notes, any sustain ringing across the next
// onset truncates to end exactly there (adjacency is legal), clipping payloads with it.
void normalizeSustainOverlaps(
    std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map)
{
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        common::core::ChartNote& note = notes[index];
        if (note.sustain.numerator <= 0)
        {
            continue;
        }
        for (std::size_t later = index + 1; later < notes.size(); ++later)
        {
            const common::core::ChartNote& next = notes[later];
            if (next.string != note.string)
            {
                continue;
            }
            const common::core::GridPosition sustain_end =
                common::core::sustainEndPosition(tempo_map, note);
            if (next.position < sustain_end)
            {
                note.sustain = common::core::beatDistance(tempo_map, note.position, next.position);
                clipPayloadsToSustain(note, /*end_lands_on_onset=*/true);
            }
            break;
        }
    }
}

// Diffs the current stream against the planned stream into removed/inserted full values; both
// inputs are sorted by (position, string).
[[nodiscard]] std::optional<ChartNotesEditPlan> diffNotes(
    const std::vector<common::core::ChartNote>& before,
    const std::vector<common::core::ChartNote>& after, std::string_view label)
{
    ChartNotesEditPlan plan;
    plan.label = std::string{label};
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    while (before_index < before.size() || after_index < after.size())
    {
        if (before_index == before.size())
        {
            plan.inserted.push_back(after[after_index++]);
            continue;
        }
        if (after_index == after.size())
        {
            plan.removed.push_back(before[before_index++]);
            continue;
        }
        const common::core::ChartNote& old_note = before[before_index];
        const common::core::ChartNote& new_note = after[after_index];
        if (keyLess(old_note, new_note))
        {
            plan.removed.push_back(before[before_index++]);
            continue;
        }
        if (keyLess(new_note, old_note))
        {
            plan.inserted.push_back(after[after_index++]);
            continue;
        }
        if (!(old_note == new_note))
        {
            plan.removed.push_back(old_note);
            plan.inserted.push_back(new_note);
        }
        ++before_index;
        ++after_index;
    }

    if (plan.removed.empty() && plan.inserted.empty())
    {
        return std::nullopt;
    }
    return plan;
}

// Finalizes a candidate stream: restores (position, string) order, applies the 40-Q2-B overlap
// normalization and the legato repair, gates the result through the whole technique matrix, and
// diffs against the current stream. The gate is what makes authoring an invalid chart impossible
// by construction — a plan whose candidate the document reader would reject refuses here, for
// every present and future verb, with no per-verb guard to forget. It validates the SAVED form,
// because a scrape's latent overrides are legal in memory and stripped by the writer.
[[nodiscard]] std::optional<ChartNotesEditPlan> finalizePlan(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    std::vector<common::core::ChartNote> candidate, std::string_view label)
{
    std::ranges::sort(candidate, keyLess);
    normalizeSustainOverlaps(candidate, tempo_map);
    // The repair hands back the saved form it had to build to judge against, which is exactly what
    // the gate validates — deriving it again here was a second deep copy of every note per
    // keystroke.
    const std::vector<common::core::ChartNote> saved_form =
        normalizeChartLegato(candidate, chart.shapes, tempo_map);
    if (!common::core::validateChartNotes(saved_form, chart.shapes, chart.tuning, tempo_map)
             .has_value())
    {
        return std::nullopt;
    }
    return diffNotes(chart.notes, candidate, label);
}

} // namespace

std::optional<ChartNotesEditPlan> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note)
{
    std::vector<common::core::ChartNote> candidate = chart.notes;
    // Placing on an occupied slot replaces the note there. Still reachable under the marker
    // model: undo/redo never move the marker, so undoing a delete can put a note back under
    // an armed caret with an empty selection — the next typed digit inserts onto that
    // occupied slot and must replace, not collide.
    std::erase_if(candidate, [&note](const common::core::ChartNote& existing) {
        return keyOf(existing) == keyOf(note);
    });
    candidate.push_back(std::move(note));
    return finalizePlan(chart, tempo_map, std::move(candidate), "Insert Note");
}

std::optional<ChartNotesEditPlan> planDeleteNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys)
{
    std::vector<common::core::ChartNote> candidate;
    candidate.reserve(chart.notes.size());
    std::size_t deleted = 0;
    for (const common::core::ChartNote& note : chart.notes)
    {
        if (std::ranges::binary_search(keys, keyOf(note)))
        {
            ++deleted;
            continue;
        }
        candidate.push_back(note);
    }
    if (deleted == 0)
    {
        return std::nullopt;
    }
    const std::string label =
        deleted == 1 ? std::string{"Delete Note"} : "Delete " + std::to_string(deleted) + " Notes";
    return finalizePlan(chart, tempo_map, std::move(candidate), label);
}

std::optional<ChartNotesEditPlan> planMoveNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::Fraction beat_delta, int string_delta,
    std::string_view label)
{
    if (keys.empty() || (beat_delta.numerator == 0 && string_delta == 0))
    {
        return std::nullopt;
    }

    const int string_count = static_cast<int>(chart.tuning.strings.size());
    std::vector<common::core::ChartNote> moved;
    std::vector<common::core::ChartNote> candidate;
    candidate.reserve(chart.notes.size());
    for (const common::core::ChartNote& note : chart.notes)
    {
        if (std::ranges::binary_search(keys, keyOf(note)))
        {
            common::core::ChartNote target = note;
            target.position =
                common::core::advanceGridPosition(tempo_map, target.position, beat_delta);
            target.string += string_delta;
            // Refused, never clamped: a move that would leave the neck or the grid is invalid.
            if (target.string < 1 || target.string > string_count)
            {
                return std::nullopt;
            }
            moved.push_back(std::move(target));
        }
        else
        {
            candidate.push_back(note);
        }
    }
    if (moved.empty())
    {
        return std::nullopt;
    }

    // Origin-clamped or converging moves that stack two notes on one slot are refused, as is
    // landing on a slot an unmoved note occupies.
    std::vector<ChartNoteKey> target_keys;
    target_keys.reserve(moved.size());
    for (const common::core::ChartNote& note : moved)
    {
        target_keys.push_back(keyOf(note));
    }
    std::ranges::sort(target_keys);
    if (std::ranges::adjacent_find(target_keys) != target_keys.end())
    {
        return std::nullopt;
    }
    for (const common::core::ChartNote& note : candidate)
    {
        if (std::ranges::binary_search(target_keys, keyOf(note)))
        {
            return std::nullopt;
        }
    }

    candidate.insert(candidate.end(), moved.begin(), moved.end());
    return finalizePlan(chart, tempo_map, std::move(candidate), label);
}

std::optional<ChartNotesEditPlan> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, int target, bool set_exact)
{
    // The transposition anchor: the shared delta comes from the snapshot's lowest fret.
    std::optional<int> lowest;
    for (const common::core::ChartNote& note : base)
    {
        if (!lowest.has_value() || note.fret < *lowest)
        {
            lowest = note.fret;
        }
    }
    if (!lowest.has_value())
    {
        return std::nullopt;
    }

    const int delta = set_exact ? 0 : target - *lowest;
    const std::string label =
        (set_exact ? "Set Fret " : "Transpose to Fret ") + std::to_string(target);
    // Retyped values compute from the SNAPSHOT (the multi-digit window replans the whole entry
    // from the pre-entry originals) and swap into the live stream for the shared finalize, whose
    // whole-matrix gate replaces the local fret caps this once carried: any out-of-range or
    // rule-violating result refuses the plan outright.
    std::vector<common::core::ChartNote> retyped_notes;
    retyped_notes.reserve(base.size());
    for (const common::core::ChartNote& note : base)
    {
        const int fret = set_exact ? target : note.fret + delta;
        common::core::ChartNote retyped = note;
        retyped.fret = fret;
        // A scrape's path translates with its start (the plan-55 transposition special case):
        // the whole gesture shifts by the same delta, preserving travel.
        if (note.attack == common::core::NoteAttack::PickSlide)
        {
            const int path_delta = fret - note.fret;
            for (common::core::SlideWaypoint& waypoint : retyped.slides)
            {
                waypoint.fret += path_delta;
            }
            if (retyped.slide_out.has_value())
            {
                retyped.slide_out->fret += path_delta;
            }
        }
        retyped_notes.push_back(std::move(retyped));
    }
    std::vector<common::core::ChartNote> candidate = chart.notes;
    for (common::core::ChartNote& note : candidate)
    {
        for (const common::core::ChartNote& retyped : retyped_notes)
        {
            if (keyOf(retyped) == keyOf(note))
            {
                note = retyped;
                break;
            }
        }
    }
    return finalizePlan(chart, tempo_map, std::move(candidate), label);
}

std::optional<ChartNotesEditPlan> planAdjustSustain(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, common::core::Fraction beat_delta)
{
    if (keys.empty() || beat_delta.numerator == 0)
    {
        return std::nullopt;
    }

    std::vector<common::core::ChartNote> candidate = chart.notes;
    bool changed = false;
    for (common::core::ChartNote& note : candidate)
    {
        if (!std::ranges::binary_search(keys, keyOf(note)))
        {
            continue;
        }
        common::core::Fraction next_sustain = note.sustain + beat_delta;
        if (next_sustain.numerator < 0)
        {
            next_sustain = common::core::Fraction{};
        }
        // A scrape needs somewhere to travel: its sustain floors at the minimum gesture window
        // instead of zero (the path re-terminates onto the shrunk tail via the payload clip).
        if (note.attack == common::core::NoteAttack::PickSlide &&
            next_sustain < g_minimum_slide_window)
        {
            next_sustain = g_minimum_slide_window;
        }
        // The minimum-sustain-distance rule (override design deliberately open): growing a tail
        // clamps it to end at least the shared margin BEFORE the next onset on ANY string, so
        // extension can never crowd another note.
        // Same-onset chord members sit at equal positions and never block each other, and
        // notes under a shared shape span are implied-held across each other's onsets (§5),
        // so span siblings never block either — the first later onset outside every shared
        // span binds. The clamp binds this verb only: pre-existing closer spacing (imports,
        // the insert truncation's exact adjacency) is left untouched, and a tail already at
        // or past the limit refuses to grow rather than shrinking to it.
        if (beat_delta.numerator > 0)
        {
            const auto shares_span = [&chart, &tempo_map, &note](
                                         const common::core::GridPosition& other) {
                return std::ranges::any_of(
                    chart.shapes, [&](const common::core::ChartShape& shape) {
                        const auto covers = [&](const common::core::GridPosition& position) {
                            return !(position < shape.position) &&
                                   common::core::beatDistance(tempo_map, shape.position, position) <
                                       shape.sustain;
                        };
                        return covers(note.position) && covers(other);
                    });
            };
            auto blocker = std::ranges::upper_bound(
                chart.notes, note.position, {}, &common::core::ChartNote::position);
            while (blocker != chart.notes.end() && shares_span(blocker->position))
            {
                ++blocker;
            }
            if (blocker != chart.notes.end())
            {
                const common::core::TimeSignatureChange signature =
                    tempo_map.timeSignatureAt(note.position.measure);
                const common::core::Fraction limit =
                    common::core::beatDistance(tempo_map, note.position, blocker->position) -
                    common::core::minimumSustainDistanceBeats(signature.denominator);
                if (limit < next_sustain)
                {
                    next_sustain = note.sustain < limit ? limit : note.sustain;
                }
            }
        }
        if (next_sustain == note.sustain)
        {
            continue;
        }
        note.sustain = next_sustain;
        clipPayloadsToSustain(note);
        changed = true;
    }
    if (!changed)
    {
        return std::nullopt;
    }
    return finalizePlan(
        chart,
        tempo_map,
        std::move(candidate),
        beat_delta.numerator > 0 ? "Grow Sustain" : "Shrink Sustain");
}

// Derives each selected note's legato direction from the previous note on its own string. The
// chart's notes are sorted by (position, string), so the note to come from is simply the last
// earlier entry sharing the string — nothing on that string can sit between them.
//
// Deliberately unbounded in time: a hammer-on from a note eight bars back is musically odd, but the
// user asserting legato is the authority on whether the notes connect, and the editor's job is only
// to record which direction that connection runs. Refusing on distance would be second-guessing the
// author; refusing on an absent or equal fret is refusing to invent data that is not there.
std::optional<ChartNotesEditPlan> planSetLegato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, const std::string_view label)
{
    if (keys.empty())
    {
        return std::nullopt;
    }

    std::vector<common::core::ChartNote> candidate = chart.notes;
    // Held lengths for the hold test, span-extended where the spans imply a held shape, judged in
    // the SAVED form — the form the gate validates. A pick slide's latent mute is the difference
    // that matters: in memory it can make an onset group read as all-muted (choked, no extension)
    // where the saved chart reads it as held, so the verb would deny a connection the gate, the
    // repair and the 3D preview all agree exists.
    std::vector<common::core::ChartNote> saved_notes;
    saved_notes.reserve(chart.notes.size());
    for (const common::core::ChartNote& note : chart.notes)
    {
        saved_notes.push_back(common::core::savedChartNote(note));
    }
    const std::vector<common::core::Fraction> effective_sustains =
        common::core::chartEffectiveSustains(saved_notes, chart.shapes, tempo_map);
    bool changed = false;
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        common::core::ChartNote& note = candidate[index];
        if (!std::ranges::binary_search(keys, keyOf(note)))
        {
            continue;
        }
        // The previous note on this string, read from the saved form of the ORIGINAL stream: the
        // original so that setting one note's attack cannot change what the next one derives from,
        // and the saved form so the verb judges the same values the gate will.
        const common::core::ChartNote* previous = nullptr;
        std::size_t previous_index = 0;
        for (std::size_t behind = index; behind > 0; --behind)
        {
            const common::core::ChartNote& earlier = saved_notes[behind - 1];
            if (earlier.string == note.string)
            {
                previous = &earlier;
                previous_index = behind - 1;
                break;
            }
        }
        // Option C: the verb records only what the predecessor justifies, which
        // `derivedLegatoAttack` is the one authority for — including that the judged fret is the
        // RELEASED one (a glide hands over its last waypoint), that the predecessor must still be
        // holdable at this onset, and that equal frets justify nothing. Past the kept-sustain bound
        // a disconnected tail is a proven release, and dragging the tail to reach the note is how
        // legato across such a gap is authored.
        //
        // The one policy the verb adds is refusing to derive ACROSS a scrape: deriving onto a
        // gesture is a guess, while accepting a pull an author already wrote from one is not, so
        // the repair keeps such a pull standing and this never invents one. A scrape's pull stays
        // authorable by ordering the edits.
        if (previous != nullptr && previous->attack == common::core::NoteAttack::PickSlide)
        {
            continue;
        }
        // A fret-hand harmonic never converts here: on an open string the NODE is the note's
        // pitch, so sending it away would silently rewrite the music rather than record a
        // direction. A stopped harmonic is different — the stop still names the pitch — which is
        // why the conversion below is willing to drop that node.
        if (common::core::fretHandHarmonic(note))
        {
            continue;
        }
        // Asked of the note the verb intends to WRITE rather than the one on the page. A harmonic's
        // node vetoes a pull-off, and the verb is willing to send a picking-hand node away for the
        // conversion — so asking with it still attached would refuse the very edit it would have
        // dropped the node for, which is how pressing H on a stopped harmonic came to do nothing.
        common::core::ChartNote asked = note;
        asked.harmonic_node.reset();
        const common::core::NoteAttack derived = common::core::derivedLegatoAttack(
            asked,
            previous,
            previous == nullptr ? common::core::Fraction{} : effective_sustains[previous_index],
            tempo_map);
        if (derived == common::core::NoteAttack::Pick || note.attack == derived)
        {
            continue;
        }
        const bool was_scrape = note.attack == common::core::NoteAttack::PickSlide;
        const bool node_leaves = nodeLeavesWithAttack(note, derived);
        note.attack = derived;
        if (was_scrape)
        {
            // A scrape's path was gesture geometry; as a pitched glide or an ordinary trail-off
            // it would be a fiction, so it leaves with the attack exactly as in planSetAttack.
            note.slides.clear();
            note.slide_out.reset();
        }
        if (node_leaves)
        {
            note.harmonic_node.reset();
        }
        changed = true;
    }
    if (!changed)
    {
        return std::nullopt;
    }
    return finalizePlan(chart, tempo_map, std::move(candidate), label);
}

std::optional<ChartNotesEditPlan> planSetAttack(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartNoteKey>& keys, const common::core::NoteAttack attack,
    const std::string_view label)
{
    if (keys.empty())
    {
        return std::nullopt;
    }

    std::vector<common::core::ChartNote> candidate = chart.notes;
    bool changed = false;
    for (common::core::ChartNote& note : candidate)
    {
        if (!std::ranges::binary_search(keys, keyOf(note)) || note.attack == attack)
        {
            continue;
        }
        // Eligible-subset skip, so a mixed selection applies to what CAN take the attack. Asked of
        // the per-note rule authority rather than restated: two of its predicates used to be copied
        // here, which meant any OTHER rule the target attack could break went unskipped, and the
        // whole-stream gate then refused the edit for every note in the selection instead of just
        // that one. Asked of the note as it would be WRITTEN, since the conversions below are part
        // of what makes it legal.
        common::core::ChartNote retyped = note;
        retyped.attack = attack;
        if (nodeLeavesWithAttack(note, attack))
        {
            retyped.harmonic_node.reset();
        }
        if (attack != common::core::NoteAttack::PickSlide &&
            note.attack == common::core::NoteAttack::PickSlide)
        {
            retyped.slides.clear();
            retyped.slide_out.reset();
        }
        if (attack == common::core::NoteAttack::Pinch && !retyped.harmonic_node.has_value())
        {
            const int stop = note.fret > 0 ? note.fret : chart.tuning.capo;
            retyped.harmonic_node = static_cast<double>(stop) + 12.0;
        }
        if (attack != common::core::NoteAttack::PickSlide &&
            !common::core::validateChartNoteAlone(retyped, chart.tuning, tempo_map).has_value())
        {
            continue;
        }
        const bool was_scrape = note.attack == common::core::NoteAttack::PickSlide;
        const bool node_leaves = nodeLeavesWithAttack(note, attack);
        note.attack = attack;
        if (node_leaves)
        {
            note.harmonic_node.reset();
        }
        // A pinch is picking while damping a node, so the verb authors one when none exists:
        // the octave at the stop — the lowest-order harmonic available at any fret and the
        // commonest squeal — matching the import default. An existing node keeps its position;
        // it names the same physical point under either picking-hand reading.
        if (attack == common::core::NoteAttack::Pinch && !note.harmonic_node.has_value())
        {
            const int stop = note.fret > 0 ? note.fret : chart.tuning.capo;
            note.harmonic_node = static_cast<double>(stop) + 12.0;
        }
        if (attack == common::core::NoteAttack::PickSlide)
        {
            // The note's own fret is the scrape start (unlike imported carriers, whose dead
            // strings carry no meaningful fret); the default travels to the far end of the
            // corpus range, downward from the neck's upper half.
            const bool upward =
                note.fret <= (g_pick_slide_default_high_fret + g_pick_slide_default_low_fret) / 2;
            applyDefaultPickSlidePath(note, upward);
        }
        else if (was_scrape)
        {
            // The path was gesture geometry; as a pitched glide or an ordinary trail-off it
            // would be a fiction. The overridden techniques were never touched, so they simply
            // resurface — except a latent slide-out, which the scrape's own terminal occupied.
            note.slides.clear();
            note.slide_out.reset();
        }
        changed = true;
    }
    if (!changed)
    {
        return std::nullopt;
    }
    return finalizePlan(chart, tempo_map, std::move(candidate), label);
}

std::expected<void, EditorUndoFailureCode> applyChartNotesChange(
    common::core::Chart& chart, const std::vector<common::core::ChartNote>& to_remove,
    const std::vector<common::core::ChartNote>& to_insert)
{
    // Work on a copy so a failed precondition never leaves a half-applied stream behind.
    std::vector<common::core::ChartNote> notes = chart.notes;
    for (const common::core::ChartNote& note : to_remove)
    {
        const auto found = std::ranges::lower_bound(notes, note, keyLess);
        if (found == notes.end() || !(*found == note))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        notes.erase(found);
    }
    for (const common::core::ChartNote& note : to_insert)
    {
        const auto insert_at = std::ranges::lower_bound(notes, note, keyLess);
        if (insert_at != notes.end() && keyOf(*insert_at) == keyOf(note))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        notes.insert(insert_at, note);
    }

    chart.notes = std::move(notes);
    return {};
}

namespace
{

// Both undo and redo replay the plan against the session's mutable chart, so the chart revision
// bumps and every projection rebuilds exactly like a fresh edit.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyToSessionChart(
    EditorEditContext& context, const std::vector<common::core::ChartNote>& to_remove,
    const std::vector<common::core::ChartNote>& to_insert)
{
    common::core::Chart* const chart = context.session.currentChart();
    if (chart == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return applyChartNotesChange(*chart, to_remove, to_insert);
}

} // namespace

std::expected<void, EditorUndoFailureCode> ChartNotesEdit::undo(EditorEditContext& context) const
{
    return applyToSessionChart(context, plan.inserted, plan.removed);
}

std::expected<void, EditorUndoFailureCode> ChartNotesEdit::redo(EditorEditContext& context) const
{
    return applyToSessionChart(context, plan.removed, plan.inserted);
}

std::string ChartNotesEdit::label() const
{
    return plan.label;
}

} // namespace rock_hero::editor::core
