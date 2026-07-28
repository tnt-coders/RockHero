#include "chart/chart_edits.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A 4/4 120 BPM default map: each measure is 2.0s, each beat 0.5s, matching the fixtures the
// controller-level chart tests build. Long enough that the planners' terminal-anchor arithmetic
// stays inside real grid.
[[nodiscard]] common::core::TempoMap makeTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
}

// Builds a note carrying only the fields the planners read, keeping the non-DMI position/bend/
// slides fields listed so -Wmissing-designated-field-initializers stays quiet.
[[nodiscard]] common::core::ChartNote makeNote(
    common::core::GridPosition position, int string, int fret, common::core::Fraction sustain = {})
{
    return common::core::ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .bend = {},
        .slides = {},
    };
}

// A six-string chart with a two-note onset at measure 2 beat 1 (strings 1 and 2) and a sustained
// note at measure 3 beat 1, mirroring the controller test's fixture. Notes are pre-sorted by
// (position, string) as every planner's diff assumes.
[[nodiscard]] common::core::Chart makeChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 2, .beat = 1}, 1, 3),
        makeNote({.measure = 2, .beat = 1}, 2, 5),
        makeNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2}),
    };
    return chart;
}

[[nodiscard]] ChartNoteKey keyAt(common::core::GridPosition position, int string)
{
    return ChartNoteKey{.position = position, .string = string};
}

// Finds the note on a (position, string) slot in a note list, or nullptr; used to inspect the
// removed/inserted sides of a plan without depending on their internal order.
[[nodiscard]] const common::core::ChartNote* noteAt(
    const std::vector<common::core::ChartNote>& notes, common::core::GridPosition position,
    int string)
{
    for (const common::core::ChartNote& note : notes)
    {
        if (note.position == position && note.string == string)
        {
            return &note;
        }
    }
    return nullptr;
}

} // namespace

// Placing a note on a free slot plans a pure insertion: nothing removed, the note inserted.
TEST_CASE("planInsertNote adds a note on an empty slot", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertNote(chart, tempo_map, makeNote({.measure = 4, .beat = 1}, 1, 5));
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.empty());
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote* added = noteAt(plan->inserted, {.measure = 4, .beat = 1}, 1);
        REQUIRE(added != nullptr);
        CHECK(added->fret == 5);
        CHECK(plan->label == "Insert Note");
    }
}

// Placing a note on an occupied slot replaces the note there: the old full value is removed and
// the new one inserted in one plan.
TEST_CASE("planInsertNote replaces a note on an occupied slot", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    // Slot measure 2 beat 1 / string 1 already holds fret 3.
    const auto plan = planInsertNote(chart, tempo_map, makeNote({.measure = 2, .beat = 1}, 1, 9));
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->removed.size() == 1);
        CHECK(plan->removed.front().fret == 3);
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted.front().fret == 9);
    }
}

// Re-placing a note identical to the one already on the slot changes nothing, so the plan is empty.
TEST_CASE("planInsertNote returns nullopt for an unchanged placement", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertNote(chart, tempo_map, chart.notes[0]);
    CHECK_FALSE(plan.has_value());
}

