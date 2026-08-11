#include "chart/chart_edits.h"
#include "chart/legato_normalize.h"
#include "chart/pick_slide_defaults.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_document.h>
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

// A valid scrape: fret 9 start, one turnaround waypoint, and the required slide-out terminal
// exactly at the one-beat sustain, per the pick-slide invariants the planners must preserve.
[[nodiscard]] common::core::ChartNote makeScrape(common::core::GridPosition position, int string)
{
    common::core::ChartNote note = makeNote(position, string, 9, common::core::Fraction{1});
    note.attack = common::core::NoteAttack::PickSlide;
    note.slides = {common::core::SlideWaypoint{.offset = common::core::Fraction{1, 2}, .fret = 3}};
    note.slide_out = common::core::SlideOut{.offset = common::core::Fraction{1}, .fret = 12};
    return note;
}

// Applies a plan and asserts the whole-chart rules gate accepts the result — the planners'
// joint contract: a plan that applies and saves but cannot re-load is exactly the
// silent-corruption class the scrape work exists to close.
void applyAndValidate(
    common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartNotesEditPlan& plan)
{
    REQUIRE(applyChartNotesChange(chart, plan.removed, plan.inserted).has_value());
    CHECK(common::core::validateChartRules(chart, tempo_map).has_value());
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

// Import's whole contract, over every technique combination a source can hand us: a note the shed
// has reduced and the repair has settled always validates. Import is a commit point, so a note it
// cannot make legal takes the WHOLE song down — which has now happened three times, each time
// because a hand-kept list of what to shed had fallen behind the rules. An exhaustive sweep is what
// retires that: a new incompatibility with no shed clause fails here rather than on someone's
// import.
//
// The two exclusions are the cases neither pass owns. A pinch's missing node is data to supply
// rather than technique to remove (the importer defaults it to the octave), so pinches are given
// one here. A pick slide's payload is authored wholesale by the scrape defaults rather than reduced
// from a source's flags, and `test_pick_slide_defaults` covers that path.
TEST_CASE("the import shed and repair make every technique combination legal", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    int combinations = 0;
    int shed_or_repaired = 0;
    for (const common::core::NoteAttack attack :
         {common::core::NoteAttack::Pick,
          common::core::NoteAttack::Pinch,
          common::core::NoteAttack::Hammer,
          common::core::NoteAttack::Pull,
          common::core::NoteAttack::Tap})
    {
        for (const int fret : {0, 5})
        {
            for (const common::core::NoteMute mute :
                 {common::core::NoteMute::None,
                  common::core::NoteMute::Palm,
                  common::core::NoteMute::Full})
            {
                for (const bool node : {false, true})
                {
                    for (const bool vibrato : {false, true})
                    {
                        for (const bool tremolo : {false, true})
                        {
                            for (const bool bent : {false, true})
                            {
                                for (const bool slid : {false, true})
                                {
                                    common::core::Chart chart;
                                    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
                                    // A predecessor a measure earlier, holding through the onset
                                    // and stopped above it, so a pull-off has something real to
                                    // release and the repair's justified branch is reached rather
                                    // than always falling through to a plain pick.
                                    common::core::ChartNote subject = makeNote(
                                        {.measure = 2, .beat = 1},
                                        1,
                                        fret,
                                        common::core::Fraction{1});
                                    subject.attack = attack;
                                    subject.mute = mute;
                                    // A pinch must carry a node, and every node must lie beyond the
                                    // stop it speaks from.
                                    if (node || attack == common::core::NoteAttack::Pinch)
                                    {
                                        subject.harmonic_node = 12.0;
                                    }
                                    subject.vibrato = vibrato;
                                    subject.tremolo = tremolo;
                                    if (bent)
                                    {
                                        subject.bend = {
                                            common::core::BendPoint{
                                                .offset = common::core::Fraction{1, 2},
                                                .semitones = 1.0
                                            },
                                        };
                                    }
                                    if (slid)
                                    {
                                        subject.slides = {
                                            common::core::SlideWaypoint{
                                                .offset = common::core::Fraction{1, 2}, .fret = 9
                                            },
                                        };
                                    }
                                    chart.notes = {
                                        makeNote(
                                            {.measure = 1, .beat = 1},
                                            1,
                                            9,
                                            common::core::Fraction{4}),
                                        subject,
                                    };

                                    for (common::core::ChartNote& note : chart.notes)
                                    {
                                        note = common::core::executableChartNote(note);
                                    }
                                    static_cast<void>(
                                        normalizeChartLegato(chart.notes, chart.shapes, tempo_map));

                                    ++combinations;
                                    shed_or_repaired += chart.notes[1] == subject ? 0 : 1;
                                    CAPTURE(
                                        static_cast<int>(attack),
                                        fret,
                                        static_cast<int>(mute),
                                        node,
                                        vibrato,
                                        tremolo,
                                        bent,
                                        slid);
                                    CHECK(
                                        common::core::validateChartRules(chart, tempo_map)
                                            .has_value());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // The sweep really swept, and it really had work to do — a pass cannot come from a loop that
    // never ran or from combinations that were all legal to begin with.
    CHECK(combinations == 960);
    CHECK(shed_or_repaired > 100);
}

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
    const auto plan = planDeleteNotes(chart, makeTempoMap(), pair);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.size() == 2);
        CHECK(plan->inserted.empty());
        CHECK(plan->label == "Delete 2 Notes");
    }

    // A single key uses the singular label.
    const std::vector<ChartNoteKey> single{keyAt({.measure = 3, .beat = 1}, 1)};
    const auto single_plan = planDeleteNotes(chart, makeTempoMap(), single);
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
    CHECK_FALSE(planDeleteNotes(chart, makeTempoMap(), missing).has_value());
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

    const auto plan = planRetypeFrets(chart, makeTempoMap(), base, 9, true);
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
    const auto plan = planRetypeFrets(chart, makeTempoMap(), base, 5, false);
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
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    // Lowest fret 25 to the cap is a +5 shift; the higher member reaches 33, past the cap —
    // refused by the shared finalize gate, which replaced the old local caps.
    CHECK_FALSE(
        planRetypeFrets(chart, makeTempoMap(), base, common::core::g_max_fret, false).has_value());
}

// An empty snapshot has no anchor fret, so no plan is produced.
TEST_CASE("planRetypeFrets returns nullopt for an empty snapshot", "[core][chart]")
{
    CHECK_FALSE(planRetypeFrets(makeChart(), makeTempoMap(), {}, 5, false).has_value());
}

// A target already matching plans nothing, like every planner since the shared finalize took
// over the diff.
TEST_CASE("planRetypeFrets returns nullopt when nothing changes", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{makeNote({.measure = 1, .beat = 1}, 1, 5)};
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    CHECK_FALSE(planRetypeFrets(chart, makeTempoMap(), base, 5, true).has_value());
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

// Entering the pick-slide attack keeps the note's fret as the scrape start, synthesizes the
// default path ending exactly at the sustain, and leaves the overridden techniques in memory —
// the chart contract that makes toggle-back restoration work.
TEST_CASE("planSetAttack enters a pick slide keeping fret and latent techniques", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    common::core::ChartNote& note = chart.notes[2]; // measure 3 / string 1, fret 7, sustain 2
    note.tremolo = true;
    note.mute = common::core::NoteMute::Palm;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        CHECK(scrape->attack == common::core::NoteAttack::PickSlide);
        CHECK(scrape->fret == 7);
        // Fret 7 sits in the neck's lower half, so the default travels upward to the high end.
        CHECK(scrape->slides.empty());
        REQUIRE(scrape->slide_out.has_value());
        if (scrape->slide_out.has_value())
        {
            CHECK(scrape->slide_out->fret == g_pick_slide_default_high_fret);
            CHECK(scrape->slide_out->offset == scrape->sustain);
        }
        // The overridden techniques stay in memory, untouched.
        CHECK(scrape->tremolo);
        CHECK(scrape->mute == common::core::NoteMute::Palm);
        CHECK(plan->label == "Pick Slide");
        // The latents are legal in memory but the rules gate binds documents, so the oracle
        // is the SAVED form: the writer omits the overrides and the reparse passes clean.
        REQUIRE(applyChartNotesChange(chart, plan->removed, plan->inserted).has_value());
        const auto saved = common::core::parseChartDocument(common::core::chartDocumentText(chart));
        REQUIRE(saved.has_value());
        CHECK(common::core::validateChartRules(*saved, tempo_map).has_value());
    }
}

// The eligible-subset skip covers EVERY per-note rule, not a hand-picked pair of them. It used to
// copy two of the validator's predicates, so a note the target attack broke some other rule on was
// not skipped — and the whole-stream gate then refused the plan for every note in the selection,
// not just that one. Here a fully muted note cannot become a pinch (a dead note sounds no pitch, so
// it carries no harmonic, and a pinch must carry a node), which neither copied predicate named.
TEST_CASE("planSetAttack skips a note any per-note rule refuses", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[0].mute = common::core::NoteMute::Full; // measure 2 / string 1, fret 3
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1),
        keyAt({.measure = 2, .beat = 1}, 2),
    };

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pinch, "Pinch Harmonic");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // The unmuted partner converts; the dead note is left exactly as it was.
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted.front().string == 2);
        CHECK(plan->inserted.front().attack == common::core::NoteAttack::Pinch);
        CHECK(noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1) == nullptr);
        applyAndValidate(chart, tempo_map, *plan);
    }
}

