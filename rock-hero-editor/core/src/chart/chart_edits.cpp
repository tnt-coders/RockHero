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
void clipPayloadsToSustain(common::core::ChartNote& note)
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
    std::erase_if(note.slides, [&note](const common::core::SlideWaypoint& waypoint) {
        return note.sustain < waypoint.offset;
    });
    if (note.slide_out.has_value() && note.sustain < note.slide_out->offset)
    {
        note.slide_out.reset();
    }
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
                clipPayloadsToSustain(note);
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
    normalizeChartLegato(candidate, tempo_map);
    std::vector<common::core::ChartNote> saved_form;
    saved_form.reserve(candidate.size());
    for (const common::core::ChartNote& note : candidate)
    {
        saved_form.push_back(common::core::savedChartNote(note));
    }
    if (!common::core::validateChartNotes(saved_form, chart.tuning, tempo_map).has_value())
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
    bool changed = false;
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        common::core::ChartNote& note = candidate[index];
        if (!std::ranges::binary_search(keys, keyOf(note)))
        {
            continue;
        }
        // The previous note on this string, read from the ORIGINAL stream so that setting one
        // note's attack cannot change what the next one derives from.
        const common::core::ChartNote* previous = nullptr;
        for (std::size_t behind = index; behind > 0; --behind)
        {
            const common::core::ChartNote& earlier = chart.notes[behind - 1];
            if (earlier.string == note.string)
            {
                previous = &earlier;
                break;
            }
        }
        // Option C: the verb infers only what the frets justify. A scrape predecessor is never
        // inferred across (deliberate legato only — its pull is authorable by ordering the edits);
        // a fret-hand harmonic never converts here, since deriving onto it would strip its
        // harmonic; equal released frets justify nothing; and the predecessor must still be
        // holdable at this onset (predecessorHoldReaches) — past the kept-sustain bound a
        // disconnected tail is a proven release, and dragging the tail to reach the note is how
        // legato across such a gap is authored. The judged fret is the RELEASED one — where the
        // predecessor's finger ends — so a glide hands over its last waypoint.
        if (previous == nullptr || previous->attack == common::core::NoteAttack::PickSlide ||
            common::core::fretHandHarmonic(note) ||
            !common::core::predecessorHoldReaches(*previous, note.position, tempo_map))
        {
            continue;
        }
        const int released = common::core::releasedFret(*previous);
        if (released == note.fret)
        {
            continue;
        }
        const common::core::NoteAttack derived = note.fret > released
                                                     ? common::core::NoteAttack::Hammer
                                                     : common::core::NoteAttack::Pull;
        if (note.attack == derived)
        {
            continue;
        }
        const bool was_scrape = note.attack == common::core::NoteAttack::PickSlide;
        // A pinch or tap owns its node with the picking hand; under the derived attack the same
        // number would silently become a fret-hand node, so it leaves with the attack.
        const bool node_leaves =
            note.harmonic_node.has_value() && (note.attack == common::core::NoteAttack::Pinch ||
                                               note.attack == common::core::NoteAttack::Tap);
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
        // Eligible-subset skips, so a mixed selection applies to what CAN take the attack: a
        // hammer or tap needs a place to strike, and a tap harmonic cannot be tremolo picked.
        if ((attack == common::core::NoteAttack::Hammer ||
             attack == common::core::NoteAttack::Tap) &&
            note.fret == 0 && !note.harmonic_node.has_value())
        {
            continue;
        }
        if (attack == common::core::NoteAttack::Tap && note.harmonic_node.has_value() &&
            note.tremolo)
        {
            continue;
        }
        const bool was_scrape = note.attack == common::core::NoteAttack::PickSlide;
        // A pinch or tap owns its node with the picking hand. Moving to any other fretting-hand
        // attack would silently re-read the same number as a fret-hand node, so it leaves with
        // the attack; between the two picking-hand owners the position keeps its meaning.
        const bool node_leaves = note.harmonic_node.has_value() &&
                                 (note.attack == common::core::NoteAttack::Pinch ||
                                  note.attack == common::core::NoteAttack::Tap) &&
                                 attack != common::core::NoteAttack::Pinch &&
                                 attack != common::core::NoteAttack::Tap;
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
