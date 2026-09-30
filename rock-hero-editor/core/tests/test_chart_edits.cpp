#include "chart/chart_edits.h"
#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <string_view>
#include <utility>
#include <variant>
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

// One 4/4 measure pair, then 6/8 from measure 3: a beat is a quarter note before the change and an
// eighth note from it, so a beat count carried across the barline is not the same duration.
[[nodiscard]] common::core::TempoMap makeMeterChangeMap()
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            common::core::TimeSignatureChange{.measure = 3, .numerator = 6, .denominator = 8},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{.measure = 21, .beat = 1, .seconds = 31.0},
        },
    };
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
    return ChartSustainStep{.note_value = common::core::g_tick_quantum_note_value, .grow = grow};
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
// stating fret 12 exactly where the ring ends — which is the SLIDE-OUT, the fret the hand leaves
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

// The vibrato regions the projection draws for the note at `index`: the channel read the way both
// surfaces read it, so a per-leg assertion checks what is drawn and not only what is stored.
[[nodiscard]] std::vector<common::core::VibratoSpanViewState> vibratoRegionsOf(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::size_t index)
{
    common::core::Arrangement arrangement{};
    arrangement.chart = chart;
    const common::core::ChartViewState state =
        common::core::makeChartViewState(arrangement, tempo_map);
    REQUIRE(index < state.notes.size());
    return state.notes[index].vibrato;
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
    const std::vector<common::core::ChartNote>& base, const ChartFretWrite write)
{
    return planRetypeFrets(chart, tempo_map, base, slotsOf(base), {}, write);
}

[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> moveNotes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, common::core::Fraction whole_note_delta,
    int string_delta, std::string_view label)
{
    return planMoveSelection(
        chart, tempo_map, note_keys, {}, whole_note_delta, string_delta, label);
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
                    // would leave wide vibrato on a scrape.
                    for (const common::core::VibratoState vibrato :
                         {common::core::VibratoState::None,
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
// An occupied slot is the gate's refusal, never a replace: the entry keys address the head that
// stands there instead of placing over it.
TEST_CASE("planInsertNote refuses an occupied slot", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    // Slot measure 2 beat 1 / string 1 already holds fret 3.
    const auto plan = planInsertNote(
        chart, tempo_map, makeTestNote({.measure = 2, .beat = 1}, 1, 9), g_fixture_sustain);
    REQUIRE_FALSE(plan.has_value());
    if (!plan.has_value())
    {
        // The refusal carries the rule's own words, which is what the log reports.
        const auto* const invalid = std::get_if<ChartPlanInvalid>(&plan.error());
        REQUIRE(invalid != nullptr);
        CHECK_FALSE(invalid->reason.empty());
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
        common::core::Fraction{1, 8});
    REQUIRE(uncrowded.has_value());
    if (uncrowded.has_value())
    {
        REQUIRE(uncrowded->inserted.size() == 1);
        CHECK(uncrowded->inserted.front().sustain == common::core::Fraction{1, 2});
    }

    // The fixture's string-1 note at measure 3 beat 1 is struck one beat after this slot, so a
    // half-note grid's ring cannot run through it: the finalize gate's normalization ends it there.
    const auto crowded = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 2, .beat = 4}, 1, 5),
        common::core::Fraction{1, 2});
    REQUIRE(crowded.has_value());
    if (crowded.has_value())
    {
        const common::core::ChartNote* placed =
            noteAt(crowded->inserted, {.measure = 2, .beat = 4}, 1);
        REQUIRE(placed != nullptr);
        CHECK(placed->sustain == common::core::Fraction{1});
    }

    // From between two lines the ring is the rest of the step, not a whole one: an eighth-note grid
    // has lines every half beat, so a note a quarter beat past one reaches the next line in another
    // quarter — exactly as the duration verb's first press snaps an off-grid end onto the grid.
    const auto between_lines = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 4, .beat = 1, .offset = {3, 4}}, 1, 5),
        common::core::Fraction{1, 8});
    REQUIRE(between_lines.has_value());
    if (between_lines.has_value())
    {
        REQUIRE(between_lines->inserted.size() == 1);
        CHECK(between_lines->inserted.front().sustain == common::core::Fraction{1, 4});
    }
}

// Re-placing a note identical to the one already on the slot changes nothing, so the plan is empty.
// The ring has to come from the GRID to make that true, because the placed ring overwrites the
// handed note's own sustain — so each case passes the grid whose step is the occupant's ring (a
// 4/4 beat is a quarter note), and the two-beat note is here so the case cannot pass merely
// because the fixture grid happened to match.
TEST_CASE("planInsertNote returns nullopt for an unchanged placement", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto grid_of = [](const common::core::ChartNote& note) {
        return note.sustain * common::core::Fraction{1, 4};
    };
    CHECK_FALSE(
        planInsertNote(chart, tempo_map, chart.notes[0], grid_of(chart.notes[0])).has_value());
    CHECK_FALSE(
        planInsertNote(chart, tempo_map, chart.notes[2], grid_of(chart.notes[2])).has_value());
}

// 40-Q2-B: inserting on a string whose earlier note's sustain rings across the new onset truncates
// that sustain to end exactly at the onset, the statement at the ring's end riding back with it. A
// truncation may SHORTEN a ring but never DELETE a statement: an insert that would erase a point
// standing past the landing, or shed vibrato stated on it, is refused whole by the plan gate
// (finalizePlan).
TEST_CASE(
    "planInsertNote truncates an overlapped sustain and refuses to erase a statement",
    "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const auto ring_with = [&chart](std::vector<common::core::Keyframe> keyframes) {
        chart.notes = {
            common::core::ChartNote{
                .position = {.measure = 1, .beat = 1},
                .string = 1,
                .fret = 5,
                .sustain = common::core::Fraction{2},
                .keyframes = std::move(keyframes),
            },
        };
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    // The new onset lands one beat into the two-beat sustain.
    const auto insert = [&chart, &tempo_map] {
        return planInsertNote(
            chart, tempo_map, makeTestNote({.measure = 1, .beat = 2}, 1, 7), g_fixture_sustain);
    };

    SECTION("an end statement rides back to the landing and nothing is lost")
    {
        ring_with({
            common::core::Keyframe{
                .offset = common::core::Fraction{1, 2}, .fret = {}, .bend = 0.5, .vibrato = {}
            },
            common::core::Keyframe{
                .offset = common::core::Fraction{2}, .fret = {}, .bend = 1.0, .vibrato = {}
            },
        });
        const auto plan = insert();
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            // The earlier note is re-emitted with its sustain cut to the onset distance and its end
            // statement carried to the new end.
            REQUIRE(plan->removed.size() == 1);
            CHECK(plan->removed.front().sustain == common::core::Fraction{2});
            CHECK(plan->removed.front().keyframes.size() == 2);

            const common::core::ChartNote* truncated =
                noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
            REQUIRE(truncated != nullptr);
            CHECK(truncated->sustain == common::core::Fraction{1});
            REQUIRE(truncated->keyframes.size() == 2);
            CHECK(truncated->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(truncated->keyframes[1].offset == common::core::Fraction{1});
            // Bound once, with the explicit guard the CI-only optional checker needs.
            const std::optional<double>& carried = truncated->keyframes[1].bend;
            REQUIRE(carried.has_value());
            if (carried.has_value())
            {
                CHECK_THAT(*carried, Catch::Matchers::WithinULP(1.0, 0));
            }

            // The placed note is inserted alongside the truncated one.
            const common::core::ChartNote* placed =
                noteAt(plan->inserted, {.measure = 1, .beat = 2}, 1);
            REQUIRE(placed != nullptr);
            CHECK(placed->fret == 7);
        }
    }

    SECTION("an interior point past the landing refuses the insert")
    {
        // The bend at 3/2 is no end statement (the ring runs on to 2), so the cut to 1 would erase
        // it: something the charter authored on a note the insert never touched.
        ring_with({
            common::core::Keyframe{
                .offset = common::core::Fraction{1, 2}, .fret = {}, .bend = 0.5, .vibrato = {}
            },
            common::core::Keyframe{
                .offset = common::core::Fraction{3, 2}, .fret = {}, .bend = 1.0, .vibrato = {}
            },
        });
        const auto plan = insert();
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].sustain == common::core::Fraction{2});
        CHECK(chart.notes[0].keyframes.size() == 2);
    }

    SECTION("vibrato stated exactly at the landing refuses the insert")
    {
        // The point at 1 survives the cut (the bound is inclusive) and becomes the end statement,
        // which leaves no vibrato: the vibrato it states would be shed on a note the insert never
        // touched.
        ring_with({
            common::core::Keyframe{
                .offset = common::core::Fraction{1},
                .fret = {},
                .bend = {},
                .vibrato = common::core::VibratoState::Narrow
            },
        });
        const auto plan = insert();
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].sustain == common::core::Fraction{2});
        CHECK(chart.notes[0].keyframes.size() == 1);
    }

    SECTION("a bend stated exactly at the landing stays as the end statement's value")
    {
        // A bend at the end is the curve's last value, so the point survives the cut whole.
        ring_with({
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .fret = {}, .bend = 1.0, .vibrato = {}
            },
        });
        const auto plan = insert();
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* truncated =
                noteAt(plan->inserted, {.measure = 1, .beat = 1}, 1);
            REQUIRE(truncated != nullptr);
            CHECK(truncated->sustain == common::core::Fraction{1});
            REQUIRE(truncated->keyframes.size() == 1);
            CHECK(truncated->keyframes[0].offset == common::core::Fraction{1});
            // Bound once, with the explicit guard the CI-only optional checker needs.
            const std::optional<double>& kept = truncated->keyframes[0].bend;
            REQUIRE(kept.has_value());
            if (kept.has_value())
            {
                CHECK_THAT(*kept, Catch::Matchers::WithinULP(1.0, 0));
            }
        }
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

// A width is its leg's own, so a point stating only a fret would begin an unvibrated leg and end
// the vibrato wherever it was planted. The planted point carries the width of the leg it divides,
// so the vibrato runs on through it as one region.
TEST_CASE("planInsertKeyframe inside a vibrated leg keeps the vibrato running", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
    note.vibrato = common::core::VibratoState::Narrow;
    chart.notes = {std::move(note)};
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan =
        planInsertKeyframe(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{2}, 9);
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    common::core::Chart applied = chart;
    applyAndValidate(applied, tempo_map, *plan);
    REQUIRE(applied.notes.size() == 1);
    REQUIRE(applied.notes[0].keyframes.size() == 1);
    CHECK(applied.notes[0].keyframes[0].offset == common::core::Fraction{2});
    CHECK(applied.notes[0].keyframes[0].fret == 9);
    CHECK(applied.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
    // Drawn, one region from the onset (2.0s) across the point (3.0s) to the ring's end (4.0s).
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(applied, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].state == common::core::VibratoState::Narrow);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

    REQUIRE(applyChartChange(applied, plan->reversed()).has_value());
    CHECK(applied == chart);
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
    CHECK(std::holds_alternative<ChartPlanInvalid>(refused(common::core::Fraction{0}, 11)));
    // Past the ring there is nothing left to state on.
    CHECK(std::holds_alternative<ChartPlanInvalid>(refused(common::core::Fraction{5}, 11)));
    // A second record on one offset leaves the offsets no longer strictly ascending.
    CHECK(std::holds_alternative<ChartPlanInvalid>(refused(common::core::Fraction{2}, 11)));
    // And a slot holding no note names nothing to state a point on.
    const auto empty = planInsertKeyframe(
        chart, tempo_map, keyAt({.measure = 4, .beat = 1}, 1), common::core::Fraction{1}, 5);
    REQUIRE_FALSE(empty.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(empty.error()));
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
    CHECK(std::holds_alternative<ChartPlanInvalid>(stilled.error()));

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

    // One note, a half note left of beat 2: clamping would land it on beat 1 as if it had moved a
    // quarter note.
    CHECK_FALSE(moveNotes(
                    chart,
                    tempo_map,
                    {keyAt({.measure = 1, .beat = 2}, 1)},
                    common::core::Fraction{-1, 2},
                    0,
                    "Move Notes")
                    .has_value());
    // The same note a quarter note left lands exactly on the origin, which is a legal destination.
    CHECK(moveNotes(
              chart,
              tempo_map,
              {keyAt({.measure = 1, .beat = 2}, 1)},
              common::core::Fraction{-1, 4},
              0,
              "Move Notes")
              .has_value());

    // Two notes that would both clamp to the origin are refused too (they would also collide).
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 1, .beat = 2}, 1), keyAt({.measure = 1, .beat = 3}, 1)
    };
    CHECK_FALSE(moveNotes(chart, tempo_map, keys, common::core::Fraction{-5, 2}, 0, "Move Notes")
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

    // The first note advanced a quarter note lands on the second, unmoved note's slot.
    CHECK_FALSE(moveNotes(chart, tempo_map, keys, common::core::Fraction{1, 4}, 0, "Move Notes")
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
        moveNotes(chart, tempo_map, keys, common::core::Fraction{1, 4}, 0, "Move Notes");
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
        moveNotes(chart, tempo_map, {}, common::core::Fraction{1, 4}, 0, "Move Notes").has_value());
    CHECK_FALSE(
        moveNotes(chart, tempo_map, keys, common::core::Fraction{}, 0, "Move Notes").has_value());

    // A key present in the request but absent from the chart moves nothing.
    const std::vector<ChartSlotKey> absent{keyAt({.measure = 9, .beat = 1}, 1)};
    CHECK_FALSE(moveNotes(chart, tempo_map, absent, common::core::Fraction{1, 4}, 0, "Move Notes")
                    .has_value());
}

