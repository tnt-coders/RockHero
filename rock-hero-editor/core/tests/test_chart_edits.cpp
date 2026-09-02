#include "chart/chart_edits.h"
#include "chart/pick_slide_defaults.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <utility>
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

[[nodiscard]] ChartSlotKey keyAt(common::core::GridPosition position, int string)
{
    return ChartSlotKey{.position = position, .string = string};
}

// The grids the duration-gesture scenarios step on. Against the 4/4 map above a quarter-note grid
// puts one line on every beat and a sixteenth-note grid one every quarter of a beat.
constexpr common::core::Fraction g_quarter_grid{1, 4};
constexpr common::core::Fraction g_sixteenth_grid{1, 16};

// One recorded step of a duration gesture, snapping onto a note value's lines. The tick spelling is
// the same step against the quantum snap-off leaves, named so a scenario's step list reads as the
// run of presses it stands for.
[[nodiscard]] ChartSustainStep gridStep(common::core::Fraction note_value, bool grow)
{
    return ChartSustainStep{.note_value = note_value, .grow = grow};
}

[[nodiscard]] ChartSustainStep tickStep(bool grow)
{
    return ChartSustainStep{.note_value = g_tick_quantum_note_value, .grow = grow};
}

// A valid scrape: fret 9 start, one turnaround keyframe, and the required slide-out terminal
// exactly at the one-beat sustain, per the pick-slide invariants the planners must preserve.
[[nodiscard]] common::core::ChartNote makeScrape(common::core::GridPosition position, int string)
{
    common::core::ChartNote note = makeTestNote(position, string, 9, common::core::Fraction{1});
    note.attack = common::core::NoteAttack::PickSlide;
    note.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 3}};
    note.slide_out = 12;
    return note;
}

// Applies a plan and asserts the whole-chart rules gate accepts the result — the planners'
// joint contract: a plan that applies and saves but cannot re-load is exactly the
// silent-corruption class the scrape work exists to close.
void applyAndValidate(
    common::core::Chart& chart, const common::core::TempoMap& tempo_map, const ChartEditPlan& plan)
{
    REQUIRE(applyChartChange(chart, plan).has_value());
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

// One keyframe's selection key: the note's slot plus the offset along that note's ring. The
// offset and not an index, because that is the identity the selection carries and the planners
// resolve against — an index would name a different keyframe after any edit that dropped one.
[[nodiscard]] ChartKeyframeKey keyframeKeyAt(
    common::core::GridPosition position, int string, common::core::Fraction offset)
{
    return ChartKeyframeKey{.note = keyAt(position, string), .offset = offset};
}

// A pitched glide over a four-beat ring: fret 7 from the onset, arriving at fret 9 two beats in
// and at fret 12 exactly where the ring ends. One note, so a plan's whole effect on the stream
// is readable without hunting for the record it touched.
[[nodiscard]] common::core::Chart makeGlideChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 1, 7, common::core::Fraction{4});
    glide.keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 9},
        common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 12},
    };
    chart.notes = {std::move(glide)};
    return chart;
}

// The glide chart's own note slot, which every keyframe key below rides.
[[nodiscard]] common::core::GridPosition glideOnset()
{
    return common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}};
}

// THE DERIVED-HELD FIGURE. A tap on string 1 at measure 2 beat 1 sounds fret 12 and rings a beat,
// with a fretting stop under it; the note a beat later on that string states fret 5, and where it
// CLAIMS legato that resolves to a pull — so the notation itself says the hand was waiting on 5.
// String 2 rings through underneath so the tap's claim reaches a real span: a strike-less claim
// beside a sounding string is the two-member opening, which is what keeps the inert sweep from
// clearing the stored field for a reason that has nothing to do with the derivation.
[[nodiscard]] common::core::Chart makeDerivedHeldChart(
    common::core::NoteAttack successor_attack, std::optional<int> stored_held)
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote tap =
        makeTestNote({.measure = 2, .beat = 1}, 1, 12, common::core::Fraction{1});
    tap.attack = common::core::NoteAttack::Tap;
    tap.held = stored_held;
    common::core::ChartNote drone =
        makeTestNote({.measure = 2, .beat = 1}, 2, 7, common::core::Fraction{4});
    common::core::ChartNote successor =
        makeTestNote({.measure = 2, .beat = 2}, 1, 5, common::core::Fraction{1});
    successor.attack = successor_attack;
    chart.notes = {std::move(tap), std::move(drone), std::move(successor)};
    return chart;
}

// The resolved stop each note claims, which is what every surface reads: the one authority the
// cases below check against rather than re-deriving what a pull-off states.
[[nodiscard]] std::vector<std::optional<int>> claimedStops(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map)
{
    return common::core::chartClaimedStops(common::core::chartConnections(chart.notes, tempo_map));
}

} // namespace

// Import's whole contract, over every technique combination a source can hand us: a note the shed
// has reduced, the strike gate has flattened, and the settle sweep has judged always validates.
// Import is a commit point, so a note it cannot make legal takes the WHOLE song down — which has
// happened three times now, each time because a hand-kept list of what to shed fell behind the
// rules. An exhaustive sweep is what retires that: a new incompatibility with no shed clause fails
// here rather than on someone's import.
//
// The two exclusions are the cases neither pass owns. A pinch's missing node is data to supply
// rather than technique to remove (the importer defaults it to the octave), so pinches are given
// one here. A pick slide's payload is authored wholesale by the scrape defaults rather than reduced
// from a source's flags, and `test_pick_slide_defaults` covers that path.
TEST_CASE("the import shed and settle make every technique combination legal", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    int combinations = 0;
    int shed_or_repaired = 0;
    for (const common::core::NoteAttack attack :
         {common::core::NoteAttack::Pick,
          common::core::NoteAttack::Pinch,
          common::core::NoteAttack::Legato,
          common::core::NoteAttack::LeftTap,
          common::core::NoteAttack::Tap,
          common::core::NoteAttack::Pop,
          common::core::NoteAttack::Slap})
    {
        for (const int fret : {0, 5})
        {
            // The two mutes are independent flags, so the sweep covers all FOUR combinations —
            // the both-muted note included, which no single mute axis could hand the shed.
            // Spelled as pairs rather than two nested loops to keep the nesting (and the column
            // budget) of the block below unchanged.
            for (const auto& [palm_mute, dead] : std::to_array<std::pair<bool, bool>>(
                     {{false, false}, {true, false}, {false, true}, {true, true}}))
            {
                for (const bool node : {false, true})
                {
                    // All THREE widths, not two: the wide tier goes through the same saved-form
                    // fixpoint as the ordinary one, and a shed that dropped only the narrow value
                    // would leave a wide shake on a scrape.
                    for (const common::core::VibratoState vibrato :
                         {common::core::VibratoState::Off,
                          common::core::VibratoState::Narrow,
                          common::core::VibratoState::Wide})
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
                                    // and stopped above it, so a connection claim has something
                                    // real to release from and the resolver's justified branch is
                                    // reached rather than the sweep flattening every claim.
                                    common::core::ChartNote subject = makeTestNote(
                                        {.measure = 2, .beat = 1},
                                        1,
                                        fret,
                                        common::core::Fraction{1});
                                    subject.attack = attack;
                                    subject.palm_mute = palm_mute;
                                    subject.dead = dead;
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
                                        subject.keyframes.push_back(
                                            common::core::Keyframe{
                                                .offset = common::core::Fraction{1, 2},
                                                .bend = 1.0,
                                            });
                                    }
                                    if (slid)
                                    {
                                        subject.keyframes = {
                                            common::core::Keyframe{
                                                .offset = common::core::Fraction{1, 2}, .fret = 9
                                            },
                                        };
                                    }
                                    chart.notes = {
                                        makeTestNote(
                                            {.measure = 1, .beat = 1},
                                            1,
                                            9,
                                            common::core::Fraction{4}),
                                        subject,
                                    };

                                    // Exactly the import's own step: the one normalizer, which
                                    // repairs what the note cannot execute and then settles the
                                    // claims the finished stream cannot justify.
                                    static_cast<void>(
                                        common::core::normalizeChart(chart, tempo_map));

                                    ++combinations;
                                    shed_or_repaired += chart.notes[1] == subject ? 0 : 1;
                                    CAPTURE(
                                        static_cast<int>(attack),
                                        fret,
                                        palm_mute,
                                        dead,
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
    // The matrix really ran, and it really had work to do — a pass cannot come from a loop that
    // never ran or from combinations that were all legal to begin with. Seven of the eight
    // attacks run (a fret-0 pop or slap with a node is a fret-hand harmonic, so their shed
    // clauses are as live as a pick's); only PickSlide sits out, whose payload
    // test_pick_slide_defaults owns. Half again as many since the vibrato axis grew its third
    // width: 1792 was the two-valued sweep.
    CHECK(combinations == 2688);
    CHECK(shed_or_repaired > 100);
}

// Placing a note on a free slot plans a pure insertion: nothing removed, the note inserted.
TEST_CASE("planInsertNote adds a note on an empty slot", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertNote(
        chart, tempo_map, makeTestNote({.measure = 4, .beat = 1}, 1, 5), g_fixture_sustain);
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
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    // Slot measure 2 beat 1 / string 1 already holds fret 3.
    const auto plan = planInsertNote(
        chart, tempo_map, makeTestNote({.measure = 2, .beat = 1}, 1, 9), g_fixture_sustain);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->removed.size() == 1);
        CHECK(plan->removed.front().fret == 3);
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted.front().fret == 9);
    }
}

// Every note rings, so a placement authors a duration: the session's grid step, clamped by the one
// bound on a ring (40-Q2-B) when the string is struck again sooner than that.
TEST_CASE("planInsertNote rings for the grid step, clamped at the next onset", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto uncrowded = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 4, .beat = 1}, 1, 5),
        common::core::Fraction{1, 2});
    REQUIRE(uncrowded.has_value());
    if (uncrowded.has_value())
    {
        REQUIRE(uncrowded->inserted.size() == 1);
        CHECK(uncrowded->inserted.front().sustain == common::core::Fraction{1, 2});
    }

    // The fixture's string-1 note at measure 3 beat 1 is struck a quarter beat after this slot, so
    // a half-beat default cannot ring through it: the finalize gate's normalization ends it there.
    const auto crowded = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 2, .beat = 4, .offset = {3, 4}}, 1, 5),
        common::core::Fraction{1, 2});
    REQUIRE(crowded.has_value());
    if (crowded.has_value())
    {
        const common::core::ChartNote* placed =
            noteAt(crowded->inserted, {.measure = 2, .beat = 4, .offset = {3, 4}}, 1);
        REQUIRE(placed != nullptr);
        CHECK(placed->sustain == common::core::Fraction{1, 4});
    }
}

// Re-placing a note identical to the one already on the slot changes nothing, so the plan is empty.
// The ring has to come from the DEFAULT to make that true, because the parameter overwrites the
// handed note's own sustain — so each case passes the occupant's ring, and the two-beat note is
// here so the case cannot pass merely because the fixture default happened to match.
TEST_CASE("planInsertNote returns nullopt for an unchanged placement", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    CHECK_FALSE(
        planInsertNote(chart, tempo_map, chart.notes[0], chart.notes[0].sustain).has_value());
    CHECK_FALSE(
        planInsertNote(chart, tempo_map, chart.notes[2], chart.notes[2].sustain).has_value());
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
            .keyframes = {
                common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .bend = 0.5},
                common::core::Keyframe{.offset = common::core::Fraction{3, 2}, .bend = 1.0},
            },
        },
    };
    const common::core::TempoMap tempo_map = makeTempoMap();

    // The new onset lands one beat into the two-beat sustain.
    const auto plan = planInsertNote(
        chart, tempo_map, makeTestNote({.measure = 1, .beat = 2}, 1, 7), g_fixture_sustain);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // The earlier note is re-emitted with its sustain cut to the onset distance and the bend
        // statement past the new tail dropped.
        REQUIRE(plan->removed.size() == 1);
        CHECK(plan->removed.front().sustain == common::core::Fraction{2});
        CHECK(plan->removed.front().keyframes.size() == 2);

        const common::core::ChartNote* truncated =
            noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(truncated != nullptr);
        CHECK(truncated->sustain == common::core::Fraction{1});
        REQUIRE(truncated->keyframes.size() == 1);
        CHECK(truncated->keyframes.front().offset == common::core::Fraction{1, 2});

        // The placed note is inserted alongside the truncated one.
        const common::core::ChartNote* placed =
            noteAt(plan->inserted, {.measure = 1, .beat = 2}, 1);
        REQUIRE(placed != nullptr);
        CHECK(placed->fret == 7);
    }
}

// Deleting matching keys removes their full values and labels with the plural count.
TEST_CASE("planDeleteSelection removes matching keys and labels the count", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();

    // Keys must arrive sorted (the binary-search precondition): the two measure-2 onset members.
    const std::vector<ChartSlotKey> pair{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };
    const auto plan = planDeleteSelection(chart, makeTempoMap(), pair, {});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->removed.size() == 2);
        CHECK(plan->inserted.empty());
        CHECK(plan->label == "Delete 2 Notes");
    }

    // A single key uses the singular label.
    const std::vector<ChartSlotKey> single{keyAt({.measure = 3, .beat = 1}, 1)};
    const auto single_plan = planDeleteSelection(chart, makeTempoMap(), single, {});
    REQUIRE(single_plan.has_value());
    if (single_plan.has_value())
    {
        CHECK(single_plan->removed.size() == 1);
        CHECK(single_plan->label == "Delete Note");
    }
}

// A key matching no note deletes nothing, so the plan is empty.
TEST_CASE("planDeleteSelection returns nullopt when no key matches", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();

    const std::vector<ChartSlotKey> missing{keyAt({.measure = 5, .beat = 1}, 1)};
    CHECK_FALSE(planDeleteSelection(chart, makeTempoMap(), missing, {}).has_value());
}

// A move that would carry a note off either end of the string range is refused whole, never
// clamped onto the nearest lane.
TEST_CASE("planMoveSelection refuses a move off the fret neck", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    // String 1 shifted down one lane leaves the neck below string 1.
    CHECK_FALSE(
        planMoveSelection(chart, tempo_map, keys, common::core::Fraction{}, -1, "Move Notes")
            .has_value());

    // Shifted up past the six-string range leaves the neck above string 6.
    CHECK_FALSE(planMoveSelection(chart, tempo_map, keys, common::core::Fraction{}, 6, "Move Notes")
                    .has_value());
}