// A zero-sustain note first extends to the minimum gesture window so the path can travel.
TEST_CASE("planSetAttack extends a zero sustain to the minimum window", "[core][chart]")
{
    const common::core::Chart chart = makeChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        CHECK(scrape->sustain == g_minimum_slide_window);
        REQUIRE(scrape->slide_out.has_value());
        if (scrape->slide_out.has_value())
        {
            CHECK(scrape->slide_out->offset == g_minimum_slide_window);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// A start in the neck's upper half scrapes downward to the low default endpoint.
TEST_CASE("planSetAttack scrapes downward from the neck's upper half", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[2].fret = 14;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        REQUIRE(scrape->slide_out.has_value());
        if (scrape->slide_out.has_value())
        {
            CHECK(scrape->slide_out->fret == g_pick_slide_default_low_fret);
        }
    }
}

// Toggling the attack in and back out restores the note field-for-field: the latents were never
// touched, the path clears on exit, and fret and sustain survive the round trip.
TEST_CASE("planSetAttack round-trips a toggled note exactly", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[2].tremolo = true;
    chart.notes[2].vibrato = true;
    const common::core::ChartNote original = chart.notes[2];
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto enter =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    REQUIRE(applyChartNotesChange(chart, enter->removed, enter->inserted).has_value());

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide");
    REQUIRE(exit.has_value());
    if (!exit.has_value())
    {
        return;
    }
    REQUIRE(applyChartNotesChange(chart, exit->removed, exit->inserted).has_value());

    const common::core::ChartNote* restored = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(restored != nullptr);
    CHECK(*restored == original);
}

// Keyed notes already carrying the attack plan nothing.
TEST_CASE("planSetAttack returns nullopt when nothing changes", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    CHECK_FALSE(
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .has_value());
    CHECK_FALSE(
        planSetAttack(chart, tempo_map, {}, common::core::NoteAttack::Pick, "Pick").has_value());
}

// A scrape's path translates with its start under retype, preserving the gesture's travel; a
// path fret pushed past the neck refuses the whole plan exactly like a member fret.
TEST_CASE("planRetypeFrets translates a scrape's path with its start", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{makeScrape({.measure = 1, .beat = 1}, 1)};

    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    const auto plan = planRetypeFrets(chart, makeTempoMap(), base, 11, /*set_exact=*/false);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote& retyped = plan->inserted.front();
        CHECK(retyped.fret == 11);
        REQUIRE(retyped.slides.size() == 1);
        CHECK(retyped.slides[0].fret == 5);
        REQUIRE(retyped.slide_out.has_value());
        if (retyped.slide_out.has_value())
        {
            CHECK(retyped.slide_out->fret == 14);
        }

        common::core::Chart applied = chart;
        applyAndValidate(applied, makeTempoMap(), *plan);
    }

    // Transposing to 28 lands the start inside the neck but pushes the terminal's 12 to 31,
    // which the finalize gate refuses.
    CHECK_FALSE(planRetypeFrets(chart, makeTempoMap(), base, 28, /*set_exact=*/false).has_value());
}