// The keyframe half of the same step (W13's ruling): a selected point moves along the ring it
// rides, by the whole-note delta a selected note would have moved its slot by.
TEST_CASE("planMoveSelection steps a selected keyframe's offset", "[core][chart]")
{
    const common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartKeyframeKey> first{keyframeKeyAt(
        {.measure = 2, .beat = 1}, 1, common::core::Fraction{2})};

    const auto plan = planMoveSelection(
        chart, tempo_map, {}, first, common::core::Fraction{1, 8}, 0, "Move Keyframe");
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
            common::core::Fraction{-1, 8},
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
            chart, tempo_map, {}, first, common::core::Fraction{-1, 2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped past the ring")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, second, common::core::Fraction{1, 2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped onto its neighbour")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, first, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped across its neighbour")
    {
        // The one bound worth stating twice: the pair would still be legal as a SET, so what
        // refuses it is the stored order, which the planner deliberately never re-sorts.
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, first, common::core::Fraction{3, 8}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped onto a later onset of its own string")
    {
        // An INTERIOR point's ceiling is its own ring's END, and a stored ring never passes the
        // head that stops it — so the head bounds the point too, through the end, and a step onto
        // it is refused rather than clamped: only the SLIDE-OUT parks on that head, because only
        // the slide-out carries the end with it.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 1}, 1, 12));
        const auto plan = planMoveSelection(
            repicked, tempo_map, {}, second, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped exactly onto the ring's end")
    {
        // KIND IS NOT THE MOVE VERB'S TO CHANGE. The end is a bound like the onset below it: a
        // point stepped onto it would BECOME the slide-out, which the burst could not then drag —
        // the replay reads slide-out-ness off the pre-gesture chart, where the point is still
        // interior — so the step is refused and the point stays where it is.
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, second, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("stepped to the last offset strictly below the end")
    {
        // The bound is EXCLUSIVE and nothing else: the step before it lands where it was aimed and
        // the ring is untouched, so the point is still a point.
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, second, common::core::Fraction{3, 16}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& stepped = plan->inserted.front();
            CHECK(stepped.sustain == common::core::Fraction{4});
            REQUIRE(stepped.keyframes.size() == 2);
            CHECK(stepped.keyframes.back().offset == common::core::Fraction{15, 4});
            CHECK(common::core::endStatedFretOrNull(stepped) == nullptr);
        }
    }
    SECTION("a point stating something else is refused onto the end the same way")
    {
        // The bound is about POSITION, so what the point STATES never enters it — which is exactly
        // what keeps the two losses the old step could inflict unreachable: a same-fret point
        // became a slide-out falling toward the fret the string already holds (a mark nothing
        // draws, dissolved by the gate, the visible point gone with it), and a point carrying a
        // vibrato was bared of it by the slide-out's fret-and-nothing-else law.
        common::core::Chart one_point;
        one_point.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note =
            makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4});
        common::core::Keyframe carried{
            .offset = common::core::Fraction{3}, .fret = 5, .bend = {}, .vibrato = {}
        };
        SECTION("a fret the path already holds")
        {
            carried.fret = 5;
        }
        SECTION("a fret and vibrato")
        {
            carried.fret = 7;
            carried.vibrato = common::core::VibratoState::Wide;
        }
        note.keyframes = {carried};
        one_point.notes = {std::move(note)};
        const auto plan = planMoveSelection(
            one_point, tempo_map, {}, second, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
        // A refused plan is not applied, so the point keeps every statement it made.
        REQUIRE(one_point.notes.size() == 1);
        REQUIRE(one_point.notes.front().keyframes.size() == 1);
        CHECK(one_point.notes.front().keyframes.front() == carried);
    }
    SECTION("stepped inside the margin before a later onset of its own string stands there")
    {
        // The bound is the ring's end itself, never proximity to the head: a charter who steps a
        // keyframe to an eighth before the next head gets exactly that, and the ring below still
        // reaches the head.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 1}, 1, 12));
        const auto plan = planMoveSelection(
            repicked, tempo_map, {}, second, common::core::Fraction{7, 32}, 0, "Move Keyframe");
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

// The slide-out is the ring's end, so stepping it steps the end: the move verb is the slide-out's
// own handle — outward the slide-out lengthens, inward it shortens — while the duration verb, which
// moves the ribbon and never a point, leaves a slide-out behind a lengthening ring. A slide-out
// cannot be stepped onto the last sounded fret: a slide-out needs a leg of its own, and the order
// refusal is what says so.
TEST_CASE("planMoveSelection drags the ring's end with its slide-out", "[core][chart]")
{
    // The glide chart's fret-12 statement sits exactly at its four-beat end: the slide-out.
    const common::core::Chart chart = makeGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartKeyframeKey> slide_out{keyframeKeyAt(
        glideOnset(), 1, common::core::Fraction{4})};
    REQUIRE(common::core::endStatedFretOrNull(chart.notes.front()) != nullptr);

    SECTION("outward lengthens the slide-out")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, slide_out, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& moved = plan->inserted.front();
            CHECK(moved.sustain == common::core::Fraction{5});
            const int* const slides_out_toward = common::core::endStatedFretOrNull(moved);
            REQUIRE(slides_out_toward != nullptr);
            if (slides_out_toward != nullptr)
            {
                CHECK(*slides_out_toward == 12);
            }
            // The junction before it did not move.
            REQUIRE(moved.keyframes.size() == 2);
            CHECK(moved.keyframes.front().offset == common::core::Fraction{2});
        }
    }
    SECTION("inward shortens the slide-out")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, slide_out, common::core::Fraction{-1, 4}, 0, "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& moved = plan->inserted.front();
            CHECK(moved.sustain == common::core::Fraction{3});
            CHECK(common::core::endStatedFretOrNull(moved) != nullptr);
        }
    }
    SECTION("outward parks on the next head on its string")
    {
        // ONE ANSWER FOR A RING'S END REACHING THE NEXT HEAD, shared with the duration verb: the
        // step onto that head lands there, and a step PAST it clamps onto it rather than refusing.
        // The slide-out IS the ring's end, so the ring ends on the head with its exit completing
        // there, which is what the store says the hands did; the spacing the mark needs to be seen
        // is presentation's. The head is five beats out, one past the four-beat slide-out.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 2}, 1, 3));
        const auto parked = [&](const common::core::Fraction step) {
            const auto plan =
                planMoveSelection(repicked, tempo_map, {}, slide_out, step, 0, "Move Keyframe");
            REQUIRE(plan.has_value());
            if (!plan.has_value())
            {
                return;
            }
            const common::core::ChartNote* const glide = noteAt(plan->inserted, glideOnset(), 1);
            REQUIRE(glide != nullptr);
            if (glide != nullptr)
            {
                CHECK(glide->sustain == common::core::Fraction{5});
                REQUIRE(glide->keyframes.size() == 2);
                CHECK(glide->keyframes.back().offset == common::core::Fraction{5});
                const int* const slides_out_toward = common::core::endStatedFretOrNull(*glide);
                REQUIRE(slides_out_toward != nullptr);
                if (slides_out_toward != nullptr)
                {
                    CHECK(*slides_out_toward == 12);
                }
            }
            // The gate accepts a ring ending exactly on the next head, adjacency being legal.
            common::core::Chart applied = repicked;
            applyAndValidate(applied, tempo_map, *plan);
        };
        // Exactly onto the head, then past it: one plan, because the clamp IS where the step lands.
        parked(common::core::Fraction{1, 4});
        parked(common::core::Fraction{1, 2});
        // A FURTHER press in the same direction is HELD, not NoChange: the plan is diffed against
        // the PRE-GESTURE chart, which still holds the four-beat ring, so the replay describes the
        // same edit the previous press did — an identical entry replacing itself, nothing visible
        // moving. NoChange is the other case, a FIRST press on a slide-out already parked there.
        parked(common::core::Fraction{3, 4});
        const auto past = planMoveSelection(
            repicked, tempo_map, {}, slide_out, common::core::Fraction{1, 2}, 0, "Move Keyframe");
        const auto further = planMoveSelection(
            repicked, tempo_map, {}, slide_out, common::core::Fraction{3, 4}, 0, "Move Keyframe");
        REQUIRE(past.has_value());
        REQUIRE(further.has_value());
        if (past.has_value() && further.has_value())
        {
            CHECK(past->inserted == further->inserted);
            CHECK(past->removed == further->removed);
        }
        // A shorter step still lands where it was aimed, close to the head included.
        const auto inside = planMoveSelection(
            repicked, tempo_map, {}, slide_out, common::core::Fraction{7, 32}, 0, "Move Keyframe");
        REQUIRE(inside.has_value());
        if (inside.has_value())
        {
            const auto glide = std::ranges::find(
                inside->inserted, glideOnset(), &common::core::ChartNote::position);
            REQUIRE(glide != inside->inserted.end());
            CHECK(glide->sustain == common::core::Fraction{39, 8});
            CHECK(common::core::endStatedFretOrNull(*glide) != nullptr);
        }
    }
    SECTION("a slide-out already on the head answers NoChange")
    {
        // A FIRST press whose whole step the clamp eats describes no edit at all, so it arms no
        // gesture and the next press in the other direction starts from the current ring rather
        // than paying back a step that moved nothing.
        common::core::Chart parked;
        parked.tuning.strings = chart.tuning.strings;
        common::core::ChartNote glide = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{5});
        glide.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 9},
            common::core::Keyframe{.offset = common::core::Fraction{5}, .fret = 12},
        };
        parked.notes = {std::move(glide), makeTestNote({.measure = 3, .beat = 2}, 1, 3)};
        const std::vector<ChartKeyframeKey> at_head{keyframeKeyAt(
            glideOnset(), 1, common::core::Fraction{5})};
        const auto plan = planMoveSelection(
            parked, tempo_map, {}, at_head, common::core::Fraction{1, 4}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
        // And stepping BACK moves again: the ring shortens with the slide-out, which never had a
        // ceiling in that direction.
        const auto back = planMoveSelection(
            parked, tempo_map, {}, at_head, common::core::Fraction{-1, 4}, 0, "Move Keyframe");
        REQUIRE(back.has_value());
        if (back.has_value())
        {
            const common::core::ChartNote* const shortened =
                noteAt(back->inserted, glideOnset(), 1);
            REQUIRE(shortened != nullptr);
            if (shortened != nullptr)
            {
                CHECK(shortened->sustain == common::core::Fraction{4});
            }
        }
    }
    SECTION("an interior point stranded past the clamped end is refused")
    {
        // The clamp lands the END, never an interior point: a junction stepped onto or past the
        // head its own ring stops at has nowhere legal to stand, and clamping it would stack it on
        // the slide-out, so it is refused rather than clamped. The ring already ends ON the head
        // here, so the slide-out's own step is a no-op and only the junction is asking to move.
        common::core::Chart parked;
        parked.tuning.strings = chart.tuning.strings;
        common::core::ChartNote glide = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{5});
        glide.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{9, 2}, .fret = 9},
            common::core::Keyframe{.offset = common::core::Fraction{5}, .fret = 12},
        };
        parked.notes = {std::move(glide), makeTestNote({.measure = 3, .beat = 2}, 1, 3)};
        const std::vector<ChartKeyframeKey> both{
            keyframeKeyAt(glideOnset(), 1, common::core::Fraction{9, 2}),
            keyframeKeyAt(glideOnset(), 1, common::core::Fraction{5}),
        };
        const auto plan = planMoveSelection(
            parked, tempo_map, {}, both, common::core::Fraction{1, 4}, 0, "Move Selection");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("a note moved back onto the slide-out leaves the ring alone")
    {
        // The note's head lands exactly where the slide-out stood, and that is legal: the ring ends
        // on the new head with its exit completing there, which is what the store says the hands
        // did. So the glide is not in the plan at all — nothing about it changed — exactly as when
        // the step stops short of it.
        common::core::Chart repicked = chart;
        repicked.notes.push_back(makeTestNote({.measure = 3, .beat = 2}, 1, 3));
        const std::vector<ChartSlotKey> landing{keyAt({.measure = 3, .beat = 2}, 1)};
        for (const common::core::Fraction step :
             {common::core::Fraction{-1, 4}, common::core::Fraction{-1, 8}})
        {
            const auto plan =
                planMoveSelection(repicked, tempo_map, landing, {}, step, 0, "Move Note");
            REQUIRE(plan.has_value());
            if (plan.has_value())
            {
                CHECK(
                    std::ranges::find(
                        plan->inserted, glideOnset(), &common::core::ChartNote::position) ==
                    plan->inserted.end());
            }
        }
    }
    SECTION("onto the last sounded fret is refused")
    {
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, slide_out, common::core::Fraction{-1, 2}, 0, "Move Keyframe");
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
    SECTION("the figure selected whole keeps its shape")
    {
        // The end bound every interior point obeys is the end AFTER the step, because one uniform
        // delta moves the slide-out too: a charter who selects the junction and the slide-out
        // together slides the whole slide-out outward, where a bound read off the ring's OLD end
        // would have refused a step that changes nothing about the figure.
        const std::vector<ChartKeyframeKey> both{
            keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2}),
            keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4}),
        };
        const auto plan = planMoveSelection(
            chart, tempo_map, {}, both, common::core::Fraction{1, 4}, 0, "Move Selection");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            REQUIRE(plan->inserted.size() == 1);
            const common::core::ChartNote& moved = plan->inserted.front();
            CHECK(moved.sustain == common::core::Fraction{5});
            REQUIRE(moved.keyframes.size() == 2);
            CHECK(moved.keyframes.front().offset == common::core::Fraction{3});
            CHECK(moved.keyframes.back().offset == common::core::Fraction{5});
            CHECK(common::core::endStatedFretOrNull(moved) != nullptr);
        }
    }
}

