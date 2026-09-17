#include "chart/chart_edits.h"
#include "chart/pick_slide_defaults.h"

#include <algorithm>
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
#include <string_view>
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
    common::core::setSlideOut(note, 12);
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

// A six-string chart holding exactly one plain note on the low string at `fret`. The harmonic
// round trip needs a fresh stream per label, because its whole claim is that the clear gives back
// the fret the set consumed.
[[nodiscard]] common::core::Chart makeSingleNoteChart(int fret)
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, fret)};
    return chart;
}

// A glide over a four-beat ring: fret 7 from the onset, arriving at fret 9 two beats in and
// stating fret 12 exactly where the ring ends — which is the RELEASE, the fret the hand leaves
// toward, since a statement at the ring's end is a fret the note never sounds. One note, so a
// plan's whole effect on the stream is readable without hunting for the record it touched.
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

// A four-beat glide on string 1 stating frets a beat apart at beats 2 and 3, so every bound a
// stepped offset can reach — the onset below it, the ring above it, and the neighbour beside it —
// is one step away from something.
[[nodiscard]] common::core::Chart makeSteppedGlideChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4});
    glide.keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 7},
        common::core::Keyframe{.offset = common::core::Fraction{3}, .fret = 9},
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

// THE MIXED FIGURE: the derived tap above plus a BARE tap on string 3 at the same instant. One
// entry then addresses two satellites of different tiers — one the notation owns, one owned by
// nobody — which is the only shape that can tell a whole-plan refusal from a per-note one.
[[nodiscard]] common::core::Chart makeMixedDerivedHeldChart()
{
    common::core::Chart chart =
        makeDerivedHeldChart(common::core::NoteAttack::Legato, std::nullopt);
    common::core::ChartNote bare =
        makeTestNote({.measure = 2, .beat = 1}, 3, 10, common::core::Fraction{1});
    bare.attack = common::core::NoteAttack::Tap;
    chart.notes.push_back(std::move(bare));
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    return chart;
}

// The NOTE-scope forms of the two planners that now take both selection operands. A scenario about
// heads alone says so by naming every note in its snapshot and no keyframe, which keeps the operand
// split visible exactly where a case exercises it — the keyframe cases call the planners directly.
[[nodiscard]] std::vector<ChartSlotKey> slotsOf(const std::vector<common::core::ChartNote>& notes)
{
    std::vector<ChartSlotKey> keys;
    keys.reserve(notes.size());
    for (const common::core::ChartNote& note : notes)
    {
        keys.push_back(chartSlotKeyOf(note));
    }
    std::ranges::sort(keys);
    return keys;
}

[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> retypeNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const ChartFretWrite write,
    common::core::ChartStopChannel channel)
{
    return planRetypeFrets(chart, tempo_map, base, slotsOf(base), {}, write, channel);
}

[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> moveNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, common::core::Fraction beat_delta, int string_delta,
    std::string_view label)
{
    return planMoveSelection(chart, tempo_map, note_keys, {}, beat_delta, string_delta, label);
}

// The SPLIT-only form of the junction toggle: keyframes alone as the operand, which is the whole
// of what every split case below asks for. The label is the planner's own answer now, so no
// caller supplies one, and the selection it plans is checked only where a case is about it.
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> splitAt(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartKeyframeKey>& keyframe_keys)
{
    std::expected<ChartJunctionPlan, ChartPlanRefusal> toggled =
        planToggleJunctions(chart, tempo_map, {}, keyframe_keys);
    if (!toggled.has_value())
    {
        return std::unexpected{toggled.error()};
    }
    return std::move(toggled->plan);
}

// The JOIN-only form: heads alone, the split's inverse asked of the same planner.
[[nodiscard]] std::expected<ChartJunctionPlan, ChartPlanRefusal> joinHeads(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& head_keys)
{
    return planToggleJunctions(chart, tempo_map, head_keys, {});
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
// Import is a commit point, so a note it cannot make legal takes the WHOLE song down — the failure
// mode whenever a hand-kept list of what to shed falls behind the rules. An exhaustive sweep is
// what retires that: a new incompatibility with no shed clause fails here rather than on someone's
// import.
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
    // test_pick_slide_defaults owns.
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

// The typed digit's location half: the caret slot resolves to the ring under it and to how far
// along that ring the slot sits, which is where a point the digit states would go.
TEST_CASE("chartPathTailAt reports which ring covers the slot and how far along", "[core][chart]")
{
    // Fret 5 from the onset, arriving at 7 two beats in and 9 three beats in, over a four-beat
    // ring: one leg before the first statement, one between two, and a hold past the last.
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const std::optional<ChartPathTail> mid = chartPathTailAt(
        chart.notes,
        tempo_map,
        {.measure = 2, .beat = 3, .offset = common::core::Fraction{1, 2}},
        1);
    REQUIRE(mid.has_value());
    if (mid.has_value())
    {
        CHECK(mid->note == keyAt(glideOnset(), 1));
        CHECK(mid->offset == common::core::Fraction{5, 2});
    }

    // The onset itself is no tail — its facts are the note's own — and neither is another string.
    CHECK_FALSE(chartPathTailAt(chart.notes, tempo_map, glideOnset(), 1).has_value());
    CHECK_FALSE(chartPathTailAt(chart.notes, tempo_map, {.measure = 2, .beat = 2}, 2).has_value());
    // Nor is anything past the ring's end.
    CHECK_FALSE(chartPathTailAt(chart.notes, tempo_map, {.measure = 3, .beat = 2}, 1).has_value());
}

// Every tail is an authoring surface for points, a plain note's included — it simply states no
// path of its own, which is no reason a digit cannot state one on it.
TEST_CASE("chartPathTailAt answers on a plain note's tail", "[core][chart]")
{
    // The fixture's string-1 note at measure 3 beat 1 rings two beats at fret 7 and states no path.
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const std::optional<ChartPathTail> plain =
        chartPathTailAt(chart.notes, tempo_map, {.measure = 3, .beat = 2}, 1);
    REQUIRE(plain.has_value());
    if (plain.has_value())
    {
        CHECK(plain->note == keyAt({.measure = 3, .beat = 1}, 1));
        CHECK(plain->offset == common::core::Fraction{1});
    }
}

// The end of a ring is the one slot the two digit verbs read differently, so the tail says which
// it is rather than each caller re-deriving it from the note's sustain: a bare digit takes the
// adjacent head there and only Alt+digit states the slide-out.
TEST_CASE("chartPathTailAt says when the offset is the ring's end", "[core][chart]")
{
    const common::core::Chart chart = makeGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const std::optional<ChartPathTail> inside =
        chartPathTailAt(chart.notes, tempo_map, {.measure = 2, .beat = 3}, 1);
    REQUIRE(inside.has_value());
    if (inside.has_value())
    {
        CHECK_FALSE(inside->at_ring_end);
    }

    // The four-beat ring ends on the next measure's downbeat.
    const std::optional<ChartPathTail> end =
        chartPathTailAt(chart.notes, tempo_map, {.measure = 3, .beat = 1}, 1);
    REQUIRE(end.has_value());
    if (end.has_value())
    {
        CHECK(end->at_ring_end);
        CHECK(end->offset == common::core::Fraction{4});
    }
}

// The create verb states one point and lets the gate judge it; the plan carries the whole note it
// rewrote, so undo restores the path exactly.
TEST_CASE("planInsertKeyframe states a point along the path", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertKeyframe(
        chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{7, 2}, 11);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        CHECK(plan->label == "Insert Keyframe");
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote& stated = plan->inserted.front();
        REQUIRE(stated.keyframes.size() == 3);
        // Inserted at its sorted place, after both existing statements.
        CHECK(stated.keyframes[2].offset == common::core::Fraction{7, 2});
        CHECK(stated.keyframes[2].fret == 11);
        // Nothing else moves: the head keeps its fret, the ring its length, the siblings theirs.
        CHECK(stated.fret == 5);
        CHECK(stated.sustain == chart.notes.front().sustain);
        CHECK(stated.keyframes[0].fret == 7);
        CHECK(stated.keyframes[1].fret == 9);

        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
        REQUIRE(applyChartChange(applied, plan->reversed()).has_value());
        CHECK(applied == chart);
    }
}

// Every refusal is the rule authority's, reached through the finalize gate: the planner states
// none of them, so it cannot drift from what the document itself would reject.
TEST_CASE("planInsertKeyframe refuses what the rules refuse", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const ChartSlotKey slot = keyAt(glideOnset(), 1);

    const auto refused = [&](common::core::Fraction offset, int fret) {
        const auto plan = planInsertKeyframe(chart, tempo_map, slot, offset, fret);
        REQUIRE_FALSE(plan.has_value());
        return plan.error();
    };

    // Offset zero is the ONSET, whose facts the note itself carries: a keyframe there would be a
    // second spelling of a value the note already states.
    CHECK(refused(common::core::Fraction{0}, 11) == ChartPlanRefusal::Invalid);
    // Past the ring there is nothing left to state on.
    CHECK(refused(common::core::Fraction{5}, 11) == ChartPlanRefusal::Invalid);
    // A second record on one offset leaves the offsets no longer strictly ascending.
    CHECK(refused(common::core::Fraction{2}, 11) == ChartPlanRefusal::Invalid);
    // And a slot holding no note names nothing to state a point on.
    const auto empty = planInsertKeyframe(
        chart, tempo_map, keyAt({.measure = 4, .beat = 1}, 1), common::core::Fraction{1}, 5);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == ChartPlanRefusal::Invalid);
}

// A scrape keeps travelling or it is no scrape, and the two halves of that fall out of the two
// authorities rather than out of a rule this planner states: a point ON the travel line says
// nothing new (the writer's oracle, which sheds it), while one repeating a neighbour's position
// stills the pick and is refused through the fixpoint.
TEST_CASE("planInsertKeyframe leaves a scrape travelling", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // Fret 9 start, a turnaround at 3 half a beat in, the terminal at 12 on the one-beat ring.
    chart.notes = {makeScrape({.measure = 2, .beat = 1}, 1)};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const ChartSlotKey slot = keyAt(glideOnset(), 1);

    // A quarter beat in the pick is passing 6 on its way from 9 to 3; stating that bends nothing.
    CHECK(
        common::core::keyframeSaysNothingNew(
            chart.notes.front(),
            common::core::Keyframe{.offset = common::core::Fraction{1, 4}, .fret = 6}));

    // Repeating the turnaround's own position leaves a segment the pick would rest on.
    const auto stilled =
        planInsertKeyframe(chart, tempo_map, slot, common::core::Fraction{3, 4}, 3);
    REQUIRE_FALSE(stilled.has_value());
    CHECK(stilled.error() == ChartPlanRefusal::Invalid);

    // A real turnaround between the two is an ordinary leg and commits.
    CHECK(planInsertKeyframe(chart, tempo_map, slot, common::core::Fraction{3, 4}, 7).has_value());
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
        moveNotes(chart, tempo_map, keys, common::core::Fraction{}, -1, "Move Notes").has_value());

    // Shifted up past the six-string range leaves the neck above string 6.
    CHECK_FALSE(
        moveNotes(chart, tempo_map, keys, common::core::Fraction{}, 6, "Move Notes").has_value());
}