// A sustain change re-terminates a scrape's path: shrink compresses the final point onto the
// new end, growth rides it out, and the sustain floors at the gesture window instead of zero.
TEST_CASE("planAdjustSustain re-terminates a scrape's path", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    SECTION("shrink compresses the terminal onto the new end")
    {
        const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{-1, 4});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::Fraction{3, 4});
            REQUIRE(scrape->slides.size() == 1);
            CHECK(scrape->slides[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->slides[0].fret == 3);
            REQUIRE(scrape->slide_out.has_value());
            if (scrape->slide_out.has_value())
            {
                CHECK(scrape->slide_out->offset == common::core::Fraction{3, 4});
                CHECK(scrape->slide_out->fret == 12);
            }
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }

    SECTION("growth rides the terminal out to the new end")
    {
        const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{1, 2});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::Fraction{3, 2});
            REQUIRE(scrape->slide_out.has_value());
            if (scrape->slide_out.has_value())
            {
                CHECK(scrape->slide_out->offset == common::core::Fraction{3, 2});
                CHECK(scrape->slide_out->fret == 12);
            }
        }
    }

    SECTION("shrink floors at the minimum gesture window")
    {
        const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{-10});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == g_minimum_slide_window);
            // The turnaround no longer fits inside the floored window; the terminal alone rides.
            CHECK(scrape->slides.empty());
            REQUIRE(scrape->slide_out.has_value());
            if (scrape->slide_out.has_value())
            {
                CHECK(scrape->slide_out->offset == g_minimum_slide_window);
                CHECK(scrape->slide_out->fret == 12);
            }
        }
    }
}