// A move that would leave the grid's start is refused outright, never clamped: the grid arithmetic
// clamps at measure 1 beat 1, and a LONE note used to be silently repositioned there (only a
// converging pair was caught, by colliding at the origin).
TEST_CASE("planMoveSelection refuses a move off the grid's start", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 2}, 1, 0),
        makeTestNote({.measure = 1, .beat = 3}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();

    // One note, two beats left of beat 2: clamping would land it on beat 1 as if it had moved one.
    CHECK_FALSE(planMoveSelection(
                    chart,
                    tempo_map,
                    {keyAt({.measure = 1, .beat = 2}, 1)},
                    common::core::Fraction{-2},
                    0,
                    "Move Notes")
                    .has_value());
    // The same note one beat left lands exactly on the origin, which is a legal destination.
    CHECK(planMoveSelection(
              chart,
              tempo_map,
              {keyAt({.measure = 1, .beat = 2}, 1)},
              common::core::Fraction{-1},
              0,
              "Move Notes")
              .has_value());

    // Two notes that would both clamp to the origin are refused too (they would also collide).
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1), keyAt({.measure = 1, .beat = 3}, 1)
    };
    CHECK_FALSE(
        planMoveSelection(chart, tempo_map, keys, common::core::Fraction{-10}, 0, "Move Notes")
            .has_value());
}

// A move whose destination is already held by an unmoved note is refused.
TEST_CASE("planMoveSelection refuses landing on an unmoved note", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 0),
        makeTestNote({.measure = 1, .beat = 2}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    // The first note advanced one beat lands on the second, unmoved note's slot.
    CHECK_FALSE(
        planMoveSelection(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes")
            .has_value());
}

// A move onto a free slot plans a removal of the origin and an insertion at the destination,
// carrying the label through.
TEST_CASE("planMoveSelection moves a note to a free slot", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto plan =
        planMoveSelection(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes");
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
TEST_CASE("planMoveSelection returns nullopt for no-op inputs", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    CHECK_FALSE(planMoveSelection(chart, tempo_map, {}, common::core::Fraction{1}, 0, "Move Notes")
                    .has_value());
    CHECK_FALSE(planMoveSelection(chart, tempo_map, keys, common::core::Fraction{}, 0, "Move Notes")
                    .has_value());

    // A key present in the request but absent from the chart moves nothing.
    const std::vector<ChartSlotKey> absent{keyAt({.measure = 9, .beat = 1}, 1)};
    CHECK_FALSE(
        planMoveSelection(chart, tempo_map, absent, common::core::Fraction{1}, 0, "Move Notes")
            .has_value());
}

// Set-exact mode assigns the typed fret to every note in the snapshot.
TEST_CASE("planRetypeFrets sets an exact fret on every note", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    const auto plan = planRetypeFrets(
        chart, makeTempoMap(), base, 9, true, common::core::ChartStopChannel::Sounding);
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
    const common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    // Lowest fret 3 to target 5 is a +2 shift: 3 to 5 and 5 to 7.
    const auto plan = planRetypeFrets(
        chart, makeTempoMap(), base, 5, false, common::core::ChartStopChannel::Sounding);
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
        makeTestNote({.measure = 1, .beat = 1}, 1, 25),
        makeTestNote({.measure = 1, .beat = 1}, 2, 28)
    };
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    // Lowest fret 25 to the cap is a +5 shift; the higher member reaches 33, past the cap —
    // refused by the shared finalize gate, which replaced the old local caps. The kind matters:
    // this is Invalid, the emptiness a pending entry paints red.
    const auto plan = planRetypeFrets(
        chart,
        makeTempoMap(),
        base,
        common::core::g_max_fret,
        false,
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::Invalid);
}

// An empty snapshot has no anchor fret, so no plan is produced — and that emptiness is a
// NoChange, not a refusal: there was nothing to edit, so nothing was disallowed.
TEST_CASE("planRetypeFrets reports NoChange for an empty snapshot", "[core][chart]")
{
    const auto plan = planRetypeFrets(
        makeTestChart(), makeTempoMap(), {}, 5, false, common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// A target already matching plans nothing, like every planner since the shared finalize took
// over the diff — and it reports NoChange, never Invalid: a valid no-op must not read as a
// refusal, or the pending entry would paint an already-correct value red.
TEST_CASE("planRetypeFrets reports NoChange when nothing changes", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{makeTestNote({.measure = 1, .beat = 1}, 1, 5)};
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    const auto plan = planRetypeFrets(
        chart, makeTempoMap(), base, 5, true, common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// Every note rings, so there is no empty ring to shrink to: a note the replay takes to zero keeps
// the ring it currently has, and the rest of the selection still moves.
TEST_CASE("planAdjustSustain holds a ring the steps would empty", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 0, common::core::Fraction{3}),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };
    const std::vector<ChartSustainStep> steps{
        gridStep(g_quarter_grid, false), gridStep(g_quarter_grid, false)
    };

    const auto plan = planAdjustSustain(chart, tempo_map, chart.notes, keys, steps);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // The one-beat ring's end reaches its own onset on the first step and would pass it on the
        // second, so it is left exactly as it was; deleting the note is the verb for removing it.
        CHECK(noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1) == nullptr);
        const common::core::ChartNote* shrunk =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 2);
        REQUIRE(shrunk != nullptr);
        CHECK(shrunk->sustain == common::core::Fraction{1});
        CHECK(plan->label == "Shrink Sustain");
    }

    // With nothing left that can shrink, the press changes nothing at all: NoChange, the same
    // answer every planner gives for an empty diff.
    const std::vector<ChartSlotKey> only_short{keyAt({.measure = 2, .beat = 1}, 1)};
    const auto unchanged = planAdjustSustain(chart, tempo_map, chart.notes, only_short, steps);
    REQUIRE_FALSE(unchanged.has_value());
    CHECK(unchanged.error() == ChartPlanRefusal::NoChange);
}

// The bug the step list exists for (user 2026-08-23): a GRID step moves the ring's END onto the
// adjacent grid line, so a ring a tick step left between lines snaps onto the grid — ceiling
// when growing, flooring when shrinking — instead of carrying its remainder forever, which is what
// a summed beat delta did.
TEST_CASE("planAdjustSustain snaps an off-grid ring onto the grid", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // A ring a tick step could have authored: one beat and a hair, ending between two grid lines.
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{961, 960})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("a grid grow ceilings onto the next line")
    {
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, true)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const grown =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(grown != nullptr);
            if (grown != nullptr)
            {
                // The next line, not 961/960 + 1: a grid step lands on the grid.
                CHECK(grown->sustain == common::core::Fraction{2});
            }
        }
    }

    SECTION("a grid shrink floors onto the previous line")
    {
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, false)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const shrunk =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(shrunk != nullptr);
            if (shrunk != nullptr)
            {
                CHECK(shrunk->sustain == common::core::Fraction{1});
            }
        }
    }

    SECTION("reversing the snap lands on the line below, not on the off-grid start")
    {
        const auto plan = planAdjustSustain(
            chart,
            tempo_map,
            chart.notes,
            keys,
            {gridStep(g_quarter_grid, true), gridStep(g_quarter_grid, false)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const reversed =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(reversed != nullptr);
            if (reversed != nullptr)
            {
                // The tick remainder is gone for good, and that is the point: a grid step means
                // "put the end on the grid line", so the run cannot return to a ring between them.
                CHECK(reversed->sustain == common::core::Fraction{1});
            }
        }
    }
}

// Two lattices in one run, and the asymmetry that follows from the law: a tick step nudges the
// ring by one tick, the grid step after it SNAPS the end that nudge moved off the grid, and a tick
// step after THAT nudges the snapped ring off it again. Reversing the grid step therefore lands on
// the line below rather than on the off-grid ring the run started from — the intended behaviour,
// not a rounding artifact.
TEST_CASE("planAdjustSustain snaps a tick-nudged ring on the next grid step", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{2})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    // Assertion-free (read inside CHECK expressions): a missing note reads as a zero ring, which no
    // step below expects, so the caller's own comparison fails.
    const auto ring_after = [&](const std::vector<ChartSustainStep>& steps) {
        const auto plan = planAdjustSustain(chart, tempo_map, chart.notes, keys, steps);
        if (!plan.has_value())
        {
            return common::core::Fraction{};
        }
        const common::core::ChartNote* const note =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        return note != nullptr ? note->sustain : common::core::Fraction{};
    };

    std::vector<ChartSustainStep> steps{tickStep(true)};
    CHECK(ring_after(steps) == common::core::Fraction{1921, 960});

    steps.push_back(gridStep(g_quarter_grid, true));
    CHECK(ring_after(steps) == common::core::Fraction{3});

    steps.push_back(tickStep(true));
    CHECK(ring_after(steps) == common::core::Fraction{2881, 960});

    // Back one grid step: the end sits a hair past the line at 3, so the line strictly before it is
    // the one at 3 itself — the tick nudge is what the grid step snaps away.
    steps.push_back(gridStep(g_quarter_grid, false));
    CHECK(ring_after(steps) == common::core::Fraction{3});

    // And one more puts the end back on the line the two-beat ring started on, so the whole run
    // describes nothing at all: NoChange, the answer that retires the gesture's entry. Only a run
    // that STARTED on a line can come back to its start — see the off-grid case above.
    steps.push_back(gridStep(g_quarter_grid, false));
    const auto closed = planAdjustSustain(chart, tempo_map, chart.notes, keys, steps);
    REQUIRE_FALSE(closed.has_value());
    CHECK(closed.error() == ChartPlanRefusal::NoChange);
}

// Each note replays the steps over its OWN end, so a chord whose members were nudged to
// different offsets snaps each member onto its own next line rather than sharing one answer.
TEST_CASE("planAdjustSustain snaps each chord member to its own line", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Ends a hair past beat 2 and a hair before beat 4 — different cells of the grid, so the
        // same grow step reaches a different line for each, by a different distance (959/960 of
        // a beat for the first, 1/960 for the second). One shared answer for the selection, a
        // line or a delta, cannot satisfy both.
        makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{961, 960}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 0, common::core::Fraction{2879, 960}),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };

    const auto plan =
        planAdjustSustain(chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, true)});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* const low =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        const common::core::ChartNote* const high =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 2);
        REQUIRE(low != nullptr);
        REQUIRE(high != nullptr);
        if (low != nullptr && high != nullptr)
        {
            CHECK(low->sustain == common::core::Fraction{2});
            CHECK(high->sustain == common::core::Fraction{3});
        }
    }
}

// Grid lines are signature-beat-derived, and the step carries the NOTE VALUE so the meter scales it
// where the ring's end actually lands: in 6/8 a beat is an eighth note, so a quarter-note grid
// steps two beats and an eighth-note grid one.
TEST_CASE("planAdjustSustain steps the grid the meter derives in 6/8", "[core][chart]")
{
    // Two 6/8 measures at one eighth-note beat per second, so the whole fixture is grid rather than
    // terminal-anchor extrapolation.
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 6, .denominator = 8},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 3, .beat = 1, .seconds = 12.0},
        },
    };
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // Half a beat: an end between the lines of every grid this test steps.
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{1, 2})};
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("an eighth-note grid lines up with the beat")
    {
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(common::core::Fraction{1, 8}, true)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const grown =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(grown != nullptr);
            if (grown != nullptr)
            {
                CHECK(grown->sustain == common::core::Fraction{1});
            }
        }
    }

    SECTION("a quarter-note grid spans two beats")
    {
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, true)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const grown =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(grown != nullptr);
            if (grown != nullptr)
            {
                // Lines sit on beats 1, 3 and 5 of the measure, so the end at beat 1½ reaches 3.
                CHECK(grown->sustain == common::core::Fraction{2});
            }
        }
    }
}

// The header's first consequence, in the meter that once falsified it: a grid-only run from an
// on-grid ring is exactly reversible. In 7/8 a quarter-note grid steps two beats, so the lines sit
// on beats 1, 3, 5 and 7 with the next downbeat one beat after the last — and a step primitive that
// stepped two beats back from that downbeat and re-snapped to the nearest line skipped beat 7,
// taking a six-beat ring to four. The ring's end here starts on beat 7 itself.
TEST_CASE("planAdjustSustain reverses a grid step exactly in 7/8", "[core][chart]")
{
    const common::core::TempoMap tempo_map{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 7, .denominator = 8},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 4, .beat = 1, .seconds = 21.0},
        },
    };
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{6})};
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    // Assertion-free (read inside CHECK expressions), as in the tick-nudged run above.
    const auto ring_after = [&](const std::vector<ChartSustainStep>& steps) {
        const auto plan = planAdjustSustain(chart, tempo_map, chart.notes, keys, steps);
        if (!plan.has_value())
        {
            return common::core::Fraction{};
        }
        const common::core::ChartNote* const note =
            noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        return note != nullptr ? note->sustain : common::core::Fraction{};
    };

    // Beat 7 to the next downbeat is one beat, the short last gap of the measure.
    CHECK(ring_after({gridStep(g_quarter_grid, true)}) == common::core::Fraction{7});
    // And back down onto beat 7 — not past it onto beat 5 — so the run describes nothing.
    const auto closed = planAdjustSustain(
        chart,
        tempo_map,
        chart.notes,
        keys,
        {gridStep(g_quarter_grid, true), gridStep(g_quarter_grid, false)});
    REQUIRE_FALSE(closed.has_value());
    CHECK(closed.error() == ChartPlanRefusal::NoChange);
}

// Growth stops at exact adjacency with the next onset on the note's OWN string — the one bound on
// a ring (40-Q2-B) — and a note on another string never blocks it, because the margin that used to
// bind against any string was the DRAWN tail's spacing rule, which presentation now owns.
TEST_CASE("planAdjustSustain grows a tail to its own string's next onset", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 0),
        makeTestNote({.measure = 1, .beat = 2}, 2, 0),
        makeTestNote({.measure = 1, .beat = 3}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};
    const std::vector<ChartSustainStep> steps{
        gridStep(g_quarter_grid, true),
        gridStep(g_quarter_grid, true),
        gridStep(g_quarter_grid, true)
    };

    const auto plan = planAdjustSustain(chart, tempo_map, chart.notes, keys, steps);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* grown = noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
        REQUIRE(grown != nullptr);
        // Two beats to its own string's restrike, and the string-2 onset one beat in blocks
        // nothing; the third step past the bound reports the bound.
        CHECK(grown->sustain == common::core::Fraction{2});
        CHECK(plan->label == "Grow Sustain");
    }
}