// A move that would leave the grid's start is refused outright, never clamped: the grid arithmetic
// clamps at measure 1 beat 1, so a clamped LONE note would land there silently, as if it had moved
// a shorter distance than the one requested.
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
    CHECK_FALSE(moveNotes(
                    chart,
                    tempo_map,
                    {keyAt({.measure = 1, .beat = 2}, 1)},
                    common::core::Fraction{-2},
                    0,
                    "Move Notes")
                    .has_value());
    // The same note one beat left lands exactly on the origin, which is a legal destination.
    CHECK(moveNotes(
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
    CHECK_FALSE(moveNotes(chart, tempo_map, keys, common::core::Fraction{-10}, 0, "Move Notes")
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
        moveNotes(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes").has_value());
}

// A move onto a free slot plans a removal of the origin and an insertion at the destination,
// carrying the label through.
TEST_CASE("planMoveSelection moves a note to a free slot", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto plan = moveNotes(chart, tempo_map, keys, common::core::Fraction{1}, 0, "Move Notes");
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

    CHECK_FALSE(
        moveNotes(chart, tempo_map, {}, common::core::Fraction{1}, 0, "Move Notes").has_value());
    CHECK_FALSE(
        moveNotes(chart, tempo_map, keys, common::core::Fraction{}, 0, "Move Notes").has_value());

    // A key present in the request but absent from the chart moves nothing.
    const std::vector<ChartSlotKey> absent{keyAt({.measure = 9, .beat = 1}, 1)};
    CHECK_FALSE(moveNotes(chart, tempo_map, absent, common::core::Fraction{1}, 0, "Move Notes")
                    .has_value());
}

// The keyframe half of the same step (W13's ruling): a selected point moves along the ring it
// rides, by the beat delta a selected note would have moved its slot by.
TEST_CASE("planMoveSelection steps a selected keyframe's offset", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartKeyframeKey> first{keyframeKeyAt(
        {.measure = 2, .beat = 1}, 1, common::core::Fraction{2})};

    const auto plan = planMoveSelection(
        chart, tempo_map, {}, first, common::core::Fraction{1, 2}, 0, "Move Keyframe");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote& stepped = plan->inserted.front();
        REQUIRE(stepped.keyframes.size() == 2);
        CHECK(stepped.keyframes[0].offset == common::core::Fraction{5, 2});
        // Only the offset moves: the point keeps every statement it made, and the point beside it
        // is untouched.
        CHECK(stepped.keyframes[0].fret == 7);
        CHECK(stepped.keyframes[1].offset == common::core::Fraction{3});
        // The note the point rides keeps its own slot and its ring.
        CHECK(stepped.position == chart.notes.front().position);
        CHECK(stepped.sustain == chart.notes.front().sustain);

        // The step back is the same verb with the opposite delta, and it lands exactly where the
        // gesture started — which is also what an undo of the entry restores, since undo IS the
        // plan walked backwards.
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
        const auto back = planMoveSelection(
            applied,
            tempo_map,
            {},
            {keyframeKeyAt({.measure = 2, .beat = 1}, 1, common::core::Fraction{5, 2})},
            common::core::Fraction{-1, 2},
            0,
            "Move Keyframe");
        REQUIRE(back.has_value());
        if (back.has_value())
        {
            applyAndValidate(applied, tempo_map, *back);
            CHECK(applied == chart);
        }
    }
}

// Every bound on a stepped offset is the rule authority's, reached through the finalize gate: the
// planner states none of them and clamps at none of them. Crossing a neighbour is a REFUSAL rather
// than a swap, because a keyframe's identity IS its offset — exchanging two would leave the
// selection pointing at the other record.
TEST_CASE("planMoveSelection refuses a keyframe stepped out of its bounds", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartKeyframeKey> first{keyframeKeyAt(
        {.measure = 2, .beat = 1}, 1, common::core::Fraction{2})};
    const std::vector<ChartKeyframeKey> second{keyframeKeyAt(
        {.measure = 2, .beat = 1}, 1, common::core::Fraction{3})};

    SECTION("stepped onto the onset")
    {
        // Offsets are strictly positive: offset zero is the onset, whose facts the note carries.
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, first, common::core::Fraction{-2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("stepped past the ring")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, second, common::core::Fraction{2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("stepped onto its neighbour")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, first, common::core::Fraction{1}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("stepped across its neighbour")
    {
        // The one bound worth stating twice: the pair would still be legal as a SET, so what
        // refuses it is the stored order, which the planner deliberately never re-sorts.
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, first, common::core::Fraction{3, 2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("stepped onto a later onset of its own string")
    {
        // The next strike on the string is a WALL for the verb: no keyframe sits on a head of its
        // own string, and a step that would land there is refused so the point stays where it is
        // — never handed to the gate's clearance repair, which would pull it back to the margin
        // line, earlier than a point deliberately parked inside the margin.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 1}, 1, 12));
        const auto plan = planMoveSelection(
            repicked, tempo_map, {}, second, common::core::Fraction{1}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("stepped inside the margin before a later onset of its own string stands there")
    {
        // The rule refuses the head, never proximity: a charter who steps a keyframe to an eighth
        // before the next head gets exactly that, and the ring below still reaches the head.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 1}, 1, 12));
        const auto plan = planMoveSelection(
            repicked, tempo_map, {}, second, common::core::Fraction{7, 8}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const auto glide =
                std::ranges::find(plan->inserted, glideOnset(), &common::core::ChartNote::position);
            REQUIRE(glide != plan->inserted.end());
            CHECK(glide->sustain == common::core::Fraction{4});
            REQUIRE(glide->keyframes.size() == 2);
            CHECK(glide->keyframes.back().offset == common::core::Fraction{31, 8});
        }
    }
}

// The release is the ring's end, so stepping it steps the end: the move verb is the fall's own
// handle — outward the slide-out lengthens, inward it shortens — while the duration verb, which
// moves the ribbon and never a point, leaves a release behind a lengthening ring. A release cannot
// be stepped onto the last sounded fret: a fall needs a leg of its own, and the order refusal is
// what says so.
TEST_CASE("planMoveSelection drags the ring's end with its release", "[core][chart]")
{
    // The glide chart's fret-12 statement sits exactly at its four-beat end: the release.
    const common::core::Chart chart = makeGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartKeyframeKey> release{keyframeKeyAt(
        glideOnset(), 1, common::core::Fraction{4})};
    REQUIRE(common::core::slideOutFretOrNull(chart.notes.front()) != nullptr);

    SECTION("outward lengthens the fall")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, release, common::core::Fraction{1}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& moved = plan->inserted.front();
            CHECK(moved.sustain == common::core::Fraction{5});
            const int* const falls_toward = common::core::slideOutFretOrNull(moved);
            REQUIRE(falls_toward != nullptr);
            if (falls_toward != nullptr)
            {
                CHECK(*falls_toward == 12);
            }
            // The junction before it did not move.
            REQUIRE(moved.keyframes.size() == 2);
            CHECK(moved.keyframes.front().offset == common::core::Fraction{2});
        }
    }
    SECTION("inward shortens the fall")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, release, common::core::Fraction{-1}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& moved = plan->inserted.front();
            CHECK(moved.sustain == common::core::Fraction{3});
            CHECK(common::core::slideOutFretOrNull(moved) != nullptr);
        }
    }
    SECTION("outward stops where it is at the next head on its string")
    {
        // The next strike is a WALL for the verb: a step onto or past the head is refused, so the
        // release stays exactly where it is — never handed to the gate's clearance repair, which
        // would pull a release deliberately parked inside the margin back to the margin line. A
        // shorter step lands where it was aimed, inside the margin included.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 2}, 1, 3));
        const auto onto = planMoveSelection(
            repicked, tempo_map, {}, release, common::core::Fraction{1}, 0, "Move Keyframe");
        REQUIRE_FALSE(onto.has_value());
        CHECK(onto.error() == ChartPlanRefusal::Invalid);
        const auto past = planMoveSelection(
            repicked, tempo_map, {}, release, common::core::Fraction{2}, 0, "Move Keyframe");
        REQUIRE_FALSE(past.has_value());
        CHECK(past.error() == ChartPlanRefusal::Invalid);
        const auto inside = planMoveSelection(
            repicked, tempo_map, {}, release, common::core::Fraction{7, 8}, 0, "Move Keyframe");
        REQUIRE(inside.has_value());
        if (inside.has_value())
        {
            const auto glide = std::ranges::find(
                inside->inserted, glideOnset(), &common::core::ChartNote::position);
            REQUIRE(glide != inside->inserted.end());
            CHECK(glide->sustain == common::core::Fraction{39, 8});
            CHECK(common::core::slideOutFretOrNull(*glide) != nullptr);
        }
    }
    SECTION("a note moved back onto the release rides the release back")
    {
        // The note's head lands where the release stood, so the release keeps its clearance from
        // the new head — the fall shortens by the margin, exactly as a note moved into a plain
        // ring truncates it. A step that stops short of the release leaves the ring alone.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 2}, 1, 3));
        const std::vector<ChartSlotKey> landing{keyAt({.measure = 3, .beat = 2}, 1)};
        const auto onto = planMoveSelection(
            repicked, tempo_map, landing, {}, common::core::Fraction{-1}, 0, "Move Note");
        REQUIRE(onto.has_value());
        if (onto.has_value())
        {
            const auto glide =
                std::ranges::find(onto->inserted, glideOnset(), &common::core::ChartNote::position);
            REQUIRE(glide != onto->inserted.end());
            CHECK(glide->sustain == common::core::Fraction{15, 4});
            CHECK(common::core::slideOutFretOrNull(*glide) != nullptr);
        }
        const auto short_of = planMoveSelection(
            repicked, tempo_map, landing, {}, common::core::Fraction{-1, 2}, 0, "Move Note");
        REQUIRE(short_of.has_value());
        if (short_of.has_value())
        {
            CHECK(
                std::ranges::find(
                    short_of->inserted, glideOnset(), &common::core::ChartNote::position) ==
                short_of->inserted.end());
        }
    }
    SECTION("onto the last sounded fret is refused")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, release, common::core::Fraction{-2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
}

// A MOVE MAY SHORTEN A RING, BUT NEVER DELETE A STATEMENT. A note moved back onto an earlier
// note's tail re-strikes it, so the gate truncates that ring at the landing and rides its release
// back with the end — both the move's to do, a ring's length and the fall it goes out on being
// exactly what this verb changes. What the clip would ALSO do is drop every other keyframe past
// the landing, erasing something the charter wrote on a note they never touched and leaving no
// record of it, so a landing that would is refused whole instead.
TEST_CASE("planMoveSelection refuses a landing that would erase a statement", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    // The mover stands five beats past the tail's onset, clear of its four-beat ring, and steps
    // back two beats — landing three beats in, where that ring is still sounding.
    constexpr common::core::GridPosition tail_onset{.measure = 2, .beat = 1, .offset = {}};
    constexpr common::core::GridPosition mover_slot{.measure = 3, .beat = 2, .offset = {}};
    const std::vector<ChartSlotKey> mover{keyAt(mover_slot, 1)};
    constexpr common::core::Fraction step_back{-2};

    // One ringing note on string 1 carrying the statement under test, plus the mover.
    const auto figure = [&](const common::core::Keyframe carried) {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote tail = makeTestNote(tail_onset, 1, 7, common::core::Fraction{4});
        tail.keyframes = {carried};
        chart.notes = {std::move(tail), makeTestNote(mover_slot, 1, 5)};
        return chart;
    };
    // The tail as the plan leaves it; the truncation rewrites it, so it stands on both sides.
    const auto shortened = [&](const ChartEditPlan& plan) {
        return noteAt(plan.inserted, tail_onset, 1);
    };

    SECTION("a bend statement past the landing refuses the move")
    {
        // Nothing else could carry the curve's arrival, so clipping it away would be a silent
        // deletion. A refused plan is not applied at all, so the chart is left exactly as it was.
        const common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{7, 2}, .fret = {}, .bend = 1.0, .vibrato = {}
            });
        const auto plan = moveNotes(chart, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a release past the landing rides back to its clearance")
    {
        // The release IS the ring's end, so it moves because the end did — no statement is lost.
        const common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{4}, .fret = 3, .bend = {}, .vibrato = {}
            });
        const auto plan = moveNotes(chart, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const tail = shortened(*plan);
            REQUIRE(tail != nullptr);
            if (tail != nullptr)
            {
                CHECK(tail->sustain == common::core::Fraction{11, 4});
                const int* const falls_toward = common::core::slideOutFretOrNull(*tail);
                REQUIRE(falls_toward != nullptr);
                if (falls_toward != nullptr)
                {
                    CHECK(*falls_toward == 3);
                }
            }
        }
    }

    SECTION("statements before the landing simply shorten the ring")
    {
        const common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .fret = 9, .bend = {}, .vibrato = {}
            });
        const auto plan = moveNotes(chart, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const tail = shortened(*plan);
            REQUIRE(tail != nullptr);
            if (tail != nullptr)
            {
                CHECK(tail->sustain == common::core::Fraction{3});
                REQUIRE(tail->keyframes.size() == 1);
                CHECK(tail->keyframes.front().offset == common::core::Fraction{1});
            }
        }
    }

    SECTION("a statement exactly on the landing is moved back, not erased")
    {
        // The clip's bound is inclusive, so it survives; standing on the new head is what the
        // clearance repair then moves it back from, the ring going with it.
        const common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{3}, .fret = 9, .bend = {}, .vibrato = {}
            });
        const auto plan = moveNotes(chart, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const tail = shortened(*plan);
            REQUIRE(tail != nullptr);
            if (tail != nullptr)
            {
                CHECK(tail->sustain == common::core::Fraction{11, 4});
                REQUIRE(tail->keyframes.size() == 1);
                CHECK(tail->keyframes.front().offset == common::core::Fraction{11, 4});
                CHECK(tail->keyframes.front().fret == 9);
            }
        }
    }
}