// A MOVE MAY SHORTEN A RING, BUT NEVER DELETE A STATEMENT. A note moved back onto an earlier
// note's tail re-strikes it, so the gate truncates that ring at the landing and rides its slide-out
// back with the end — both the move's to do, a ring's length and the slide-out it goes out on being
// exactly what this verb changes. What the clip would ALSO do is drop every other keyframe past
// the landing, or overwrite a value standing on it — a statement the charter wrote, lost on a note
// they never touched — so a landing that would is refused whole. The
// refusal is the plan gate's (finalizePlan), not the move verb's own, so it holds in both
// directions: an unmoved ring a landing shortens, and a moved ring that now runs through an unmoved
// head.
TEST_CASE("planMoveSelection refuses a landing that would erase a statement", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    // The mover stands five beats past the tail's onset, clear of its four-beat ring, and steps
    // back a half note — landing three beats in, where that ring is still sounding.
    constexpr common::core::GridPosition tail_onset{.measure = 2, .beat = 1, .offset = {}};
    constexpr common::core::GridPosition mover_slot{.measure = 3, .beat = 2, .offset = {}};
    const std::vector<ChartSlotKey> mover{keyAt(mover_slot, 1)};
    constexpr common::core::Fraction step_back{-1, 2};

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
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }

    SECTION("a slide-out past the landing rides back onto the landing itself")
    {
        // The slide-out IS the ring's end, so it moves because the end did — no statement is lost —
        // and it lands exactly on the new head, three beats out, which is where the store says the
        // slide-out completes. Nothing spaces it: the drawn tail is presentation's to place.
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
                CHECK(tail->sustain == common::core::Fraction{3});
                REQUIRE(tail->keyframes.size() == 1);
                CHECK(tail->keyframes.front().offset == common::core::Fraction{3});
                const int* const slides_out_toward = common::core::endStatedFretOrNull(*tail);
                REQUIRE(slides_out_toward != nullptr);
                if (slides_out_toward != nullptr)
                {
                    CHECK(*slides_out_toward == 3);
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

    SECTION("a statement exactly on the landing stands there, not erased")
    {
        // The clip's bound is inclusive, so it survives — and standing on the new head is legal, so
        // it stays: the truncated ring ends on the landing with the statement, now its slide-out,
        // sliding out toward fret 9 there.
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
                CHECK(tail->sustain == common::core::Fraction{3});
                REQUIRE(tail->keyframes.size() == 1);
                CHECK(tail->keyframes.front().offset == common::core::Fraction{3});
                CHECK(tail->keyframes.front().fret == 9);
            }
        }
    }

    SECTION("a same-fret statement on the landing stands there too")
    {
        // The same landing on a point that says nothing YET: becoming the end's own statement, it
        // slides out toward the fret the string already holds — which draws its chip like any
        // silent point's mark, so the edit leaves it standing and the note's own focus decides how
        // long it lives. A statement is never deleted by a verb that was handed a ring's length.
        const common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{3}, .fret = 7, .bend = {}, .vibrato = {}
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
                CHECK(tail->keyframes.front().offset == common::core::Fraction{3});
                CHECK(tail->keyframes.front().fret == 7);
            }
        }
    }

    SECTION("a slide-out riding onto a point stating another fret refuses the move")
    {
        // The point three beats in states fret 9 and the slide-out at the ring's end goes toward 3.
        // The landing pulls the end back onto the point, and the slide-out arriving there would
        // overwrite the 9 with its 3 (overlayKeyframe) — as much a lost statement as an erased one.
        common::core::Chart chart = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{3}, .fret = 9, .bend = {}, .vibrato = {}
            });
        common::core::setSlideOut(chart.notes.front(), 3);
        const auto refused = moveNotes(chart, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE_FALSE(refused.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(refused.error()));

        // The same landing onto a point already stating the slide-out's fret overwrites nothing:
        // the two statements of one instant say the same, and the shortened tail keeps one of them.
        common::core::Chart restated = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{3}, .fret = 3, .bend = {}, .vibrato = {}
            });
        common::core::setSlideOut(restated.notes.front(), 3);
        const auto plan = moveNotes(restated, tempo_map, mover, step_back, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const tail = shortened(*plan);
            REQUIRE(tail != nullptr);
            if (tail != nullptr)
            {
                CHECK(tail->sustain == common::core::Fraction{3});
                REQUIRE(tail->keyframes.size() == 1);
                CHECK(tail->keyframes.front().offset == common::core::Fraction{3});
                CHECK(tail->keyframes.front().fret == 3);
            }
        }
    }

    SECTION("a moved ring running through an unmoved head refuses to erase its own point")
    {
        // The other direction: the RINGING note moves, forward a half note to three beats before
        // the head that stays put, so its own four-beat ring now runs through that head and is
        // truncated there. A point past the head would be erased from the note being moved.
        const std::vector<ChartSlotKey> ringing{keyAt(tail_onset, 1)};
        constexpr common::core::Fraction step_forward{1, 2};
        constexpr common::core::GridPosition moved_onset{.measure = 2, .beat = 3, .offset = {}};
        const common::core::Chart erasing = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{7, 2}, .fret = {}, .bend = 1.0, .vibrato = {}
            });
        const auto refused = moveNotes(erasing, tempo_map, ringing, step_forward, 0, "Move Note");
        REQUIRE_FALSE(refused.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(refused.error()));

        // With nothing standing past the head, the moved ring simply ends on it.
        const common::core::Chart clear = figure(
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .fret = 9, .bend = {}, .vibrato = {}
            });
        const auto plan = moveNotes(clear, tempo_map, ringing, step_forward, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            const common::core::ChartNote* const moved = noteAt(plan->inserted, moved_onset, 1);
            REQUIRE(moved != nullptr);
            if (moved != nullptr)
            {
                CHECK(moved->sustain == common::core::Fraction{3});
                REQUIRE(moved->keyframes.size() == 1);
                CHECK(moved->keyframes.front().offset == common::core::Fraction{1});
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
        common::core::Fraction{1, 4},
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
}

// Set-exact mode assigns the typed fret to every note in the snapshot.
TEST_CASE("planRetypeFrets sets an exact fret on every note", "[core][chart]")
{
    const common::core::Chart chart = makeTestChart();
    const std::vector<common::core::ChartNote> base{chart.notes[0], chart.notes[1]};

    const auto plan = retypeNotes(chart, makeTempoMap(), base, ChartFretSet{.fret = 9});
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
    const auto plan = retypeNotes(chart, makeTempoMap(), base, ChartFretShift{.delta = 2});
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
        chart, makeTempoMap(), base, ChartFretShift{.delta = common::core::g_max_fret - 3});
    REQUIRE_FALSE(plan.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
}

// An empty snapshot has nothing to write, so no plan is produced — and that emptiness is a
// NoChange, not a refusal: there was nothing to edit, so nothing was disallowed.
TEST_CASE("planRetypeFrets reports NoChange for an empty snapshot", "[core][chart]")
{
    const auto plan = retypeNotes(makeTestChart(), makeTempoMap(), {}, ChartFretShift{.delta = 2});
    REQUIRE_FALSE(plan.has_value());
    CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
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

    const auto plan = retypeNotes(chart, makeTempoMap(), base, ChartFretSet{.fret = 5});
    REQUIRE_FALSE(plan.has_value());
    CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
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
            ChartFretSet{.fret = 10});
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
            ChartFretShift{.delta = 2});
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
            ChartFretShift{.delta = 1});
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
            ChartFretSet{.fret = 0});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
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
            ChartFretSet{.fret = 4});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
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
            ChartFretShift{.delta = common::core::g_max_fret - 9});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(unchanged.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(closed.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(closed.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(no_keys.error()));

    const auto no_steps = planAdjustSustain(chart, tempo_map, chart.notes, keys, {});
    REQUIRE_FALSE(no_steps.has_value());
    CHECK(std::holds_alternative<ChartPlanNoChange>(no_steps.error()));
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
    CHECK(std::holds_alternative<ChartPlanNoChange>(closed.error()));
}

// A ring the replay would empty holds the value it CURRENTLY has — read from the live chart, not
// from the gesture's start. Holding the start value instead would grow the note back on a shrink
// press. A step into the floor therefore moves NOTHING, and the planner says so the only way a pure
// function of the step list can: it answers the plan it answered last time. DROPPING that press so
// the run banks no overshoot is the shared gesture authority's (`commitChartGestureStep` compares
// the replay against the plan its entry already holds), and the controller-level twin in
// `test_chart_sustain_gesture.cpp` is where that is asserted — so nothing here pops a step.
TEST_CASE("planAdjustSustain holds an emptied ring and repeats its plan", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{3})};
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};
    const std::vector<common::core::ChartNote> base = chart.notes;

    common::core::Chart live = chart;
    std::vector<ChartSustainStep> steps;
    // A press as the caller makes it: the step is appended, the whole run replays over `base`, and
    // the live chart is walked to what the plan describes. The plan comes back so a caller can ask
    // whether the press moved anything.
    const auto press = [&](bool grow) {
        steps.push_back(gridStep(g_quarter_grid, grow));
        std::expected<ChartEditPlan, ChartPlanRefusal> plan =
            planAdjustSustain(live, tempo_map, base, keys, steps);
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            live.notes = base;
            REQUIRE(applyChartChange(live, *plan).has_value());
        }
        return plan;
    };
    // Assertion-free (read inside CHECK expressions): a missing note reads as a zero ring, which
    // no step below expects, so the caller's own comparison fails.
    const auto ring = [&] {
        const common::core::ChartNote* const note =
            noteAt(live.notes, {.measure = 2, .beat = 1}, 1);
        return note != nullptr ? note->sustain : common::core::Fraction{};
    };
    // Whether two presses describe the same edit, which is the question the gesture authority asks.
    // The label is left out of it: it names the run's NET direction, which a run standing still has
    // no new value for.
    const auto describes_the_same = [](const std::expected<ChartEditPlan, ChartPlanRefusal>& lhs,
                                       const std::expected<ChartEditPlan, ChartPlanRefusal>& rhs) {
        return lhs.has_value() && rhs.has_value() && lhs->removed == rhs->removed &&
               lhs->inserted == rhs->inserted;
    };

    static_cast<void>(press(/*grow=*/false));
    CHECK(ring() == common::core::Fraction{2});
    const auto second = press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    // The next line IS the onset, so the ring holds where the previous step left it rather than
    // being clamped to some invented floor or restored to its three-beat start — and the plan is
    // the one before it, to the note. It stays that way for every further press into the floor.
    const auto floored = press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    CHECK(describes_the_same(floored, second));
    const auto still_floored = press(/*grow=*/false);
    CHECK(ring() == common::core::Fraction{1});
    CHECK(describes_the_same(still_floored, second));

    // The run the caller is left holding, having dropped both of those presses: two shrinks. The
    // first grow off it is the first step back, with no unseen overshoot to pay.
    std::vector<ChartSustainStep> recorded{
        gridStep(g_quarter_grid, false),
        gridStep(g_quarter_grid, false),
        gridStep(g_quarter_grid, true)
    };
    const auto regrown = planAdjustSustain(live, tempo_map, base, keys, recorded);
    REQUIRE(regrown.has_value());
    if (regrown.has_value())
    {
        live.notes = base;
        REQUIRE(applyChartChange(live, *regrown).has_value());
        CHECK(ring() == common::core::Fraction{2});
    }
    // A run that replays back to its start describes nothing: NoChange is what tells the caller to
    // take the gesture's entry back out and walk the chart to `base`.
    recorded.push_back(gridStep(g_quarter_grid, true));
    const auto closed = planAdjustSustain(live, tempo_map, base, keys, recorded);
    REQUIRE_FALSE(closed.has_value());
    CHECK(std::holds_alternative<ChartPlanNoChange>(closed.error()));
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
        CHECK(scrape->sustain == g_minimum_slide_window);
        // The turnaround is clipped away with the tail, and the terminal rides the shortening
        // ring — it is a keyframe at the end now, so the floored scrape keeps exactly one.
        REQUIRE(scrape->keyframes.size() == 1);
        if (scrape->keyframes.size() == 1)
        {
            CHECK(scrape->keyframes[0].offset == g_minimum_slide_window);
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
        const int* const terminal = common::core::endStatedFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == 12);
        }
    }
}

TEST_CASE("planAdjustSustain returns a grown slide-out to an unpitched slide", "[core][chart]")
{
    common::core::Chart chart = makeSteppedGlideChart();
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt(glideOnset(), 1)};

    // First gesture: shrink the pitched glide onto its last fret statement. That statement becomes
    // the slide-out, so the note's tail is now unpitched.
    const auto slide_out_plan =
        planAdjustSustain(chart, tempo_map, chart.notes, keys, {gridStep(g_quarter_grid, false)});
    REQUIRE(slide_out_plan.has_value());
    applyAndValidate(chart, tempo_map, *slide_out_plan);
    const common::core::ChartNote* slid_out = noteAt(chart.notes, glideOnset(), 1);
    REQUIRE(slid_out != nullptr);
    CHECK(slid_out->sustain == common::core::Fraction{3});
    REQUIRE(common::core::endStatedFretOrNull(*slid_out) != nullptr);

    const std::vector<common::core::ChartNote> base = chart.notes;

    // Second gesture, first step: growing past the slide-out turns it back into an ordinary pitched
    // keyframe because the keyframe stays put while the ring moves on.
    const auto grown_plan =
        planAdjustSustain(chart, tempo_map, base, keys, {gridStep(g_quarter_grid, true)});
    REQUIRE(grown_plan.has_value());
    applyAndValidate(chart, tempo_map, *grown_plan);
    const common::core::ChartNote* grown = noteAt(chart.notes, glideOnset(), 1);
    REQUIRE(grown != nullptr);
    CHECK(grown->sustain == common::core::Fraction{4});
    CHECK(common::core::endStatedFretOrNull(*grown) == nullptr);
    REQUIRE(grown->keyframes.size() == 2);
    CHECK(grown->keyframes.back().offset == common::core::Fraction{3});
    CHECK(grown->keyframes.back().fret == 9);

    // Same second gesture, opposite step: the replay has returned to the gesture's start. That
    // must be reported as NoChange so the controller retires the grow entry and restores the
    // slide-out, instead of refusing the visible shrink and leaving the grown tail stuck.
    const auto returned = planAdjustSustain(
        chart,
        tempo_map,
        base,
        keys,
        {gridStep(g_quarter_grid, true), gridStep(g_quarter_grid, false)});
    REQUIRE_FALSE(returned.has_value());
    CHECK(std::holds_alternative<ChartPlanNoChange>(returned.error()));
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        CHECK(scrape->attack == common::core::NoteAttack::PickSlide);
        CHECK(scrape->fret == 7);
        // Fret 7 sits in the neck's lower half, so the default travels upward to the high end.
        // The synthesized path is the terminal alone: one keyframe, the slide-out at the ring's
        // end.
        CHECK(scrape->keyframes.size() == 1);
        const int* const terminal = common::core::endStatedFretOrNull(*scrape);
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pinch, "Pinch Harmonic")
            .plan;
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* stub = noteAt(plan->inserted, {.measure = 2, .beat = 1}, 1);
        REQUIRE(stub != nullptr);
        CHECK(stub->sustain == common::core::Fraction{1});
        CHECK(stub->sustain > g_minimum_slide_window);
        // The terminal ends the ring by definition, so the sustain above IS the gesture's
        // length: a scrape rings no longer than it travels.
        CHECK(common::core::endStatedFretOrNull(*stub) != nullptr);

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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        const int* const terminal = common::core::endStatedFretOrNull(*scrape);
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const common::core::ChartNote* scrape =
            noteAt(plan->inserted, {.measure = 3, .beat = 1}, 1);
        REQUIRE(scrape != nullptr);
        const int* const terminal = common::core::endStatedFretOrNull(*scrape);
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    REQUIRE(applyChartChange(chart, *enter).has_value());

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide")
            .plan;
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

    // A note already at the attack is a no-op, never a refusal: nothing is named for the flash.
    const ChartSelectionPlan unchanged =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide");
    REQUIRE_FALSE(unchanged.plan.has_value());
    if (!unchanged.plan.has_value())
    {
        CHECK(std::holds_alternative<ChartPlanNoChange>(unchanged.plan.error()));
    }
    CHECK(unchanged.refused.empty());
    CHECK_FALSE(planSetAttack(chart, tempo_map, {}, common::core::NoteAttack::Pick, "Pick")
                    .plan.has_value());
}