// 40-Q2-B: inserting on a string whose earlier note's sustain rings across the new onset truncates
// that sustain to end exactly at the onset, clipping payload points beyond the shortened tail.
TEST_CASE("planInsertNote truncates an overlapped sustain and clips its payload", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        common::core::ChartNote{
            .position = {.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = common::core::Fraction{2},
            .bend =
                {
                    common::core::BendPoint{
                        .offset = common::core::Fraction{1, 2}, .semitones = 0.5
                    },
                    common::core::BendPoint{
                        .offset = common::core::Fraction{3, 2}, .semitones = 1.0
                    },
                },
            .slides = {},
        },
    };
    const common::core::TempoMap tempo_map = makeTempoMap();

    // The new onset lands one beat into the two-beat sustain.
    const auto plan = planInsertNote(chart, tempo_map, makeNote({.measure = 1, .beat = 2}, 1, 7));
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // The earlier note is re-emitted with its sustain cut to the onset distance and the bend
        // point past the new tail dropped.
        REQUIRE(plan->removed.size() == 1);
        CHECK(plan->removed.front().sustain == common::core::Fraction{2});
        CHECK(plan->removed.front().bend.size() == 2);

        const common::core::ChartNote* truncated =
            noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(truncated != nullptr);
        CHECK(truncated->sustain == common::core::Fraction{1});
        REQUIRE(truncated->bend.size() == 1);
        CHECK(truncated->bend.front().offset == common::core::Fraction{1, 2});

        // The placed note is inserted alongside the truncated one.
        const common::core::ChartNote* placed =
            noteAt(plan->inserted, {.measure = 1, .beat = 2}, 1);
        REQUIRE(placed != nullptr);
        CHECK(placed->fret == 7);
    }
}

// Deleting matching keys removes their full values and labels with the plural count.
TEST_CASE("planDeleteNotes removes matching keys and labels the count", "[core][chart]")
{
    const common::core::Chart chart = makeChart();

    // Keys must arrive sorted (the binary-search precondition): the two measure-2 onset members.
    const std::vector<ChartNoteKey> pair{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };
    const auto plan = planDeleteNotes(chart, pair);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.size() == 2);
        CHECK(plan->inserted.empty());
        CHECK(plan->label == "Delete 2 Notes");
    }

    // A single key uses the singular label.
    const std::vector<ChartNoteKey> single{keyAt({.measure = 3, .beat = 1}, 1)};
    const auto single_plan = planDeleteNotes(chart, single);
    REQUIRE(single_plan.has_value());
    if (single_plan.has_value())
    {
        CHECK(single_plan->removed.size() == 1);
        CHECK(single_plan->label == "Delete Note");
    }
}

// A key matching no note deletes nothing, so the plan is empty.
TEST_CASE("planDeleteNotes returns nullopt when no key matches", "[core][chart]")
{
    const common::core::Chart chart = makeChart();

    const std::vector<ChartNoteKey> missing{keyAt({.measure = 5, .beat = 1}, 1)};
    CHECK_FALSE(planDeleteNotes(chart, missing).has_value());
}

// A move that would carry a note off either end of the string range is refused whole, never
// clamped onto the nearest lane.
TEST_CASE("planMoveNotes refuses a move off the fret neck", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    // String 1 shifted down one lane leaves the neck below string 1.
    CHECK_FALSE(planMoveNotes(chart, tempo_map, keys, common::core::Fraction{}, -1, "Move Notes")
                    .has_value());

    // Shifted up past the six-string range leaves the neck above string 6.
    CHECK_FALSE(planMoveNotes(chart, tempo_map, keys, common::core::Fraction{}, 6, "Move Notes")
                    .has_value());
}

// Two notes that both clamp to the grid origin under a large negative beat delta would stack on
// one slot; the move is refused rather than collapsing them.
TEST_CASE("planMoveNotes refuses origin-clamped converging moves", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 2}, 1, 0),
        makeNote({.measure = 1, .beat = 3}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1), keyAt({.measure = 1, .beat = 3}, 1)
    };

    // Both notes clamp to measure 1 beat 1 on string 1, colliding at the origin.
    CHECK_FALSE(planMoveNotes(chart, tempo_map, keys, common::core::Fraction{-10}, 0, "Move Notes")
                    .has_value());
}