// A mixed selection is one press and one plan: the note moves its slot, and the keyframe it
// carries rides along at an UNCHANGED offset, because an offset is relative to the onset it hangs
// from and stepping both would move it twice.
TEST_CASE("planMoveSelection carries a selected note's own keyframe along", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planMoveSelection(
        chart,
        tempo_map,
        {keyAt({.measure = 2, .beat = 1}, 1)},
        {keyframeKeyAt({.measure = 2, .beat = 1}, 1, common::core::Fraction{2})},
        common::core::Fraction{1},
        0,
        "Move Selection");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        const common::core::ChartNote& moved = plan->inserted.front();
        CHECK(moved.position == common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
        REQUIRE(moved.keyframes.size() == 2);
        CHECK(moved.keyframes[0].offset == common::core::Fraction{2});
        CHECK(moved.keyframes[1].offset == common::core::Fraction{3});
    }
}

// The string step reaches notes alone: a keyframe has no string of its own, and a selected head
// carries its whole path across by construction. So Alt+Up over keyframes alone plans nothing —
// the inert outcome, not a refusal.
TEST_CASE("planMoveSelection leaves a keyframe-only selection to the string step", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const auto plan = planMoveSelection(
        chart,
        makeTempoMap(),
        {},
        {keyframeKeyAt({.measure = 2, .beat = 1}, 1, common::core::Fraction{2})},
        common::core::Fraction{},
        1,
        "Move Keyframe");
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// Set-exact mode assigns the typed fret to every note in the snapshot.
TEST_CASE("planRetypeFrets sets an exact fret on every note", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        base,
        ChartFretSet{.fret = 9},
        common::core::ChartStopChannel::Sounding);
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

// The shift moves every addressed stop by one delta, preserving the shape.
TEST_CASE("planRetypeFrets shifts every stop by one delta", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    // A +2 shift: 3 to 5 and 5 to 7.
    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        base,
        ChartFretShift{.delta = 2},
        common::core::ChartStopChannel::Sounding);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* low = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        REQUIRE(low != nullptr);
        CHECK(low->fret == 5);
        const common::core::ChartNote* high = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 2);
        REQUIRE(high != nullptr);
        CHECK(high->fret == 7);
        CHECK(plan->label == "Shift Frets +2");
    }
}

// A shift that would push any member past the fret cap refuses the whole plan rather than
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
    // refused by the shared finalize gate. The kind matters: this is Invalid, the emptiness a
    // pending entry paints red.
    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        base,
        ChartFretShift{.delta = common::core::g_max_fret - 3},
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::Invalid);
}

// An empty snapshot has nothing to write, so no plan is produced — and that emptiness is a
// NoChange, not a refusal: there was nothing to edit, so nothing was disallowed.
TEST_CASE("planRetypeFrets reports NoChange for an empty snapshot", "[core][chart]")
{
    const auto plan = retypeNotes(
        makeTestChart(),
        makeTempoMap(),
        {},
        ChartFretShift{.delta = 2},
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// A target already matching plans nothing, like every planner sharing the finalize diff — and it
// reports NoChange, never Invalid: a valid no-op must not read as a refusal, or the pending entry
// would paint an already-correct value red.
TEST_CASE("planRetypeFrets reports NoChange when nothing changes", "[core][chart]")
{
    const std::vector<common::core::ChartNote> base{makeTestNote({.measure = 1, .beat = 1}, 1, 5)};
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = base;

    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        base,
        ChartFretSet{.fret = 5},
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error() == ChartPlanRefusal::NoChange);
}

// A selected keyframe retypes like a head (W13's ruling), and needs no channel of its own: the
// selection kind is what says which stop the digit reached. The head it rides keeps its own fret,
// which is the fret-verb law read the other way round.
TEST_CASE("planRetypeFrets retypes a selected keyframe's fret", "[core][chart]")
{
    const common::core::Chart chart = makeGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("set-exact assigns the target to the selected point alone")
    {
        const auto plan = planRetypeFrets(
            chart,
            tempo_map,
            chart.notes,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            ChartFretSet{.fret = 10},
            common::core::ChartStopChannel::Sounding);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& retyped = plan->inserted.front();
            CHECK(retyped.fret == 7);
            REQUIRE(retyped.keyframes.size() == 2);
            CHECK(retyped.keyframes[0].fret == 10);
            CHECK(retyped.keyframes[1].fret == 12);
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }
    SECTION("transposing shifts every selected point by one delta")
    {
        // One delta across both points and not the head, which this press never named: the
        // shape a chord slide needs, one level inside the note.
        const auto plan = planRetypeFrets(
            chart,
            tempo_map,
            chart.notes,
            {},
            {
                keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2}),
                keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4}),
            },
            ChartFretShift{.delta = 2},
            common::core::ChartStopChannel::Sounding);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& retyped = plan->inserted.front();
            CHECK(retyped.fret == 7);
            REQUIRE(retyped.keyframes.size() == 2);
            CHECK(retyped.keyframes[0].fret == 11);
            CHECK(retyped.keyframes[1].fret == 14);
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }
    SECTION("a head and one of its own points retype together")
    {
        // The mixed operand: the head is addressed because the selection named it, the point
        // because it named the point, and one delta moves both. Neither reaches the point the
        // selection left alone.
        common::core::Chart mixed = makeGlideChart();
        mixed.notes.front().fret = 5;
        const auto plan = planRetypeFrets(
            mixed,
            tempo_map,
            mixed.notes,
            {keyAt(glideOnset(), 1)},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            ChartFretShift{.delta = 1},
            common::core::ChartStopChannel::Sounding);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& retyped = plan->inserted.front();
            CHECK(retyped.fret == 6);
            REQUIRE(retyped.keyframes.size() == 2);
            CHECK(retyped.keyframes[0].fret == 10);
            CHECK(retyped.keyframes[1].fret == 12);
        }
    }
}

// The rules refuse a keyframe's fret exactly as they refuse a head's, through the finalize gate:
// the planner carries no fret bound of its own to keep in step with them.
TEST_CASE("planRetypeFrets refuses a keyframe fret the rules reject", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("below the capo floor")
    {
        // A stop on or below the capo is nothing pressed, so the normalizer strips the position
        // channel and the note stops being its own normal form. Fret 0 is under the floor even
        // with no capo: an open string cannot be slid to.
        const common::core::Chart chart = makeGlideChart();
        const auto plan = planRetypeFrets(
            chart,
            tempo_map,
            chart.notes,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            ChartFretSet{.fret = 0},
            common::core::ChartStopChannel::Sounding);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("below a real capo's floor")
    {
        common::core::Chart capoed = makeGlideChart();
        capoed.tuning.capo = 5;
        const auto plan = planRetypeFrets(
            capoed,
            tempo_map,
            capoed.notes,
            {},
            {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
            ChartFretSet{.fret = 4},
            common::core::ChartStopChannel::Sounding);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
    SECTION("a member pushed past the fret cap refuses the whole plan")
    {
        // Whole-plan, like every other refusal here: shifting the lower point onto the cap would
        // take the higher one past it, so neither moves.
        const common::core::Chart chart = makeGlideChart();
        const auto plan = planRetypeFrets(
            chart,
            tempo_map,
            chart.notes,
            {},
            {
                keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2}),
                keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4}),
            },
            ChartFretShift{.delta = common::core::g_max_fret - 9},
            common::core::ChartStopChannel::Sounding);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
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

// What the step list exists for: a GRID step moves the ring's END onto the adjacent grid line, so
// a ring a tick step left between lines snaps onto the grid — ceiling when growing, flooring when
// shrinking — instead of carrying its remainder forever, which is what a summed beat delta would
// do.
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

// The header's first consequence, in the meter that exposes it: a grid-only run from an on-grid
// ring is exactly reversible. In 7/8 a quarter-note grid steps two beats, so the lines sit on
// beats 1, 3, 5 and 7 with the next downbeat one beat after the last — and a step primitive that
// stepped two beats back from that downbeat and re-snapped to the nearest line would skip beat 7,
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
// a ring (40-Q2-B) — and a note on another string never blocks it, because spacing a DRAWN tail
// against any string is presentation's rule, not the ring's.
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
// from the gesture's start. Holding the start value instead would grow the note back on a shrink
// press. A running gesture's step into the floor moves nothing, so it is REFUSED and the caller
// records nothing for it: the next grow is the first step back, with no unseen overshoot to pay.
TEST_CASE("planAdjustSustain holds an emptied ring and refuses the step into it", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{3})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};
    const std::vector<common::core::ChartNote> base = chart.notes;

    common::core::Chart live = chart;
    std::vector<ChartSustainStep> steps;
    // A press as the caller makes it: the step is appended first, and a refused step is taken back
    // out, since the caller records nothing for it.
    const auto press = [&](bool grow) {
        steps.push_back(gridStep(g_quarter_grid, grow));
        const auto plan = planAdjustSustain(live, tempo_map, base, keys, steps);
        if (plan.has_value())
        {
            live.notes = base;
            REQUIRE(applyChartChange(live, *plan).has_value());
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
        steps.pop_back();
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
    // being clamped to some invented floor or restored to its three-beat start — and the press is
    // refused, leaving the run with the two steps it had.
    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    CHECK(steps.size() == 2);
    press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    CHECK(steps.size() == 2);
    // The first grow is the first step back: nothing was banked by the two refused presses.
    press(/*grow=*/true);
    CHECK(ring() == common::core::Fraction{2});
    // A run that replays back to its start describes nothing: NoChange is what tells the caller to
    // take the gesture's entry back out and walk the chart to `base`.
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
        // The turnaround is clipped away with the tail, and the terminal rides the shortening
        // ring — it is a keyframe at the end now, so the floored scrape keeps exactly one.
        REQUIRE(scrape->keyframes.size() == 1);
        if (scrape->keyframes.size() == 1)
        {
            CHECK(scrape->keyframes[0].offset == common::core::g_minimum_slide_window);
            CHECK(scrape->keyframes[0].fret == 12);
        }
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
        REQUIRE(scrape->keyframes.size() == 2);
        if (scrape->keyframes.size() == 2)
        {
            CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->keyframes[0].fret == 3);
        }
        // And the terminal re-attaches at the replayed end, still aimed where it was.
        const int* const terminal = common::core::slideOutFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == 12);
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
        // The synthesized path is the terminal alone: one keyframe, the release at the ring's end.
        CHECK(scrape->keyframes.size() == 1);
        const int* const terminal = common::core::slideOutFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == g_pick_slide_default_high_fret);
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

// The eligible-subset skip covers EVERY per-note rule, not a hand-picked pair of them: copying a
// couple of the validator's predicates would leave a note the target attack breaks some OTHER rule
// on unskipped, and the whole-stream gate would then refuse the plan for every note in the
// selection, not just that one. Here a dead note cannot become a pinch, which no shortlist of
// predicates would name. The refusal is specifically the PINCH's: a dead note may carry a harmonic
// node, but only the on-neck kind a hand stands on, and the verb synthesizes an off-neck one when
// it converts. Asking the authority instead of restating it is what keeps this case correct as
// that rule narrows.
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
// somewhere to travel: a quarter note, not a degeneracy floor of an eighth of a beat, which a
// corpus survey found to be 16x shorter than any scrape anyone charted. A ring that CAN hold the
// gesture is kept exactly as authored — the ring is the note's own truth, and this verb changes
// the attack, not the duration.
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
        CHECK(common::core::slideOutFretOrNull(*stub) != nullptr);

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
        const int* const terminal = common::core::slideOutFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == g_pick_slide_default_low_fret);
        }
    }
}