// The entry describes the whole gesture, so its label names the NET direction, not the last press:
// grow, grow, shrink leaves the ring one step longer than the gesture found it, and the undo menu
// must offer to take a growth back. A label read off the last step would say "Shrink" over an
// entry whose undo shortens the ring.
TEST_CASE("planAdjustSustain labels the entry by its net direction", "[core][chart]")
{
    // The measure-3 note rings two beats with no same-string successor, so no bound is involved.
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto grown = planAdjustSustain(
        chart,
        tempo_map,
        chart.notes,
        keys,
        {gridStep(g_quarter_grid, true),
         gridStep(g_quarter_grid, true),
         gridStep(g_quarter_grid, false)});
    REQUIRE(grown.has_value());
    if (grown.has_value())
    {
        const common::core::ChartNote* const note =
            noteAt(grown->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(note != nullptr);
        if (note != nullptr)
        {
            CHECK(note->sustain == common::core::Fraction{3});
        }
        CHECK(grown->label == "Grow Sustain");
    }

    // And the mirror: shrink, shrink, grow nets to one step shorter.
    const auto shrunk = planAdjustSustain(
        chart,
        tempo_map,
        chart.notes,
        keys,
        {gridStep(g_quarter_grid, false),
         gridStep(g_quarter_grid, false),
         gridStep(g_quarter_grid, true)});
    REQUIRE(shrunk.has_value());
    if (shrunk.has_value())
    {
        CHECK(shrunk->label == "Shrink Sustain");
    }
}

// A tail already sitting at the bound stays there rather than being rewritten to the same value,
// so the press has nothing to plan.
TEST_CASE("planAdjustSustain leaves a tail already at the bound alone", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{2}),
        makeTestNote({.measure = 1, .beat = 2}, 2, 0),
        makeTestNote({.measure = 1, .beat = 3}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 1, .beat = 1}, 1)};

    const auto plan =
        planAdjustSustain(chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, true)});
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// Empty keys and an empty step list both plan no sustain change — from a stream that IS the base,
// the two no-ops the verb sees before any gesture has recorded anything.
TEST_CASE("planAdjustSustain plans nothing for no-op inputs", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto no_keys =
        planAdjustSustain(chart, tempo_map, chart.notes, {}, {gridStep(g_quarter_grid, true)});
    REQUIRE_FALSE(no_keys.has_value());
    CHECK(no_keys.error() == ChartPlanRefusal::NoChange);

    const auto no_steps = planAdjustSustain(chart, tempo_map, chart.notes, keys, {});
    REQUIRE_FALSE(no_steps.has_value());
    CHECK(no_steps.error() == ChartPlanRefusal::NoChange);
}

// The gesture's whole point, at the planner level: every press replays the whole run over the rings
// the gesture STARTED at, so a chord member pinned at its own bound diverges from its neighbour on
// the way out and rejoins it on exactly the step that put it there. Stepping from the live ring
// cannot do this — the clamp would become the next step's starting value and the chord would come
// back a different shape.
TEST_CASE("planAdjustSustain replays a chord from the gesture's start", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 0, common::core::Fraction{1}),
        // The string-1 member's own restrike three beats later: its bound, and nothing to the
        // string-2 member.
        makeTestNote({.measure = 2, .beat = 4}, 1, 0),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };
    const std::vector<common::core::ChartNote> base = chart.notes;

    // The controller's discipline in miniature: the press appends its step to the run, and the plan
    // describes start → now, so the live chart walks back to the start before it applies.
    common::core::Chart live = chart;
    std::vector<ChartSustainStep> steps;
    const auto press = [&](bool grow) {
        steps.push_back(gridStep(g_quarter_grid, grow));
        const auto plan = planAdjustSustain(live, tempo_map, base, keys, steps);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            live.notes = base;
            REQUIRE(applyChartChange(live, *plan).has_value());
        }
    };
    // Assertion-free on purpose: it is read inside CHECK expressions, and a Catch2 assertion
    // nested in another assertion's expression is the shape to avoid. A missing note reads as a
    // zero ring, which no step below expects, so the caller's own comparison fails.
    const auto ring_of = [&](int string) {
        const common::core::ChartNote* const note =
            noteAt(live.notes, {.measure = 2, .beat = 1}, string);
        return note != nullptr ? note->sustain : common::core::Fraction{};
    };

    press(/*grow=*/true);
    CHECK(ring_of(1) == common::core::Fraction{2});
    CHECK(ring_of(2) == common::core::Fraction{2});
    press(/*grow=*/true);
    CHECK(ring_of(1) == common::core::Fraction{3});
    CHECK(ring_of(2) == common::core::Fraction{3});

    // A fourth beat would ring through the string-1 restrike, so that member pins at its bound
    // while its neighbour keeps going.
    press(/*grow=*/true);
    CHECK(ring_of(1) == common::core::Fraction{3});
    CHECK(ring_of(2) == common::core::Fraction{4});

    // Coming back, the pinned member leaves its bound on exactly the step that put it there — the
    // clamp never entered the replay, so it could not shorten the next step's starting value.
    press(/*grow=*/false);
    CHECK(ring_of(1) == common::core::Fraction{3});
    CHECK(ring_of(2) == common::core::Fraction{3});
    press(/*grow=*/false);
    CHECK(ring_of(1) == common::core::Fraction{2});
    CHECK(ring_of(2) == common::core::Fraction{2});

    // Back on the ring it started from, the gesture describes nothing, which is NoChange rather
    // than a plan describing nothing: the controller's answer is to take the gesture's undo entry
    // back out and walk the chart to `base` itself.
    steps.push_back(gridStep(g_quarter_grid, false));
    const auto closed = planAdjustSustain(live, tempo_map, base, keys, steps);
    REQUIRE_FALSE(closed.has_value());
    CHECK(closed.error() == ChartPlanRefusal::NoChange);
}

// A ring the replay would empty holds the value it CURRENTLY has — read from the live chart, not
// from the gesture's start — and rejoins the replay the moment it is positive again. Holding the
// start value instead would grow the note back on a shrink press. The floor stays out of the replay
// itself, which is what makes the overshoot payable: every step taken past the floor has to be paid
// back before the ring moves again.
TEST_CASE("planAdjustSustain holds an emptied ring at its live value", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{3})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};
    const std::vector<common::core::ChartNote> base = chart.notes;

    common::core::Chart live = chart;
    std::vector<ChartSustainStep> steps;
    const auto press = [&](bool grow) {
        steps.push_back(gridStep(g_quarter_grid, grow));
        const auto plan = planAdjustSustain(live, tempo_map, base, keys, steps);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            live.notes = base;
            REQUIRE(applyChartChange(live, *plan).has_value());
        }
    };
    // Assertion-free (read inside CHECK expressions): a missing note reads as a zero ring, which
    // no step below expects, so the caller's own comparison fails.
    const auto ring = [&] {
        const common::core::ChartNote* const note =
            noteAt(live.notes, {.measure = 2, .beat = 1}, 1);
        return note != nullptr ? note->sustain : common::core::Fraction{};
    };

    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{2});
    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    // The next line IS the onset, so the ring holds where the previous step left it rather than
    // being clamped to some invented floor or restored to its three-beat start.
    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    // Paying the overshoot back: still held on the step that returns the end to the onset, ringing
    // again on the one after it.
    press(/*grow=*/true);
    CHECK(ring() == common::core::Fraction{1});
    press(/*grow=*/true);
    CHECK(ring() == common::core::Fraction{1});
    press(/*grow=*/true);
    CHECK(ring() == common::core::Fraction{2});
    // A run that replays back to its start still describes nothing even after three floored steps:
    // NoChange is what tells the caller to take the gesture's entry back out and walk the chart to
    // `base`.
    steps.push_back(gridStep(g_quarter_grid, true));
    const auto closed = planAdjustSustain(live, tempo_map, base, keys, steps);
    REQUIRE_FALSE(closed.has_value());
    CHECK(closed.error() == ChartPlanRefusal::NoChange);
}

// Payload is clipped out of the PRE-GESTURE note, so growing back inside one gesture restores a
// keyframe an earlier step's shrink clipped away — the second thing recomputing from the start
// buys, and one a live-stepping verb loses permanently.
TEST_CASE("planAdjustSustain restores payload an earlier step clipped", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};
    const std::vector<common::core::ChartNote> base = chart.notes;

    common::core::Chart live = chart;
    std::vector<ChartSustainStep> steps;
    // Sixteenth-note steps against the 4/4 map: a quarter of a beat each, so the one-beat scrape
    // walks its ring down in four presses and back up in three.
    const auto press = [&](bool grow) {
        steps.push_back(gridStep(g_sixteenth_grid, grow));
        const auto plan = planAdjustSustain(live, tempo_map, base, keys, steps);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            live.notes = base;
            REQUIRE(applyChartChange(live, *plan).has_value());
        }
    };

    // All the way to the onset, where the scrape floors at the minimum gesture window; the
    // turnaround is clipped away with the tail.
    for (int index = 0; index < 4; ++index)
    {
        press(/*grow=*/false);
    }
    const common::core::ChartNote* scrape = noteAt(live.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(scrape != nullptr);
    if (scrape != nullptr)
    {
        CHECK(scrape->sustain == common::core::g_minimum_slide_window);
        CHECK(scrape->keyframes.empty());
    }

    // Growing back inside the same gesture puts the turnaround back, because the ring is replayed
    // from the note the gesture started with rather than from the floored one.
    for (int index = 0; index < 3; ++index)
    {
        press(/*grow=*/true);
    }
    scrape = noteAt(live.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(scrape != nullptr);
    if (scrape != nullptr)
    {
        CHECK(scrape->sustain == common::core::Fraction{3, 4});
        REQUIRE(scrape->keyframes.size() == 1);
        if (scrape->keyframes.size() == 1)
        {
            CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->keyframes[0].fret == 3);
        }
        REQUIRE(scrape->slide_out.has_value());
        if (scrape->slide_out.has_value())
        {
            CHECK(scrape->sustain == common::core::Fraction{3, 4});
        }
    }
}

// Applying a removal-and-insertion whose preconditions hold swaps in the new stream.
TEST_CASE("applyChartChange applies a removal and insertion", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> to_remove{chart.notes[0]};
    const std::vector<common::core::ChartNote> to_insert{makeTestNote(
        {.measure = 4, .beat = 1}, 1, 2)};

    const auto result = applyChartChange(
        chart,
        ChartEditPlan{
            .removed = to_remove,
            .inserted = to_insert,
            .label = {},
        });
    CHECK(result.has_value());
    CHECK(chart.notes.size() == 3);
    CHECK(noteAt(chart.notes, {.measure = 2, .beat = 1}, 1) == nullptr);
    const common::core::ChartNote* added = noteAt(chart.notes, {.measure = 4, .beat = 1}, 1);
    REQUIRE(added != nullptr);
    CHECK(added->fret == 2);
}

// A removal whose full value no longer matches the chart is rejected and the chart is left
// untouched.
TEST_CASE("applyChartChange rejects a stale removal", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::Chart original = chart;
    // The slot exists, but the fret no longer matches the recorded value.
    const std::vector<common::core::ChartNote> to_remove{makeTestNote(
        {.measure = 2, .beat = 1}, 1, 99)};

    const auto result = applyChartChange(
        chart,
        ChartEditPlan{
            .removed = to_remove,
            .inserted = {},
            .label = {},
        });
    CHECK_FALSE(result.has_value());
    if (!result.has_value())
    {
        CHECK(result.error() == EditorUndoFailureCode::PreflightRejected);
    }
    CHECK(chart == original);
}

// A valid removal followed by an insertion that collides with a surviving note rejects the whole
// change: the chart is untouched, proving the preflight is atomic.
TEST_CASE("applyChartChange rejects a colliding insertion atomically", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::Chart original = chart;
    // Removing the measure-3 note would succeed on its own, but the insertion targets the still
    // occupied measure-2 / string-2 slot.
    const std::vector<common::core::ChartNote> to_remove{chart.notes[2]};
    const std::vector<common::core::ChartNote> to_insert{makeTestNote(
        {.measure = 2, .beat = 1}, 2, 8)};

    const auto result = applyChartChange(
        chart,
        ChartEditPlan{
            .removed = to_remove,
            .inserted = to_insert,
            .label = {},
        });
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
    common::core::Chart chart = makeTestChart();
    common::core::ChartNote& note = chart.notes[2]; // measure 3 / string 1, fret 7, sustain 2
    note.tremolo = true;
    note.palm_mute = true;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

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
        CHECK(scrape->keyframes.empty());
        REQUIRE(scrape->slide_out.has_value());
        if (scrape->slide_out.has_value())
        {
            CHECK(*scrape->slide_out == g_pick_slide_default_high_fret);
        }
        // The overridden techniques stay in memory, untouched.
        CHECK(scrape->tremolo);
        CHECK(scrape->palm_mute);
        CHECK(plan->label == "Pick Slide");
        // The latents are legal in memory but the rules gate binds documents, so the oracle
        // is the SAVED form: the writer omits the overrides and the reparse passes clean.
        REQUIRE(applyChartChange(chart, *plan).has_value());
        const auto saved =
            common::core::parseChartDocument(common::core::chartDocumentText(chart, tempo_map));
        REQUIRE(saved.has_value());
        CHECK(common::core::validateChartRules(*saved, tempo_map).has_value());
    }
}

// The eligible-subset skip covers EVERY per-note rule, not a hand-picked pair of them. It used to
// copy two of the validator's predicates, so a note the target attack broke some other rule on was
// not skipped — and the whole-stream gate then refused the plan for every note in the selection,
// not just that one. Here a dead note cannot become a pinch, which neither copied predicate named.
// Note the refusal is specifically the PINCH's: a dead note may carry a harmonic node as of
// 2026-08-18, but only the on-neck kind a hand stands on, and the verb synthesizes an off-neck one
// when it converts. The test needed no change when that rule narrowed, which is the whole point of
// asking the authority instead of restating it.
TEST_CASE("planSetAttack skips a note any per-note rule refuses", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[0].dead = true; // measure 2 / string 1, fret 3
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
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

// A ring too short to hold a gesture at all grows to the DEFAULT scrape length so the path has
// somewhere to travel: a quarter note (user 2026-08-18), not the old degeneracy floor of an eighth
// of a beat, which a corpus survey found to be 16x shorter than any scrape anyone charted. A ring
// that CAN hold the gesture is kept exactly as authored — the ring is the note's own truth, and
// this verb changes the attack, not the duration.
TEST_CASE("planSetAttack grows only a ring too short to scrape", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{1, 16}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 7, common::core::Fraction{1, 2}),
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
    };

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* stub = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        REQUIRE(stub != nullptr);
        CHECK(stub->sustain == pickSlideDefaultSustainBeats(4));
        CHECK(stub->sustain > common::core::g_minimum_slide_window);
        // The terminal ends the ring by definition, so the sustain above IS the gesture's
        // length: a scrape rings no longer than it travels.
        CHECK(stub->slide_out.has_value());

        const common::core::ChartNote* kept = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 2);
        REQUIRE(kept != nullptr);
        CHECK(kept->sustain == common::core::Fraction{1, 2});

        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// A start in the neck's upper half scrapes downward to the low default endpoint.
TEST_CASE("planSetAttack scrapes downward from the neck's upper half", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2].fret = 14;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

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
            CHECK(*scrape->slide_out == g_pick_slide_default_low_fret);
        }
    }
}