// A move whose destination is already held by an unmoved note is refused.
TEST_CASE("planMoveNotes refuses landing on an unmoved note", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 0),
        makeNote({.measure = 1, .beat = 2}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    // The first note advanced one beat lands on the second, unmoved note's slot.
    CHECK_FALSE(planMoveNotes(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes")
                    .has_value());
}

// A move onto a free slot plans a removal of the origin and an insertion at the destination,
// carrying the label through.
TEST_CASE("planMoveNotes moves a note to a free slot", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto plan =
        planMoveNotes(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->removed.size() == 1);
        CHECK(noteAt(plan->removed, {.measure = 2, .beat = 1}, 1) != nullptr);
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote* moved = noteAt(plan->inserted, {.measure = 2, .beat = 2}, 1);
        REQUIRE(moved != nullptr);
        CHECK(moved->fret == 3);
        CHECK(plan->label == "Move Notes");
    }
}

// Empty keys, a zero delta, and keys that match nothing all plan no move.
TEST_CASE("planMoveNotes returns nullopt for no-op inputs", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    CHECK_FALSE(planMoveNotes(chart, tempo_map, {}, common::core::Fraction{1}, 0, "Move Notes")
                    .has_value());
    CHECK_FALSE(planMoveNotes(chart, tempo_map, keys, common::core::Fraction{}, 0, "Move Notes")
                    .has_value());

    // A key present in the request but absent from the chart moves nothing.
    const std::vector<ChartNoteKey> absent{keyAt({.measure = 9, .beat = 1}, 1)};
    CHECK_FALSE(planMoveNotes(chart, tempo_map, absent, common::core::Fraction{1}, 0, "Move Notes")
                    .has_value());
}

// Set-exact mode assigns the typed fret to every note in the snapshot.
TEST_CASE("planRetypeFrets sets an exact fret on every note", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    const auto plan = planRetypeFrets(base, 9, true);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.size() == 2);
        REQUIRE(plan->inserted.size() == 2);
        for (const common::core::ChartNote& note : plan->inserted)
        {
            CHECK(note.fret == 9);
        }
        CHECK(plan->label == "Set Fret 9");
    }
}

// Transpose mode shifts every note by the delta that lands the snapshot's lowest fret on the
// target, preserving the shape.
TEST_CASE("planRetypeFrets transposes from the lowest fret", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    // Lowest fret 3 to target 5 is a +2 shift: 3 to 5 and 5 to 7.
    const auto plan = planRetypeFrets(base, 5, false);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* low = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        REQUIRE(low != nullptr);
        CHECK(low->fret == 5);
        const common::core::ChartNote* high = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 2);
        REQUIRE(high != nullptr);
        CHECK(high->fret == 7);
        CHECK(plan->label == "Transpose to Fret 5");
    }
}

// A transposition that would push any member past the fret cap refuses the whole plan rather than
// clamping the offending note.
TEST_CASE("planRetypeFrets refuses to push a member past the fret cap", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{
        makeNote({.measure = 1, .beat = 1}, 1, 25), makeNote({.measure = 1, .beat = 1}, 2, 28)
    };

    // Lowest fret 25 to the cap is a +5 shift; the higher member reaches 33, past the cap.
    CHECK_FALSE(planRetypeFrets(base, common::core::g_max_fret, false).has_value());
}

// An empty snapshot has no anchor fret, so no plan is produced.
TEST_CASE("planRetypeFrets returns nullopt for an empty snapshot", "[core][chart]")
{
    CHECK_FALSE(planRetypeFrets({}, 5, false).has_value());
}

// Unlike the other planners, retype reports a populated-but-empty plan when the target already
// matches, rather than nullopt.
TEST_CASE("planRetypeFrets returns an empty plan when nothing changes", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{makeNote({.measure = 1, .beat = 1}, 1, 5)};

    const auto plan = planRetypeFrets(base, 5, true);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.empty());
        CHECK(plan->inserted.empty());
        CHECK(plan->label == "Set Fret 5");
    }
}

// Shrinking a sustain past zero floors it at zero rather than going negative.
TEST_CASE("planAdjustSustain floors a shrunk sustain at zero", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{1})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{-2});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* shrunk =
            noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(shrunk != nullptr);
        CHECK(shrunk->sustain == common::core::Fraction{});
        CHECK(plan->label == "Shrink Sustain");
    }
}