// Under a capo the low endpoint yields to the first playable fret: every fret a slide gesture
// names sits at or above capo + 1, so a downward default scrape under a high capo terminates at
// capo + 1 rather than at the bare corpus default, which the gate refuses. The chart's other notes
// are lifted above the capo so the fixture itself stays legal.
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
        const int* const terminal = common::core::slideOutFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == 6);
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

// E25 is a PRESENTATION rule, so the verbs do not trim: X on a held note deadens it and leaves the
// ring exactly where it was, because a dead note's damped stroke has a duration like any other and
// that duration is what a legato claim after it reads. What changes is only what a surface draws,
// which no plan touches.
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
        common::core::presentedChartNotes(
            common::core::chartConnections(chart.notes, tempo_map), tempo_map)
            .notes;
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
// that rule for free — including when it MOVES, which is the property the indirection buys. A dead
// note sounds no pitch, so pitch MODULATION refuses it (vibrato here) and refuses only THAT note,
// leaving the rest of the selection muted; a harmonic node does NOT refuse it, because the node is
// positional on a dead note. Neither restricts a palm mute.
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
// stays where it was authored — in both modes, for a scrape and a pitched slide alike. Translating
// a scrape's path along with its start is a bug: every keyframe is placed on its fret on purpose.
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
            // The whole path at once, terminal included: the release is the keyframe at the
            // ring's end, so one comparison says every stop stayed where it was authored.
            CHECK(retyped.keyframes == chart.notes.front().keyframes);
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
            retypeNotes(
                chart,
                makeTempoMap(),
                chart.notes,
                ChartFretShift{.delta = 11 - chart.notes.front().fret},
                common::core::ChartStopChannel::Sounding),
            11);
    }
    SECTION("scrape: set-exact assigns the start only")
    {
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart,
            retypeNotes(
                chart,
                makeTempoMap(),
                chart.notes,
                ChartFretSet{.fret = 11},
                common::core::ChartStopChannel::Sounding),
            11);
    }
    SECTION("scrape: a high start is typable because the path does not follow it")
    {
        // The path stays where it was authored, so every fret the start itself can reach is
        // typable: transposing to 24 never carries the terminal's 12 up to 27 and past the cap.
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart,
            retypeNotes(
                chart,
                makeTempoMap(),
                chart.notes,
                ChartFretShift{.delta = 24 - chart.notes.front().fret},
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
            retypeNotes(
                chart,
                makeTempoMap(),
                chart.notes,
                ChartFretShift{.delta = 3},
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
            retypeNotes(
                chart,
                makeTempoMap(),
                chart.notes,
                ChartFretSet{.fret = 9},
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

    const auto exact = retypeNotes(
        chart,
        makeTempoMap(),
        chart.notes,
        ChartFretSet{.fret = 3},
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(exact.has_value());
    CHECK(exact.error() == ChartPlanRefusal::Invalid);
    const auto shifted = retypeNotes(
        chart,
        makeTempoMap(),
        chart.notes,
        ChartFretShift{.delta = 3 - chart.notes.front().fret},
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

    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        chart.notes,
        ChartFretSet{.fret = 0},
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

    const auto plan = retypeNotes(
        chart,
        makeTempoMap(),
        chart.notes,
        ChartFretSet{.fret = 7},
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

// THE COMMIT LAW's history half: an entry is the written form of its transition. Planting a point
// the path already passes through writes as nothing, and the edit that gives it a meaning writes
// as the creation of both points at once.
TEST_CASE("writtenChartPlan records the written form of a transition", "[core][chart]")
{
    const common::core::ChartNote plain =
        makeTestNote({.measure = 1, .beat = 1}, 1, 5, common::core::Fraction{1});
    common::core::ChartNote started = plain;
    // Past the last stop the path holds 5, so a 5 stated there says nothing.
    started.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1, 4}, .fret = 5}};
    common::core::ChartNote landed = started;
    landed.keyframes.push_back(
        common::core::Keyframe{.offset = common::core::Fraction{3, 4}, .fret = 9});

    const ChartEditPlan plant{.removed = {plain}, .inserted = {started}, .label = "Insert"};
    CHECK(writtenChartPlan(plant).empty());

    const ChartEditPlan land{.removed = {started}, .inserted = {landed}, .label = "Type"};
    const ChartEditPlan written = writtenChartPlan(land);
    REQUIRE(written.removed.size() == 1);
    REQUIRE(written.inserted.size() == 1);
    CHECK(written.removed.front() == plain);
    CHECK(written.inserted.front() == landed);
    CHECK(written.label == "Type");
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
            // The turnaround, then the terminal: the release is the keyframe at the ring's end,
            // so compressing the ring moves it and leaves the count alone.
            REQUIRE(scrape->keyframes.size() == 2);
            CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->keyframes[0].fret == 3);
            const int* const terminal = common::core::slideOutFretOrNull(*scrape);
            REQUIRE(terminal != nullptr);
            if (terminal != nullptr)
            {
                CHECK(*terminal == 12);
            }
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }

    SECTION("growth stops where it is at the next head on its string")
    {
        // The terminal rides the end and no keyframe sits on a head of its own string, so the
        // next strike is a WALL for a scrape's growth, as it is for the move verb: a step that
        // reaches it leaves the ring where it is instead of handing the terminal to the gate's
        // clearance repair. A shorter step still lands inside the margin.
        common::core::Chart walled = chart;
        walled.notes.push_back(makeTestNote({.measure = 3, .beat = 2, .offset = {1, 2}}, 1, 5));
        std::ranges::sort(walled.notes, common::core::chartNoteOrderLess);
        const auto onto = planAdjustSustain(
            walled, tempo_map, walled.notes, keys, {gridStep(common::core::Fraction{1, 8}, true)});
        REQUIRE_FALSE(onto.has_value());
        CHECK(onto.error() == ChartPlanRefusal::NoChange);
        const auto inside = planAdjustSustain(
            walled, tempo_map, walled.notes, keys, {gridStep(g_sixteenth_grid, true)});
        REQUIRE(inside.has_value());
        if (inside.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(inside->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::Fraction{5, 4});
            CHECK(common::core::slideOutFretOrNull(*scrape) != nullptr);
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
            // The terminal rides a scrape's ring in both directions, so it sits at the grown end.
            const common::core::Keyframe* const release = common::core::releaseKeyframe(*scrape);
            REQUIRE(release != nullptr);
            if (release != nullptr)
            {
                CHECK(release->offset == common::core::Fraction{3, 2});
                CHECK(release->fret == 12);
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
            // The turnaround no longer fits inside the floored window; the terminal alone rides,
            // and it is a keyframe of its own at the floored end.
            REQUIRE(scrape->keyframes.size() == 1);
            const common::core::Keyframe* const release = common::core::releaseKeyframe(*scrape);
            REQUIRE(release != nullptr);
            if (release != nullptr)
            {
                CHECK(release->offset == common::core::g_minimum_slide_window);
                CHECK(release->fret == 12);
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
    common::core::setSlideOut(scrape, 3);
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
        // The surviving turnaround, then the re-aimed terminal at the new end.
        REQUIRE(shrunk->keyframes.size() == 2);
        CHECK(shrunk->keyframes[0].offset == common::core::Fraction{1, 4});
        CHECK(shrunk->keyframes[0].fret == 3);
        const common::core::Keyframe* const release = common::core::releaseKeyframe(*shrunk);
        REQUIRE(release != nullptr);
        if (release != nullptr)
        {
            CHECK(release->offset == common::core::Fraction{1, 2});
            CHECK(release->fret == 12);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// A note inserted inside a scrape's ring would cut the scrape short onto its own onset, and a
// scrape's terminal is a release standing at the ring's end — so the terminal would sit on the new
// head, which no edit may do. The insert is refused rather than leaving a chip no one can reach.
// A note placed on a scrape's path re-strikes the string, so the scrape ends there like any ring
// (40-Q2-B) — and its terminal, which the truncation carried onto the new head, keeps its clearance
// from it exactly as a loaded chart's would. One normalization for every producer, not a refusal.
TEST_CASE("planInsertNote shortens a scrape under a note placed on its path", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 1, .beat = 1, .offset = {1, 2}}, 1, 5),
        g_fixture_sustain);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const auto scrape = std::ranges::find(
            plan->inserted,
            common::core::GridPosition{.measure = 1, .beat = 1},
            &common::core::ChartNote::position);
        REQUIRE(scrape != plan->inserted.end());
        // Half a beat to the new head, less the quarter-beat margin: the terminal re-aims onto
        // the fret it still travels toward.
        CHECK(scrape->sustain == common::core::Fraction{1, 4});
        CHECK(common::core::isScrape(scrape->attack));
        const int* const terminal = common::core::slideOutFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == 12);
        }
        applyAndValidate(chart, tempo_map, *plan);
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

// Entering a pick slide CONVERTS an existing pitched glide rather than discarding it: the glide
// already IS a path, so its frets and direction are what the charter drew and the scrape keeps
// them, with the last leg promoted to the gesture's required terminal. Exiting is still destructive
// — `slides` is the path's own storage, definitionally outside the latent contract, so toggling
// back clears the path rather than resurrecting the glide, and undo is the recovery. Pinned so that
// asymmetry with the technique latents stays deliberate.
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
    // from the charter's own glide rather than the synthesized default's far endpoint. The
    // statement MOVES to the ring's end, where it is the release, so the note still carries
    // exactly one keyframe.
    REQUIRE(scrape->keyframes.size() == 1);
    const int* const terminal = common::core::slideOutFretOrNull(*scrape);
    REQUIRE(terminal != nullptr);
    if (terminal != nullptr)
    {
        CHECK(*terminal == 9);
        CHECK(*terminal != g_pick_slide_default_high_fret);
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
    CHECK(common::core::slideOutFretOrNull(*restored) == nullptr);
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
    // Each predecessor rings its whole one-beat gap, so its hold reaches the successor's onset —
    // the strict adjacency the resolver requires. The kept-sustain bound plays no part: the legato
    // test reads the STORED ring, never the drawn tail.
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
        common::core::setSlideOut(chart.notes[0], 5);
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
        common::core::setSlideOut(chart.notes[0], 12);
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

// No node ever leaves with an `H` press: the claim stores no direction, so there is no attack
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
        // node vetoes the clause. The claim is therefore unjustified and the note is left untouched
        // — the verb never drops the node to buy itself a legal conversion.
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

// Shrinking a tail does not repair the claim it disconnects: mid-burst the broken claim simply
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
    const auto settled = planSettleChart(chart, tempo_map, chart, "Settle Legato");
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
        CHECK_FALSE(planSettleChart(clean, tempo_map, clean, "Settle Legato").has_value());
    }
    const auto folded = planSettleChart(chart, tempo_map, pre_burst, "Shrink Sustain");
    REQUIRE(folded.has_value());
    if (folded.has_value())
    {
        REQUIRE(folded->inserted.size() == 2);
        CHECK(folded->inserted[0].sustain == common::core::Fraction{1});
        CHECK(folded->inserted[1].attack == common::core::NoteAttack::Pick);
        CHECK(folded->label == "Shrink Sustain");
    }
}

// The settle fold's base is the CHART the replaced entry was applied to, not merely its notes: a
// nudge off the string a claim was made from is a legal burst, and the fold has to describe the
// whole of it or the caller's walk-back would leave the burst half-undone.
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

    // The burst: the ringing note moves up a string, which leaves the claim behind it with nothing
    // to connect to. Mid-burst that is legal and transient — the sweep at the next settle point is
    // what flattens it.
    common::core::Chart burst = pre_burst;
    burst.notes[0].string = 4;

    const auto settled = planSettleChart(burst, tempo_map, pre_burst, "Move Note");
    REQUIRE(settled.has_value());
    if (settled.has_value())
    {
        // Applied to the pre-burst chart — which is exactly where the caller's walk-back leaves the
        // live chart — the plan lands on the burst's own state with the claim flattened.
        common::core::Chart applied = pre_burst;
        REQUIRE(applyChartChange(applied, *settled).has_value());
        REQUIRE(applied.notes.size() == 2);
        CHECK(applied.notes[0].string == 4);
        CHECK(applied.notes[0].fret == 5);
        CHECK(applied.notes[1].attack == common::core::NoteAttack::Pick);

        // And one undo of the folded entry takes the whole burst back.
        REQUIRE(applyChartChange(applied, settled->reversed()).has_value());
        CHECK(applied == pre_burst);
    }
}

// E4's landing requirement binds both strike attacks, so the in-plan flatten must cover both: a tap
// or a left-hand tap on an open string with no node is not a tap at all. A junk `Tapped` flag is
// real Guitar Pro data, so without the flatten such a stream reaches validation unrepaired and
// fails the whole import.
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
            // first. Both attacks land on a plain pick — the claim stores no direction, so there is
            // nothing for either of them to be rescued into.
            const auto retyped = retypeNotes(
                chart,
                tempo_map,
                {chart.notes[0]},
                ChartFretSet{.fret = 7},
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

            const auto stranded = retypeNotes(
                chart,
                tempo_map,
                {chart.notes[0]},
                ChartFretSet{.fret = 0},
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
            const auto kept = retypeNotes(
                noded,
                tempo_map,
                {noded.notes[0]},
                ChartFretSet{.fret = 0},
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

// A range verb carries a note's held stop with no case of its own, because the stop is one of the
// note's own fields and travels wherever the note does.
TEST_CASE("The range verbs carry a note's held stop", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // A chord ringing across a tap on a string it never holds: the tap's claim reaches a real span
    // that way, so the inert sweep has no reason to take the stop it states.
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{4}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{4}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1, 2}),
    };
    chart.notes[2].attack = common::core::NoteAttack::Tap;
    chart.notes[2].held = 5;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> tap_key{
        ChartSlotKey{.position = {.measure = 2, .beat = 2, .offset = {}}, .string = 3}
    };

    // A beat later, still under the chord: the tap keeps its sounding fret and its stated stop
    // alike.
    const auto plan =
        moveNotes(chart, tempo_map, tap_key, common::core::Fraction{1}, 0, "Move Note");
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        REQUIRE(plan->inserted.size() == 1);
        CHECK(
            plan->inserted.front().position ==
            common::core::GridPosition{.measure = 2, .beat = 3, .offset = {}});
        CHECK(plan->inserted.front().fret == 12);
        CHECK(plan->inserted.front().held == std::optional{5});
        applyAndValidate(chart, tempo_map, *plan);
    }
}

// A fret entry is one plan against one array, so undoing it is the same primitive run backwards
// and a failed precondition leaves the chart untouched.
TEST_CASE("A retype applies and reverses atomically", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = retypeNotes(
        chart,
        tempo_map,
        {chart.notes[0]},
        ChartFretSet{.fret = 7},
        common::core::ChartStopChannel::Sounding);
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    REQUIRE(applyChartChange(chart, *plan).has_value());
    CHECK(chart.notes.size() == original.notes.size());
    CHECK(chart.notes[0].fret == 7);

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

// `Shift+L` on a selected keyframe severs the gesture there (W10's addendum): the origin's path
// ENDS at the junction and a new head takes the remainder. The origin keeps the keyframe it
// arrives at — the leg the user split at is real travel — so the junction is the equal-fret
// handover W10's ruling 2 names, and the later keyframes rebase onto the new onset.
TEST_CASE("planToggleJunctions severs a glide at its junction", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan =
        splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    CHECK(plan->label == "Split Note");
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
    // left, and its own later keyframe rides along rebased onto the new onset (4 - 2 = 2) — where
    // it lands on the product's own ring end, so the fixture's release stays a release.
    CHECK(product.fret == 9);
    CHECK(product.sustain == common::core::Fraction{2});
    REQUIRE(product.keyframes.size() == 1);
    CHECK(product.keyframes[0].offset == common::core::Fraction{2});
    CHECK(product.keyframes[0].fret == 12);
    // W10's store for a split head — never Pick, never a stored tie. The addendum's proposed
    // UNSTRUCK reading needs LegatoMotion::Continuation, which is unbuilt, so this is a claim the
    // settle sweep still flattens; the default is a proposal, not a ruling.
    CHECK(product.attack == common::core::NoteAttack::Legato);

    // One entry, and it reverses field for field.
    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// A head must sit on a stated fret and needs a remainder to take (W10's ruling 2). Both refusals
// are Invalid rather than a clamp: rounding the interpolated fret between stating points would be
// invented data, and a key naming no keyframe at all is simply skipped.
TEST_CASE("planToggleJunctions refuses what cannot carry a head", "[core][chart]")
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
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
        CHECK(chart == original);
    }

    SECTION("a keyframe at the ring's end")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a scrape, whose terminal the origin would lose")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote scrape =
            makeTestNote(glideOnset(), 1, 9, common::core::Fraction{4});
        scrape.attack = common::core::NoteAttack::PickSlide;
        scrape.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 5}};
        common::core::setSlideOut(scrape, 3);
        chart.notes = {std::move(scrape)};

        // The release reaches only the product that ends where the gesture did, and the origin's
        // own arrival retreats a margin inside its end, so the origin keeps no falls-away — and
        // a scrape's terminal is required, so the gate refuses the whole split rather than
        // shipping a pick slide that stops travelling.
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a key naming no keyframe")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{3})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }
}