// The three boolean techniques share ONE planner, and the reason that is safe rather than merely
// tidy is that eligibility is asked of the per-note rule authority instead of being restated. Each
// flag therefore inherits its own rules for free, and they are different rules: a pinch harmonic
// cannot be deadened (a damped string cannot squeal, so the normalizer would take the pinch with
// the deadening) while a dead note cannot take vibrato (it modulates a pitch the note does not
// have).
// Vibrato has its own planner now that the field is a width axis, so the second half below asks
// THAT verb the same question: one law, two different answers - and neither is written in this
// file or in either verb.
TEST_CASE("planSetNoteFlag inherits each flag's own per-note rule", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::TempoMap tempo_map = makeTempoMap();

    // A pinch harmonic and a plain note, selected together. (The tap harmonic, whose tremolo
    // exclusion this half used to probe, is a disabled form; the pinch's deadening rule — a damped
    // string cannot squeal — is the same kind of per-note refusal on an enabled one.)
    chart.notes[0].attack = common::core::NoteAttack::Pinch;
    chart.notes[0].harmonic_node = 17.0;
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1),
        keyAt({.measure = 2, .beat = 1}, 2),
    };

    const ChartSelectionPlan deadened =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead");
    // The pinch is named as the one note refused, with the rule's own words for the log.
    REQUIRE(deadened.refused.size() == 1);
    CHECK(deadened.refused[0].note == keys[0]);
    CHECK_FALSE(deadened.refused[0].reason.empty());
    const auto& dead = deadened.plan;
    REQUIRE(dead.has_value());
    if (dead.has_value())
    {
        // Only the plain note takes it; the pinch is skipped rather than the edit refused.
        CHECK(dead->inserted.size() == 1);
        CHECK(std::ranges::all_of(dead->inserted, [](const common::core::ChartNote& note) {
            return note.dead && !note.harmonic_node.has_value();
        }));
        applyAndValidate(chart, tempo_map, *dead);
    }

    // Vibrato is refused by a different rule on a different note, and its verb needs no knowledge
    // of either: a dead note sounds no pitch to modulate. Asked of planSetVibrato because the
    // width axis is not one of the bools above, and the point of the test is that BOTH planners
    // read the same authority rather than restating it.
    common::core::Chart dead_chart = makeTestChart();
    dead_chart.notes[0].dead = true;
    const auto vibrato =
        planSetVibrato(
            dead_chart, tempo_map, keys, {}, common::core::VibratoState::Narrow, "Vibrato")
            .plan;
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
        planSetEmphasis(chart, tempo_map, keys, common::core::NoteEmphasis::Ghost, "Ghost Note")
            .plan;
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
        planSetEmphasis(chart, tempo_map, keys, common::core::NoteEmphasis::Accent, "Accent").plan;
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
            .plan.has_value());
    CHECK_FALSE(
        planSetEmphasis(chart, tempo_map, {}, common::core::NoteEmphasis::Ghost, "Ghost Note")
            .plan.has_value());
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
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute").plan;
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
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note").plan;
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
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, false, "Remove Palm Mute")
            .plan;
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
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note").plan;
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
    const std::vector<common::core::Fraction> ink_end =
        common::core::chartPresentation(
            common::core::chartConnections(chart.notes, tempo_map), tempo_map)
            .ink_end;
    REQUIRE(ink_end.size() == chart.notes.size());
    CHECK(ink_end[held] == common::core::Fraction{});
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
    // Measure 3 / string 1 becomes a natural harmonic: the node over a pressed stop would be the
    // disabled artificial form.
    chart.notes[2].fret = 0;
    chart.notes[2].harmonic_node = 12.0;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{
        keyAt({.measure = 2, .beat = 1}, 1),
        keyAt({.measure = 2, .beat = 1}, 2),
        keyAt({.measure = 3, .beat = 1}, 1),
    };

    const auto dead =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note").plan;
    REQUIRE(dead.has_value());
    if (!dead.has_value())
    {
        return;
    }
    // The vibrato note is skipped; the plain note AND the harmonic both take the X.
    REQUIRE(dead->inserted.size() == 2);
    CHECK(std::ranges::none_of(dead->inserted, [](const common::core::ChartNote& note) {
        return common::core::hasVibrato(note.vibrato);
    }));
    CHECK(std::ranges::all_of(dead->inserted, [](const common::core::ChartNote& note) {
        return note.dead;
    }));
    CHECK(std::ranges::any_of(dead->inserted, [](const common::core::ChartNote& note) {
        return note.harmonic_node.has_value();
    }));
    applyAndValidate(chart, tempo_map, *dead);

    const auto palm =
        planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute").plan;
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
                    .plan.has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, true, "Dead Note")
                    .plan.has_value());
}

// Keyed notes already carrying the mute plan nothing, and neither does an empty key set.
TEST_CASE("planSetNoteFlag returns nullopt when nothing changes", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[0].palm_mute = true;
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::PalmMute, true, "Palm Mute")
                    .plan.has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, keys, ChartNoteFlag::Dead, false, "Remove Dead")
                    .plan.has_value());
    CHECK_FALSE(planSetNoteFlag(chart, tempo_map, {}, ChartNoteFlag::PalmMute, true, "Palm Mute")
                    .plan.has_value());
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
            // The whole path at once, terminal included: the slide-out is the keyframe at the
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
                ChartFretShift{.delta = 11 - chart.notes.front().fret}),
            11);
    }
    SECTION("scrape: set-exact assigns the start only")
    {
        chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
        check_path_kept(
            chart, retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretSet{.fret = 11}), 11);
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
                ChartFretShift{.delta = 24 - chart.notes.front().fret}),
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
            chart, retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretShift{.delta = 3}), 8);
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
            chart, retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretSet{.fret = 9}), 9);
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

    const auto exact = retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretSet{.fret = 3});
    REQUIRE_FALSE(exact.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(exact.error()));
    const auto shifted = retypeNotes(
        chart, makeTempoMap(), chart.notes, ChartFretShift{.delta = 3 - chart.notes.front().fret});
    REQUIRE_FALSE(shifted.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(shifted.error()));
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

    const auto plan = retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretSet{.fret = 0});
    REQUIRE_FALSE(plan.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
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

    const auto plan = retypeNotes(chart, makeTempoMap(), chart.notes, ChartFretSet{.fret = 7});
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
            // The turnaround, then the terminal: the slide-out is the keyframe at the ring's end,
            // so compressing the ring moves it and leaves the count alone.
            REQUIRE(scrape->keyframes.size() == 2);
            CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
            CHECK(scrape->keyframes[0].fret == 3);
            const int* const terminal = common::core::endStatedFretOrNull(*scrape);
            REQUIRE(terminal != nullptr);
            if (terminal != nullptr)
            {
                CHECK(*terminal == 12);
            }
            common::core::Chart applied = chart;
            applyAndValidate(applied, tempo_map, *plan);
        }
    }

    SECTION("growth clamps at the next head on its string with its terminal on it")
    {
        // A scrape's growth is clamped by the ONE bound on a ring (40-Q2-B) like any other note's,
        // with no clause of its own: a step that reaches the next strike ends the gesture exactly
        // there and the terminal, which rides the end, lands on the head. That is what the store
        // says the pick did; the drawn chip is spaced before the head by presentation.
        common::core::Chart walled = chart;
        walled.notes.push_back(makeTestNote({.measure = 3, .beat = 2, .offset = {1, 2}}, 1, 5));
        std::ranges::sort(walled.notes, common::core::chartNoteOrderLess);
        const auto onto = planAdjustSustain(
            walled, tempo_map, walled.notes, keys, {gridStep(common::core::Fraction{1, 8}, true)});
        REQUIRE(onto.has_value());
        if (onto.has_value())
        {
            const common::core::ChartNote* walled_scrape =
                noteAt(onto->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(walled_scrape != nullptr);
            CHECK(walled_scrape->sustain == common::core::Fraction{3, 2});
            REQUIRE(walled_scrape->keyframes.size() == 2);
            CHECK(walled_scrape->keyframes.back().offset == common::core::Fraction{3, 2});
            CHECK(common::core::endStatedFretOrNull(*walled_scrape) != nullptr);
        }
        const auto inside = planAdjustSustain(
            walled, tempo_map, walled.notes, keys, {gridStep(g_sixteenth_grid, true)});
        REQUIRE(inside.has_value());
        if (inside.has_value())
        {
            const common::core::ChartNote* scrape =
                noteAt(inside->inserted, {.measure = 3, .beat = 1}, 1);
            REQUIRE(scrape != nullptr);
            CHECK(scrape->sustain == common::core::Fraction{5, 4});
            CHECK(common::core::endStatedFretOrNull(*scrape) != nullptr);
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
            const common::core::Keyframe* const slide_out = common::core::endFretStatement(*scrape);
            REQUIRE(slide_out != nullptr);
            if (slide_out != nullptr)
            {
                CHECK(slide_out->offset == common::core::Fraction{3, 2});
                CHECK(slide_out->fret == 12);
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
            CHECK(scrape->sustain == g_minimum_slide_window);
            // The turnaround no longer fits inside the floored window; the terminal alone rides,
            // and it is a keyframe of its own at the floored end.
            REQUIRE(scrape->keyframes.size() == 1);
            const common::core::Keyframe* const slide_out = common::core::endFretStatement(*scrape);
            REQUIRE(slide_out != nullptr);
            if (slide_out != nullptr)
            {
                CHECK(slide_out->offset == g_minimum_slide_window);
                CHECK(slide_out->fret == 12);
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
        const common::core::Keyframe* const slide_out = common::core::endFretStatement(*shrunk);
        REQUIRE(slide_out != nullptr);
        if (slide_out != nullptr)
        {
            CHECK(slide_out->offset == common::core::Fraction{1, 2});
            CHECK(slide_out->fret == 12);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }
}

// A note placed on a scrape's path re-strikes the string, so the scrape ends there like any ring
// (40-Q2-B) — and its terminal, which rides the end, lands on the new head, where the store says
// the pick stopped. One normalization for every producer; the spacing that keeps the chip reachable
// is presentation's, so the drawn gesture still ends one margin short. A scrape's turnaround is an
// authored statement, so a landing whose terminal would ride over it refuses: landing exactly ON
// the turnaround, the terminal's fret would overwrite the turnaround's (overlayKeyframe).
TEST_CASE("planInsertNote shortens a scrape under a note placed on its path", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // Turnaround at fret 3 half a beat in, terminal at fret 12 on the one-beat ring's end.
    chart.notes = {makeScrape({.measure = 1, .beat = 1}, 1)};
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 1, .beat = 1, .offset = {3, 4}}, 1, 5),
        g_fixture_sustain);
    REQUIRE(plan.has_value());
    if (plan.has_value())
    {
        const auto scrape = std::ranges::find(
            plan->inserted,
            common::core::GridPosition{.measure = 1, .beat = 1},
            &common::core::ChartNote::position);
        REQUIRE(scrape != plan->inserted.end());
        // Three quarters of a beat to the new head, exactly: the terminal rides onto it, past the
        // turnaround it still travels away from.
        CHECK(scrape->sustain == common::core::Fraction{3, 4});
        CHECK(common::core::isScrape(scrape->attack));
        REQUIRE(scrape->keyframes.size() == 2);
        CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
        CHECK(scrape->keyframes[0].fret == 3);
        CHECK(scrape->keyframes[1].offset == common::core::Fraction{3, 4});
        const int* const terminal = common::core::endStatedFretOrNull(*scrape);
        REQUIRE(terminal != nullptr);
        if (terminal != nullptr)
        {
            CHECK(*terminal == 12);
        }
        common::core::Chart applied = chart;
        applyAndValidate(applied, tempo_map, *plan);
    }

    const auto onto_turnaround = planInsertNote(
        chart,
        tempo_map,
        makeTestNote({.measure = 1, .beat = 1, .offset = {1, 2}}, 1, 5),
        g_fixture_sustain);
    REQUIRE_FALSE(onto_turnaround.has_value());
    CHECK(std::holds_alternative<ChartPlanInvalid>(onto_turnaround.error()));
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
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
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
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
    // statement MOVES to the ring's end, where it is the slide-out; the keyframe it left stays
    // as a bare leg boundary, silent here (nothing vibrated before it) and the commit law's to
    // sweep.
    REQUIRE(scrape->keyframes.size() == 2);
    CHECK(scrape->keyframes.front().offset == common::core::Fraction{1, 2});
    CHECK(common::core::keyframeStatesNothing(scrape->keyframes.front()));
    const int* const terminal = common::core::endStatedFretOrNull(*scrape);
    REQUIRE(terminal != nullptr);
    if (terminal != nullptr)
    {
        CHECK(*terminal == 9);
        CHECK(*terminal != g_pick_slide_default_high_fret);
    }

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide")
            .plan;
    REQUIRE(exit.has_value());
    if (!exit.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *exit);
    const common::core::ChartNote* restored = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(restored != nullptr);
    CHECK(restored->tremolo);
    // Only the silent bare boundary remains, and the sweep takes it.
    REQUIRE(restored->keyframes.size() == 1);
    CHECK(common::core::keyframeStatesNothing(restored->keyframes.front()));
    common::core::ChartNote swept = *restored;
    CHECK(common::core::stripSilentKeyframes(swept));
    CHECK(swept.keyframes.empty());
    CHECK(common::core::endStatedFretOrNull(*restored) == nullptr);
}

// The glide's fret leaves its instant for the scrape's terminal, but a vibrated note's keyframe
// there is also the vibrato's ending: the fret goes and the bare keyframe stays, so the round trip
// back to a pick still ends the vibrato where the charter ended it.
TEST_CASE("planSetAttack keeps a vibrato ending through the scrape round trip", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    chart.notes[2].vibrato = common::core::VibratoState::Narrow;
    chart.notes[2].keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 9}
    };
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 3, .beat = 1}, 1)};

    const auto enter =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::PickSlide, "Pick Slide")
            .plan;
    REQUIRE(enter.has_value());
    if (!enter.has_value())
    {
        return;
    }
    // The vibrato latent makes the in-memory chart deliberately dirty, as in the glide case above.
    REQUIRE(applyChartChange(chart, *enter).has_value());
    const common::core::ChartNote* scrape = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(scrape != nullptr);
    REQUIRE(scrape->keyframes.size() == 2);
    CHECK(scrape->keyframes[0].offset == common::core::Fraction{1, 2});
    CHECK(common::core::keyframeStatesNothing(scrape->keyframes[0]));
    const int* const terminal = common::core::endStatedFretOrNull(*scrape);
    REQUIRE(terminal != nullptr);
    if (terminal != nullptr)
    {
        CHECK(*terminal == 9);
    }

    const auto exit =
        planSetAttack(chart, tempo_map, keys, common::core::NoteAttack::Pick, "Remove Pick Slide")
            .plan;
    REQUIRE(exit.has_value());
    if (!exit.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *exit);
    const common::core::ChartNote* restored = noteAt(chart.notes, {.measure = 3, .beat = 1}, 1);
    REQUIRE(restored != nullptr);
    CHECK(restored->vibrato == common::core::VibratoState::Narrow);
    REQUIRE(restored->keyframes.size() == 1);
    CHECK(restored->keyframes[0].offset == common::core::Fraction{1, 2});
    CHECK(common::core::keyframeStatesNothing(restored->keyframes[0]));
    // Drawn, the region runs from the onset (4.0s) to the ending half a beat in (4.25s).
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 2);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].state == common::core::VibratoState::Narrow);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.25, 1e-9));
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
    const ChartSelectionPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
    CHECK(planned.refused.empty());
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