// Under a capo the low endpoint yields to the first playable fret: every fret a slide gesture
// names sits at or above capo + 1 (user ruling 2026-08-20), so a downward default scrape under a
// high capo terminates at capo + 1 rather than at the bare corpus default the gate now refuses.
// The chart's other notes are lifted above the capo so the fixture itself stays legal.
TEST_CASE("planSetAttack floors the default scrape terminal above the capo", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.tuning.capo = 5;
    for (common::core::ChartNote& note : chart.notes)
    {
        note.fret += 5;
    }
    chart.notes[2].fret = 20;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

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
            CHECK(*scrape->slide_out == 6);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// Toggling the attack in and back out restores the note field-for-field: the latents were never
// touched, the path clears on exit, and fret and sustain survive the round trip.
TEST_CASE("planSetAttack round-trips a toggled note exactly", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2].tremolo = true;
    chart.notes[2].vibrato = common::core::VibratoState::Narrow;
    const common::core::ChartNote original = chart.notes[2];
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto enter =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    REQUIRE(applyChartChange(chart, *enter).has_value());

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide");
    REQUIRE(exit.has_value());
    if (!exit.has_value())
    {
        return;
    }
    REQUIRE(applyChartChange(chart, *exit).has_value());

    const common::core::ChartNote* restored = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(restored != nullptr);
    CHECK(*restored == original);
}

// Keyed notes already carrying the attack plan nothing.
TEST_CASE("planSetAttack returns nullopt when nothing changes", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    CHECK_FALSE(
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .has_value());
    CHECK_FALSE(
        planSetAttack(chart, tempo_map, {}, common::core::NoteAttack::Pick, "Pick").has_value());
}

// The three boolean techniques share ONE planner, and the reason that is safe rather than merely
// tidy is that eligibility is asked of the per-note rule authority instead of being restated. Each
// flag therefore inherits its own rules for free, and they are different rules: a tap harmonic
// cannot be tremolo picked (the damping finger leaves the string, so nothing holds the node under
// re-picking) while a dead note cannot take a shake (it modulates a pitch the note does not have).
// Vibrato has its own planner now that the field is a width axis, so the second half below asks
// THAT verb the same question: one law, two different answers - and neither is written in this
// file or in either verb.
TEST_CASE("planSetNoteFlag inherits each flag's own per-note rule", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    // A tap harmonic and a plain note, selected together.
    chart.notes[0].attack = common::core::NoteAttack::Tap;
    chart.notes[0].harmonic_node = 17.0;
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1),
        keyAt({.measure = 2, .beat = 1}, 2),
    };

    const auto tremolo =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Tremolo, true, "Tremolo");
    REQUIRE(tremolo.has_value());
    if (tremolo.has_value())
    {
        // Only the plain note takes it; the tap harmonic is skipped rather than the edit refused.
        CHECK(tremolo->inserted.size() == 1);
        CHECK(std::ranges::all_of(tremolo->inserted, [](const common::core::ChartNote& note) {
            return note.tremolo && !note.harmonic_node.has_value();
        }));
        applyAndValidate(chart, tempo_map, *tremolo);
    }

    // Vibrato is refused by a different rule on a different note, and its verb needs no knowledge
    // of either: a dead note sounds no pitch to modulate. Asked of planSetVibrato because the
    // width axis is not one of the bools above, and the point of the test is that BOTH planners
    // read the same authority rather than restating it.
    common::core::Chart dead_chart = makeTestChart();
    dead_chart.notes[0].dead = true;
    const auto vibrato = planSetVibrato(
        dead_chart, tempo_map, keys, {}, common::core::VibratoState::Narrow, "Vibrato");
    REQUIRE(vibrato.has_value());
    if (vibrato.has_value())
    {
        CHECK(vibrato->inserted.size() == 1);
        CHECK(std::ranges::none_of(vibrato->inserted, [](const common::core::ChartNote& note) {
            return note.dead;
        }));
        applyAndValidate(dead_chart, tempo_map, *vibrato);
    }
}

// Emphasis is ONE three-valued axis, which is the whole structural difference from the two mutes
// below: a note carries exactly one end of it, so striking an already-ghosted note as an accent
// REPLACES the ghost rather than joining it. Nothing can ever be both, and that is a property of
// the field rather than a rule the verb enforces.
TEST_CASE("planSetEmphasis moves a note along one axis", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto ghost =
        planSetEmphasis(chart, tempo_map, keys, common::core::NoteEmphasis::Ghost, "Ghost Note");
    REQUIRE(ghost.has_value());
    if (!ghost.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *ghost);
    const common::core::ChartNote* note = noteAt(chart.notes, {.measure = 2, .beat = 1}, 1);
    REQUIRE(note != nullptr);
    if (note != nullptr)
    {
        CHECK(common::core::isGhosted(note->emphasis));
    }

    const auto accent =
        planSetEmphasis(chart, tempo_map, keys, common::core::NoteEmphasis::Accent, "Accent");
    REQUIRE(accent.has_value());
    if (!accent.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *accent);
    note = noteAt(chart.notes, {.measure = 2, .beat = 1}, 1);
    REQUIRE(note != nullptr);
    if (note != nullptr)
    {
        // The ghost is GONE rather than carried alongside: one field, one value.
        CHECK(common::core::isAccented(note->emphasis));
        CHECK_FALSE(common::core::isGhosted(note->emphasis));
    }

    // A write recording what the document already records is not an edit, so it pushes no history
    // entry — the same savedChartNote gate the mute verb uses.
    CHECK_FALSE(
        planSetEmphasis(chart, tempo_map, keys, common::core::NoteEmphasis::Accent, "Accent")
            .has_value());
    CHECK_FALSE(
        planSetEmphasis(chart, tempo_map, {}, common::core::NoteEmphasis::Ghost, "Ghost Note")
            .has_value());
}

// The two mutes are independent fields, so each verb writes exactly its own and reads nothing of
// the other. A note therefore ends up carrying BOTH when both are set — a dead string inside a
// palm-muted chord — and clearing one leaves the other standing.
TEST_CASE("planSetNoteFlag writes one mute without disturbing the other", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto palm =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute");
    REQUIRE(palm.has_value());
    if (!palm.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *palm);
    const common::core::ChartNote* note = noteAt(chart.notes, {.measure = 2, .beat = 1}, 1);
    REQUIRE(note != nullptr);
    if (note != nullptr)
    {
        CHECK(note->palm_mute);
        CHECK_FALSE(note->dead);
    }

    const auto dead =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note");
    REQUIRE(dead.has_value());
    if (!dead.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *dead);
    note = noteAt(chart.notes, {.measure = 2, .beat = 1}, 1);
    REQUIRE(note != nullptr);
    if (note != nullptr)
    {
        // Both mutes on one note, which is the whole point of the two-flag model.
        CHECK(note->palm_mute);
        CHECK(note->dead);
    }

    const auto cleared =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, false, "Remove Palm Mute");
    REQUIRE(cleared.has_value());
    if (!cleared.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *cleared);
    note = noteAt(chart.notes, {.measure = 2, .beat = 1}, 1);
    REQUIRE(note != nullptr);
    if (note != nullptr)
    {
        CHECK_FALSE(note->palm_mute);
        CHECK(note->dead);
    }
}

// E25 is a PRESENTATION rule now (plan ruling 5, 2026-08-21), so the verbs stopped trimming: X on
// a held note deadens it and leaves the ring exactly where it was, because a dead note's damped
// stroke has a duration like any other and that duration is what a legato claim after it reads.
// What changes is only what a surface draws, which no plan touches.
TEST_CASE("planSetNoteFlag leaves a deadened note's ring alone", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    // measure 3 / string 1 carries a two-beat tail in the fixture.
    constexpr std::size_t held = 2;
    const common::core::Fraction ring = chart.notes[held].sustain;
    REQUIRE(ring.numerator > 0);
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto dead =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note");
    REQUIRE(dead.has_value());
    if (dead.has_value())
    {
        REQUIRE(dead->inserted.size() == 1);
        CHECK(dead->inserted.front().dead);
        CHECK(dead->inserted.front().sustain == ring);
        applyAndValidate(chart, tempo_map, *dead);
    }

    // The other half of the same rule, so the pair is stated in one place: the ring survives the
    // press and NOTHING draws it (E25 as rule 4 of the presentation).
    const std::vector<common::core::ChartNote> presented =
        common::core::presentedChartNotes(chart.notes, tempo_map);
    REQUIRE(presented.size() == chart.notes.size());
    CHECK(presented[held].sustain == common::core::Fraction{});
    CHECK(chart.notes[held].sustain == ring);

    // And the duration verbs go on working on it: nothing about the note is frozen by the mute.
    // The two-beat ring ends on a grid line, so one grid step is one beat.
    const auto grown =
        planAdjustSustain(chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, true)});
    REQUIRE(grown.has_value());
    if (grown.has_value())
    {
        REQUIRE(grown->inserted.size() == 1);
        CHECK(grown->inserted.front().sustain == ring + common::core::Fraction{1});
    }
}

// Eligibility is asked of the per-note rule authority rather than restated, so the verb tracks
// that rule for free — including when it MOVES. A dead note sounds no pitch, so pitch
// MODULATION refuses it (vibrato here) and refuses only THAT note, leaving the rest of the
// selection muted; a harmonic node does NOT refuse it, because the node is positional on a dead
// note (2026-08-18). Neither restricts a palm mute. This test needed no change to the verb when
// that rule moved, which is the property the indirection buys.
TEST_CASE("planSetNoteFlag skips notes the dead-note rule refuses", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[0].vibrato = common::core::VibratoState::Narrow; // measure 2 / string 1
    chart.notes[2].harmonic_node = 12.0;                         // measure 3 / string 1, fret 7
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1),
        keyAt({.measure = 2, .beat = 1}, 2),
        keyAt({.measure = 3, .beat = 1}, 1),
    };

    const auto dead =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note");
    REQUIRE(dead.has_value());
    if (!dead.has_value())
    {
        return;
    }
    // The vibrato note is skipped; the plain note AND the harmonic both take the X.
    REQUIRE(dead->inserted.size() == 2);
    CHECK(std::ranges::none_of(dead->inserted, [](const common::core::ChartNote& note) {
        return common::core::isShaking(note.vibrato);
    }));
    CHECK(std::ranges::all_of(dead->inserted, [](const common::core::ChartNote& note) {
        return note.dead;
    }));
    CHECK(std::ranges::any_of(dead->inserted, [](const common::core::ChartNote& note) {
        return note.harmonic_node.has_value();
    }));
    applyAndValidate(chart, tempo_map, *dead);

    const auto palm =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute");
    REQUIRE(palm.has_value());
    if (palm.has_value())
    {
        // A palm-muted harmonic is ordinary, and so is a palm-muted vibrato.
        CHECK(palm->inserted.size() == 3);
        applyAndValidate(chart, tempo_map, *palm);
    }
}

// A scrape records neither mute — savedChartNote strips both, the in-memory override contract —
// so the verb skips it instead of storing a flag no surface draws and no document keeps. Asked of
// the writer's own authority rather than restated as an attack test, so the two can never
// disagree about where a mute is real.
TEST_CASE("planSetNoteFlag leaves a pick slide unmuted", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute")
                    .has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note")
                    .has_value());
}

// Keyed notes already carrying the mute plan nothing, and neither does an empty key set.
TEST_CASE("planSetNoteFlag returns nullopt when nothing changes", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[0].palm_mute = true;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute")
                    .has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, false, "Remove Dead")
                    .has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, {}, ChartNoteFlag::PalmMute, true, "Palm Mute")
                    .has_value());
}

// The fret-verb law: retyping edits exactly the selected notes' own frets, so a slide's path
// stays where it was authored — in both modes, for a scrape and a pitched slide alike. The old
// scrape path translation was ruled a bug (every keyframe was placed on its fret on purpose).
TEST_CASE("planRetypeFrets leaves a slide's path in place in both modes", "[core][chart]")
{
    // Asserts the retyped note carries the expected start with the fixture's authored path
    // untouched, and that the applied chart still passes the whole-chart rules gate.
    const auto check_path_kept = [](const common::core::Chart& chart,
                                    const std::expected<ChartEditPlan, ChartPlanRefusal>& plan,
                                    const int expected_start) {
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& retyped = plan->inserted.front();
            CHECK(retyped.fret == expected_start);
            REQUIRE(retyped.keyframes.size() == 1);
            CHECK(retyped.keyframes[0].fret == chart.notes.front().keyframes[0].fret);
            // Bound once each so every check and access is provably the same object: the
            // optional-access checker cannot tie two separate calls of front() together.
            const std::optional<int>& retyped_out = retyped.slide_out;
            const std::optional<int>& original_out = chart.notes.front().slide_out;
            REQUIRE(retyped_out.has_value() == original_out.has_value());
            if (retyped_out.has_value() && original_out.has_value())
            {
                CHECK(*retyped_out == *original_out);
            }
            common::core::Chart applied = chart;
            applyAndValidate(applied, makeTempoMap(), *plan);
        }
    };

    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};

    SECTION("scrape: transpose moves the start only")
    {
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart,
            planRetypeFrets(
                chart,
                makeTempoMap(),
                chart.notes,
                11,
                /*set_exact=*/false,
                common::core::ChartStopChannel::Sounding),
            11);
    }
    SECTION("scrape: set-exact assigns the start only")
    {
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart,
            planRetypeFrets(
                chart,
                makeTempoMap(),
                chart.notes,
                11,
                /*set_exact=*/true,
                common::core::ChartStopChannel::Sounding),
            11);
    }
    SECTION("scrape: a start past the old translated-path ceiling is now legal")
    {
        // Under the deleted translation, transposing to 24 pushed the terminal's 12 to 27 and
        // refused; with the path in place every fret the start itself can reach is typable.
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart,
            planRetypeFrets(
                chart,
                makeTempoMap(),
                chart.notes,
                24,
                /*set_exact=*/false,
                common::core::ChartStopChannel::Sounding),
            24);
    }
    SECTION("pitched slide: transpose moves the start only")
    {
        common::core::ChartNote slide =
            makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1});
        slide.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}
        };
        chart.notes = {std::move(slide)};
        check_path_kept(
            chart,
            planRetypeFrets(
                chart,
                makeTempoMap(),
                chart.notes,
                8,
                /*set_exact=*/false,
                common::core::ChartStopChannel::Sounding),
            8);
    }
    SECTION("pitched slide: set-exact assigns the start only")
    {
        common::core::ChartNote slide =
            makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1});
        slide.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}
        };
        chart.notes = {std::move(slide)};
        check_path_kept(
            chart,
            planRetypeFrets(
                chart,
                makeTempoMap(),
                chart.notes,
                9,
                /*set_exact=*/true,
                common::core::ChartStopChannel::Sounding),
            9);
    }
}