// When compression would land the terminal fret on its new predecessor, the nearest earlier
// differing fret takes over so the path keeps traveling.
TEST_CASE("planAdjustSustain keeps a compressed scrape traveling", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    common::core::ChartNote scrape = makeScrape({.measure = 3, .beat = 1}, 1);
    // 9 -> 3 -> 12 -> 3: valid travel whose terminal fret equals the first surviving leg's.
    scrape.slides = {
        common::core::SlideWaypoint{.offset = common::core::Fraction{1, 4}, .fret = 3},
        common::core::SlideWaypoint{.offset = common::core::Fraction{1, 2}, .fret = 12},
    };
    scrape.slide_out = common::core::SlideOut{.offset = common::core::Fraction{1}, .fret = 3};
    chart.notes[2] = scrape;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    // Shrink to 3/8: only the first turnaround survives, and the terminal fret 3 would sit
    // still against it, so the earlier differing fret 12 terminates instead.
    const auto plan = planAdjustSustain(chart, tempo_map, keys, common::core::Fraction{-5, 8});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* shrunk =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(shrunk != nullptr);
        CHECK(shrunk->sustain == common::core::Fraction{3, 8});
        REQUIRE(shrunk->slides.size() == 1);
        CHECK(shrunk->slides[0].offset == common::core::Fraction{1, 4});
        CHECK(shrunk->slides[0].fret == 3);
        REQUIRE(shrunk->slide_out.has_value());
        if (shrunk->slide_out.has_value())
        {
            CHECK(shrunk->slide_out->offset == common::core::Fraction{3, 8});
            CHECK(shrunk->slide_out->fret == 12);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// The 40-Q2-B truncation a later insert forces re-terminates a scrape's path the same way, so
// an edit near a scrape can never leave an invalid chart behind.
TEST_CASE("planInsertNote truncation re-terminates a scrape", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
    const common::core::TempoMap tempo_map = makeTempoMap();

    // A new onset half a beat into the scrape truncates its sustain to exact adjacency.
    const auto plan = planInsertNote(
        chart, tempo_map, makeNote({.measure = 1, .beat = 1, .offset = {1, 2}}, 1, 5));
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* truncated =
            noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(truncated != nullptr);
        CHECK(truncated->sustain == common::core::Fraction{1, 2});
        REQUIRE(truncated->slide_out.has_value());
        // Travel survives: consecutive neck positions still strictly differ through the
        // terminal.
        int previous_fret = truncated->fret;
        for (const common::core::SlideWaypoint& waypoint : truncated->slides)
        {
            CHECK(waypoint.fret != previous_fret);
            previous_fret = waypoint.fret;
        }
        if (truncated->slide_out.has_value())
        {
            CHECK(truncated->slide_out->offset == truncated->sustain);
            CHECK(truncated->slide_out->fret != previous_fret);
        }
        // The terminal lands exactly ON the inserted onset — structurally legal, since the
        // waypoint-on-onset rule never sees a slide-out; the whole-chart gate is the oracle
        // that the applied chart can be re-read.
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// Entering a pick slide REPLACES a pitched glide with the scrape path: `slides` is the path's
// own storage, definitionally outside the latent contract, so toggling back clears the path
// rather than resurrecting the glide — undo is the recovery. Pinned so the asymmetry with the
// technique latents stays deliberate.
TEST_CASE("planSetAttack replaces a pitched glide and does not restore it", "[core][chart]")
{
    common::core::Chart chart = makeChart();
    chart.notes[2].tremolo = true;
    chart.notes[2].slides = {
        common::core::SlideWaypoint{.offset = common::core::Fraction{1, 2}, .fret = 9}
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto enter =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    // The tremolo latent makes the in-memory chart deliberately dirty, so validate the saved
    // form rather than the raw stream.
    REQUIRE(applyChartNotesChange(chart, enter->removed, enter->inserted).has_value());
    const auto saved = common::core::parseChartDocument(common::core::chartDocumentText(chart));
    REQUIRE(saved.has_value());
    CHECK(common::core::validateChartRules(*saved, tempo_map).has_value());
    const common::core::ChartNote* scrape = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(scrape != nullptr);
    CHECK(scrape->slides.empty());
    REQUIRE(scrape->slide_out.has_value());
    if (scrape->slide_out.has_value())
    {
        CHECK(scrape->slide_out->fret == g_pick_slide_default_high_fret);
    }

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide");
    REQUIRE(exit.has_value());
    if (!exit.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *exit);
    const common::core::ChartNote* restored = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(restored != nullptr);
    CHECK(restored->tremolo);
    CHECK(restored->slides.empty());
    CHECK_FALSE(restored->slide_out.has_value());
}

// The legato direction is derived from the previous note on the SAME string, never authored, so one
// verb covers hammer-on and pull-off and the two can never disagree with the fret data.
TEST_CASE("planSetLegato derives hammer versus pull from the previous fret", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // String 1 climbs 3 -> 7: the 7 is hammered onto.
        makeNote({.measure = 1, .beat = 1}, 1, 3),
        makeNote({.measure = 1, .beat = 2}, 1, 7),
        // String 2 falls 9 -> 5: the 5 is pulled off to.
        makeNote({.measure = 1, .beat = 3}, 2, 9),
        makeNote({.measure = 1, .beat = 4}, 2, 5),
    };
    // One-beat gaps sit at the kept-sustain bound, so the predecessors hold their tails to the
    // margin — the connection the derivation requires there.
    chart.notes[0].sustain = common::core::Fraction{3, 4};
    chart.notes[2].sustain = common::core::Fraction{3, 4};

    const std::vector<ChartNoteKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1),
        keyAt({.measure = 1, .beat = 4}, 2),
    };
    const auto plan = planSetLegato(chart, tempo_map, keys, "Legato");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 2);
        // Insertions stay in chart order: the string-1 climb first, then the string-2 fall.
        CHECK(plan->inserted[0].string == 1);
        CHECK(plan->inserted[0].attack == common::core::NoteAttack::Hammer);
        CHECK(plan->inserted[1].string == 2);
        CHECK(plan->inserted[1].attack == common::core::NoteAttack::Pull);
    }
}