// The resolver is the only authority on eligibility, and the refusal channel names each note it
// refused, with that note's own reason, so an all-skipped press is never a dead key.
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "nothing earlier on the string to connect to");
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "no connection between the two stops");
    }

    SECTION("a released predecessor whose connection cannot be authored")
    {
        // The predecessor's ring stops short, and the one tail the assist may not spend is a
        // gesture's own authored window: a slide-out's exit is data the author placed, so the note
        // is skipped whole rather than having its gesture rewritten to buy a connection.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3, common::core::Fraction{1, 2}),
            makeTestNote({.measure = 1, .beat = 3}, 1, 7),
        };
        common::core::setSlideOut(chart.notes[0], 5);
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 3}, 1));
        CHECK(
            planned.refused[0].reason ==
            "the ring before it stops short and cannot be grown to reach it");
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "no connection between the two stops");
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
        const ChartSelectionPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        // Both refusals are listed, in the planner's walk order: the tap, then the scrape.
        REQUIRE(planned.refused.size() == 2);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(
            planned.refused[0].reason ==
            "the picking hand strikes it, so no connection describes it");
        CHECK(planned.refused[1].note == keyAt({.measure = 1, .beat = 3}, 2));
        CHECK(
            planned.refused[1].reason ==
            "the picking hand strikes it, so no connection describes it");
    }

    SECTION("two notes refused for different reasons each keep their own")
    {
        // The reason belongs to the NOTE, not to the press: a tap riding the selection and a note
        // first on its string are turned down for unrelated reasons, and both travel back intact.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 3, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 2}, 1, 7),
            // First on string 2, so nothing justifies a claim on it.
            makeTestNote({.measure = 1, .beat = 2}, 2, 4),
        };
        chart.notes[1].attack = common::core::NoteAttack::Tap;
        const std::vector<ChartSlotKey> keys{
            keyAt({.measure = 1, .beat = 2}, 1),
            keyAt({.measure = 1, .beat = 2}, 2),
        };
        const ChartSelectionPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 2);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(
            planned.refused[0].reason ==
            "the picking hand strikes it, so no connection describes it");
        CHECK(planned.refused[1].note == keyAt({.measure = 1, .beat = 2}, 2));
        CHECK(planned.refused[1].reason == "nothing earlier on the string to connect to");
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
        const ChartSelectionPlan planned =
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
        const ChartSelectionPlan planned =
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
        const ChartSelectionPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
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

    SECTION("a slide-out predecessor's tail is never reshaped")
    {
        // A slide-out's exit is authored gesture geometry, so the assist refuses to spend it even
        // though the connection itself would be legal (the resolver reads the FRET AT THE RING'S
        // END, so a pull off the last pitched stop resolves once the hold reaches). The hold IS the
        // only blocker here, which is exactly what the skip reason reports.
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {
            makeTestNote({.measure = 1, .beat = 1}, 1, 9, common::core::Fraction{1}),
            makeTestNote({.measure = 1, .beat = 3}, 1, 5),
        };
        common::core::setSlideOut(chart.notes[0], 12);
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 3}, 1));
        CHECK(
            planned.refused[0].reason ==
            "the ring before it stops short and cannot be grown to reach it");
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 3}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 3}, 1));
        CHECK(planned.refused[0].reason == "no connection between the two stops");
    }
}

// No node ever leaves with an `H` press: the claim stores no direction, so there is no attack
// whose meaning a node could contradict. The resolver's node clauses do the work instead, which is
// why a fret-hand harmonic skips ITSELF rather than needing a guard in the verb. (The case that
// CLAIMED and kept its node — a stopped harmonic hammered above its predecessor — is the disabled
// artificial form and cannot be exercised while it is; it returns with that form.)
TEST_CASE("planSetLegato leaves every harmonic node where it found it", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "no connection between the two stops");
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "no connection between the two stops");
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
    const ChartSelectionPlan planned = planSetLegato(chart, tempo_map, keys, "Legato");
    REQUIRE(planned.plan.has_value());
    if (planned.plan.has_value())
    {
        // Only the resolvable note changes; the other keeps its pick and stays out of the plan.
        REQUIRE(planned.plan->inserted.size() == 1);
        CHECK(planned.plan->inserted[0].string == 1);
        CHECK(planned.plan->inserted[0].attack == common::core::NoteAttack::Legato);
    }
    // The refusal names the note that kept its pick, not just that one did.
    REQUIRE(planned.refused.size() == 1);
    CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 2));
    CHECK(planned.refused[0].reason == "nothing earlier on the string to connect to");
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
        const ChartSelectionPlan planned =
            planSetLegato(chart, tempo_map, {keyAt({.measure = 1, .beat = 2}, 1)}, "Legato");
        CHECK_FALSE(planned.plan.has_value());
        REQUIRE(planned.refused.size() == 1);
        CHECK(planned.refused[0].note == keyAt({.measure = 1, .beat = 2}, 1));
        CHECK(planned.refused[0].reason == "nothing earlier on the string to connect to");
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
        const ChartSelectionPlan planned =
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
            const auto retyped =
                retypeNotes(chart, tempo_map, {chart.notes[0]}, ChartFretSet{.fret = 7});
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

            const auto stranded =
                retypeNotes(chart, tempo_map, {chart.notes[0]}, ChartFretSet{.fret = 0});
            REQUIRE(stranded.has_value());
            if (stranded.has_value())
            {
                REQUIRE(stranded->inserted.size() == 1);
                CHECK(stranded->inserted[0].fret == 0);
                CHECK(stranded->inserted[0].attack == common::core::NoteAttack::Pick);
                applyAndValidate(chart, tempo_map, *stranded);
            }

            // A node is the other thing a strike can land on, so the same retype leaves the attack
            // standing: the gate is `fret > 0 || node`, not `fret > 0`. Probed with the left-hand
            // tap alone: a node under the picking-hand tap is the disabled tapped harmonic, which
            // the plan gate refuses for that reason rather than for stranding.
            if (attack == common::core::NoteAttack::Tap)
            {
                continue;
            }
            common::core::Chart noded;
            noded.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            noded.notes = {makeTestNote({.measure = 1, .beat = 1}, 1, 5)};
            noded.notes[0].attack = attack;
            noded.notes[0].harmonic_node = 12.0;
            const auto kept =
                retypeNotes(noded, tempo_map, {noded.notes[0]}, ChartFretSet{.fret = 0});
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

// A fret entry is one plan against one array, so undoing it is the same primitive run backwards
// and a failed precondition leaves the chart untouched.
TEST_CASE("A retype applies and reverses atomically", "[core][chart]")
{
    common::core::Chart chart = makeTestChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = retypeNotes(chart, tempo_map, {chart.notes[0]}, ChartFretSet{.fret = 7});
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
    // A strike payload, to pin that the struck product inherits the origin's payloads.
    chart.notes[0].palm_mute = true;
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
    // The arrival stands AT the new head, at the origin's own ring end: the store holds what the
    // hands did, and the chart PROVES the statement is an arrival rather than a slide-out, naming
    // the very stop the new head is struck at, at the same instant. Presentation alone spaces the
    // DRAWN copy a margin early.
    CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
    CHECK(origin.keyframes[0].fret == 9);

    const common::core::ChartNote& product = chart.notes[1];
    CHECK(product.position == common::core::GridPosition{.measure = 2, .beat = 3, .offset = {}});
    CHECK(product.string == 1);
    // The remainder is the same note restarted: its fret is the junction's, its ring is what was
    // left, and its own later keyframe rides along rebased onto the new onset (4 - 2 = 2) — where
    // it lands on the product's own ring end, so the fixture's end statement stays a slide-out.
    CHECK(product.fret == 9);
    CHECK(product.sustain == common::core::Fraction{2});
    REQUIRE(product.keyframes.size() == 1);
    CHECK(product.keyframes[0].offset == common::core::Fraction{2});
    CHECK(product.keyframes[0].fret == 12);
    // A split head is STRUCK and stores `Pick` directly, keeping the origin's payloads: a junction
    // with no strike is the join's one longer ring, so the split authors the re-attack.
    CHECK(product.attack == common::core::NoteAttack::Pick);
    CHECK(product.palm_mute);

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
        // A mid-travel vibrato statement: a real keyframe that says nothing about where the hand
        // is.
        chart.notes[0].keyframes.insert(
            chart.notes[0].keyframes.begin(),
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .vibrato = common::core::VibratoState::Narrow
            });
        const common::core::Chart original = chart;
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
        CHECK(chart == original);
    }

    SECTION("a keyframe at the ring's end")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{4})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
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

        // A scrape is ONE gesture of the picking hand end to end, so it has no junction to sever:
        // the walk refuses it outright rather than shipping a product the charter never wrote (a
        // fretting-hand note with the scrape's remainder) that the join could never take back.
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }

    SECTION("a key naming no keyframe")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            splitAt(chart, tempo_map, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{3})});
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
    }
}

// An arrival that says nothing the origin's path does not already say is KEPT, where a silent
// SLIDE-OUT is not. The difference is the FACE: a slide-out toward the fret the string already
// holds draws nothing and wears no head, so the gate dissolves it, while an arrival wears a linked
// head at its stored instant and is ordinary visible authoring state. The join takes it straight
// back over.
TEST_CASE("planToggleJunctions keeps a silent arrival on the origin", "[core][chart]")
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
    REQUIRE(origin.keyframes.size() == 2);
    CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
    CHECK(origin.keyframes[0].fret == 9);
    // The silent one, standing at the cut: it restates the fret the path already holds, and is an
    // ARRIVAL because the new head is struck at that very fret.
    CHECK(origin.keyframes[1].offset == common::core::Fraction{3});
    CHECK(origin.keyframes[1].fret == 9);
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

// THE CUT: a note struck inside a ring divides it losslessly. The origin keeps its path to the
// cut, every keyframe past the cut rides the fresh head rebased onto its onset, and the end
// statement stays at the end of the whole ring — now the new head's end. One entry, reversed
// exactly.
TEST_CASE("planCutRing strikes a fresh head on the ring's remainder", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    // Three beats in: past the arrival at 9, a beat short of the slide-out at the ring's end.
    const auto plan =
        planCutRing(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{3}, 10);
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    CHECK(plan->label == "Cut Ring");
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 2);
    const common::core::ChartNote& origin = chart.notes[0];
    CHECK(origin.position == glideOnset());
    CHECK(origin.fret == 7);
    CHECK(origin.sustain == common::core::Fraction{3});
    REQUIRE(origin.keyframes.size() == 1);
    CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
    CHECK(origin.keyframes[0].fret == 9);

    const common::core::ChartNote& head = chart.notes[1];
    CHECK(head.position == common::core::GridPosition{.measure = 2, .beat = 4, .offset = {}});
    CHECK(head.string == 1);
    CHECK(head.fret == 10);
    CHECK(head.sustain == common::core::Fraction{1});
    CHECK(head.attack == common::core::NoteAttack::Pick);
    // The slide-out rides over, rebased (4 - 3 = 1): still at the end of the whole ring.
    REQUIRE(head.keyframes.size() == 1);
    CHECK(head.keyframes[0].offset == common::core::Fraction{1});
    CHECK(head.keyframes[0].fret == 12);

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// A statement standing exactly at the cut becomes the origin's END statement, and the chart then
// proves what it is against the new head: a slide-out onto it where the frets differ — a point
// that was visible becomes a zone keyframe — and an arrival where the head is struck at its stop.
TEST_CASE("planCutRing ends the origin on a statement standing at the cut", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a different fret slides out onto the new head")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan =
            planCutRing(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{2}, 5);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& origin = chart.notes[0];
        const common::core::ChartNote& head = chart.notes[1];
        CHECK(origin.sustain == common::core::Fraction{2});
        REQUIRE(origin.keyframes.size() == 1);
        CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
        CHECK(origin.keyframes[0].fret == 9);
        CHECK(head.fret == 5);
        CHECK(head.sustain == common::core::Fraction{2});
        REQUIRE(head.keyframes.size() == 1);
        CHECK(head.keyframes[0].offset == common::core::Fraction{2});
        CHECK(head.keyframes[0].fret == 12);

        const bool arrives = common::core::arrivesIntoNextHead(origin, head, tempo_map);
        CHECK_FALSE(arrives);
        const int* const slide_out = common::core::slideOutFretOrNull(origin, arrives);
        REQUIRE(slide_out != nullptr);
        if (slide_out != nullptr)
        {
            CHECK(*slide_out == 9);
        }
    }

    SECTION("the head's own fret arrives")
    {
        common::core::Chart chart = makeGlideChart();
        const auto plan =
            planCutRing(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{2}, 9);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& origin = chart.notes[0];
        REQUIRE(origin.keyframes.size() == 1);
        CHECK(origin.keyframes[0].offset == common::core::Fraction{2});
        CHECK(origin.keyframes[0].fret == 9);
        const bool arrives = common::core::arrivesIntoNextHead(origin, chart.notes[1], tempo_map);
        CHECK(arrives);
        CHECK(common::core::slideOutFretOrNull(origin, arrives) == nullptr);
    }
}

// The channel states in force at the cut open the head, so the sound does not change across it:
// a bend reached before the cut is the head's onset bend, vibrato begun before it the head's
// vibrato. Neither needs a keyframe of its own on the head.
TEST_CASE("planCutRing opens the head on the channel states in force", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote ringing =
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{4});
    ringing.keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{1}, .bend = 1.0},
        common::core::Keyframe{
            .offset = common::core::Fraction{2}, .vibrato = common::core::VibratoState::Narrow
        },
    };
    chart.notes = {std::move(ringing)};

    const auto plan = planCutRing(
        chart, tempo_map, keyAt({.measure = 2, .beat = 1}, 2), common::core::Fraction{3}, 7);
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].keyframes.size() == 2);
    const common::core::ChartNote& head = chart.notes[1];
    CHECK(head.fret == 7);
    CHECK_THAT(head.bend, Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK(head.vibrato == common::core::VibratoState::Narrow);
    CHECK(head.keyframes.empty());
}

