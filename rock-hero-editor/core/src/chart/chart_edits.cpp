#include "chart/chart_edits.h"

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
// sustain drops like any payload.
void clipPayloadsToSustain(common::core::ChartNote& note)
{
    std::erase_if(note.bend, [&note](const common::core::BendPoint& point) {
        return note.sustain < point.offset;
    });
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
// normalization, and diffs against the current stream.
[[nodiscard]] std::optional<ChartNotesEditPlan> finalizePlan(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    std::vector<common::core::ChartNote> candidate, std::string_view label)
{
    std::ranges::sort(candidate, keyLess);
    normalizeSustainOverlaps(candidate, tempo_map);
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
    const common::core::Chart& chart, const std::vector<ChartNoteKey>& keys)
{
    ChartNotesEditPlan plan;
    for (const common::core::ChartNote& note : chart.notes)
    {
        if (std::ranges::binary_search(keys, keyOf(note)))
        {
            plan.removed.push_back(note);
        }
    }
    if (plan.removed.empty())
    {
        return std::nullopt;
    }
    plan.label = plan.removed.size() == 1
                     ? std::string{"Delete Note"}
                     : "Delete " + std::to_string(plan.removed.size()) + " Notes";
    return plan;
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
    ChartNotesEditPlan plan;
    plan.label = (set_exact ? "Set Fret " : "Transpose to Fret ") + std::to_string(target);
    for (const common::core::ChartNote& note : base)
    {
        const int fret = set_exact ? target : note.fret + delta;
        if (fret < 0 || fret > common::core::g_max_fret)
        {
            return std::nullopt;
        }
        if (fret == note.fret)
        {
            continue;
        }
        plan.removed.push_back(note);
        common::core::ChartNote retyped = note;
        retyped.fret = fret;
        plan.inserted.push_back(std::move(retyped));
    }
    return plan;
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
        // The minimum-sustain-distance rule (settled 2026-07-18; override design deliberately
        // open): growing a tail clamps it to end at least the margin — 1/16 of a whole note —
        // BEFORE the next onset on ANY string, so extension can never crowd another note.
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
                    common::core::Fraction{signature.denominator, 16};
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