// A direction that is not derivable is refused rather than guessed: the editor never invents a fact
// the chart does not carry.
TEST_CASE("planSetLegato refuses a direction it cannot derive", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("no earlier note on the string")
    {
        // The only note on string 1 has nothing to come from. A note on ANOTHER string earlier in
        // time must not stand in for it — you cannot hammer on from a different string.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 2, 5),
            makeNote({.measure = 1, .beat = 2}, 1, 7),
        };
        CHECK_FALSE(planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato")
                        .has_value());
    }

    SECTION("the earlier note sits at the same fret")
    {
        // Neither hammered nor pulled: the fret does not move, so there is no direction to record.
        // The predecessor holds its tail so the refusal is the equal fret, not the hold test.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 7),
            makeNote({.measure = 1, .beat = 2}, 1, 7),
        };
        chart.notes[0].sustain = common::core::Fraction{3, 4};
        CHECK_FALSE(planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato")
                        .has_value());
    }

    SECTION("the earlier note provably released before this one")
    {
        // A bare predecessor at the kept-sustain bound is a proven release: a held note would
        // carry a tail reaching the margin. Dragging the tail there is how legato across the
        // gap is authored.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 3),
            makeNote({.measure = 1, .beat = 3}, 1, 7),
        };
        CHECK_FALSE(planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato")
                        .has_value());

        chart.notes[0].sustain = common::core::Fraction{7, 4};
        const auto plan =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted[0].attack == common::core::NoteAttack::Hammer);
        }
    }

    SECTION("the earlier note is a fret-hand harmonic")
    {
        // A touch holds nothing to hand over, so neither direction is derivable across it (E19
        // says as much for the pull-off). The verb used to ask this only of the note it was
        // changing, never of the predecessor, and was safe purely because `releasedFret` happens
        // to report 0 for a fret-hand harmonic — an accident, not the rule.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{3, 4}),
            makeNote({.measure = 1, .beat = 2}, 1, 7),
        };
        chart.notes[0].harmonic_node = 12.0;
        CHECK_FALSE(planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato")
                        .has_value());
    }
}