// WHERE the origin's arrival lands is the shared clearance authority's answer
// (\ref latestStatementBeforeStrike), the one every other producer asks — the importer's
// synthesized arrivals and the load and plan-gate repairs. The split walk states no rule of its
// own, so a crowded leg has no case here to refuse: the authority halves the leg instead, which
// always leaves both a leg and a gap. A margin subtracted by hand in the walk did have such a
// case, and it reached the user as `Shift+L` silently doing nothing on the commonest split there
// is — a grid-step ring cut at the default 1/16 grid.
TEST_CASE("planToggleJunctions lands the origin's arrival at the clearance", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a junction one margin after the onset")
    {
        common::core::Chart chart = makeGlideChart();
        // A leg exactly as long as the margin: retreating by the margin would put the arrival AT
        // the origin's own onset, where no keyframe may sit. The authority halves it instead.
        const common::core::Fraction margin = common::core::minimumSustainDistanceBeats(4);
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(), common::core::Keyframe{.offset = margin, .fret = 8});
        const auto plan = splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, margin)});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& origin = chart.notes[0];
        CHECK(origin.fret == 7);
        CHECK(origin.sustain == margin);
        REQUIRE(origin.keyframes.size() == 1);
        CHECK(origin.keyframes[0].fret == 8);
        CHECK(origin.keyframes[0].offset == common::core::Fraction{1, 8});
        // The junction hands its own fret to the new head, and the remainder rides on.
        const common::core::ChartNote& split = chart.notes[1];
        CHECK(split.fret == 8);
        CHECK(split.position.offset == margin);
        CHECK(split.sustain == common::core::Fraction{15, 4});
    }

    SECTION("a junction crowding the statement before it")
    {
        common::core::Chart chart = makeGlideChart();
        // The other crowded shape: the last leg starts at a statement an eighth of a beat back,
        // inside the 4/4 margin, so the halving is measured from THAT statement rather than from
        // the onset — no repair may ever take an earlier statement's place.
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(),
            common::core::Keyframe{.offset = common::core::Fraction{15, 8}, .fret = 8});
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& origin = chart.notes[0];
        CHECK(origin.sustain == common::core::Fraction{2});
        REQUIRE(origin.keyframes.size() == 2);
        CHECK(origin.keyframes[0].offset == common::core::Fraction{15, 8});
        // Halfway from that statement to the new head, never a whole margin back off it.
        CHECK(origin.keyframes[1].fret == 9);
        CHECK(origin.keyframes[1].offset == common::core::Fraction{31, 16});
        CHECK(chart.notes[1].fret == 9);
    }
}

// A point that says nothing the origin's path does not already say has no leg for the origin to
// keep: the retreated copy would be authoring state the origin never meant, standing on the tail's
// tip until the caret leaving the note dissolved it. The one silence law sheds it in the walk, so
// the origin ends on a plain tail — and joining the head back plants the same silent point at the
// junction, so the round trip through the join is still exact.
TEST_CASE("planToggleJunctions sheds a silent arrival from the origin", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart = makeGlideChart();
    // The glide's arrival at 9, then a point a beat later restating 9 where the path already
    // holds it — the point a digit typed at the note's own fret plants.
    chart.notes[0].keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 9},
        common::core::Keyframe{.offset = common::core::Fraction{3}, .fret = 9},
    };
    const common::core::Chart original = chart;

    const auto plan =
        splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{3})});
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 2);
    const common::core::ChartNote& origin = chart.notes[0];
    CHECK(origin.sustain == common::core::Fraction{3});
    REQUIRE(origin.keyframes.size() == 1);
    CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
    CHECK(origin.keyframes[0].fret == 9);
    const common::core::ChartNote& split = chart.notes[1];
    CHECK(split.position == common::core::GridPosition{.measure = 2, .beat = 4, .offset = {}});
    CHECK(split.fret == 9);
    CHECK(split.sustain == common::core::Fraction{1});
    CHECK(split.keyframes.empty());

    const auto joined = joinHeads(chart, tempo_map, {keyAt(split.position, 1)});
    REQUIRE(joined.has_value());
    if (!joined.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, joined->plan);
    CHECK(chart == original);
}

// What a note's own fret NAMES, and what it can reach — the operand both the picker and the
// planner read, so the row offered and the node committed are one answer.
TEST_CASE("chartHarmonicNodeCandidates reads the fret as a label", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a label's rows are ordered by partial, lowest first")
    {
        // The editor resolves against the whole partial bound, so "5" names three nodes — and the
        // ORDER is the contract: the first row is what a choiceless press writes and what the
        // picker opens on, which is the 4th partial. Here the NEAREST node is the 4th's 4.98 too;
        // the label in the section below is where the two rules part company.
        const common::core::Chart chart = makeSingleNoteChart(5);
        const std::vector<common::core::HarmonicNodeCandidate> candidates =
            chartHarmonicNodeCandidates(chart.notes[0], chart.tuning, tempo_map);
        REQUIRE(candidates.size() == 3);
        CHECK(candidates[0].partial == 4);
        CHECK_THAT(candidates[0].position, Catch::Matchers::WithinAbs(4.9800, 0.001));
        CHECK(candidates[1].partial == 13);
        CHECK_THAT(candidates[1].position, Catch::Matchers::WithinAbs(4.5421, 0.001));
        CHECK(candidates[2].partial == 15);
        CHECK_THAT(candidates[2].position, Catch::Matchers::WithinAbs(5.3695, 0.001));
    }

    SECTION("the conventional ambiguity leads its own rows")
    {
        // "3" is the label whose two conventional readings sit a third of a fret apart, and the
        // lowest partial still leads: the 6th's 3.156 before the 7th's 2.669, with the two
        // high-order rows the bound admits behind them.
        const common::core::Chart chart = makeSingleNoteChart(3);
        const std::vector<common::core::HarmonicNodeCandidate> candidates =
            chartHarmonicNodeCandidates(chart.notes[0], chart.tuning, tempo_map);
        REQUIRE(candidates.size() == 4);
        CHECK(candidates[0].partial == 6);
        CHECK_THAT(candidates[0].position, Catch::Matchers::WithinAbs(3.1564, 0.001));
        CHECK(candidates[1].partial == 7);
        CHECK_THAT(candidates[1].position, Catch::Matchers::WithinAbs(2.6687, 0.001));
        CHECK(candidates[2].partial == 11);
        CHECK_THAT(candidates[2].position, Catch::Matchers::WithinAbs(3.4741, 0.001));
        CHECK(candidates[3].partial == 13);
        CHECK_THAT(candidates[3].position, Catch::Matchers::WithinAbs(2.8921, 0.001));
    }

    SECTION("an attack whose saved form records no node names nothing")
    {
        // The reachability filter is the RULE AUTHORITY's answer rather than a bound restated
        // here, so an attack that cannot record a node at all drops every row: a scrape is
        // unpitched travel end to end. The press then leaves such a note exactly alone.
        common::core::Chart scraping;
        scraping.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        scraping.notes = {makeScrape({.measure = 2, .beat = 1}, 1)};
        CHECK(chartHarmonicNodeCandidates(scraping.notes[0], scraping.tuning, tempo_map).empty());
    }

    SECTION("an open string names nothing")
    {
        // The ONE fret with nothing to offer: its label is zero, which is not a touch at all.
        const common::core::Chart chart = makeSingleNoteChart(0);
        CHECK(chartHarmonicNodeCandidates(chart.notes[0], chart.tuning, tempo_map).empty());
    }

    SECTION("the labels import calls dead do name nodes here")
    {
        // 1, 11 and 13 reach no harmonic under import's snapping cap, so they were dead keys for
        // the verb too. Under the editor's wider bound each names at least one high-order node, and
        // the verb now states it rather than skipping the note.
        for (const int fret : {1, 11, 13})
        {
            INFO("fret " << fret);
            const common::core::Chart chart = makeSingleNoteChart(fret);
            const std::vector<common::core::HarmonicNodeCandidate> candidates =
                chartHarmonicNodeCandidates(chart.notes[0], chart.tuning, tempo_map);
            CHECK_FALSE(candidates.empty());
        }
    }

    SECTION("a pinch's fret is not the fretting hand's label")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        common::core::ChartNote& pinch = chart.notes[0];
        pinch.attack = common::core::NoteAttack::Pinch;
        pinch.harmonic_node = 17.0;
        CHECK(chartHarmonicNodeCandidates(pinch, chart.tuning, tempo_map).empty());
    }
}