// A scrape cannot sit still: retyping its start onto its first path position refuses through
// the finalize gate's always-traveling rule, in both modes — the fixture's first keyframe is
// fret 3, so a target of 3 stills the opening segment.
TEST_CASE("planRetypeFrets refuses a scrape stilled against its first path point", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};

    const auto exact = planRetypeFrets(
        chart,
        makeTempoMap(),
        chart.notes,
        3,
        /*set_exact=*/true,
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(exact.has_value());
    CHECK(exact.error() == ChartPlanRefusal::Invalid);
    const auto shifted = planRetypeFrets(
        chart,
        makeTempoMap(),
        chart.notes,
        3,
        /*set_exact=*/false,
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(shifted.has_value());
    CHECK(shifted.error() == ChartPlanRefusal::Invalid);
}

// An open string cannot slide, so retyping a slid note to 0 refuses whole — and the refusal is
// Invalid, the kind the pending entry paints red instead of silently no-oping.
TEST_CASE("planRetypeFrets refuses fret 0 on a slid note", "[core][chart]")
{
    common::core::ChartNote slide =
        makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1});
    slide.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}};

    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {std::move(slide)};

    const auto plan = planRetypeFrets(
        chart,
        makeTempoMap(),
        chart.notes,
        0,
        /*set_exact=*/true,
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::Invalid);
}

// The stilled-scrape refusal is scrape-only: a pitched slide's equal-fret segment is the legal
// hold-then-glide encoding the importer emits, so retyping a pitched start onto its first
// keyframe's fret is a legitimate correction and must pass, not refuse.
TEST_CASE("planRetypeFrets accepts a pitched slide retyped onto its keyframe fret", "[core][chart]")
{
    common::core::ChartNote slide =
        makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1});
    slide.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}};

    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {std::move(slide)};

    const auto plan = planRetypeFrets(
        chart,
        makeTempoMap(),
        chart.notes,
        7,
        /*set_exact=*/true,
        common::core::ChartStopChannel::Sounding);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted.front().fret == 7);
        REQUIRE(plan->inserted.front().keyframes.size() == 1);
        CHECK(plan->inserted.front().keyframes[0].fret == 7);

        common::core::Chart applied = chart;
        applyAndValidate(applied, makeTempoMap(), *plan);
    }
}

// A sustain change re-terminates a scrape's path: shrink compresses the final point onto the
// new end, growth rides it out, and the sustain floors at the gesture window instead of zero.
TEST_CASE("planAdjustSustain re-terminates a scrape's path", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2] = makeScrape({.measure = 3, .beat = 1}, 1);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    SECTION("shrink compresses the terminal onto the new end")
    {
        // A sixteenth-note step is a quarter beat off the one-beat ring's on-grid end.
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(g_sixteenth_grid, false)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::Fraction{3, 4});
            REQUIRE(scrape->keyframes.size() == 1);
            CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->keyframes[0].fret == 3);
            REQUIRE(scrape->slide_out.has_value());
            if (scrape->slide_out.has_value())
            {
                CHECK(scrape->sustain == common::core::Fraction{3, 4});
                CHECK(*scrape->slide_out == 12);
            }
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }

    SECTION("growth rides the terminal out to the new end")
    {
        // An eighth-note step is half a beat in 4/4.
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(common::core::Fraction{1, 8}, true)});
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
                CHECK(scrape->sustain == common::core::Fraction{3, 2});
                CHECK(*scrape->slide_out == 12);
            }
        }
    }

    SECTION("shrink floors at the minimum gesture window")
    {
        // One quarter-note step takes the one-beat ring's end back onto its own onset.
        const auto plan = planAdjustSustain(
            chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, false)});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::g_minimum_slide_window);
            // The turnaround no longer fits inside the floored window; the terminal alone rides.
            CHECK(scrape->keyframes.empty());
            REQUIRE(scrape->slide_out.has_value());
            if (scrape->slide_out.has_value())
            {
                CHECK(scrape->sustain == common::core::g_minimum_slide_window);
                CHECK(*scrape->slide_out == 12);
            }
        }
    }
}

// When compression would land the terminal fret on its new predecessor, the nearest earlier
// differing fret takes over so the path keeps traveling.
TEST_CASE("planAdjustSustain keeps a compressed scrape traveling", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    common::core::ChartNote scrape = makeScrape({.measure = 3, .beat = 1}, 1);
    // 9 -> 3 -> 12 -> 3: valid travel whose terminal fret equals the first surviving leg's.
    scrape.keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{1, 4}, .fret = 3},
        common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 12},
    };
    scrape.slide_out = 3;
    chart.notes[2] = scrape;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    // Shrink to half a beat: only the first turnaround survives, and the terminal fret 3 would sit
    // still against it, so the earlier differing fret 12 terminates instead.
    const auto plan = planAdjustSustain(
        chart, tempo_map, chart.notes, keys, {gridStep(common::core::Fraction{1, 8}, false)});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* shrunk =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(shrunk != nullptr);
        CHECK(shrunk->sustain == common::core::Fraction{1, 2});
        REQUIRE(shrunk->keyframes.size() == 1);
        CHECK(shrunk->keyframes[0].offset == common::core::Fraction{1, 4});
        CHECK(shrunk->keyframes[0].fret == 3);
        REQUIRE(shrunk->slide_out.has_value());
        if (shrunk->slide_out.has_value())
        {
            CHECK(shrunk->sustain == common::core::Fraction{1, 2});
            CHECK(*shrunk->slide_out == 12);
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
        chart,
        tempo_map,
        makeTestNote({.measure = 1, .beat = 1, .offset = {1, 2}}, 1, 5),
        g_fixture_sustain);
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
        for (const common::core::Keyframe& keyframe : truncated->keyframes)
        {
            const std::optional<int>& fret = keyframe.fret;
            REQUIRE(fret.has_value());
            CHECK(*fret != previous_fret);
            previous_fret = *fret;
        }
        if (truncated->slide_out.has_value())
        {
            CHECK(*truncated->slide_out != previous_fret);
        }
        // The terminal lands exactly ON the inserted onset — structurally legal, since the
        // keyframe-on-onset rule never sees a slide-out; the whole-chart gate is the oracle
        // that the applied chart can be re-read.
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// A slide that HOLDS a fret cannot become a scrape. The rule authority requires a scrape's whole
// path to keep traveling (consecutive neck positions strictly differ, the start fret included),
// because a pick cannot rest on a fret and still be scraping — where an ordinary slide's
// equal-fret segment is a legitimate hold. The note is skipped like any other ineligible one, so
// the verb refuses rather than authoring an invalid gesture.
TEST_CASE("planSetAttack refuses a scrape on a slide that holds a fret", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    // The note's own fret is 7, so a keyframe at 7 is a segment with no travel at all.
    chart.notes[2].keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto plan =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    CHECK_FALSE(plan.has_value());
}

// Entering a pick slide CONVERTS an existing pitched glide rather than discarding it (user
// 2026-08-18): the glide already IS a path, so its frets and direction are what the charter drew
// and the scrape keeps them, with the last leg promoted to the gesture's required terminal.
// Exiting is still destructive — `slides` is the path's own storage, definitionally outside the
// latent contract, so toggling back clears the path rather than resurrecting the glide, and undo
// is the recovery. Pinned so that asymmetry with the technique latents stays deliberate.
TEST_CASE("planSetAttack converts a pitched glide into the scrape path", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2].tremolo = true;
    chart.notes[2].keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 9}
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto enter =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    // The tremolo latent makes the in-memory chart deliberately dirty, so validate the saved
    // form rather than the raw stream.
    REQUIRE(applyChartChange(chart, *enter).has_value());
    const auto saved =
        common::core::parseChartDocument(common::core::chartDocumentText(chart, tempo_map));
    REQUIRE(saved.has_value());
    CHECK(common::core::validateChartRules(*saved, tempo_map).has_value());
    const common::core::ChartNote* scrape = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(scrape != nullptr);
    // The glide's single keyframe was its whole path, so it becomes the terminal: fret 9 kept
    // from the charter's own glide rather than the synthesized default's far endpoint.
    CHECK(scrape->keyframes.empty());
    REQUIRE(scrape->slide_out.has_value());
    if (scrape->slide_out.has_value())
    {
        CHECK(*scrape->slide_out == 9);
        CHECK(*scrape->slide_out != g_pick_slide_default_high_fret);
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
    CHECK(restored->keyframes.empty());
    CHECK_FALSE(restored->slide_out.has_value());
}

// The press writes a CLAIM and nothing more: no direction is stored, and both directions resolve
// back through the one resolver, so a hammer-on and a pull-off are the same authored statement.
TEST_CASE("planSetLegato claims a connection in both directions", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // String 1 climbs 3 -> 7: the 7 resolves as a hammer-on.
        makeTestNote({.measure = 1, .beat = 1}, 1, 3),
        makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        // String 2 falls 9 -> 5: the 5 resolves as a pull-off.
        makeTestNote({.measure = 1, .beat = 3}, 2, 9),
        makeTestNote({.measure = 1, .beat = 4}, 2, 5),
    };
    // One-beat gaps sit at the kept-sustain bound, so the predecessors hold their tails to the
    // margin — the connection the resolver requires there.
    chart.notes[0].sustain = common::core::Fraction{1};
    chart.notes[2].sustain = common::core::Fraction{1};

    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1),
        keyAt({.measure = 1, .beat = 4}, 2),
    };
    const ChartLegatoPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
    CHECK(planned.skipped == 0);
    REQUIRE(planned.plan.has_value());
    if (planned.plan.has_value())
    {
        REQUIRE(planned.plan->inserted.size() == 2);
        // Insertions stay in chart order: the string-1 climb first, then the string-2 fall. BOTH
        // carry the same attack — the direction lives only in the resolution.
        CHECK(planned.plan->inserted[0].string == 1);
        CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Legato);
        CHECK(planned.plan->inserted[1].string == 2);
        CHECK(planned.plan->inserted[1].attack == common::core::NoteAttack::Legato);

        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *planned.plan);
        const common::core::ChartConnections connections =
            common::core::chartConnections(applied.notes, tempo_map);
        REQUIRE(connections.legato.size() == 4);
        CHECK(connections.legato[1] == common::core::LegatoMotion::Hammer);
        CHECK(connections.legato[3] == common::core::LegatoMotion::Pull);
    }
}

// The resolver is the only authority on eligibility, and the skip channel reports what it refused
// so an all-skipped press is never a dead key.
TEST_CASE("planSetLegato skips exactly what the resolver refuses", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("no earlier note on the string")
    {
        // The only note on string 1 has nothing to connect to. A note on ANOTHER string earlier in
        // time must not stand in for it — you cannot hammer on from a different string.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 2, 5),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        };
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::NoPredecessor);
    }

    SECTION("the earlier note sits at the same fret")
    {
        // Neither hammered nor pulled: the fret does not move, so there is no connection to record.
        // The predecessor holds its tail so the refusal is the equal fret, not the hold test.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 7),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        };
        chart.notes[0].sustain = common::core::Fraction{1};
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::NoConnection);
    }

    SECTION("a released predecessor whose connection cannot be authored")
    {
        // The predecessor's ring stops short, and the one tail the assist may not spend is a
        // gesture's own authored window: a trail-off's exit is data the author placed, so the note
        // is skipped whole rather than having its gesture rewritten to buy a connection.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3, common::core::Fraction{1, 2}),
            makeTestNote({.measure = 1, .beat = 3}, 1, 7),
        };
        chart.notes[0].slide_out = 5;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::PredecessorReleased);
    }

    SECTION("the earlier note is a fret-hand harmonic")
    {
        // A touch holds nothing to hand over, so neither direction resolves across it (E19 says as
        // much for the pull-off), and the assist cannot buy it either.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 0, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        };
        chart.notes[0].harmonic_node = 12.0;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::NoConnection);
    }

    SECTION("a picking-hand rider is skipped in both directions")
    {
        // Tap, pinch, and scrape onsets are already fully described, so a connection claim would
        // say nothing about them and the verb leaves them entirely alone.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
            makeScrape({.measure = 1, .beat = 3}, 2),
        };
        chart.notes[1].attack = common::core::NoteAttack::Tap;
        const std::vector<ChartSlotKey> keys{
            keyAt({.measure = 1, .beat = 2}, 1),
            keyAt({.measure = 1, .beat = 3}, 2),
        };
        const ChartLegatoPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 2);
        CHECK(planned.reason == ChartLegatoSkip::PickingHandOnset);
    }
}

// The D14 assist: when the hold is the only thing missing, the verb grows the predecessor's ring
// to the successor's onset and claims the connection in the same plan — the ring is the held-ness
// datum, and the verb writes it rather than demanding the drag first.
TEST_CASE(
    "planSetLegato grows a released predecessor's ring to author the connection", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a single note across the bound connects")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3),
            makeTestNote({.measure = 1, .beat = 3}, 1, 7),
        };
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (planned.plan.has_value())
        {
            // The grown predecessor rides the same plan: its tail ends exactly at the margin
            // point before the onset (two beats less the quarter-beat margin in 4/4).
            REQUIRE(planned.plan->inserted.size() == 2);
            CHECK(planned.plan->inserted[0].sustain == common::core::Fraction{2});
            CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Pick);
            CHECK(planned.plan->inserted[1].attack == common::core::NoteAttack::Legato);
        }
    }

    SECTION("the descending direction grows the same way")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 9),
            makeTestNote({.measure = 1, .beat = 3}, 1, 5),
        };
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (planned.plan.has_value())
        {
            REQUIRE(planned.plan->inserted.size() == 2);
            CHECK(planned.plan->inserted[0].sustain == common::core::Fraction{2});
            CHECK(planned.plan->inserted[1].attack == common::core::NoteAttack::Legato);
        }
    }

    SECTION("a chord onto a chord grows every member's predecessor uniformly")
    {
        // Groups are allowed by the ruled assist: pressing H on a selection IS intent, one
        // uniform rule with no single-vs-group branch, and same-onset members never block each
        // other's growth.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3),
            makeTestNote({.measure = 1, .beat = 1}, 2, 5),
            makeTestNote({.measure = 2, .beat = 1}, 1, 5),
            makeTestNote({.measure = 2, .beat = 1}, 2, 7),
        };
        const std::vector<ChartSlotKey> keys{
            keyAt({.measure = 2, .beat = 1}, 1),
            keyAt({.measure = 2, .beat = 1}, 2),
        };
        const ChartLegatoPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
        REQUIRE(planned.plan.has_value());
        if (planned.plan.has_value())
        {
            REQUIRE(planned.plan->inserted.size() == 4);
            CHECK(planned.plan->inserted[0].sustain == common::core::Fraction{4});
            CHECK(planned.plan->inserted[1].sustain == common::core::Fraction{4});
            CHECK(planned.plan->inserted[2].attack == common::core::NoteAttack::Legato);
            CHECK(planned.plan->inserted[3].attack == common::core::NoteAttack::Legato);
        }
    }

    SECTION("a trail-off predecessor's tail is never reshaped")
    {
        // A trail-off's exit is authored gesture geometry, so the assist refuses to spend it even
        // though the connection itself would be legal (the resolver reads the RELEASED fret, so a
        // pull off the last pitched stop resolves once the hold reaches). The hold IS the only
        // blocker here, which is exactly what the skip reason reports.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 3}, 1, 5),
        };
        chart.notes[0].slide_out = 12;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::PredecessorReleased);
    }

    SECTION("a scrape predecessor connects to nothing, however its hold reaches")
    {
        // A scrape's travel is the pick's position, not a finger's, so the resolver disqualifies
        // it outright: the press skips for the connection, not the hold, and no held-enough tail
        // could change that — which is why the scrape never reaches the assist's guard at all.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeScrape({.measure = 1, .beat = 1}, 1),
            makeTestNote({.measure = 1, .beat = 3}, 1, 5),
        };
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::NoConnection);
    }
}