// A note's node comes along only where it keeps meaning the same thing. The verb used to send it
// away on a pinch or a tap attack and keep it otherwise, which asked about the wrong thing: what
// matters is whether the DERIVED attack changes who owns the node, or forbids one outright.
TEST_CASE("planSetLegato keeps a picking-hand node only where it survives", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a hammer-on onto a stopped harmonic keeps its node")
    {
        // Fret 9 under a node at 21 is the tapped-harmonic gesture: the fretting hand presses the
        // stop while the picking hand keeps damping the node, so the hammer-on changes neither.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{3, 4}),
            makeNote({.measure = 1, .beat = 2}, 1, 9),
        };
        chart.notes[1].harmonic_node = 21.0;
        const auto plan =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted[0].attack == common::core::NoteAttack::Hammer);
            CHECK(plan->inserted[0].harmonic_node.has_value());
        }
    }

    SECTION("a pull-off sends any node away")
    {
        // A pull-off releases onto a plain stopped pitch and can sound no harmonic at any fret.
        // Keeping the node here left the verb silently inert: the repair inside the gate turned the
        // whole thing back into a plain pick, so pressing H on a stopped harmonic did nothing.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{3, 4}),
            makeNote({.measure = 1, .beat = 2}, 1, 5),
        };
        chart.notes[1].harmonic_node = 17.0;
        const auto plan =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted[0].attack == common::core::NoteAttack::Pull);
            CHECK_FALSE(plan->inserted[0].harmonic_node.has_value());
        }
    }

    SECTION("an open string sends its node away, because the hand owning it would change")
    {
        // On fret 0 the same number re-reads as a fret-hand node — a different technique — so a
        // tap harmonic's node cannot follow the note into a hammer-on.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{3, 4}),
            makeNote({.measure = 1, .beat = 2}, 1, 0),
        };
        chart.notes[1].attack = common::core::NoteAttack::Tap;
        chart.notes[1].harmonic_node = 12.0;
        const auto plan =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        // Fret 0 against a fret-0 predecessor releases nothing above or below, so there is no
        // direction to derive and the verb leaves the note alone rather than stripping its node.
        CHECK_FALSE(plan.has_value());
    }
}

// Mixed selections edit what they can and leave the rest, rather than refusing wholesale — the
// mixed-validity policy's "apply where valid".
TEST_CASE("planSetLegato applies to the derivable subset of a selection", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 3),
        makeNote({.measure = 1, .beat = 2}, 1, 7),
        // String 2's note is first on its string, so it has no derivable direction.
        makeNote({.measure = 1, .beat = 2}, 2, 4),
    };
    chart.notes[0].sustain = common::core::Fraction{3, 4};

    const std::vector<ChartNoteKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1),
        keyAt({.measure = 1, .beat = 2}, 2),
    };
    const auto plan = planSetLegato(chart, tempo_map, keys, "Legato");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // Only the derivable note changes; the other keeps its plain
        // pick and stays out of the plan.
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted[0].string == 1);
        CHECK(plan->inserted[0].attack == common::core::NoteAttack::Hammer);
    }
}