// The harmonic verb's whole arithmetic: the fret a charter typed IS the node the finger touches,
// resolved against the stop the string actually SPEAKS from rather than against the number typed.
// The three sections are the three hands that stop can come from — the nut, a capo, and the stop a
// tap holds beside its own landing point — and one formula covers all of them because fret
// positions are logarithmic, so the stop and the offset simply add.
TEST_CASE("planSetHarmonic states the typed fret as the node it names", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("an open string's typed fret resolves against the nut")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const touched =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(touched != nullptr);
            if (touched != nullptr)
            {
                // The touch presses nothing, so the fret goes and the node carries the position.
                CHECK(touched->fret == 0);
                const std::optional<double>& node = touched->harmonic_node;
                REQUIRE(node.has_value());
                if (node.has_value())
                {
                    // The 4th partial's nut-side node, which "5" is the conventional label for.
                    CHECK_THAT(*node, Catch::Matchers::WithinAbs(4.9800, 0.001));
                }
            }
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a capo'd string resolves against the capo")
    {
        // Our frets are ABSOLUTE where Guitar Pro's labels are capo-relative, so the charter types
        // 7 for the node a capo at 2 puts five frets up, and the offset — not the typed number —
        // is what names the partial.
        common::core::Chart chart = makeSingleNoteChart(7);
        chart.tuning.capo = 2;
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const touched =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(touched != nullptr);
            if (touched != nullptr)
            {
                CHECK(touched->fret == 0);
                const std::optional<double>& node = touched->harmonic_node;
                REQUIRE(node.has_value());
                if (node.has_value())
                {
                    CHECK_THAT(*node, Catch::Matchers::WithinAbs(6.9800, 0.001));
                }
            }
            applyAndValidate(chart, tempo_map, *plan);
        }
    }

    SECTION("a tap resolves against the stop its fretting hand holds")
    {
        // The tap-harmonic figure: hold 5, tap the octave twelve frets above it. The node is
        // measured from the HELD stop, so the landing point states itself and the offset is the
        // octave — asking the note's own fret would measure the node from where the tap landed.
        common::core::Chart chart = makeSingleNoteChart(17);
        common::core::ChartNote& tap = chart.notes[0];
        tap.attack = common::core::NoteAttack::Tap;
        tap.held = 5;
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const touched =
                noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
            REQUIRE(touched != nullptr);
            if (touched != nullptr)
            {
                const std::optional<double>& node = touched->harmonic_node;
                REQUIRE(node.has_value());
                if (node.has_value())
                {
                    CHECK_THAT(*node, Catch::Matchers::WithinAbs(17.0, 0.001));
                }
            }
        }
    }

    SECTION("the chosen partial binds every label that offers it and nothing else")
    {
        // A chord across two labels, choosing a partial only one of them names: "3" takes the 7th
        // partial's node, while "5" — whose rows are the 4th, 13th and 15th — takes its own first
        // row, the lowest partial a choiceless press would have written. One plan, both members, as
        // the uniform-scope law requires.
        common::core::Chart chart = makeSingleNoteChart(3);
        chart.notes.push_back(makeTestNote({.measure = 2, .beat = 1}, 2, 5));
        const std::vector<ChartSlotKey> chord{
            keyAt({.measure = 2, .beat = 1}, 1), keyAt({.measure = 2, .beat = 1}, 2)
        };

        const auto plan = planSetHarmonic(chart, tempo_map, chord, 7, "Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            const std::optional<double>& ambiguous = chart.notes[0].harmonic_node;
            REQUIRE(ambiguous.has_value());
            if (ambiguous.has_value())
            {
                CHECK_THAT(*ambiguous, Catch::Matchers::WithinAbs(2.6687, 0.001));
            }
            const std::optional<double>& settled = chart.notes[1].harmonic_node;
            REQUIRE(settled.has_value());
            if (settled.has_value())
            {
                CHECK_THAT(*settled, Catch::Matchers::WithinAbs(4.9800, 0.001));
            }
        }
    }
}

// The skip is the whole of the ruling on a fret that names nothing: the verb states a node or
// leaves the note alone, and never moves the hand to the nearest node to invent one. An open string
// states no position at all — its offset is zero, which is not a touch — and under the editor's
// partial bound it is the only fret in that position. A press that only skipped is NoChange, silent
// like the mute rows.
TEST_CASE("planSetHarmonic skips a fret that names no node", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const common::core::Chart chart = makeSingleNoteChart(0);
    const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
    REQUIRE_FALSE(plan.has_value());
    if (!plan.has_value())
    {
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }
}

// The clear inverts the set exactly, which is what makes the pair a true toggle with no memory of
// an overridden technique: the finger presses where it was touching, and the arithmetic returns
// every integer label the set can produce. Under the editor's partial bound that is every integer
// fret but the open string — the nut-side nodes of partials 3 to 6 and of the high-order partials
// the wider bound admits at 1, 11 and 13, plus the 3rd partial's bridge-side node at 19.
TEST_CASE("planClearHarmonic presses the fret the touch was standing on", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    for (const int fret : {1, 3, 4, 5, 7, 11, 13, 19})
    {
        INFO("fret " << fret);
        common::core::Chart chart = makeSingleNoteChart(fret);
        const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
        REQUIRE(set.has_value());
        if (set.has_value())
        {
            applyAndValidate(chart, tempo_map, *set);
            CHECK(chart.notes[0].fret == 0);
            const auto cleared = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic");
            REQUIRE(cleared.has_value());
            if (cleared.has_value())
            {
                applyAndValidate(chart, tempo_map, *cleared);
                CHECK(chart.notes[0].fret == fret);
                CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
            }
        }
    }
}

// THE FRETTING HAND'S CLEAR REACHES ONLY THE FRETTING HAND'S NODES. An IMPORTED artificial keeps
// the stop its fretting hand is pressing, since the node it loses belonged to the other hand — and
// a PINCH is left exactly as it was, because the thumb's graze is the `PinchHarmonic` row's to
// clear (planClearPinchHarmonic). One clear once reached both, which let the picker's "No harmonic"
// strip a pinch the charter had only selected beside a real carrier.
TEST_CASE("planClearHarmonic removes the harmonic the fretting hand owns", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("an imported artificial keeps the stop it is pressing")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        chart.notes[0].harmonic_node = 17.0;

        const auto plan = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes[0].fret == 5);
            CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        }
    }

    SECTION("a pinch is no carrier, so the press finds nothing to clear")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        common::core::ChartNote& pinch = chart.notes[0];
        pinch.attack = common::core::NoteAttack::Pinch;
        pinch.harmonic_node = 17.0;

        const auto plan = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic");
        REQUIRE_FALSE(plan.has_value());
        if (!plan.has_value())
        {
            CHECK(plan.error() == ChartPlanRefusal::NoChange);
        }
        // Nothing was written, so the thumb's graze and the attack carrying it both stand.
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pinch);
        CHECK(chart.notes[0].fret == 5);
        const std::optional<double>& grazed = chart.notes[0].harmonic_node;
        REQUIRE(grazed.has_value());
        if (grazed.has_value())
        {
            CHECK_THAT(*grazed, Catch::Matchers::WithinAbs(17.0, 0.001));
        }
    }
}

// THE PINCH ROW'S OWN CLEAR, and why that row is not a plain attack toggle: clearing to the pick
// alone would leave a stop and a node standing — an artificial harmonic nobody authored. The FRET
// is untouched, because a pinch's fret was pressed all along; there is no press-where-you-touched
// arithmetic here at all, which is what leaves an open-string pinch open rather than pressing its
// graze down as a fret number.
TEST_CASE("planClearPinchHarmonic returns the pinch to the pick it was picked as", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("a pinch loses its node and its attack")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        common::core::ChartNote& pinch = chart.notes[0];
        pinch.attack = common::core::NoteAttack::Pinch;
        pinch.harmonic_node = 17.0;

        const auto plan = planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
            CHECK(chart.notes[0].fret == 5);
            CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        }
    }

    SECTION("an open-string pinch's graze is never pressed as a fret")
    {
        // A thumb grazing out over the body was never standing anywhere a fret number can name, and
        // the untouched fret is what says so: the string stays open.
        common::core::Chart chart = makeSingleNoteChart(0);
        common::core::ChartNote& pinch = chart.notes[0];
        pinch.attack = common::core::NoteAttack::Pinch;
        pinch.harmonic_node = 12.0;

        const auto plan = planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
            CHECK(chart.notes[0].fret == 0);
            CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        }
    }

    SECTION("a fret-hand carrier is no pinch, so the press finds nothing to clear")
    {
        // The mirror of the clear above: this row owns one hand, and the other hand's touch stays
        // planClearHarmonic's.
        common::core::Chart chart = makeSingleNoteChart(5);
        const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
        REQUIRE(set.has_value());
        if (set.has_value())
        {
            applyAndValidate(chart, tempo_map, *set);
        }

        const auto plan = planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic");
        REQUIRE_FALSE(plan.has_value());
        if (!plan.has_value())
        {
            CHECK(plan.error() == ChartPlanRefusal::NoChange);
        }
    }
}

// WHAT "CARRIES" MEANS FOR THE FRET-HAND VERB — what its clear removes, what its picker ticks, and
// what makes every member of a selection a carrier: any node whose attack keeps it on the neck.
// Deliberately wider than the fret-hand harmonic proper, since a tap's node and an imported
// artificial's both count (nothing else in the editor could un-harmonic them); the pinch is the one
// node it excludes, and a note with no node at all carries nothing to remove.
TEST_CASE("carriesNeckHarmonic answers for the hand that owns the node", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    // The fret-hand touch, made through the verb: fret 0, because the finger presses nothing, with
    // the node it stands on.
    common::core::Chart touched = makeSingleNoteChart(12);
    const auto set = planSetHarmonic(touched, tempo_map, keys, std::nullopt, "Harmonic");
    REQUIRE(set.has_value());
    if (set.has_value())
    {
        applyAndValidate(touched, tempo_map, *set);
    }
    CHECK(touched.notes[0].fret == 0);
    CHECK(carriesNeckHarmonic(touched.notes[0]));

    common::core::ChartNote pinch = makeTestNote({.measure = 2, .beat = 1}, 1, 5);
    pinch.attack = common::core::NoteAttack::Pinch;
    pinch.harmonic_node = 17.0;
    CHECK_FALSE(carriesNeckHarmonic(pinch));

    CHECK_FALSE(carriesNeckHarmonic(makeTestNote({.measure = 2, .beat = 1}, 1, 5)));
}