// No node ever leaves with an `H` press now: the claim stores no direction, so there is no attack
// whose meaning a node could contradict. The resolver's node clauses do the work instead, which is
// why a fret-hand harmonic skips ITSELF rather than needing a guard in the verb.
TEST_CASE("planSetLegato leaves every harmonic node where it found it", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a stopped harmonic above its predecessor claims and keeps its node")
    {
        // Fret 9 under a node at 21 is the tapped-harmonic gesture: the fretting hand presses the
        // stop while the picking hand keeps damping the node, and the hammer clause accepts a node
        // as something to strike.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 9),
        };
        chart.notes[1].harmonic_node = 21.0;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (planned.plan.has_value())
        {
            REQUIRE(planned.plan->inserted.size() == 1);
            CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Legato);
            CHECK(planned.plan->inserted[0].harmonic_node.has_value());
        }
    }

    SECTION("a noded note under a higher predecessor is refused, not stripped")
    {
        // The release would be a pull-off, and a pull-off releases onto a plain stopped pitch: the
        // node vetoes the clause. Under the stored-direction model the verb dropped the node to
        // make the conversion legal; now the claim is simply unjustified and the note is untouched.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 5),
        };
        chart.notes[1].harmonic_node = 17.0;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
        CHECK(planned.reason == ChartLegatoSkip::NoConnection);
    }

    SECTION("an open-string harmonic skips itself")
    {
        // Fret 0 with a node satisfies neither clause at any predecessor: the pull is vetoed by the
        // node and the hammer has no stop above the predecessor to reach. No guard in the verb says
        // so — the resolver does.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 0),
        };
        chart.notes[1].harmonic_node = 12.0;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.skipped == 1);
    }
}

// Mixed selections claim what they can and leave the rest, rather than refusing wholesale — the
// mixed-validity policy's "apply where valid" — and report the remainder.
TEST_CASE("planSetLegato applies to the resolvable subset of a selection", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 3),
        makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        // String 2's note is first on its string, so nothing justifies a claim on it.
        makeTestNote({.measure = 1, .beat = 2}, 2, 4),
    };
    chart.notes[0].sustain = common::core::Fraction{1};

    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1),
        keyAt({.measure = 1, .beat = 2}, 2),
    };
    const ChartLegatoPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
    REQUIRE(planned.plan.has_value());
    if (planned.plan.has_value())
    {
        // Only the resolvable note changes; the other keeps its pick and stays out of the plan.
        REQUIRE(planned.plan->inserted.size() == 1);
        CHECK(planned.plan->inserted[0].string == 1);
        CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Legato);
    }
    CHECK(planned.skipped == 1);
    CHECK(planned.reason == ChartLegatoSkip::NoPredecessor);
}

// A left-hand tap is a LOCAL statement, so the resolver reports its motion unconditionally — but
// the press must not read that as justification, or it would replace an authored tap with a claim
// the chart cannot keep.
TEST_CASE("planSetLegato asks the claim's own question of a left-hand tap", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a tap nothing justifies keeps its attack")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {makeTestNote({.measure = 1, .beat = 2}, 1, 7)};
        chart.notes[0].attack = common::core::NoteAttack::LeftTap;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        CHECK(planned.reason == ChartLegatoSkip::NoPredecessor);
    }

    SECTION("a tap the chart CAN justify becomes the claim")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
        };
        chart.notes[1].attack = common::core::NoteAttack::LeftTap;
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (planned.plan.has_value())
        {
            REQUIRE(planned.plan->inserted.size() == 1);
            CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Legato);
        }
    }
}

// Shrinking a tail no longer repairs the claim it disconnected: mid-burst the broken claim simply
// plays as the pick it sounds like, and the settle sweep is what flattens it — in one batch, at the
// moment the burst ends.
TEST_CASE("a shrink leaves its broken claim for the settle sweep", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 9),
        makeTestNote({.measure = 1, .beat = 3}, 1, 5),
    };
    chart.notes[0].sustain = common::core::Fraction{2};
    chart.notes[1].attack = common::core::NoteAttack::Legato;

    const auto plan = planAdjustSustain(
        chart,
        tempo_map,
        chart.notes,
        {keyAt({.measure = 1, .beat = 1}, 1)},
        {gridStep(g_quarter_grid, false)});
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        // Only the tail is in the plan: the claim is untouched, and the chart stays valid with it.
        REQUIRE(plan->inserted.size() == 1);
        CHECK(plan->inserted[0].sustain == common::core::Fraction{1});
        applyAndValidate(chart, tempo_map, *plan);
        CHECK(chart.notes[1].attack == common::core::NoteAttack::Legato);
        const common::core::ChartConnections connections =
            common::core::chartConnections(chart.notes, tempo_map);
        REQUIRE(connections.legato.size() == 2);
        CHECK(connections.legato[1] == common::core::LegatoMotion::Unjustified);
    }

    // The sweep is what ends the transience, and it can be expressed against any base: against the
    // current stream it is a one-note plan, and against the PRE-BURST stream it carries the tail
    // change too, which is how the settle folds into the burst's own undo entry.
    common::core::Chart pre_burst = chart;
    pre_burst.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{2}),
        chart.notes[1],
    };
    const auto settled = planSettleLegato(chart, tempo_map, chart, "Settle Legato");
    REQUIRE(settled.has_value());
    if (settled.has_value())
    {
        REQUIRE(settled->inserted.size() == 1);
        CHECK(settled->inserted[0].attack == common::core::NoteAttack::Pick);
        CHECK(settled->label == "Settle Legato");

        // Idempotent, and silent when there is nothing to settle: that emptiness is exactly what
        // tells the controller to leave its coalescing windows armed.
        common::core::Chart clean = chart;
        REQUIRE(applyChartChange(clean, *settled).has_value());
        CHECK_FALSE(planSettleLegato(clean, tempo_map, clean, "Settle Legato").has_value());
    }
    const auto folded = planSettleLegato(chart, tempo_map, pre_burst, "Shrink Sustain");
    REQUIRE(folded.has_value());
    if (folded.has_value())
    {
        REQUIRE(folded->inserted.size() == 2);
        CHECK(folded->inserted[0].sustain == common::core::Fraction{1});
        CHECK(folded->inserted[1].attack == common::core::NoteAttack::Pick);
        CHECK(folded->label == "Shrink Sustain");
    }
}

// The settle fold's base is the CHART the replaced entry was applied to, not merely its notes: an
// arpeggio-hold conversion is a legal burst, and the fold has to describe the whole of it or the
// caller's walk-back would leave the conversion half-undone.
TEST_CASE("the settle fold describes the whole burst it replaces", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart pre_burst;
    pre_burst.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    pre_burst.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 3, 5, common::core::Fraction{2}),
        makeTestNote({.measure = 1, .beat = 3}, 3, 7),
    };
    pre_burst.notes[1].attack = common::core::NoteAttack::Legato;

    // The burst: the ringing note is converted into a silently-held stop, which leaves the claim
    // behind it with nothing to connect to. Mid-burst that is legal and transient — the sweep at
    // the next settle point is what flattens it.
    common::core::Chart burst = pre_burst;
    burst.notes[0].attack = common::core::NoteAttack::None;
    burst.notes[0].sustain = common::core::Fraction{};

    const auto settled = planSettleLegato(burst, tempo_map, pre_burst, "Hold Stop");
    REQUIRE(settled.has_value());
    if (settled.has_value())
    {
        // Applied to the pre-burst chart — which is exactly where the caller's walk-back leaves the
        // live chart — the plan lands on the burst's own state with the claim flattened.
        common::core::Chart applied = pre_burst;
        REQUIRE(applyChartChange(applied, *settled).has_value());
        REQUIRE(applied.notes.size() == 2);
        CHECK(applied.notes[0].attack == common::core::NoteAttack::None);
        CHECK(applied.notes[0].fret == 5);
        CHECK(applied.notes[1].attack == common::core::NoteAttack::Pick);

        // And one undo of the folded entry takes the whole burst back.
        REQUIRE(applyChartChange(applied, settled->reversed()).has_value());
        CHECK(applied == pre_burst);
    }
}

// E4's landing requirement binds both strike attacks, so the in-plan flatten must cover both: a tap
// or a left-hand tap on an open string with no node is not a tap at all. A junk `Tapped` flag is
// real Guitar Pro data, and before this the stream reached validation unrepaired and failed the
// whole import.
TEST_CASE("the in-plan flatten gives a stranded strike somewhere to land", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a strike already stranded in the stream is repaired by the plan that reaches it")
    {
        for (const common::core::NoteAttack attack :
             {common::core::NoteAttack::Tap, common::core::NoteAttack::LeftTap})
        {
            common::core::Chart chart;
            chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            chart.notes = {
                makeTestNote({.measure = 1, .beat = 1}, 1, 9),
                makeTestNote({.measure = 1, .beat = 2}, 1, 0),
            };
            chart.notes[0].sustain = common::core::Fraction{3, 4};
            chart.notes[1].attack = attack;
            CAPTURE(static_cast<int>(attack));

            // Through a real edit: retyping the predecessor leaves the strikeless strike in the
            // stream, and the gate would refuse the whole plan if the flatten had not converted it
            // first. Both attacks land on a plain pick — there is no direction left for one of them
            // to be rescued into, which is the asymmetry the stored-direction model needed and this
            // one does not.
            const auto retyped = planRetypeFrets(
                chart,
                tempo_map,
                {chart.notes[0]},
                7,
                /*set_exact=*/true,
                common::core::ChartStopChannel::Sounding);
            REQUIRE(retyped.has_value());
            if (retyped.has_value())
            {
                const auto struck = std::ranges::find_if(
                    retyped->inserted,
                    [](const common::core::ChartNote& note) { return note.fret == 0; });
                REQUIRE(struck != retyped->inserted.end());
                if (struck != retyped->inserted.end())
                {
                    CHECK(struck->attack == common::core::NoteAttack::Pick);
                }
                applyAndValidate(chart, tempo_map, *retyped);
            }
        }
    }

    // The one thing that CAN take a left-hand tap away, and the reason it is intra-note: a tap
    // needs a stop to strike, so its own fret reaching the open string strands it. Nothing
    // relational may touch it — no neighbour, no sweep, no plain `H` — but this is the note's own
    // edit, so the flatten rides that entry and one undo restores the tap with the fret.
    SECTION("a strike its OWN edit strands flattens inside that edit's plan")
    {
        for (const common::core::NoteAttack attack :
             {common::core::NoteAttack::Tap, common::core::NoteAttack::LeftTap})
        {
            common::core::Chart chart;
            chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            chart.notes = {makeTestNote({.measure = 1, .beat = 1}, 1, 5)};
            chart.notes[0].attack = attack;
            CAPTURE(static_cast<int>(attack));

            const auto stranded = planRetypeFrets(
                chart,
                tempo_map,
                {chart.notes[0]},
                0,
                /*set_exact=*/true,
                common::core::ChartStopChannel::Sounding);
            REQUIRE(stranded.has_value());
            if (stranded.has_value())
            {
                REQUIRE(stranded->inserted.size() == 1);
                CHECK(stranded->inserted[0].fret == 0);
                CHECK(stranded->inserted[0].attack == common::core::NoteAttack::Pick);
                applyAndValidate(chart, tempo_map, *stranded);
            }

            // A node is the other thing a strike can land on, so the same retype leaves the attack
            // standing: the gate is `fret > 0 || node`, not `fret > 0`.
            common::core::Chart noded;
            noded.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            noded.notes = {makeTestNote({.measure = 1, .beat = 1}, 1, 5)};
            noded.notes[0].attack = attack;
            noded.notes[0].harmonic_node = 12.0;
            const auto kept = planRetypeFrets(
                noded,
                tempo_map,
                {noded.notes[0]},
                0,
                /*set_exact=*/true,
                common::core::ChartStopChannel::Sounding);
            REQUIRE(kept.has_value());
            if (kept.has_value())
            {
                REQUIRE(kept->inserted.size() == 1);
                CHECK(kept->inserted[0].attack == attack);
                applyAndValidate(noded, tempo_map, *kept);
            }
        }
    }
}

// The verb's cases, at the planner. Converting is the only fret-stating path there is; sounding a
// hold again is its inverse; an empty slot gains the neutral open-string hold the charter then
// types a stop onto. Every one of them passes the shared finalize, so a result the document reader
// would reject refuses here.
TEST_CASE("planToggleSilentHold authors, converts and sounds again", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::Fraction step{1, 4};

    SECTION("an empty slot gains a hold at the open string")
    {
        const ChartSlotKey slot{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 3};
        const auto plan = planToggleSilentHold(chart, tempo_map, {slot}, step);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            CHECK(plan->removed.empty());
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted.front().attack == common::core::NoteAttack::None);
            CHECK(plan->inserted.front().fret == 0);
            CHECK(plan->inserted.front().sustain == common::core::Fraction{});
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a note is converted, and the hold carries its fret")
    {
        // The techniques the new attack cannot state go with the ring: the note here is a plain
        // one, so a palm mute is added first to prove the strip rather than assume it.
        chart.notes[0].palm_mute = true;
        const ChartSlotKey slot{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1};
        const auto plan = planToggleSilentHold(chart, tempo_map, {slot}, step);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->removed.size() == 1);
            CHECK(plan->removed.front().fret == 3);
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted.front().attack == common::core::NoteAttack::None);
            // Position, string and FRET survive; the ring and the technique do not.
            CHECK(plan->inserted.front().position == plan->removed.front().position);
            CHECK(plan->inserted.front().string == plan->removed.front().string);
            CHECK(plan->inserted.front().fret == 3);
            CHECK(plan->inserted.front().sustain == common::core::Fraction{});
            CHECK_FALSE(plan->inserted.front().palm_mute);
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a hold is sounded again at the caller's step")
    {
        chart.notes[0].attack = common::core::NoteAttack::None;
        chart.notes[0].sustain = common::core::Fraction{};
        const ChartSlotKey slot{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1};
        const auto plan = planToggleSilentHold(chart, tempo_map, {slot}, step);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted.front().attack == common::core::NoteAttack::Pick);
            CHECK(plan->inserted.front().fret == 3);
            CHECK(plan->inserted.front().sustain == step);
            applyAndValidate(chart, tempo_map, *plan);
        }
    }
}