// A scrape's path is gesture geometry; retyping the attack drops it exactly as planSetAttack does,
// because a pitched glide reading of it would be a fiction.
TEST_CASE("planSetLegato drops a scrape's path with the attack", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 3),
        makeScrape({.measure = 1, .beat = 2}, 1),
    };
    chart.notes[0].sustain = common::core::Fraction{3, 4};
    REQUIRE_FALSE(chart.notes[1].slides.empty());

    const auto plan =
        planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        // The scrape starts at fret 9 above the fret-3 note, so it hammers on.
        CHECK(plan->inserted[0].attack == common::core::NoteAttack::Hammer);
        CHECK(plan->inserted[0].slides.empty());
    }
}

// Disconnecting a tail repairs the legato it justified, inside the same plan: the shared
// finalize runs the legato repair after the sustain change, so a pull whose predecessor no
// longer reaches it returns to a plain pick in the same undo entry.
TEST_CASE("planAdjustSustain repairs legato its shrink disconnects", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 9),
        makeNote({.measure = 1, .beat = 3}, 1, 5),
    };
    chart.notes[0].sustain = common::core::Fraction{7, 4};
    chart.notes[1].attack = common::core::NoteAttack::Pull;

    const auto plan = planAdjustSustain(
        chart, tempo_map, {keyAt({.measure = 1, .beat = 1}, 1)}, common::core::Fraction{-1});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 2);
        CHECK(plan->inserted[0].sustain == common::core::Fraction{3, 4});
        CHECK(plan->inserted[1].attack == common::core::NoteAttack::Pick);
    }
}

// E4's landing requirement binds Tap exactly as it binds Hammer, so the repair must cover both:
// a Tap on an open string with no node is not a tap at all. A junk `Tapped` flag is real Guitar
// Pro data, and before this the stream reached validation unrepaired and failed the whole import.
TEST_CASE("the repair gives a strikeless tap and hammer somewhere to land", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNoteKey> nothing{};

    // A tap with nowhere to strike becomes a plain pick — never a pull, which would invent
    // legato out of a picking-hand articulation even though a higher predecessor sits behind it.
    common::core::Chart tapped;
    tapped.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    tapped.notes = {
        makeNote({.measure = 1, .beat = 1}, 1, 9),
        makeNote({.measure = 1, .beat = 2}, 1, 0),
    };
    tapped.notes[0].sustain = common::core::Fraction{3, 4};
    tapped.notes[1].attack = common::core::NoteAttack::Tap;
    // Deleting nothing still funnels through the shared finalize, which is where the repair runs.
    const auto tap_plan = planDeleteNotes(tapped, tempo_map, nothing);
    CHECK_FALSE(tap_plan.has_value()); // nothing to delete, so no plan — the chart is untouched

    // Through a real edit: retyping the predecessor leaves the strikeless tap in the stream, and
    // the gate would refuse it if the repair had not converted it first.
    const auto retyped =
        planRetypeFrets(tapped, tempo_map, {tapped.notes[0]}, 7, /*set_exact=*/true);
    REQUIRE(retyped.has_value());
    if (retyped.has_value())
    {
        const auto struck = std::ranges::find_if(
            retyped->inserted, [](const common::core::ChartNote& note) { return note.fret == 0; });
        REQUIRE(struck != retyped->inserted.end());
        if (struck != retyped->inserted.end())
        {
            CHECK(struck->attack == common::core::NoteAttack::Pick);
        }
    }

    // The hammer keeps its rescue: a still-held higher predecessor makes it the pull-off the
    // frets support, which is the one asymmetry between the two attacks.
    common::core::Chart hammered = tapped;
    hammered.notes[1].attack = common::core::NoteAttack::Hammer;
    const auto hammer_plan =
        planRetypeFrets(hammered, tempo_map, {hammered.notes[0]}, 7, /*set_exact=*/true);
    REQUIRE(hammer_plan.has_value());
    if (hammer_plan.has_value())
    {
        const auto struck =
            std::ranges::find_if(hammer_plan->inserted, [](const common::core::ChartNote& note) {
                return note.fret == 0;
            });
        REQUIRE(struck != hammer_plan->inserted.end());
        if (struck != hammer_plan->inserted.end())
        {
            CHECK(struck->attack == common::core::NoteAttack::Pull);
        }
    }
}

} // namespace rock_hero::editor::core