// A FRET-HAND HARMONIC HAS NO STOP TO RETYPE. Its finger stands on the node and presses nothing, so
// the digit channel has nothing to land on — and landing it anyway authored `fret 5 + node 4.98`,
// a stop and a touch naming two different places, which passed the node-beyond-the-stop rule
// because 4.98 is not beyond nothing. Refused whole, so the pending entry paints red.
TEST_CASE("planRetypeFrets refuses the sounding stop of a fret-hand harmonic", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart = makeSingleNoteChart(5);
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};
    const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
    REQUIRE(set.has_value());
    if (set.has_value())
    {
        applyAndValidate(chart, tempo_map, *set);
    }

    const auto plan = planRetypeFrets(
        chart,
        tempo_map,
        chart.notes,
        keys,
        {},
        ChartFretSet{.fret = 7},
        common::core::ChartStopChannel::Sounding);
    REQUIRE_FALSE(plan.has_value());
    if (!plan.has_value())
    {
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
}

// A NODE TRAVELS WITH ITS STOP. The node is `stop + offset` on a logarithmic board, so a stop that
// moves while its node stands still names an offset the harmonic never had: an artificial at fret 5
// touching 17 is the octave, and retyped to 7 it must touch 19 to stay one.
TEST_CASE("planRetypeFrets carries a node with the stop it is measured from", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart = makeSingleNoteChart(5);
    chart.notes[0].harmonic_node = 17.0;
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const auto plan = planRetypeFrets(
        chart,
        tempo_map,
        chart.notes,
        keys,
        {},
        ChartFretSet{.fret = 7},
        common::core::ChartStopChannel::Sounding);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        applyAndValidate(chart, tempo_map, *plan);
        CHECK(chart.notes[0].fret == 7);
        const std::optional<double>& node = chart.notes[0].harmonic_node;
        REQUIRE(node.has_value());
        if (node.has_value())
        {
            CHECK_THAT(*node, Catch::Matchers::WithinAbs(19.0, 0.001));
        }
    }
}

// The uniform-scope law on the pinch harmonic row, which reads exactly like the flag rows': a
// selection where every note already carries the picking thumb's graze clears, and anything short
// of that sets. The fret-hand harmonic has no row here at all — its set states a VALUE, so it runs
// as its own verb rather than as a toggle — and the two nodes never read as one another's
// technique: a node the fretting side owns is not this row's, and the thumb's is not the fret
// hand's.
TEST_CASE("The pinch harmonic law reads the whole selection for the direction", "[core][chart]")
{
    common::core::Chart chart = makeSingleNoteChart(5);
    chart.notes.push_back(makeTestNote({.measure = 2, .beat = 1}, 2, 5));
    chart.notes[0].attack = common::core::NoteAttack::Pinch;
    chart.notes[0].harmonic_node = 17.0;
    const ChartTechniqueLaw law = chartTechniqueLaw(ChartTechnique::PinchHarmonic);

    ChartSelection carrying;
    carrying.add(ChartNoteKey{.slot = keyAt({.measure = 2, .beat = 1}, 1)});
    CHECK(law.carried(chart, carrying));

    // One note without a graze makes the press an ordinary SET over the whole scope.
    ChartSelection mixed;
    mixed.add(ChartNoteKey{.slot = keyAt({.measure = 2, .beat = 1}, 1)});
    mixed.add(ChartNoteKey{.slot = keyAt({.measure = 2, .beat = 1}, 2)});
    CHECK_FALSE(law.carried(chart, mixed));

    // An empty operand answers false, so such a press means set — and a set with nothing to write
    // plans to NoChange, the inert outcome every other row has.
    CHECK_FALSE(law.carried(chart, ChartSelection{}));

    // A node the fretting side owns belongs to the `H` verb, so it never reads as this row's.
    chart.notes[0].attack = common::core::NoteAttack::Pick;
    chart.notes[0].fret = 0;
    chart.notes[0].harmonic_node = 4.98;
    CHECK_FALSE(law.carried(chart, carrying));
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
TEST_CASE("planToggleJunctions splits at every selected junction", "[core][chart]")
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
    common::core::setSlideOut(glide, 3);
    chart.notes = {std::move(glide)};
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = splitAt(
        chart,
        tempo_map,
        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1}),
         keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
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
    CHECK(common::core::slideOutFretOrNull(second) == nullptr);
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
    const int* const terminal = common::core::slideOutFretOrNull(third);
    REQUIRE(terminal != nullptr);
    if (terminal != nullptr)
    {
        CHECK(*terminal == 3);
    }
    // The earlier products hand off to a re-picked head, and their arrivals retreat a margin
    // inside the ring, so nothing sits at their end and neither invents a trail-off.
    CHECK(common::core::slideOutFretOrNull(chart.notes[0]) == nullptr);

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// THE JOIN, the split run backward: the selected head stops being a note and becomes a junction
// POINT on its same-string predecessor's path. The two rings lie end to end, the head's own
// keyframes rebase onto the predecessor's onset and ride along, and the point takes the selection
// so the next press splits it straight back.
TEST_CASE("planToggleJunctions joins a head into its predecessor as a point", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote head =
        makeTestNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2});
    head.attack = common::core::NoteAttack::Legato;
    head.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{1}, .fret = 9}};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4}), std::move(head)
    };
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 3, .beat = 1}, 1)});
    REQUIRE(joined.has_value());
    if (!joined.has_value())
    {
        return;
    }
    CHECK(joined->plan.label == "Join Notes");
    applyAndValidate(chart, tempo_map, joined->plan);

    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& path = chart.notes[0];
    CHECK(path.position == common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
    CHECK(path.fret == 5);
    // The two rings end to end: four beats to the junction, then the head's own two.
    CHECK(path.sustain == common::core::Fraction{6});
    REQUIRE(path.keyframes.size() == 2);
    // The junction states the fret the head sounded — the very value a split's product head would
    // take back — and the head's own later statement rides along rebased onto this onset.
    CHECK(path.keyframes[0].offset == common::core::Fraction{4});
    CHECK(path.keyframes[0].fret == 7);
    CHECK_FALSE(path.keyframes[0].bend.has_value());
    CHECK_FALSE(path.keyframes[0].vibrato.has_value());
    CHECK(path.keyframes[1].offset == common::core::Fraction{5});
    CHECK(path.keyframes[1].fret == 9);

    // The point takes the selection, which is what makes the verb a toggle: the next press finds
    // the junction it just made and splits it back.
    CHECK(
        joined->selection ==
        std::vector<ChartSelectionKey>{ChartKeyframeKey{
            .note = keyAt({.measure = 2, .beat = 1}, 1), .offset = common::core::Fraction{4}
        }});

    // One entry, and it reverses field for field.
    REQUIRE(applyChartChange(chart, joined->plan.reversed()).has_value());
    CHECK(chart == original);
}

// W10'S TIE, and the whole of why it needs no tie datum. Joining an EQUAL-fret head leaves a point
// that restates the fret the path is already running on, so the commit law owns it: it draws, it
// takes the selection, it dissolves with focus, and the history entry sheds it. What the document
// records is one longer ring and one note fewer — never a tie.
TEST_CASE("planToggleJunctions joins an equal-fret head as a silent point", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote head =
        makeTestNote({.measure = 3, .beat = 1}, 1, 5, common::core::Fraction{2});
    head.attack = common::core::NoteAttack::Legato;
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4}), std::move(head)
    };
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 3, .beat = 1}, 1)});
    REQUIRE(joined.has_value());
    if (!joined.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, joined->plan);

    // In MEMORY the point is there: it is what the lane draws and what the selection names.
    REQUIRE(chart.notes.size() == 1);
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{4});
    CHECK(chart.notes[0].keyframes[0].fret == 5);
    CHECK(chart.notes[0].sustain == common::core::Fraction{6});

    // The HISTORY records the written form, and the written form has no point at all: the entry is
    // exactly one grown ring and one removed note.
    const ChartEditPlan written = writtenChartPlan(joined->plan);
    CHECK(written.removed.size() == 2);
    REQUIRE(written.inserted.size() == 1);
    CHECK(written.inserted[0].sustain == common::core::Fraction{6});
    CHECK(written.inserted[0].keyframes.empty());
}

// THE ROUND TRIP, and the reason the join is written as the split's inverse rather than as a
// second law: splitting a gesture and joining the product back restores the chart field for field.
// The arrival the split retreated off the new head RETURNS to the junction, asked of the same
// clearance authority backward — without that return the round trip would quietly lose a quarter
// beat of travel on every pass.
TEST_CASE("planToggleJunctions makes split then join a byte-exact round trip", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto split =
        splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
    REQUIRE(split.has_value());
    if (!split.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *split);
    REQUIRE(chart.notes.size() == 2);
    // The arrival stands a margin short of the new head while the origin's ring runs on to it.
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(
        chart.notes[0].keyframes[0].offset ==
        common::core::Fraction{2} - common::core::minimumSustainDistanceBeats(4));

    const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 2, .beat = 3}, 1)});
    REQUIRE(joined.has_value());
    if (!joined.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, joined->plan);
    CHECK(chart == original);
}

// What cannot be joined is REFUSED whole, never clamped and never partly applied: each of these
// would author a handover the format cannot state, and the press simply does nothing.
TEST_CASE("planToggleJunctions refuses what cannot join", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a head with no predecessor on its string")
    {
        const common::core::Chart chart = makeSingleNoteChart(5);
        const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 2, .beat = 1}, 1)});
        REQUIRE_FALSE(joined.has_value());
        CHECK(joined.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a predecessor whose tail already falls away")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote trailing =
            makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4});
        common::core::setSlideOut(trailing, 3);
        chart.notes = {
            std::move(trailing),
            makeTestNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2})
        };
        const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 3, .beat = 1}, 1)});
        REQUIRE_FALSE(joined.has_value());
        CHECK(joined.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a scrape predecessor, which has no fretting finger to hand over")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeScrape({.measure = 2, .beat = 1}, 1),
            makeTestNote({.measure = 2, .beat = 2}, 1, 7, common::core::Fraction{1})
        };
        const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 2, .beat = 2}, 1)});
        REQUIRE_FALSE(joined.has_value());
        CHECK(joined.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a head carrying a harmonic node, which a point cannot state")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote squeal =
            makeTestNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2});
        squeal.attack = common::core::NoteAttack::Pinch;
        squeal.harmonic_node = 19.0;
        chart.notes = {
            makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4}),
            std::move(squeal)
        };
        const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 3, .beat = 1}, 1)});
        REQUIRE_FALSE(joined.has_value());
        CHECK(joined.error() == ChartPlanRefusal::Invalid);
    }
}

// Both halves in ONE press and one entry, which is the whole claim of a single verb: the selection
// holds a keyframe on one string and a head on another, and the label says both ran.
TEST_CASE("planToggleJunctions splits and joins in one press", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    common::core::ChartNote head =
        makeTestNote({.measure = 3, .beat = 1}, 2, 7, common::core::Fraction{2});
    head.attack = common::core::NoteAttack::Legato;
    chart.notes.push_back(makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{4}));
    chart.notes.push_back(std::move(head));
    std::ranges::sort(chart.notes, common::core::chartNoteOrderLess);
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto toggled = planToggleJunctions(
        chart,
        tempo_map,
        {keyAt({.measure = 3, .beat = 1}, 2)},
        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
    REQUIRE(toggled.has_value());
    if (!toggled.has_value())
    {
        return;
    }
    CHECK(toggled->plan.label == "Split and Join");
    applyAndValidate(chart, tempo_map, toggled->plan);

    // Three notes: string 1's glide became two, string 2's pair became one.
    REQUIRE(chart.notes.size() == 3);
    const common::core::ChartNote* const grown =
        noteAt(chart.notes, {.measure = 2, .beat = 1, .offset = {}}, 2);
    REQUIRE(grown != nullptr);
    if (grown != nullptr)
    {
        CHECK(grown->sustain == common::core::Fraction{6});
        REQUIRE(grown->keyframes.size() == 1);
        CHECK(grown->keyframes[0].fret == 7);
    }
    const common::core::ChartNote* const product =
        noteAt(chart.notes, {.measure = 2, .beat = 3, .offset = {}}, 1);
    REQUIRE(product != nullptr);
    if (product != nullptr)
    {
        CHECK(product->fret == 9);
        CHECK(product->attack == common::core::NoteAttack::Legato);
    }

    REQUIRE(applyChartChange(chart, toggled->plan.reversed()).has_value());
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

// The dissolve law's static half: a pending point dissolves iff it changes NEITHER the path
// function NOR the state. Every case of the described flow falls out of that one rule, which is
// why these three sections share a planner and not a branch.
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
    // On the junction, not the release: a release states its fret and nothing else.
    chart.notes[0].keyframes[0].vibrato = common::core::VibratoState::Narrow;
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
    CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
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
        const common::core::Chart chart = makeGlideChart();
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

// DERIVED HELD, the editor half. Authoring the pull-off is what makes the stored field a second
// spelling of one fact, so the entry that authors it is the entry that clears the field — and the
// stop itself does not move, because the notation states it.
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
        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*before},
            ChartFretSet{.fret = 9},
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