// The releasing direction's LABEL, which has to say what the press did. Two pure scopes have their
// own words, and a MIXED one gets the plural (user ruling 2026-08-27): both kinds ARE held-stop
// releases, so the plural is the one word true of every slot in the press, where either singular
// would lie about half of it.
TEST_CASE("planToggleSilentHold labels a mixed release as held stops", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // One sounding note so both stated stops reach a shape, a silently-held stop, and a tap
    // carrying its own held stop — the two shapes a release can take, at one slot.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{}),
        makeTestNote({.measure = 2, .beat = 1}, 3, 12, common::core::Fraction{1, 2}),
    };
    chart.notes[1].attack = common::core::NoteAttack::None;
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    chart.notes[2].held = 7;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::Fraction step{1, 4};

    const ChartSlotKey silent{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2};
    const ChartSlotKey riding{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 3};

    const auto mixed = planToggleSilentHold(chart, tempo_map, {silent, riding}, step);
    REQUIRE(mixed.has_value());
    if (mixed.has_value())
    {
        CHECK(mixed->label == "Release Held Stops");
    }

    // The two discriminations, so the plural is proven to come from the MIXTURE and not from the
    // count of slots: each shape alone keeps its own singular word.
    const auto sounded = planToggleSilentHold(chart, tempo_map, {silent}, step);
    REQUIRE(sounded.has_value());
    if (sounded.has_value())
    {
        CHECK(sounded->label == "Sound Note");
    }
    const auto released = planToggleSilentHold(chart, tempo_map, {riding}, step);
    REQUIRE(released.has_value());
    if (released.has_value())
    {
        CHECK(released->label == "Release Held Stop");
    }
}

// Whole-plan atomicity over BOTH shapes of claim. The settle takes a claimed stop that reaches no
// shape, and what it takes differs by shape — the whole note where the note IS the claim, the field
// alone where a sounding onset carries it — so a press judged by which notes vanished would apply
// half of itself the moment its subject was the field.
TEST_CASE("planToggleSilentHold refuses a press whose statement the settle takes", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // A dyad on strings 1 and 3 holding across the next beat, where the string-1 stop is stated by
    // SOUND at the OPEN string — the very stop the verb plants under a right-hand onset — so a hand
    // claiming string 1 again there restates what the shape already says, which adds nothing, and
    // the settle takes that claim. A different fret there would say the hand had MOVED, which
    // states a shape of its own (the mid-span continue/split law) and would not be swept at all.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{1}),
        makeTestNote({.measure = 2, .beat = 1}, 3, 9, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 2}, 1, 12, common::core::Fraction{1, 2}),
        makeTestNote({.measure = 2, .beat = 2}, 2, 7, common::core::Fraction{1, 2}),
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    const common::core::TempoMap tempo_map = makeTempoMap();

    const std::vector<ChartSlotKey> both{
        ChartSlotKey{.position = {.measure = 2, .beat = 2, .offset = {}}, .string = 1},
        ChartSlotKey{.position = {.measure = 2, .beat = 2, .offset = {}}, .string = 2}
    };
    // The tap would state a held stop on a string the shape already sounds, which states nothing;
    // the string-2 note would convert into a stop that does. Half a press is not the press.
    const auto refused = planToggleSilentHold(chart, tempo_map, both, common::core::Fraction{1, 4});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == ChartPlanRefusal::Invalid);

    // The discrimination: the string-2 slot ALONE states a stop that survives, so the same press
    // over the scope that leaves the tap out is an ordinary conversion.
    const auto accepted =
        planToggleSilentHold(chart, tempo_map, {both[1]}, common::core::Fraction{1, 4});
    REQUIRE(accepted.has_value());
    if (accepted.has_value())
    {
        REQUIRE(accepted->inserted.size() == 1);
        CHECK(accepted->inserted.front().attack == common::core::NoteAttack::None);
    }
}

// The range verbs carry silently-held stops with no case of their own, because they are notes.
TEST_CASE("The range verbs carry silently held stops", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    common::core::ChartNote hold = makeTestNote({.measure = 2, .beat = 1}, 3, 5);
    hold.attack = common::core::NoteAttack::None;
    hold.sustain = common::core::Fraction{};
    chart.notes.push_back(hold);
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> hold_key{
        ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 3}
    };
    const std::vector<ChartSlotKey> note_key{
        ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1}
    };

    SECTION("deleting a hold alone is one note deleted")
    {
        const auto plan = planDeleteSelection(chart, tempo_map, hold_key, {});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            CHECK(plan->label == "Delete Note");
            REQUIRE(plan->removed.size() == 1);
            CHECK(plan->removed.front().attack == common::core::NoteAttack::None);
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a moved hold rides along with the notes")
    {
        std::vector<ChartSlotKey> both = note_key;
        both.insert(both.end(), hold_key.begin(), hold_key.end());
        std::ranges::sort(both);
        const auto plan = planMoveSelection(
            chart, tempo_map, both, common::core::Fraction{1}, 0, "Move Selection");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 2);
            for (const common::core::ChartNote& moved : plan->inserted)
            {
                CHECK(
                    moved.position ==
                    common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
            }
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a note moved onto a held slot is refused")
    {
        const std::vector<ChartSlotKey> other_note{
            ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2}
        };
        const auto refused = planMoveSelection(
            chart, tempo_map, other_note, common::core::Fraction{}, 1, "Move Note");
        REQUIRE_FALSE(refused.has_value());
        if (!refused.has_value())
        {
            CHECK(refused.error() == ChartPlanRefusal::Invalid);
        }
    }
}

// The conversion is one plan against one array, so undoing it is the same primitive run backwards
// and a failed precondition leaves the chart untouched.
TEST_CASE("A conversion applies and reverses atomically", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const ChartSlotKey slot{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1};
    const auto plan = planToggleSilentHold(chart, tempo_map, {slot}, common::core::Fraction{1, 4});
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    REQUIRE(applyChartChange(chart, *plan).has_value());
    CHECK(chart.notes.size() == original.notes.size());
    CHECK(chart.notes[0].attack == common::core::NoteAttack::None);

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);

    // A stale removal rejects the whole change and leaves the chart where it was.
    common::core::Chart other = makeTestChart();
    ChartEditPlan stale = *plan;
    stale.removed = {makeTestNote({.measure = 9, .beat = 1}, 1, 5)};
    const auto rejected = applyChartChange(other, stale);
    CHECK_FALSE(rejected.has_value());
    CHECK(other == original);
}

// `Shift+L` on a selected keyframe severs the gesture there (W10's 2026-08-26 addendum): the
// origin's path ENDS at the junction and a new head takes the remainder. The origin keeps the
// keyframe it arrives at — the leg the user split at is real travel — so the junction is the
// equal-fret handover W10's ruling 2 names, and the later keyframes rebase onto the new onset.
TEST_CASE("planDisconnectKeyframes severs a glide at its junction", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planDisconnectKeyframes(
        chart,
        tempo_map,
        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
        "Disconnect Keyframe");
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    CHECK(plan->label == "Disconnect Keyframe");
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 2);
    const common::core::ChartNote& origin = chart.notes[0];
    CHECK(origin.position == glideOnset());
    CHECK(origin.fret == 7);
    CHECK(origin.sustain == common::core::Fraction{2});
    CHECK(origin.attack == common::core::NoteAttack::Pick);
    REQUIRE(origin.keyframes.size() == 1);
    // The arrival retreats by the glide-into-a-landing margin (a quarter beat in 4/4): a
    // fret-stating keyframe may not sit on a later onset of its own string, because the head
    // states those coordinates itself. The RING below still runs to that head.
    CHECK(
        origin.keyframes[0].offset ==
        common::core::Fraction{2} - common::core::minimumSustainDistanceBeats(4));
    CHECK(origin.keyframes[0].fret == 9);

    const common::core::ChartNote& product = chart.notes[1];
    CHECK(product.position == common::core::GridPosition{.measure = 2, .beat = 3, .offset = {}});
    CHECK(product.string == 1);
    // The remainder is the same note restarted: its fret is the junction's, its ring is what was
    // left, and its own later keyframe rides along rebased onto the new onset (4 - 2 = 2).
    CHECK(product.fret == 9);
    CHECK(product.sustain == common::core::Fraction{2});
    REQUIRE(product.keyframes.size() == 1);
    CHECK(product.keyframes[0].offset == common::core::Fraction{2});
    CHECK(product.keyframes[0].fret == 12);
    // W10's signed store for a split head — never Pick, never a stored tie. The addendum's
    // proposed UNSTRUCK reading needs LegatoMotion::Continuation, which is unbuilt, so this is a
    // claim today's settle sweep still flattens; the default is a proposal, not a ruling.
    CHECK(product.attack == common::core::NoteAttack::Legato);

    // One entry, and it reverses field for field.
    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// A head must sit on a stated fret and needs a remainder to take (W10's ruling 2). Both refusals
// are Invalid rather than a clamp: rounding the interpolated fret between stating points was
// killed explicitly as invented data, and a key naming no keyframe at all is simply skipped.
TEST_CASE("planDisconnectKeyframes refuses what cannot carry a head", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a keyframe stating no fret")
    {
        common::core::Chart chart = makeGlideChart();
        // A mid-travel shake statement: a real keyframe that says nothing about where the hand is.
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(),
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .vibrato = common::core::VibratoState::Narrow
            });
        const common::core::Chart original = chart;
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
        CHECK(chart == original);
    }

    SECTION("a keyframe at the ring's end")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4})},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a junction with no room for the retreated arrival")
    {
        common::core::Chart chart = makeGlideChart();
        // Exactly one margin after the onset: retreating the arrival would put it AT the onset,
        // where no keyframe may sit. Refused rather than clamped onto the onset — a clamped
        // arrival is an arrival time nobody authored.
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(),
            common::core::Keyframe{
                .offset = common::core::minimumSustainDistanceBeats(4), .fret = 8
            });
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::minimumSustainDistanceBeats(4))},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a junction crowding the statement before it")
    {
        common::core::Chart chart = makeGlideChart();
        // The other half of the no-room refusal: the retreat has to clear the STATEMENT before
        // the junction as well as the onset. An eighth of a beat is inside the 4/4 margin, so
        // retreating the arrival at two beats would put it before the statement at 15/8.
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(),
            common::core::Keyframe{.offset = common::core::Fraction{15, 8}, .fret = 8});
        const common::core::Chart original = chart;
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
        CHECK(chart == original);
    }

    SECTION("a scrape, whose terminal the origin would lose")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote scrape =
            makeTestNote(glideOnset(), 1, 9, common::core::Fraction{4});
        scrape.attack = common::core::NoteAttack::PickSlide;
        scrape.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 5}};
        scrape.slide_out = 3;
        chart.notes = {std::move(scrape)};

        // The origin ends at a stated fret now, so it keeps no falls-away — and a scrape's
        // terminal is required, so the gate refuses the whole split rather than shipping a
        // pick slide that stops travelling.
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a key naming no keyframe")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan = planDisconnectKeyframes(
            chart,
            tempo_map,
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{3})},
            "Disconnect Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }
}

// The uniform-scope law under vibrato's TWO scopes: the direction a press means is read across
// every anchor the selection holds, notes and keyframes alike, so a press clears only when all of
// them already shake. The planner tests above cover what a press writes; this covers which press
// it is, which is the half `ChartTechniqueLaw::carried` owns.
TEST_CASE("The vibrato law reads both its scopes for the direction", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
    note.vibrato = common::core::VibratoState::Narrow;
    note.keyframes = {
        // The shake ends here, so the state in force AT this point is still.
        common::core::Keyframe{
            .offset = common::core::Fraction{2},
            .fret = 9,
            .vibrato = common::core::VibratoState::Off
        },
        // And starts again here.
        common::core::Keyframe{
            .offset = common::core::Fraction{3}, .vibrato = common::core::VibratoState::Narrow
        },
    };
    chart.notes = {std::move(note)};
    const ChartTechniqueLaw law = chartTechniqueLaw(ChartTechnique::Vibrato);
    const ChartSelectionKey note_key = ChartNoteKey{.slot = keyAt(glideOnset(), 1)};

    SECTION("a keyframe carries the shake when the state in force where it stands is shaking")
    {
        ChartSelection selection;
        selection.add(
            ChartKeyframeKey{.note = keyAt(glideOnset(), 1), .offset = common::core::Fraction{3}});
        CHECK(law.carried(chart, selection));
    }

    SECTION("and does not when its own statement is the one that stopped it")
    {
        ChartSelection selection;
        selection.add(
            ChartKeyframeKey{.note = keyAt(glideOnset(), 1), .offset = common::core::Fraction{2}});
        CHECK_FALSE(law.carried(chart, selection));
    }

    SECTION("a mixed selection clears only when every anchor in it already shakes")
    {
        ChartSelection shaking;
        shaking.add(note_key);
        shaking.add(
            ChartKeyframeKey{.note = keyAt(glideOnset(), 1), .offset = common::core::Fraction{3}});
        CHECK(law.carried(chart, shaking));

        // The onset shakes and the other anchor does not, so the press means SET — the same
        // partly-carried answer a mixed note selection has always given.
        ChartSelection mixed;
        mixed.add(note_key);
        mixed.add(
            ChartKeyframeKey{.note = keyAt(glideOnset(), 1), .offset = common::core::Fraction{2}});
        CHECK_FALSE(law.carried(chart, mixed));
    }

    SECTION("a selection this technique reaches nothing in means SET, and plans to nothing")
    {
        // A key naming a slot the chart holds nothing on: the operand resolves to no note at all,
        // which is the empty-operand rule every verb follows rather than a case of its own.
        ChartSelection nothing_reached;
        nothing_reached.add(ChartNoteKey{.slot = keyAt(glideOnset(), 2)});
        CHECK_FALSE(law.carried(chart, nothing_reached));
        const auto plan = law.plan(chart, makeTempoMap(), nothing_reached, true, "Vibrato");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }
}