// The head is STRUCK: none of the origin's strike facts ride over — attack, node, mute, dead,
// tremolo, emphasis — where the split's severed head keeps them all. The origin keeps
// its own.
TEST_CASE("planCutRing strikes the head with strike defaults", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const auto check_struck = [](const common::core::ChartNote& head) {
        CHECK(head.attack == common::core::NoteAttack::Pick);
        CHECK_FALSE(head.harmonic_node.has_value());
        CHECK_FALSE(head.palm_mute);
        CHECK_FALSE(head.dead);
        CHECK_FALSE(head.tremolo);
        CHECK(head.emphasis == common::core::NoteEmphasis::Normal);
    };

    SECTION("a tapped, muted, tremolo, accented ring")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote ringing =
            makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4});
        ringing.attack = common::core::NoteAttack::Tap;
        ringing.palm_mute = true;
        ringing.tremolo = true;
        ringing.emphasis = common::core::NoteEmphasis::Accent;
        chart.notes = {std::move(ringing)};

        const auto plan = planCutRing(
            chart, tempo_map, keyAt({.measure = 2, .beat = 1}, 1), common::core::Fraction{2}, 7);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& origin = chart.notes[0];
        CHECK(origin.attack == common::core::NoteAttack::Tap);
        CHECK(origin.palm_mute);
        CHECK(origin.tremolo);
        CHECK(origin.emphasis == common::core::NoteEmphasis::Accent);
        check_struck(chart.notes[1]);
    }

    SECTION("a natural harmonic's ring")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        // The open string touched at the twelfth fret.
        common::core::ChartNote ringing =
            makeTestNote({.measure = 2, .beat = 1}, 1, 0, common::core::Fraction{4});
        ringing.harmonic_node = 12.0;
        chart.notes = {std::move(ringing)};

        const auto plan = planCutRing(
            chart, tempo_map, keyAt({.measure = 2, .beat = 1}, 1), common::core::Fraction{2}, 7);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].harmonic_node.has_value());
        check_struck(chart.notes[1]);
    }
}

// What the cut refuses, whole: a scrape, which is one picking-hand gesture with no junction; an
// offset at the ring's end or its onset, which has nothing to divide; and a slot holding no note.
TEST_CASE("planCutRing refuses what has no remainder to strike", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a scrape")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {makeScrape({.measure = 2, .beat = 1}, 1)};
        const auto plan = planCutRing(
            chart, tempo_map, keyAt({.measure = 2, .beat = 1}, 1), common::core::Fraction{1, 4}, 5);
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }

    SECTION("an offset at the ring's end")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            planCutRing(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{4}, 5);
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }

    SECTION("an offset at the onset")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            planCutRing(chart, tempo_map, keyAt(glideOnset(), 1), common::core::Fraction{0}, 5);
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }

    SECTION("a slot holding no note")
    {
        const common::core::Chart chart = makeGlideChart();
        const auto plan =
            planCutRing(chart, tempo_map, keyAt(glideOnset(), 2), common::core::Fraction{1}, 5);
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
}

// The fret a key carrying no value states: the stated stop inside a ring (never the travel toward
// the next), the last pitched stop at and past a ring's end (never a slide-out's target), and the
// open string where nothing sounded before.
TEST_CASE("chartFretInForceAt reads the stop the string already holds", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const common::core::Chart chart = makeGlideChart();
    const auto in_force = [&chart, &tempo_map](common::core::GridPosition position, int string) {
        return chartFretInForceAt(chart.notes, tempo_map, keyAt(position, string));
    };

    // One beat in, travelling from 7 toward the arrival at 9: the stop stated last is 7.
    CHECK(in_force({.measure = 2, .beat = 2}, 1) == 7);
    // Three beats in, past the arrival.
    CHECK(in_force({.measure = 2, .beat = 4}, 1) == 9);
    // At the ring's end, where the slide-out states 12: the last pitched stop, never the target.
    CHECK(in_force({.measure = 3, .beat = 1}, 1) == 9);
    // Past it too, so a head placed after a slide-out never manufactures a shift slide.
    CHECK(in_force({.measure = 3, .beat = 3}, 1) == 9);
    // Before the string's first note, and on a string with none at all: the open string.
    CHECK(in_force({.measure = 1, .beat = 1}, 1) == 0);
    CHECK(in_force({.measure = 2, .beat = 2}, 4) == 0);

    // A plain note that has stopped ringing hands its own fret on.
    const common::core::Chart plain = makeSingleNoteChart(5);
    CHECK(chartFretInForceAt(plain.notes, tempo_map, keyAt({.measure = 3, .beat = 1}, 1)) == 5);
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
// resolved against the stop an UNPRESSED string speaks from — the nut, or the capo — because the
// touch this verb authors stands on the open string. One formula covers every hand, because fret
// positions are logarithmic and the stop and the offset simply add.
TEST_CASE("planSetHarmonic states the typed fret as the node it names", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    SECTION("an open string's typed fret resolves against the nut")
    {
        common::core::Chart chart = makeSingleNoteChart(5);
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
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
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
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

    SECTION("a tap's typed fret is refused while the tapped harmonic is disabled")
    {
        // On a tap `H` would author an OPEN-STRING tapped harmonic (the tapping finger leaves the
        // fret it landed on and touches a node of the whole string). That form is disabled for now:
        // the normalizer reduces it to a plain note, so the plan gate refuses the edit as one that
        // does not survive normal form.
        common::core::Chart chart = makeSingleNoteChart(17);
        common::core::ChartNote& tap = chart.notes[0];
        tap.attack = common::core::NoteAttack::Tap;
        chart.notes.push_back(makeTestNote({.measure = 2, .beat = 1}, 2, 7));
        const auto plan = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
        CHECK_FALSE(plan.has_value());
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Tap);
        CHECK(chart.notes[0].fret == 17);
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

        const auto plan = planSetHarmonic(chart, tempo_map, chord, 7, "Harmonic").plan;
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

// The refusal is the whole of the ruling on a fret that names nothing: the verb states a node or
// leaves the note alone, and never moves the hand to the nearest node to invent one. An open string
// states no position at all — its offset is zero, which is not a touch — and under the editor's
// partial bound it is the only fret in that position. The plan is NoChange, and the note is named
// as refused, so the press is not a dead key.
TEST_CASE("planSetHarmonic refuses a fret that names no node", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};

    const common::core::Chart chart = makeSingleNoteChart(0);
    const ChartSelectionPlan planned =
        planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic");
    REQUIRE_FALSE(planned.plan.has_value());
    if (!planned.plan.has_value())
    {
        CHECK(std::holds_alternative<ChartPlanNoChange>(planned.plan.error()));
    }
    REQUIRE(planned.refused.size() == 1);
    CHECK(planned.refused[0].note == keys[0]);
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
        const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
        REQUIRE(set.has_value());
        if (set.has_value())
        {
            applyAndValidate(chart, tempo_map, *set);
            CHECK(chart.notes[0].fret == 0);
            const auto cleared = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic").plan;
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

        const auto plan = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic").plan;
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes[0].fret == 5);
            CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        }
    }

    SECTION("a tapped harmonic keeps the stop it is pressing and loses the touch")
    {
        // The same press-where-you-touched arithmetic read off a positive fret: the label IS that
        // fret, so the note comes back as the plain tap it was stopped as, with the tapping finger
        // off the node.
        common::core::Chart chart = makeSingleNoteChart(5);
        chart.notes[0].attack = common::core::NoteAttack::Tap;
        chart.notes[0].harmonic_node = 17.0;

        const auto plan = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic").plan;
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes[0].attack == common::core::NoteAttack::Tap);
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

        const auto plan = planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic").plan;
        REQUIRE_FALSE(plan.has_value());
        if (!plan.has_value())
        {
            CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
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

        const auto plan =
            planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic").plan;
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

        const auto plan =
            planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic").plan;
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
        const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
        REQUIRE(set.has_value());
        if (set.has_value())
        {
            applyAndValidate(chart, tempo_map, *set);
        }

        const auto plan =
            planClearPinchHarmonic(chart, tempo_map, keys, "Remove Pinch Harmonic").plan;
        REQUIRE_FALSE(plan.has_value());
        if (!plan.has_value())
        {
            CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
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
    const auto set = planSetHarmonic(touched, tempo_map, keys, std::nullopt, "Harmonic").plan;
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
    const auto set = planSetHarmonic(chart, tempo_map, keys, std::nullopt, "Harmonic").plan;
    REQUIRE(set.has_value());
    if (set.has_value())
    {
        applyAndValidate(chart, tempo_map, *set);
    }

    const auto plan =
        planRetypeFrets(chart, tempo_map, chart.notes, keys, {}, ChartFretSet{.fret = 7});
    REQUIRE_FALSE(plan.has_value());
    if (!plan.has_value())
    {
        CHECK(std::holds_alternative<ChartPlanInvalid>(plan.error()));
    }
}

// A NODE TRAVELS WITH ITS STOP. The node is `stop + offset` on a logarithmic board, so a stop that
// moves while its node stands still names an offset the harmonic never had: a harmonic at fret 5
// touching 17 is the octave, and retyped to 7 it must touch 19 to stay one. One law over every
// pressed stop under a node, whichever hand touches it — what differs between the two forms is only
// which hand made the onset, and neither the fret nor the offset cares.
TEST_CASE("planRetypeFrets carries a node with the stop it is measured from", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartSlotKey> keys{keyAt({.measure = 2, .beat = 1}, 1)};
    const auto retyped_to_seven = [&tempo_map, &keys](common::core::Chart& chart) {
        const auto plan =
            planRetypeFrets(chart, tempo_map, chart.notes, keys, {}, ChartFretSet{.fret = 7});
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
        }
        CHECK(chart.notes[0].fret == 7);
        const std::optional<double>& node = chart.notes[0].harmonic_node;
        REQUIRE(node.has_value());
        if (node.has_value())
        {
            CHECK_THAT(*node, Catch::Matchers::WithinAbs(19.0, 0.001));
        }
    };

    SECTION("a pinch harmonic's pressed stop")
    {
        // The pinch is the one harmonic over a pressed stop the chart accepts while the artificial
        // and tapped forms are disabled; the law is the same for all three, and this is where it
        // can still be exercised.
        common::core::Chart chart = makeSingleNoteChart(5);
        chart.notes[0].attack = common::core::NoteAttack::Pinch;
        chart.notes[0].harmonic_node = 17.0;
        retyped_to_seven(chart);
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
// them already vibrate. The planner tests above cover what a press writes; this covers which press
// it is, which is the half `ChartTechniqueLaw::carried` owns.
TEST_CASE("The vibrato law reads both its scopes for the direction", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
    note.vibrato = common::core::VibratoState::Narrow;
    note.keyframes = {
        // The vibrato ends here, so the state in force AT this point is still.
        common::core::Keyframe{
            .offset = common::core::Fraction{2},
            .fret = 9,
            .vibrato = common::core::VibratoState::None
        },
        // And starts again here.
        common::core::Keyframe{
            .offset = common::core::Fraction{3}, .vibrato = common::core::VibratoState::Narrow
        },
    };
    chart.notes = {std::move(note)};
    const ChartTechniqueLaw law = chartTechniqueLaw(ChartTechnique::Vibrato);
    const ChartSelectionKey note_key = ChartNoteKey{.slot = keyAt(glideOnset(), 1)};

    SECTION("a keyframe carries the vibrato when the state in force where it stands is vibrating")
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

    SECTION("a mixed selection clears only when every anchor in it already vibrates")
    {
        ChartSelection vibrating;
        vibrating.add(note_key);
        vibrating.add(
            ChartKeyframeKey{.note = keyAt(glideOnset(), 1), .offset = common::core::Fraction{3}});
        CHECK(law.carried(chart, vibrating));

        // The onset vibrates and the other anchor does not, so the press means SET — the same
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
        const auto plan = law.plan(chart, makeTempoMap(), nothing_reached, true, "Vibrato").plan;
        REQUIRE_FALSE(plan.has_value());
        CHECK(std::holds_alternative<ChartPlanNoChange>(plan.error()));
    }
}

// Uniform scope, one level inside the note: every selected keyframe on a note splits it, so two
// selected junctions make three. The channel states in force at each split become the product's
// ONSET values, which is what keeps the sound identical across the cut, and the slide-out
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
    // vibrato carry across the cut instead of restarting at the note's own defaults.
    const common::core::ChartNote& second = chart.notes[1];
    CHECK(second.fret == 9);
    CHECK_THAT(second.bend, Catch::Matchers::WithinULP(1.0, 0));
    CHECK(second.vibrato == common::core::VibratoState::Narrow);
    // Its own arrival stands AT the head that follows it, which the relation reads as an arrival
    // and not a slide-out: the fret it names is that head's own stop.
    REQUIRE(second.keyframes.size() == 1);
    CHECK(second.keyframes[0].offset == common::core::Fraction{1});
    CHECK(second.keyframes[0].fret == 11);

    // The third opens at the second junction, where the bend has not been restated and the leg
    // that junction begins states no width — the vibrato was the first junction's leg alone — and
    // it is the one that ends where the gesture did, so it keeps the terminal.
    const common::core::ChartNote& third = chart.notes[2];
    CHECK(third.fret == 11);
    CHECK_THAT(third.bend, Catch::Matchers::WithinULP(1.0, 0));
    CHECK(third.vibrato == common::core::VibratoState::None);
    const int* const terminal = common::core::endStatedFretOrNull(third);
    REQUIRE(terminal != nullptr);
    if (terminal != nullptr)
    {
        CHECK(*terminal == 3);
    }
    // The first product's own arrival stands at ITS cut too, carrying the bend the junction stated
    // (the curve's last value, completing as that product's ring does) while the VIBRATO stated
    // there goes, having no ring left to vibrate in. Nothing is lost: that vibrato is the second
    // product's onset state, which the check above already pins.
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    const common::core::Keyframe& first_arrival = chart.notes[0].keyframes[0];
    CHECK(first_arrival.offset == common::core::Fraction{1});
    CHECK(first_arrival.fret == 9);
    CHECK_FALSE(hasVibrato(first_arrival.vibrato));
    const std::optional<double>& first_arrival_bend = first_arrival.bend;
    REQUIRE(first_arrival_bend.has_value());
    if (first_arrival_bend.has_value())
    {
        CHECK_THAT(*first_arrival_bend, Catch::Matchers::WithinULP(1.0, 0));
    }

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
    CHECK_FALSE(hasVibrato(path.keyframes[0].vibrato));
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

// Nothing carries in the vibrato channel, so the head's onset width becomes the width of the leg
// its junction point begins — even where the predecessor's last leg vibrates at that same width,
// which a compare against the width in force would have dropped, leaving the head's leg unvibrated.
TEST_CASE("planToggleJunctions joins a vibrating head as its own leg's width", "[core][chart]")
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    common::core::ChartNote predecessor =
        makeTestNote({.measure = 2, .beat = 1}, 1, 5, common::core::Fraction{4});
    predecessor.vibrato = common::core::VibratoState::Narrow;
    common::core::ChartNote head =
        makeTestNote({.measure = 3, .beat = 1}, 1, 7, common::core::Fraction{2});
    head.attack = common::core::NoteAttack::Legato;
    head.vibrato = common::core::VibratoState::Narrow;
    chart.notes = {std::move(predecessor), std::move(head)};
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto joined = joinHeads(chart, tempo_map, {keyAt({.measure = 3, .beat = 1}, 1)});
    REQUIRE(joined.has_value());
    if (!joined.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, joined->plan);

    REQUIRE(chart.notes.size() == 1);
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{4});
    CHECK(chart.notes[0].keyframes[0].fret == 7);
    CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
    // Both legs vibrate at one width, so the ring draws as one region from onset to end.
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(5.0, 1e-9));

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
// The arrival the split leaves stands AT the junction, so the join's merge takes it over with
// nothing to restore.
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
    // The arrival stands ON the new head, at the origin's own ring end.
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{2});

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
        CHECK(std::holds_alternative<ChartPlanInvalid>(joined.error()));
    }

    SECTION("a predecessor whose tail already slides out")
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
        CHECK(std::holds_alternative<ChartPlanInvalid>(joined.error()));
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
        CHECK(std::holds_alternative<ChartPlanInvalid>(joined.error()));
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
        CHECK(std::holds_alternative<ChartPlanInvalid>(joined.error()));
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
        CHECK(product->attack == common::core::NoteAttack::Pick);
    }

    REQUIRE(applyChartChange(chart, toggled->plan.reversed()).has_value());
    CHECK(chart == original);
}