// Growing a sustain clamps it to end the minimum sustain distance before the next onset on any
// string; a same-onset chord member never blocks the growth.
TEST_CASE("planAdjustSustain clamps a grown sustain before the next onset", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 0),
        makeNote({.measure = 1, .beat = 1}, 2, 0),
        makeNote({.measure = 1, .beat = 3}, 2, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    // The next onset is two beats out on string 2; the margin in 4/4 is a quarter beat, so the
    // grown tail clamps to 7/4 beats even though ten beats were requested.
    const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{10});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* grown = noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(grown != nullptr);
        CHECK(grown->sustain == common::core::Fraction{7, 4});
        CHECK(plan->label == "Grow Sustain");
    }
}

// A tail already sitting at the clamp limit refuses to grow rather than being rewritten to the
// same value, so the plan is empty.
TEST_CASE("planAdjustSustain refuses to grow a tail already at the limit", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{7, 4}),
        makeNote({.measure = 1, .beat = 1}, 2, 0),
        makeNote({.measure = 1, .beat = 3}, 2, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    CHECK_FALSE(planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{1}).has_value());
}

// Empty keys and a zero delta both plan no sustain change.
TEST_CASE("planAdjustSustain returns nullopt for no-op inputs", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    CHECK_FALSE(planAdjustSustain(chart, tempo_map, {}, common::core::Fraction{1}).has_value());
    CHECK_FALSE(planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{}).has_value());
}

// Applying a removal-and-insertion whose preconditions hold swaps in the new stream.
TEST_CASE("applyChartNotesChange applies a removal and insertion", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    const std::vector<common::core::ChartNote> to_remove{chart.notes[0]};
    const std::vector<common::core::ChartNote> to_insert{makeNote({.measure = 4, .beat = 1}, 1, 2)};

    const auto result = applyChartNotesChange(chart, to_remove, to_insert);
    CHECK(result.has_value());
    CHECK(chart.notes.size() == 3);
    CHECK(noteAt(chart.notes, {.measure = 2, .beat = 1}, 1) == nullptr);
    const common::core::ChartNote* added = noteAt(chart.notes, {.measure = 4, .beat = 1}, 1);
    REQUIRE(added != nullptr);
    CHECK(added->fret == 2);
}

// A removal whose full value no longer matches the chart is rejected and the chart is left
// untouched.
TEST_CASE("applyChartNotesChange rejects a stale removal", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    const common::core::Chart original = chart;
    // The slot exists, but the fret no longer matches the recorded value.
    const std::vector<common::core::ChartNote> to_remove{makeNote(
        {.measure = 2, .beat = 1}, 1, 99)};

    const auto result = applyChartNotesChange(chart, to_remove, {});
    CHECK_FALSE(result.has_value());
    if (!result.has_value())
    {
        CHECK(result.error() == EditorUndoFailureCode::PreflightRejected);
    }
    CHECK(chart == original);
}

// A valid removal followed by an insertion that collides with a surviving note rejects the whole
// change: the chart is untouched, proving the preflight is atomic.
TEST_CASE("applyChartNotesChange rejects a colliding insertion atomically", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    const common::core::Chart original = chart;
    // Removing the measure-3 note would succeed on its own, but the insertion targets the still
    // occupied measure-2 / string-2 slot.
    const std::vector<common::core::ChartNote> to_remove{chart.notes[2]};
    const std::vector<common::core::ChartNote> to_insert{makeNote({.measure = 2, .beat = 1}, 2, 8)};

    const auto result = applyChartNotesChange(chart, to_remove, to_insert);
    CHECK_FALSE(result.has_value());
    if (!result.has_value())
    {
        CHECK(result.error() == EditorUndoFailureCode::PreflightRejected);
    }
    CHECK(chart == original);
}

} // namespace rock_hero::editor::core