// Uniform scope, one level inside the note: every selected keyframe on a note splits it, so two
// selected junctions make three. The channel states in force at each split become the product's
// ONSET values, which is what keeps the sound identical across the cut, and the falls-away
// terminal goes with the last product because a slide-out is the ring's end by definition.
TEST_CASE("planDisconnectKeyframes splits at every selected junction", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote glide = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
    glide.keyframes = {
        common::core::Keyframe{
            .offset = common::core::Fraction{1},
            .fret = 9,
            .bend = 1.0,
            .vibrato = common::core::VibratoState::Narrow
        },
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 11},
    };
    glide.slide_out = 3;
    chart.notes = {std::move(glide)};
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planDisconnectKeyframes(
        chart,
        tempo_map,
        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1}),
         keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
        "Disconnect Keyframe");
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 3);
    CHECK(chart.notes[0].sustain == common::core::Fraction{1});
    CHECK(chart.notes[1].sustain == common::core::Fraction{1});
    CHECK(chart.notes[2].sustain == common::core::Fraction{2});

    // The state the first junction states is what the second note OPENS with, so the push and the
    // shake carry across the cut instead of restarting at the note's own defaults.
    const common::core::ChartNote& second = chart.notes[1];
    CHECK(second.fret == 9);
    CHECK_THAT(second.bend, Catch::Matchers::WithinULP(1.0, 0));
    CHECK(second.vibrato == common::core::VibratoState::Narrow);
    CHECK_FALSE(second.slide_out.has_value());
    // Its own arrival retreats by the same margin before the head that follows it.
    REQUIRE(second.keyframes.size() == 1);
    CHECK(
        second.keyframes[0].offset ==
        common::core::Fraction{1} - common::core::minimumSustainDistanceBeats(4));
    CHECK(second.keyframes[0].fret == 11);

    // The third opens at the second junction, where the shake still stands and the bend has not
    // been restated — and it is the one that ends where the gesture did, so it keeps the terminal.
    const common::core::ChartNote& third = chart.notes[2];
    CHECK(third.fret == 11);
    CHECK_THAT(third.bend, Catch::Matchers::WithinULP(1.0, 0));
    CHECK(third.vibrato == common::core::VibratoState::Narrow);
    REQUIRE(third.slide_out.has_value());
    CHECK(*third.slide_out == 3);
    // The earlier products end at a stated fret instead, so neither invents a trail-off.
    CHECK_FALSE(chart.notes[0].slide_out.has_value());

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// The vibrato channel's two authoring scopes are ONE planner, because they are one channel: the
// note's own field is the statement at offset zero and a keyframe states a change from there.
// This is the plain half — a selected keyframe takes the statement, and the onset it rides is
// left exactly as the charter wrote it.
TEST_CASE("planSetVibrato states the shake at a selected keyframe", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planSetVibrato(
        chart,
        tempo_map,
        {},
        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
        common::core::VibratoState::Narrow,
        "Vibrato");
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& note = chart.notes[0];
    // The note reached only through its keyframe keeps the shake it opens with: a keyframe's
    // statement is a change from the onset, never a rewrite of it.
    CHECK_FALSE(common::core::isShaking(note.vibrato));
    REQUIRE(note.keyframes.size() == 2);
    CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Narrow);
    CHECK_FALSE(note.keyframes[1].vibrato.has_value());
    // The position channel is untouched — the coupling law works because the keyframe is one
    // record, not because a verb copies fields between channels.
    CHECK(note.keyframes[0].fret == 9);

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// The dissolve law's static half, generalized (user, 2026-08-26): a pending point dissolves iff
// it changes NEITHER the path function NOR the state. Every case of the described flow falls out
// of that one rule, which is why these three sections share a planner and not a branch.
TEST_CASE("planSetVibrato dissolves a statement that changes nothing", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a shake stated again inside a region it already covers leaves no point behind")
    {
        common::core::Chart chart = makeGlideChart();
        chart.notes[0].vibrato = common::core::VibratoState::Narrow;
        const auto plan = planSetVibrato(
            chart,
            tempo_map,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            common::core::VibratoState::Narrow,
            "Vibrato");
        // Nothing is authored at all: the statement restates the state in force where it stands,
        // so it is dropped, and dropping it leaves the keyframe exactly as it was.
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }

    SECTION("clearing the shake from a point whose only job it was dissolves the point")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // A delayed start: the ring opens still and shakes from two beats in. Nothing about the
        // hand's position is stated, so this point exists for the shake alone.
        note.keyframes = {common::core::Keyframe{
            .offset = common::core::Fraction{2}, .vibrato = common::core::VibratoState::Narrow
        }};
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
            chart,
            tempo_map,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            common::core::VibratoState::Off,
            "Remove Vibrato");
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The chart may never hold a keyframe stating nothing, so the point goes with the
        // statement — through the one strip authority, not a removal rule written here.
        CHECK(chart.notes[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("only the statements this press wrote are judged for redundancy")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {
            // Already redundant, and authored by someone else: this press never points at it.
            common::core::Keyframe{
                .offset = common::core::Fraction{1},
                .fret = 9,
                .vibrato = common::core::VibratoState::Narrow
            },
            // The shake ends here until the press below states it again.
            common::core::Keyframe{
                .offset = common::core::Fraction{2},
                .fret = 11,
                .vibrato = common::core::VibratoState::Off
            },
        };
        chart.notes = {std::move(note)};

        const auto plan = planSetVibrato(
            chart,
            tempo_map,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            common::core::VibratoState::Narrow,
            "Vibrato");
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 2);
        // The untouched restatement stays: quietly rewriting it would make this press an editor
        // of data the user never pointed at.
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
        // The written one said what was already true, so it is no statement — but the keyframe
        // still states a fret, so the point itself stands.
        CHECK_FALSE(chart.notes[0].keyframes[1].vibrato.has_value());
        CHECK(chart.notes[0].keyframes[1].fret == 11);
    }
}

// The note scope keeps its onset semantics exactly: pressing the verb with the NOTE selected
// writes the channel's opening statement and leaves every keyframe's statement alone, redundant
// or not, because this press wrote none of them.
TEST_CASE("planSetVibrato on a note writes the onset alone", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    chart.notes[0].keyframes[1].vibrato = common::core::VibratoState::Narrow;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planSetVibrato(
        chart,
        tempo_map,
        {keyAt(glideOnset(), 1)},
        {},
        common::core::VibratoState::Narrow,
        "Vibrato");
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].vibrato == common::core::VibratoState::Narrow);
    REQUIRE(chart.notes[0].keyframes.size() == 2);
    CHECK(chart.notes[0].keyframes[1].vibrato == common::core::VibratoState::Narrow);
}

// Delete is the same verb one level in: it takes every statement the selected keyframe makes, so
// the keyframe always empties and always goes. The label names what was actually deleted.
TEST_CASE("planDeleteSelection takes a keyframe and its statements", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("one keyframe leaves the note and its siblings standing")
    {
        common::core::Chart chart = makeGlideChart();
        const common::core::Chart original = chart;
        const auto plan = planDeleteSelection(
            chart, tempo_map, {}, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        CHECK(plan->label == "Delete Keyframe");
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{4});
        CHECK(chart.notes[0].keyframes[0].fret == 12);
        // The note's own onset facts are none of this verb's business.
        CHECK(chart.notes[0].fret == 7);
        CHECK(chart.notes[0].sustain == common::core::Fraction{4});

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("two keyframes are counted as keyframes")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan = planDeleteSelection(
            chart,
            tempo_map,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2}),
             keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        CHECK(plan->label == "Delete 2 Keyframes");
    }

    SECTION("a keyframe whose note goes too needs no removal of its own")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan = planDeleteSelection(
            chart,
            tempo_map,
            {keyAt(glideOnset(), 1)},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        // The note takes its whole ring with it, so the count stays one note — not one note and
        // a keyframe that would have named a record no longer there.
        CHECK(plan->label == "Delete Note");
        applyAndValidate(chart, tempo_map, *plan);
        CHECK(chart.notes.empty());
    }
}

// DERIVED HELD, the editor half (user ruling 2026-08-31). Authoring the pull-off is what makes the
// stored field a second spelling of one fact, so the entry that authors it is the entry that
// clears the field — and the stop itself does not move, because the notation now states it.
TEST_CASE("Authoring a pull-off clears the held stop it states", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::GridPosition tap_slot{.measure = 2, .beat = 1, .offset = {}};
    const common::core::GridPosition successor_slot{.measure = 2, .beat = 2, .offset = {}};

    SECTION("the field goes and the resolved stop stays, in one entry")
    {
        // A CONTRADICTING stored value, so the clearing cannot be mistaken for a no-op: the field
        // says 7 and the pull-off is about to say 5.
        common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, 7);
        REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{7});

        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt(successor_slot, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (!planned.plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *planned.plan);

        const common::core::ChartNote* const tap = noteAt(chart.notes, tap_slot, 1);
        REQUIRE(tap != nullptr);
        if (tap == nullptr)
        {
            return;
        }
        CHECK_FALSE(tap->held.has_value());
        // The tap itself is untouched otherwise: its onset belongs to the picking hand.
        CHECK(tap->attack == common::core::NoteAttack::Tap);
        CHECK(tap->fret == 12);
        // And the stop is STILL STATED — by the notation, at the pull-off's own fret.
        CHECK(claimedStops(chart, tempo_map).front() == std::optional{5});
    }

    SECTION("an AGREEING stored value goes too — one statement, not two")
    {
        common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, 5);
        const ChartLegatoPlan planned =
            planSetLegato(chart, tempo_map, {keyAt(successor_slot, 1)}, "Legato");
        REQUIRE(planned.plan.has_value());
        if (!planned.plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *planned.plan);
        const common::core::ChartNote* const tap = noteAt(chart.notes, tap_slot, 1);
        REQUIRE(tap != nullptr);
        if (tap == nullptr)
        {
            return;
        }
        CHECK_FALSE(tap->held.has_value());
        CHECK(claimedStops(chart, tempo_map).front() == std::optional{5});
    }

    SECTION("without the pull-off the stored value is authority and stays")
    {
        // The discrimination: the same figure with the successor left a plain pick keeps its field,
        // so what clears it above is the derivation and not the plan gate's other sweeps.
        common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, 7);
        const common::core::ChartNote* const before = noteAt(chart.notes, tap_slot, 1);
        REQUIRE(before != nullptr);
        if (before == nullptr)
        {
            return;
        }
        const auto plan = planRetypeFrets(
            chart,
            tempo_map,
            {*before},
            9,
            /*set_exact=*/true,
            common::core::ChartStopChannel::Held);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        const common::core::ChartNote* const tap = noteAt(chart.notes, tap_slot, 1);
        REQUIRE(tap != nullptr);
        if (tap == nullptr)
        {
            return;
        }
        CHECK(tap->held == std::optional{9});
    }
}

// The other half of the same ruling: where the derivation owns the stop, authoring one is REFUSED
// rather than skipped — the charter typed at a value the notation states, and the pending box has
// to paint that red instead of reporting a digit that landed nowhere.
TEST_CASE("The held channel is refused where a pull-off states the stop", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::GridPosition tap_slot{.measure = 2, .beat = 1, .offset = {}};
    const common::core::Chart chart =
        makeDerivedHeldChart(common::core::NoteAttack::Legato, std::nullopt);
    REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{5});

    const common::core::ChartNote* const tap = noteAt(chart.notes, tap_slot, 1);
    REQUIRE(tap != nullptr);
    if (tap == nullptr)
    {
        return;
    }
    const auto plan = planRetypeFrets(
        chart, tempo_map, {*tap}, 9, /*set_exact=*/true, common::core::ChartStopChannel::Held);
    REQUIRE_FALSE(plan.has_value());
    if (plan.has_value())
    {
        return;
    }
    // Invalid, never NoChange: the two emptinesses are what the pending box's red state reads.
    CHECK(plan.error() == ChartPlanRefusal::Invalid);
}

// THE DEFAULT SATELLITE IS A TARGET (user ruling 2026-09-02), which is the other side of the
// refusal above and the reason the two must not be answered by one test. A bare tap's held stop
// resolves to the grip under it — 0 where no span covers it — so the satellite that states it is
// DRAWN, and the held channel reaches every right-hand onset. Typing there AUTHORS a real held
// stop, because nothing owns a default.
//
// ITS BEHAVIOURAL HALF FAILS UNDER PRE-CHANGE CODE, deliberately: the channel was gated on the
// STORED field, so a digit typed at a tap that stated nothing was passed through untouched and the
// plan diffed empty — `planRetypeFrets` answered NoChange where this case requires a plan. (The
// resolution line above is new API, so the case as a whole is also new; the section in
// `test_chart_projection.cpp` carries the same claim against unchanged signatures.)
TEST_CASE("The held channel authors at a bare tap's default satellite", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::GridPosition tap_slot{.measure = 2, .beat = 1, .offset = {}};
    common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, std::nullopt);
    // Nothing states a stop under this tap: no stored field, and the successor is a plain pick, so
    // there is no pull-off to derive one either.
    REQUIRE_FALSE(claimedStops(chart, tempo_map).front().has_value());
    // The resolution answers anyway, and THE DEFAULT is what its satellite prints — the open
    // string, since a lone member states no shape and nothing covers this tap.
    const common::core::ChartResolutions resolutions =
        common::core::chartResolutions(chart.notes, tempo_map);
    REQUIRE(resolutions.held_stops.front() == std::optional{0});

    const common::core::ChartNote* const before = noteAt(chart.notes, tap_slot, 1);
    REQUIRE(before != nullptr);
    if (before == nullptr)
    {
        return;
    }
    const auto plan = planRetypeFrets(
        chart, tempo_map, {*before}, 9, /*set_exact=*/true, common::core::ChartStopChannel::Held);
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);
    const common::core::ChartNote* const tap = noteAt(chart.notes, tap_slot, 1);
    REQUIRE(tap != nullptr);
    if (tap == nullptr)
    {
        return;
    }
    // A real AUTHORED stop now, in the field the charter's ink lives in — the tier moved from
    // default to authored, which is the whole of what this satellite is for.
    CHECK(tap->held == std::optional{9});
    CHECK(claimedStops(chart, tempo_map).front() == std::optional{9});
    // And the tap itself is untouched: the held channel addresses the stop under the onset, never
    // the fret the picking hand sounds.
    CHECK(tap->fret == 12);
    CHECK(tap->attack == common::core::NoteAttack::Tap);
}

// THE SAME REFUSAL FROM THE OTHER VERB. N states the fretting hand's stop at a slot, or releases
// it, and where a PULL-OFF states that stop there is neither a field to write nor one to clear —
// withdrawing the statement would mean unwriting the pull-off, which is not this verb's act. So
// the press is REFUSED in either direction rather than quietly doing nothing, which is what
// reading the raw field left it doing: the entry diffed empty and the refusal came back as
// NoChange, silent where the pending box needs a red.
TEST_CASE("Arpeggio hold is refused where a pull-off states the stop", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::Fraction step{1, 4};
    const std::vector<ChartSlotKey> tap_slot{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("the pulled-off tap is refused")
    {
        const common::core::Chart chart =
            makeDerivedHeldChart(common::core::NoteAttack::Legato, std::nullopt);
        REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{5});
        const auto plan = planToggleSilentHold(chart, tempo_map, tap_slot, step);
        REQUIRE_FALSE(plan.has_value());
        if (!plan.has_value())
        {
            CHECK(plan.error() == ChartPlanRefusal::Invalid);
        }
    }

    SECTION("the same figure with a PICKED successor states the stop as ever")
    {
        // The discrimination is the successor's attack alone: nothing states the tap's stop now, so
        // the verb seeds the open string exactly as it does under any other right-hand onset, and
        // the tap's own sound is untouched.
        const common::core::Chart chart =
            makeDerivedHeldChart(common::core::NoteAttack::Pick, std::nullopt);
        REQUIRE_FALSE(claimedStops(chart, tempo_map).front().has_value());
        const auto plan = planToggleSilentHold(chart, tempo_map, tap_slot, step);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            CHECK(plan->inserted.front().held == std::optional{0});
            CHECK(plan->inserted.front().attack == common::core::NoteAttack::Tap);
            CHECK(plan->inserted.front().fret == 12);
        }
    }
}

} // namespace rock_hero::editor::core