// The other half of the same rule: where the derivation owns the stop, authoring one is REFUSED
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
    const auto plan = retypeNotes(
        chart, tempo_map, {*tap}, ChartFretSet{.fret = 9}, common::core::ChartStopChannel::Held);
    REQUIRE_FALSE(plan.has_value());
    if (plan.has_value())
    {
        return;
    }
    // Invalid, never NoChange: the two emptinesses are what the pending box's red state reads.
    CHECK(plan.error() == ChartPlanRefusal::Invalid);
}

// THE PLANT'S FACE, the editor half. A fretting-hand source's plant is the notation's stop exactly
// as a derived tap stop is — the pull-off prints it — so the held channel is refused at its
// satellite, and settles clean where the digit agrees. Read off the wide planted table, which is
// what keeps a fretting-hand note from ever being handed a held field its attack forbids.
TEST_CASE("The held channel is refused at a fretting-hand source's plant", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote source =
        makeTestNote({.measure = 2, .beat = 1}, 1, 7, common::core::Fraction{1});
    common::core::ChartNote successor =
        makeTestNote({.measure = 2, .beat = 2}, 1, 5, common::core::Fraction{1});
    successor.attack = common::core::NoteAttack::Legato;
    chart.notes = {std::move(source), std::move(successor)};
    const common::core::GridPosition source_slot{.measure = 2, .beat = 1, .offset = {}};
    const common::core::ChartNote* const planted_source = noteAt(chart.notes, source_slot, 1);
    REQUIRE(planted_source != nullptr);
    if (planted_source == nullptr)
    {
        return;
    }
    REQUIRE_FALSE(planted_source->held.has_value());

    SECTION("typing another value at the plant is refused")
    {
        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*planted_source},
            ChartFretSet{.fret = 9},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("typing the plant itself settles as the no-op it is")
    {
        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*planted_source},
            ChartFretSet{.fret = 5},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }

    SECTION("a MIXED entry naming the plant beside a bare tap is refused whole")
    {
        // Whole-plan like every other refusal here: one member the notation owns rejects the
        // entry rather than leaving the tap's default retyped and the plant untouched. The plant
        // is a satellite the entry addresses, so it answers for the whole rather than passing
        // through while the tap alone takes the digit.
        common::core::Chart mixed = chart;
        common::core::ChartNote bare =
            makeTestNote({.measure = 2, .beat = 1}, 3, 10, common::core::Fraction{1});
        bare.attack = common::core::NoteAttack::Tap;
        mixed.notes.push_back(std::move(bare));
        std::ranges::sort(mixed.notes, common::core::chartNoteOrderLess);
        const common::core::ChartNote* const source_again = noteAt(mixed.notes, source_slot, 1);
        const common::core::ChartNote* const tap = noteAt(mixed.notes, source_slot, 3);
        REQUIRE(source_again != nullptr);
        REQUIRE(tap != nullptr);
        if (source_again == nullptr || tap == nullptr)
        {
            return;
        }
        const auto plan = retypeNotes(
            mixed,
            tempo_map,
            {*source_again, *tap},
            ChartFretSet{.fret = 4},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
}

// THE HELD CHANNEL'S DELETE (THE PLANT'S FACE): a clearing planner of its own, because a default
// satellite carries no statement at all and a Delete on it must never author a real held 0.
// Four answers off one table: an AUTHORED stop is withdrawn, a DEFAULT clears nothing, and a
// DERIVED tap stop and a PLANT are the notation's and refuse.
TEST_CASE("Clearing held stops withdraws the charter's statement and nothing else", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const ChartSlotKey tap_slot{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1};

    SECTION("an authored stop is withdrawn and the onset under it kept whole")
    {
        // A picked successor claims no connection, so the stored 9 is the charter's own.
        const common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, 9);
        const auto plan = planClearHeldStops(chart, tempo_map, {tap_slot});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        REQUIRE(plan->inserted.size() == 1);
        CHECK_FALSE(plan->inserted.front().held.has_value());
        CHECK(plan->inserted.front().attack == common::core::NoteAttack::Tap);
        CHECK(plan->inserted.front().fret == 12);
        CHECK(plan->label == "Release Held Stop");
    }

    SECTION("a default clears nothing")
    {
        const common::core::Chart chart =
            makeDerivedHeldChart(common::core::NoteAttack::Pick, std::nullopt);
        const auto plan = planClearHeldStops(chart, tempo_map, {tap_slot});
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }

    SECTION("a derived tap stop is the notation's and refuses")
    {
        const common::core::Chart chart =
            makeDerivedHeldChart(common::core::NoteAttack::Legato, std::nullopt);
        const auto plan = planClearHeldStops(chart, tempo_map, {tap_slot});
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("a plant is the notation's and refuses")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote source =
            makeTestNote({.measure = 2, .beat = 1}, 1, 7, common::core::Fraction{1});
        common::core::ChartNote successor =
            makeTestNote({.measure = 2, .beat = 2}, 1, 5, common::core::Fraction{1});
        successor.attack = common::core::NoteAttack::Legato;
        chart.notes = {std::move(source), std::move(successor)};
        const auto plan = planClearHeldStops(chart, tempo_map, {tap_slot});
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }
}

// SAME-FRET SETTLE, the boundary of the refusal above. Typing the value the derived satellite
// ALREADY SHOWS asks for the state the chart is in, so it is not an authoring attempt the
// derivation has anything to fend off: the entry settles as the no-op it is. The two halves must be
// one test, because what is at stake is exactly where the line between them falls — a refusal that
// keyed on the derivation's PRESENCE alone, never looking at the digit, would paint the pending box
// red over a digit that asked for nothing.
TEST_CASE(
    "The held channel settles clean where the typed digit agrees with the derivation",
    "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    // Both taps sound at one instant; the string is the whole of what tells them apart.
    const common::core::GridPosition onset_slot{.measure = 2, .beat = 1, .offset = {}};

    SECTION("the agreeing digit is a NO-OP, not a refusal")
    {
        const common::core::Chart chart =
            makeDerivedHeldChart(common::core::NoteAttack::Legato, std::nullopt);
        REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{5});
        const common::core::ChartNote* const tap = noteAt(chart.notes, onset_slot, 1);
        REQUIRE(tap != nullptr);
        if (tap == nullptr)
        {
            return;
        }
        // The field is EMPTY, which is what makes NoChange a proof rather than a coincidence: a
        // plan that wrote the agreeing value into it would have diffed non-empty and come back as
        // a plan. So an empty diff says nothing was authored beside the derivation, and an empty
        // diff is what leaves the undo stack untouched at the settle.
        REQUIRE_FALSE(tap->held.has_value());

        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*tap},
            ChartFretSet{.fret = 5},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        // NoChange, never Invalid: the pending box settles clean instead of painting red.
        CHECK(plan.error() == ChartPlanRefusal::NoChange);
    }

    SECTION("a MIXED entry writes at the satellite the derivation does not own")
    {
        // The per-note semantics: an agreeing derived member stops being a refusal CAUSE and
        // contributes nothing, so every other member of the same entry is retyped as ever.
        common::core::Chart chart = makeMixedDerivedHeldChart();
        REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{5});
        const common::core::ChartNote* const derived = noteAt(chart.notes, onset_slot, 1);
        const common::core::ChartNote* const bare = noteAt(chart.notes, onset_slot, 3);
        REQUIRE(derived != nullptr);
        REQUIRE(bare != nullptr);
        if (derived == nullptr || bare == nullptr)
        {
            return;
        }
        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*derived, *bare},
            ChartFretSet{.fret = 5},
            common::core::ChartStopChannel::Held);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        const common::core::ChartNote* const settled = noteAt(chart.notes, onset_slot, 1);
        const common::core::ChartNote* const authored = noteAt(chart.notes, onset_slot, 3);
        REQUIRE(settled != nullptr);
        REQUIRE(authored != nullptr);
        if (settled == nullptr || authored == nullptr)
        {
            return;
        }
        // The derived member is untouched — no field written beside the statement the notation
        // already makes — while the default satellite took the digit.
        CHECK_FALSE(settled->held.has_value());
        CHECK(claimedStops(chart, tempo_map).front() == std::optional{5});
        CHECK(authored->held == std::optional{5});
    }

    SECTION("a MIXED entry is still refused WHOLE where the derived member disagrees")
    {
        // The scope of a refusal is unchanged: one owned stop the digit contradicts rejects the
        // entry rather than leaving a chord half retyped, so the bare tap beside it takes nothing.
        const common::core::Chart chart = makeMixedDerivedHeldChart();
        const common::core::ChartNote* const derived = noteAt(chart.notes, onset_slot, 1);
        const common::core::ChartNote* const bare = noteAt(chart.notes, onset_slot, 3);
        REQUIRE(derived != nullptr);
        REQUIRE(bare != nullptr);
        if (derived == nullptr || bare == nullptr)
        {
            return;
        }
        const auto plan = retypeNotes(
            chart,
            tempo_map,
            {*derived, *bare},
            ChartFretSet{.fret = 9},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(plan.has_value());
        if (plan.has_value())
        {
            return;
        }
        CHECK(plan.error() == ChartPlanRefusal::Invalid);
    }

    SECTION("the AUTHORED tier is untouched: an agreeing digit there is still an entry")
    {
        // The discrimination that keeps the settle on the derived tier alone. The same figure with
        // a PICKED successor derives nothing, so the stored 5 is the charter's own ink — and
        // retyping it to 5 is a write of a value already written, which diffs empty for the
        // ordinary reason and NOT through the derivation's exemption. Typing a DIFFERENT digit
        // there plans as ever, which is what says the tier never learned a refusal.
        const common::core::Chart chart = makeDerivedHeldChart(common::core::NoteAttack::Pick, 5);
        REQUIRE(claimedStops(chart, tempo_map).front() == std::optional{5});
        const common::core::ChartNote* const tap = noteAt(chart.notes, onset_slot, 1);
        REQUIRE(tap != nullptr);
        if (tap == nullptr)
        {
            return;
        }
        const auto same = retypeNotes(
            chart,
            tempo_map,
            {*tap},
            ChartFretSet{.fret = 5},
            common::core::ChartStopChannel::Held);
        REQUIRE_FALSE(same.has_value());
        if (!same.has_value())
        {
            CHECK(same.error() == ChartPlanRefusal::NoChange);
        }
        const auto moved = retypeNotes(
            chart,
            tempo_map,
            {*tap},
            ChartFretSet{.fret = 9},
            common::core::ChartStopChannel::Held);
        CHECK(moved.has_value());
    }
}

// THE DEFAULT SATELLITE IS A TARGET, the other side of the refusal above and the reason the two
// must not be answered by one test. A bare tap's held stop resolves to the grip under it — 0 where
// no span covers it — so the satellite that states it is DRAWN, and the held channel reaches every
// right-hand onset. Typing there AUTHORS a real held stop, because nothing owns a default: gating
// the channel on the STORED field instead would pass the digit through untouched and diff empty.
// `test_chart_projection.cpp` carries the same claim at the projection.
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
    const auto plan = retypeNotes(
        chart, tempo_map, {*before}, ChartFretSet{.fret = 9}, common::core::ChartStopChannel::Held);
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

} // namespace rock_hero::editor::core