// The vibrato channel's two authoring scopes are ONE planner, because they are one channel: the
// note's own field is the first leg's width and a keyframe's is the width of the leg it begins.
// This is the plain half — a selected keyframe takes the width, and the onset it rides is left
// exactly as the charter wrote it.
TEST_CASE("planSetVibrato states the vibrato at a selected keyframe", "[core][chart]")
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
                          "Vibrato")
                          .plan;
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& note = chart.notes[0];
    // The note reached only through its keyframe keeps its own first leg: a keyframe's width is
    // its own leg's, never a rewrite of the onset.
    CHECK_FALSE(common::core::hasVibrato(note.vibrato));
    REQUIRE(note.keyframes.size() == 2);
    CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Narrow);
    CHECK_FALSE(hasVibrato(note.keyframes[1].vibrato));
    // The position channel is untouched — the coupling law works because the keyframe is one
    // record, not because a verb copies fields between channels.
    CHECK(note.keyframes[0].fret == 9);
    // Drawn, only that leg vibrates: from the keyframe (two beats in, 3.0s) to the next one, the
    // slide-out at the ring's end (4.0s).
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK(regions[0].state == common::core::VibratoState::Narrow);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// The planner writes the width at every anchor it points at, or ends the vibrato there
// (endVibratoAt), and never dissolves a point itself: the commit law judges what is left. A width
// equal to the leg before it, or a bare point after an unvibrated leg, is silent authoring state
// the history and the writer shed like any silent point; a bare point after a vibrated leg is the
// one stored form of the vibrato ending there, and is document.
TEST_CASE("planSetVibrato leaves silent points to the commit law", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    SECTION("a width repeated at a keyframe is silent authoring state")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        note.vibrato = common::core::VibratoState::Narrow;
        // A point restating the fret begins an unvibrated leg two beats in, so the vibrato ends
        // there while the point also states a fret.
        note.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 7},
        };
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;
        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                              common::core::VibratoState::Narrow,
                              "Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        // The planner still writes the width: in memory the point stands and carries it.
        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].fret == 7);
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);

        // It says nothing: with it gone the onset's leg vibrates the same stretch, so the ring
        // draws one region from the onset (2.0s) to its end (4.0s) either way.
        common::core::Chart dissolved = chart;
        CHECK(common::core::stripSilentKeyframes(dissolved.notes[0]));
        CHECK(dissolved.notes[0].keyframes.empty());
        const std::vector<common::core::VibratoSpanViewState> regions =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(regions.size() == 1);
        CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
        const std::vector<common::core::VibratoSpanViewState> dissolved_regions =
            vibratoRegionsOf(dissolved, tempo_map, 0);
        REQUIRE(dissolved_regions.size() == 1);
        CHECK(dissolved_regions[0].state == regions[0].state);
        CHECK_THAT(dissolved_regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        CHECK_THAT(dissolved_regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

        // The HISTORY records the written form, which has no point at all.
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        CHECK(written.inserted[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("clearing a width-only point after an unvibrated leg leaves it bare and silent")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // A delayed start: the ring opens still and vibrates from two beats in. Nothing about the
        // hand's position is stated, so this point exists for the vibrato alone.
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
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The leg before the point does not vibrate, so with the width gone the point says
        // nothing. It stays in the plan as a bare leg boundary — authoring state, which the
        // history sheds and the note's leaving focus dissolves.
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{2});
        CHECK(common::core::keyframeStatesNothing(chart.notes[0].keyframes[0]));
        CHECK(vibratoRegionsOf(chart, tempo_map, 0).empty());
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        CHECK(written.inserted[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("clearing the onset's vibrato before a bare ending leaves the ending silent")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // The ordinary vibrato from the onset, ended two beats in by a bare point.
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {common::core::Keyframe{.offset = common::core::Fraction{2}}};
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {keyAt(glideOnset(), 1)},
                              {},
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The ending now follows an unvibrated leg, so it says nothing: legal in the plan, shed by
        // the history.
        CHECK_FALSE(common::core::hasVibrato(chart.notes[0].vibrato));
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(common::core::keyframeStatesNothing(chart.notes[0].keyframes[0]));
        CHECK(vibratoRegionsOf(chart, tempo_map, 0).empty());
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        CHECK(written.inserted[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("setting a width equal to the leg before it plans a silent repeat")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // The ordinary vibrato from the onset, stepping to the wide one two beats in.
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {common::core::Keyframe{
            .offset = common::core::Fraction{2}, .vibrato = common::core::VibratoState::Wide
        }};
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                              common::core::VibratoState::Narrow,
                              "Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The width is written, and repeats the onset's leg: the point is silent, so the ring
        // draws one ordinary region from the onset (2.0s) to its end (4.0s) and the history keeps
        // no point at all.
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
        const std::vector<common::core::VibratoSpanViewState> regions =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(regions.size() == 1);
        CHECK(regions[0].state == common::core::VibratoState::Narrow);
        CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        CHECK(written.inserted[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("clearing a width before a bare ending leaves both points silent")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // A still onset, the wide vibrato from one beat in, ended by a bare point at two.
        note.keyframes = {
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .vibrato = common::core::VibratoState::Wide
            },
            common::core::Keyframe{.offset = common::core::Fraction{2}},
        };
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})},
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // Both points are bare after unvibrated legs now: legal in the plan, silent, and shed by
        // the history.
        REQUIRE(chart.notes[0].keyframes.size() == 2);
        CHECK(common::core::keyframeStatesNothing(chart.notes[0].keyframes[0]));
        CHECK(common::core::keyframeStatesNothing(chart.notes[0].keyframes[1]));
        CHECK(vibratoRegionsOf(chart, tempo_map, 0).empty());
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        CHECK(written.inserted[0].keyframes.empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("clearing a width-only point after a vibrated leg ends the vibrato there")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        // The ordinary vibrato from the onset, stepping to the wide one two beats in.
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {common::core::Keyframe{
            .offset = common::core::Fraction{2}, .vibrato = common::core::VibratoState::Wide
        }};
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // Dissolving the point would let the onset's leg run on to the ring's end, so it stays
        // bare: the stored form of vibrato ending where nothing else does.
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{2});
        CHECK_FALSE(chart.notes[0].keyframes[0].fret.has_value());
        CHECK_FALSE(chart.notes[0].keyframes[0].bend.has_value());
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::None);
        // Drawn, the onset's region ends at the point (3.0s) instead of running to 4.0s.
        const std::vector<common::core::VibratoSpanViewState> regions =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(regions.size() == 1);
        CHECK(regions[0].state == common::core::VibratoState::Narrow);
        CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
        // The bare ending is document, not authoring state: the history keeps it.
        const ChartEditPlan written = writtenChartPlan(*plan);
        REQUIRE(written.inserted.size() == 1);
        REQUIRE(written.inserted[0].keyframes.size() == 1);
        CHECK(written.inserted[0].keyframes[0] == chart.notes[0].keyframes[0]);

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("clearing a width-only point on a hold leaves the path alone")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 5, common::core::Fraction{4});
        // A hold on the 5 until beat 1 1/2, then a glide to 7 by beat 2, the ordinary vibrato
        // stepping to the wide one at beat 1 while the hand still rests.
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {
            common::core::Keyframe{
                .offset = common::core::Fraction{1}, .vibrato = common::core::VibratoState::Wide
            },
            common::core::Keyframe{.offset = common::core::Fraction{3, 2}, .fret = 5},
            common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 7},
        };
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})},
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The point stays bare: a fret restated there would state a stop the path already holds.
        REQUIRE(chart.notes[0].keyframes.size() == 3);
        CHECK(chart.notes[0].keyframes[0].offset == common::core::Fraction{1});
        CHECK_FALSE(chart.notes[0].keyframes[0].fret.has_value());
        CHECK_FALSE(chart.notes[0].keyframes[0].bend.has_value());
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::None);
        CHECK(chart.notes[0].keyframes[1] == original.notes[0].keyframes[1]);
        CHECK(chart.notes[0].keyframes[2] == original.notes[0].keyframes[2]);

        // Drawn, the path is the one it was: the hold's stop at beat 1 1/2 (2.75s) and the glide's
        // at beat 2 (3.0s). The bare keyframe states no stop — it wears a head at the 5 the hand
        // rests on — while the vibrato region closes at it (2.5s).
        common::core::Arrangement arrangement{};
        arrangement.chart = chart;
        const common::core::ChartViewState state =
            common::core::makeChartViewState(arrangement, tempo_map);
        REQUIRE(state.notes.size() == 1);
        const common::core::NoteViewState& view = state.notes.front();
        CHECK_THAT(view.start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        REQUIRE(view.slides.size() == 2);
        CHECK(view.slides[1].fret == 7);
        CHECK_THAT(view.slides[1].seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
        REQUIRE(view.keyframes.size() == 3);
        CHECK(view.keyframes[0].offset == common::core::Fraction{1});
        CHECK(
            view.keyframes[0].mark ==
            common::core::KeyframeMark{common::core::KeyframeRestMark{.fret = 5}});
        REQUIRE(view.vibrato.size() == 1);
        CHECK(view.vibrato[0].state == common::core::VibratoState::Narrow);
        CHECK_THAT(view.vibrato[0].end_seconds, Catch::Matchers::WithinAbs(2.5, 1e-9));

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("a vibrato change in the middle of a glide is refused")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 5, common::core::Fraction{4});
        // A glide from 5 to 7 over two beats, bent halfway along it: a bend may change mid-travel,
        // a vibrato may not (common::core::shedMidTravelVibrato).
        note.keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{1}, .bend = 1.0},
            common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 7},
        };
        chart.notes = {std::move(note)};

        CHECK_FALSE(planSetVibrato(
                        chart,
                        tempo_map,
                        {},
                        {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{1})},
                        common::core::VibratoState::Narrow,
                        "Add Vibrato")
                        .plan.has_value());
    }

    SECTION("clearing the vibrato from a point that also states a fret keeps the point")
    {
        common::core::Chart chart = makeGlideChart();
        chart.notes[0].keyframes[0].vibrato = common::core::VibratoState::Narrow;
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                              common::core::VibratoState::None,
                              "Remove Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        // The width goes and the fret stays, so the point still says something and stands.
        REQUIRE(chart.notes[0].keyframes.size() == 2);
        CHECK(chart.notes[0].keyframes[0].fret == 9);
        CHECK_FALSE(hasVibrato(chart.notes[0].keyframes[0].vibrato));
        CHECK(vibratoRegionsOf(chart, tempo_map, 0).empty());

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("a press writes only the anchors it points at")
    {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {
            // Authored by someone else: this press never points at it.
            common::core::Keyframe{
                .offset = common::core::Fraction{1},
                .fret = 9,
                .vibrato = common::core::VibratoState::Narrow
            },
            // The leg this begins does not vibrate until the press below states it.
            common::core::Keyframe{
                .offset = common::core::Fraction{2},
                .fret = 11,
                .vibrato = common::core::VibratoState::None
            },
        };
        chart.notes = {std::move(note)};
        const common::core::Chart original = chart;

        const auto plan = planSetVibrato(
                              chart,
                              tempo_map,
                              {},
                              {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                              common::core::VibratoState::Narrow,
                              "Vibrato")
                              .plan;
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        applyAndValidate(chart, tempo_map, *plan);

        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 2);
        // The untouched width stays exactly as it was authored.
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Narrow);
        // The pointed-at leg now states its own width, beside the fret it already stated.
        CHECK(chart.notes[0].keyframes[1].vibrato == common::core::VibratoState::Narrow);
        CHECK(chart.notes[0].keyframes[1].fret == 11);

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }
}

// The note scope keeps its onset semantics exactly: pressing the verb with the NOTE selected
// writes the first leg's width and leaves every keyframe's width alone, because this press
// pointed at none of them.
TEST_CASE("planSetVibrato on a note writes the onset alone", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    // On the junction, not the slide-out: a slide-out states its fret and nothing else.
    chart.notes[0].keyframes[0].vibrato = common::core::VibratoState::Narrow;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planSetVibrato(
                          chart,
                          tempo_map,
                          {keyAt(glideOnset(), 1)},
                          {},
                          common::core::VibratoState::Narrow,
                          "Vibrato")
                          .plan;
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

// A note's width is its FIRST leg's: on a ring that carries keyframes, the press on the note
// vibrates from the onset to the first keyframe and never rides through the slide stop there.
TEST_CASE("planSetVibrato on a note vibrates its first leg only", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planSetVibrato(
                          chart,
                          tempo_map,
                          {keyAt(glideOnset(), 1)},
                          {},
                          common::core::VibratoState::Narrow,
                          "Vibrato")
                          .plan;
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    // The onset (2.0s) to the glide's stop two beats in (3.0s), and nothing past it.
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// A keyframe's width runs to the NEXT keyframe, so a slide stop after it ends the vibrato there
// even though the ring sounds on past it.
TEST_CASE("planSetVibrato on a keyframe vibrates only up to the next slide stop", "[core][chart]")
{
    common::core::Chart chart = makeSteppedGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto plan = planSetVibrato(
                          chart,
                          tempo_map,
                          {},
                          {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                          common::core::VibratoState::Narrow,
                          "Vibrato")
                          .plan;
    REQUIRE(plan.has_value());
    if (!plan.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *plan);

    // The stop at beat 2 (3.0s) to the stop at beat 3 (3.5s); the ring rings on to 4.0s unvibrated.
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(3.5, 1e-9));

    REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
    CHECK(chart == original);
}

// Vibrating THROUGH a slide stop stays authorable, one press per leg: the note's press takes the
// first leg and the stop's press the leg it begins. The projection draws adjacent legs at one width
// as ONE merged region, so the result reads as a single unbroken vibrato across the stop.
TEST_CASE("planSetVibrato through a slide stop is one press per leg", "[core][chart]")
{
    common::core::Chart chart = makeGlideChart();
    const common::core::Chart original = chart;
    const common::core::TempoMap tempo_map = makeTempoMap();

    const auto first_leg = planSetVibrato(
                               chart,
                               tempo_map,
                               {keyAt(glideOnset(), 1)},
                               {},
                               common::core::VibratoState::Narrow,
                               "Vibrato")
                               .plan;
    REQUIRE(first_leg.has_value());
    if (!first_leg.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *first_leg);
    const common::core::Chart after_first = chart;

    const auto second_leg = planSetVibrato(
                                chart,
                                tempo_map,
                                {},
                                {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})},
                                common::core::VibratoState::Narrow,
                                "Vibrato")
                                .plan;
    REQUIRE(second_leg.has_value());
    if (!second_leg.has_value())
    {
        return;
    }
    applyAndValidate(chart, tempo_map, *second_leg);

    // One merged region from the onset (2.0s) to the slide-out at the ring's end (4.0s).
    const std::vector<common::core::VibratoSpanViewState> regions =
        vibratoRegionsOf(chart, tempo_map, 0);
    REQUIRE(regions.size() == 1);
    CHECK_THAT(regions[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
    CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

    REQUIRE(applyChartChange(chart, second_leg->reversed()).has_value());
    CHECK(chart == after_first);
    REQUIRE(applyChartChange(chart, first_leg->reversed()).has_value());
    CHECK(chart == original);
}

// Delete is the same verb one level in: it takes the selected keyframe itself — every statement it
// makes and the leg boundary it is. The label names what was actually deleted.
TEST_CASE("planDeleteSelection takes a keyframe and its statements", "[core][chart]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();

    // A four-beat fret-7 hold vibrating from its onset, carrying `keyframe` two beats in.
    const auto vibrated_hold = [](const common::core::Keyframe& keyframe) {
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        common::core::ChartNote note = makeTestNote(glideOnset(), 1, 7, common::core::Fraction{4});
        note.vibrato = common::core::VibratoState::Narrow;
        note.keyframes = {keyframe};
        chart.notes = {std::move(note)};
        return chart;
    };

    SECTION("a deleted vibrato ending lets the vibrato before it run on")
    {
        // The bare keyframe is the vibrato's ending, so the region stops there (3.0s) until the
        // boundary is deleted, and then runs to the ring's end (4.0s).
        common::core::Chart chart =
            vibrated_hold(common::core::Keyframe{.offset = common::core::Fraction{2}});
        const common::core::Chart original = chart;
        const std::vector<common::core::VibratoSpanViewState> before =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(before.size() == 1);
        CHECK_THAT(before[0].end_seconds, Catch::Matchers::WithinAbs(3.0, 1e-9));

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
        CHECK(chart.notes[0].keyframes.empty());
        CHECK(chart.notes[0].vibrato == common::core::VibratoState::Narrow);
        const std::vector<common::core::VibratoSpanViewState> after =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(after.size() == 1);
        CHECK(after[0].state == common::core::VibratoState::Narrow);
        CHECK_THAT(after[0].start_seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
        CHECK_THAT(after[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    // Each technique is its own authored surface (user ruling, 2026-09-29): Delete takes the
    // point's FRET and leaves its width for `V` to clear, so the point stands as a vibrato change;
    // a second Delete, on a point with no fret left, takes it whole, and the onset's vibrato runs
    // on.
    SECTION("a keyframe carrying a fret and a width loses its fret, then goes whole")
    {
        common::core::Chart chart = vibrated_hold(
            common::core::Keyframe{
                .offset = common::core::Fraction{2},
                .fret = 9,
                .vibrato = common::core::VibratoState::Wide,
            });
        const common::core::Chart original = chart;
        const std::vector<ChartKeyframeKey> point{keyframeKeyAt(
            glideOnset(), 1, common::core::Fraction{2})};
        const auto plan = planDeleteSelection(chart, tempo_map, {}, point);
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        CHECK(plan->label == "Remove Fret");
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK_FALSE(chart.notes[0].keyframes[0].fret.has_value());
        CHECK(chart.notes[0].keyframes[0].vibrato == common::core::VibratoState::Wide);
        CHECK(chart.notes[0].fret == 7);

        const auto again = planDeleteSelection(chart, tempo_map, {}, point);
        REQUIRE(again.has_value());
        if (!again.has_value())
        {
            return;
        }
        CHECK(again->label == "Delete Keyframe");
        applyAndValidate(chart, tempo_map, *again);
        // Erased whole: no bare boundary stays behind to end the onset's vibrato.
        CHECK(chart.notes[0].keyframes.empty());
        const std::vector<common::core::VibratoSpanViewState> regions =
            vibratoRegionsOf(chart, tempo_map, 0);
        REQUIRE(regions.size() == 1);
        CHECK(regions[0].state == common::core::VibratoState::Narrow);
        CHECK_THAT(regions[0].end_seconds, Catch::Matchers::WithinAbs(4.0, 1e-9));

        REQUIRE(applyChartChange(chart, again->reversed()).has_value());
        REQUIRE(applyChartChange(chart, plan->reversed()).has_value());
        CHECK(chart == original);
    }

    SECTION("a keyframe carrying a fret and a bend keeps its bend")
    {
        common::core::Chart chart = vibrated_hold(
            common::core::Keyframe{
                .offset = common::core::Fraction{2},
                .fret = 9,
                .bend = 2.0,
                .vibrato = common::core::VibratoState::Narrow,
            });
        const auto plan = planDeleteSelection(
            chart, tempo_map, {}, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        CHECK(plan->label == "Remove Fret");
        applyAndValidate(chart, tempo_map, *plan);
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        const common::core::Keyframe& point = chart.notes[0].keyframes[0];
        CHECK_FALSE(point.fret.has_value());
        const std::optional<double>& bend = point.bend;
        REQUIRE(bend.has_value());
        if (bend.has_value())
        {
            CHECK_THAT(*bend, Catch::Matchers::WithinULP(2.0, 0));
        }
    }

    SECTION("a fret whose point says nothing else takes the point with it")
    {
        // The width repeats the onset's, so with the fret gone the point says nothing new.
        common::core::Chart chart = vibrated_hold(
            common::core::Keyframe{
                .offset = common::core::Fraction{2},
                .fret = 9,
                .vibrato = common::core::VibratoState::Narrow,
            });
        const auto plan = planDeleteSelection(
            chart, tempo_map, {}, {keyframeKeyAt(glideOnset(), 1, common::core::Fraction{2})});
        REQUIRE(plan.has_value());
        if (!plan.has_value())
        {
            return;
        }
        CHECK(plan->label == "Delete Keyframe");
        applyAndValidate(chart, tempo_map, *plan);
        CHECK(chart.notes[0].keyframes.empty());
    }

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

// EVERY VERB PRODUCES TICK-LATTICE POSITIONS. Validation refuses an instant between two ticks, so
// a verb that authored one — a ring one exact septuplet step long, a move by one exact step, a beat
// count carried into a meter of another denominator — would be refused on exactly the grids and
// maps it is most needed on. Each case is a plan the gate must ACCEPT, on a position it could not
// have reached by beat arithmetic.
TEST_CASE("Chart verbs land every instant on the tick lattice", "[core][chart]")
{
    // A seventh of a 4/4 beat: 137.14 ticks, so no exact step from a line is a tick.
    constexpr common::core::Fraction septuplet_grid{1, 28};
    const common::core::GridPosition downbeat{.measure = 2, .beat = 1};

    SECTION("a placed ring ends on the septuplet grid's next line, and the next placement touches")
    {
        const common::core::TempoMap tempo_map = makeTempoMap();
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        const common::core::GridPosition next_line =
            adjacentTempoGridPosition(tempo_map, septuplet_grid, downbeat, true);
        CHECK(common::core::isOnTickLattice(tempo_map, next_line));

        const auto first =
            planInsertNote(chart, tempo_map, makeTestNote(downbeat, 1, 5), septuplet_grid);
        REQUIRE(first.has_value());
        if (first.has_value())
        {
            applyAndValidate(chart, tempo_map, *first);
            CHECK(common::core::sustainEndPosition(tempo_map, chart.notes.front()) == next_line);
        }
        const auto second =
            planInsertNote(chart, tempo_map, makeTestNote(next_line, 1, 7), septuplet_grid);
        REQUIRE(second.has_value());
        if (second.has_value())
        {
            applyAndValidate(chart, tempo_map, *second);
            REQUIRE(chart.notes.size() == 2);
            // Entered one after another, septuplet notes touch exactly, which legato needs.
            CHECK(
                common::core::sustainEndPosition(tempo_map, chart.notes[0]) ==
                chart.notes[1].position);
        }
    }

    SECTION("a move by one septuplet step lands the note on the grid's next line")
    {
        const common::core::TempoMap tempo_map = makeTempoMap();
        const common::core::GridPosition next_line =
            adjacentTempoGridPosition(tempo_map, septuplet_grid, downbeat, true);
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {makeTestNote(
            downbeat, 1, 5, common::core::beatDistance(tempo_map, downbeat, next_line))};
        REQUIRE(common::core::validateChartRules(chart, tempo_map).has_value());

        // The press is measured at the anchor as its distance to the next line: 137 ticks, where
        // one exact step is 137.14 and would leave every landing between two ticks.
        const ChartMoveDelta delta = chartMoveGestureDelta(
            tempo_map,
            {keyAt(downbeat, 1)},
            {},
            {ChartMoveStep{.note_value = septuplet_grid, .direction = ChartStepDirection::Right}});
        CHECK(delta.whole_notes == common::core::wholeNoteDistance(tempo_map, downbeat, next_line));
        const auto plan =
            moveNotes(chart, tempo_map, {keyAt(downbeat, 1)}, delta.whole_notes, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes.front().position == next_line);
            CHECK(
                chart.notes.front().sustain ==
                common::core::beatDistance(tempo_map, downbeat, next_line));
        }
    }

    SECTION("a note moved across a meter change keeps its real length and every instant on a tick")
    {
        const common::core::TempoMap tempo_map = makeMeterChangeMap();
        // An odd tick of a quarter-note beat is half a tick of an eighth-note one, so a beat count
        // carried into measure 3 lands between ticks; the whole-note delta lands on them.
        const common::core::GridPosition onset{
            .measure = 2, .beat = 3, .offset = common::core::Fraction{959, 960}
        };
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        chart.notes = {makeTestNote(onset, 1, 5, common::core::Fraction{1})};
        chart.notes.front().keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{1, 2}, .fret = 7}
        };
        REQUIRE(common::core::validateChartRules(chart, tempo_map).has_value());

        // A half note right: the quarter-note ring is two eighth-note beats once it sits in 6/8,
        // and the point half a quarter in is one eighth in — the same music, re-counted.
        const auto plan = moveNotes(
            chart, tempo_map, {keyAt(onset, 1)}, common::core::Fraction{1, 2}, 0, "Move Note");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            const common::core::ChartNote& moved = chart.notes.front();
            CHECK(
                moved.position ==
                common::core::GridPosition{
                    .measure = 3, .beat = 2, .offset = common::core::Fraction{479, 480}
                });
            CHECK(moved.sustain == common::core::Fraction{2});
            REQUIRE(moved.keyframes.size() == 1);
            CHECK(moved.keyframes.front().offset == common::core::Fraction{1});
        }
    }

    SECTION("a keyframe stepped by a tick across the barline lands on the new meter's tick")
    {
        const common::core::TempoMap tempo_map = makeMeterChangeMap();
        const common::core::GridPosition onset{.measure = 2, .beat = 4};
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        // Three beats from the last 4/4 beat: one quarter to the barline, then two eighths.
        chart.notes = {makeTestNote(onset, 1, 5, common::core::Fraction{3})};
        chart.notes.front().keyframes = {
            common::core::Keyframe{.offset = common::core::Fraction{1}, .fret = 7}
        };
        REQUIRE(common::core::validateChartRules(chart, tempo_map).has_value());

        // One tick is 1/3840 of a whole note everywhere; as a 4/4 beat count (1/960) it would be
        // half a 6/8 tick, which the gate refuses.
        const auto plan = planMoveSelection(
            chart,
            tempo_map,
            {},
            {keyframeKeyAt(onset, 1, common::core::Fraction{1})},
            common::core::g_tick_quantum_note_value,
            0,
            "Move Keyframe");
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            CHECK(chart.notes.front().keyframes.front().offset == common::core::Fraction{481, 480});
        }
    }

    SECTION("a scrape's default ring authored a tick before a meter change ends on a tick")
    {
        const common::core::TempoMap tempo_map = makeMeterChangeMap();
        const common::core::GridPosition onset{
            .measure = 2, .beat = 4, .offset = common::core::Fraction{959, 960}
        };
        common::core::Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        // A one-tick ring that ends exactly on the barline: too short to scrape, and the only
        // short ring from this onset whose end is a tick in the meter it lands in.
        chart.notes = {makeTestNote(onset, 1, 5, common::core::Fraction{1, 960})};
        REQUIRE(common::core::validateChartRules(chart, tempo_map).has_value());

        // The quarter-note default is a note VALUE: one tick to the barline, then 959 ticks of 6/8,
        // which is one eighth-note beat and 479/480 of the next. Stated as one BEAT at the onset's
        // meter it would end 479.5 ticks into measure 3, between two ticks.
        const auto plan = planSetAttack(
                              chart,
                              tempo_map,
                              {keyAt(onset, 1)},
                              common::core::NoteAttack::PickSlide,
                              "Pick Slide")
                              .plan;
        REQUIRE(plan.has_value());
        if (plan.has_value())
        {
            applyAndValidate(chart, tempo_map, *plan);
            const common::core::ChartNote& scrape = chart.notes.front();
            CHECK(scrape.sustain == common::core::Fraction{1919, 960});
            CHECK(
                common::core::sustainEndPosition(tempo_map, scrape) ==
                common::core::GridPosition{
                    .measure = 3, .beat = 2, .offset = common::core::Fraction{479, 480}
                });
        }
    }
}

} // namespace rock_hero::editor::core
