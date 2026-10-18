#include "chart/chart_edits.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Chart;
using common::core::ChartNote;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::Keyframe;
using common::core::NoteAttack;
using common::core::TempoMap;
using common::core::testing::anchor;

// A 4/4 120 BPM map, eight measures long.
[[nodiscard]] TempoMap makeCommonTimeMap()
{
    return TempoMap::defaultMap(common::core::TimeDuration{16.0});
}

// 4/4 for three measures, then 6/8: the fixtures' content stands on both sides of a meter change,
// where a beat is a different note value and every address reads the new meter.
[[nodiscard]] TempoMap makeMeterChangeMap()
{
    return TempoMap{
        common::core::testing::metersAt(
            {{.at = "1:1", .numerator = 4, .denominator = 4},
             {.at = "4:1", .numerator = 6, .denominator = 8}}),
        std::vector{
            anchor("1:1", 0),
            anchor("21:1", 31000),
        },
    };
}

// A chart dense enough that most edits reach a neighbour: rings ending exactly on the next head of
// their string, a glide whose interior point a truncation would erase, a justified legato, a tap
// that a fret of zero strands, an open string, a silent keyframe, and a chord.
[[nodiscard]] Chart makeGateChart()
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote glide = makeTestNote({.measure = 1, .beat = 3}, 3, 7, Fraction{1});
    glide.keyframes = {
        Keyframe{.offset = Fraction{1, 2}, .fret = 8},
        Keyframe{.offset = Fraction{1}, .fret = 9},
    };
    ChartNote hammer = makeTestNote({.measure = 2, .beat = 1}, 4, 4, Fraction{1, 4});
    hammer.attack = NoteAttack::Legato;
    ChartNote tap = makeTestNote({.measure = 2, .beat = 4}, 6, 5, Fraction{1, 8});
    tap.attack = NoteAttack::Tap;
    ChartNote held = makeTestNote({.measure = 3, .beat = 3}, 1, 7, Fraction{1, 2});
    held.keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 7}};
    chart.notes = {
        makeTestNote({.measure = 1, .beat = 1}, 1, 3, Fraction{1, 2}),
        makeTestNote({.measure = 1, .beat = 1}, 2, 5),
        makeTestNote({.measure = 1, .beat = 3}, 1, 5, Fraction{1}),
        std::move(glide),
        makeTestNote({.measure = 1, .beat = 4}, 4, 2, Fraction{1, 4}),
        std::move(hammer),
        makeTestNote({.measure = 2, .beat = 3}, 5, 0, Fraction{1, 8}),
        std::move(tap),
        std::move(held),
        makeTestNote({.measure = 5, .beat = 1}, 1, 5, Fraction{1, 2}),
        makeTestNote({.measure = 5, .beat = 1}, 2, 7, Fraction{1, 2}),
        makeTestNote({.measure = 5, .beat = 1}, 3, 7, Fraction{1, 2}),
        makeTestNote({.measure = 6, .beat = 1}, 5, 3, Fraction{2}),
    };
    return chart;
}

// A fixture chart under the name its builder goes by, so a check over all of them names the one
// that fails.
struct FixtureChart
{
    std::string_view name;
    Chart chart;
};

// Every fixture chart the gate is exercised on.
[[nodiscard]] std::vector<FixtureChart> fixtureCharts()
{
    return {
        {.name = "makeTestChart", .chart = makeTestChart()},
        {.name = "makeGlideChart", .chart = makeGlideChart()},
        {.name = "makeAbuttingStringChart(false)", .chart = makeAbuttingStringChart(false)},
        {.name = "makeAbuttingStringChart(true)", .chart = makeAbuttingStringChart(true)},
        {.name = "makeGateChart", .chart = makeGateChart()},
    };
}

// The whole-stream gate the touched-set gate must agree with: the same production authorities,
// each run over every note of the candidate. Answers the normalized candidate, or the refusal.
[[nodiscard]] std::expected<std::vector<ChartNote>, ChartPlanRefusal> wholeStreamGate(
    const Chart& chart, const TempoMap& tempo_map, const std::vector<ChartNote>& base,
    std::vector<ChartNote> candidate)
{
    if (std::ranges::any_of(
            common::core::normalizeSustainOverlaps(candidate, tempo_map),
            &common::core::TailTruncation::statement_lost))
    {
        return std::unexpected{
            ChartPlanInvalid{"the edit would cut an authored statement off a ring it shortens"}
        };
    }
    for (ChartNote& note : candidate)
    {
        static_cast<void>(common::core::flattenStrandedStrike(note));
    }
    std::vector<ChartNote> saved_form;
    saved_form.reserve(candidate.size());
    for (const ChartNote& note : candidate)
    {
        saved_form.push_back(common::core::savedChartNote(note));
    }
    if (const std::expected<void, common::core::ChartError> valid =
            common::core::validateChartNotes(saved_form, chart.tuning, tempo_map);
        !valid.has_value())
    {
        return std::unexpected{ChartPlanInvalid{valid.error().message}};
    }
    if (candidate == base)
    {
        return std::unexpected{ChartPlanNoChange{}};
    }
    return candidate;
}

// What one candidate's two gates answered, tallied so the script provably reaches every outcome.
struct Outcomes
{
    std::size_t plans{0};
    std::size_t unchanged{0};
    std::size_t refused{0};
    std::size_t statements_lost{0};
    std::size_t duplicates{0};
};

// Asserts the gate's answer for `candidate` is exactly the whole-stream gate's: the same refusal
// (kind and reason), or a plan that carries `base` to the same normalized stream under `label`.
void requireSameAnswer(
    const Chart& chart, const TempoMap& tempo_map, const std::vector<ChartNote>& base,
    const std::vector<ChartNote>& candidate, Outcomes& outcomes)
{
    const std::expected<std::vector<ChartNote>, ChartPlanRefusal> whole =
        wholeStreamGate(chart, tempo_map, base, candidate);
    Chart based = chart;
    based.notes = base;
    const std::expected<JudgedChartPlan, ChartPlanRefusal> touched =
        finalizePlan(based, tempo_map, candidate, "Gate");
    REQUIRE(touched.has_value() == whole.has_value());
    if (!whole.has_value())
    {
        const ChartPlanRefusal& expected_refusal = whole.error();
        const ChartPlanRefusal& refusal = touched.error();
        REQUIRE(refusal.index() == expected_refusal.index());
        const auto* const expected_invalid = std::get_if<ChartPlanInvalid>(&expected_refusal);
        if (expected_invalid == nullptr)
        {
            ++outcomes.unchanged;
            return;
        }
        CHECK(std::get<ChartPlanInvalid>(refusal).reason == expected_invalid->reason);
        ++outcomes.refused;
        if (expected_invalid->reason.starts_with("the edit would cut"))
        {
            ++outcomes.statements_lost;
        }
        if (expected_invalid->reason.starts_with("notes must be sorted"))
        {
            ++outcomes.duplicates;
        }
        return;
    }
    if (!touched.has_value())
    {
        return;
    }
    CHECK(touched->plan.label == "Gate");
    Chart replayed = chart;
    replayed.notes = base;
    REQUIRE(applyChartChange(replayed, touched->plan).has_value());
    CHECK(replayed.notes == *whole);
    ++outcomes.plans;
}

// The note stream with `note` added at its slot, ahead of any note already holding it.
[[nodiscard]] std::vector<ChartNote> withNote(std::vector<ChartNote> notes, ChartNote note)
{
    const auto at = std::ranges::lower_bound(notes, note, common::core::chartNoteOrderLess);
    notes.insert(at, std::move(note));
    return notes;
}

// The note stream with the note at `index` removed.
[[nodiscard]] std::vector<ChartNote> without(std::vector<ChartNote> notes, const std::size_t index)
{
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
    return notes;
}

// The note stream with the note at `index` rewritten by `edit`, which keeps its slot.
template <typename Edit>
[[nodiscard]] std::vector<ChartNote> rewritten(
    std::vector<ChartNote> notes, const std::size_t index, const Edit& edit)
{
    edit(notes[index]);
    return notes;
}

// The position a quarter after `position` on a 4/4 grid, rolling into the next measure.
[[nodiscard]] GridPosition beatAfter(const GridPosition position)
{
    return position.beat < 4 ? GridPosition{.measure = position.measure, .beat = position.beat + 1}
                             : GridPosition{.measure = position.measure + 1, .beat = 1};
}

// Every candidate the edit script derives from a chart, each in slot order: a short and a long
// ring planted at every slot of a quarter grid (empty slots, the insides of rings, held slots),
// legal and off-the-neck frets alike; every note removed, retyped, re-struck under each attack,
// regrown and shrunk, and moved a quarter and a string; and two onsets planted at once inside each
// note's ring.
[[nodiscard]] std::vector<std::vector<ChartNote>> editScript(const Chart& chart)
{
    const std::vector<ChartNote>& notes = chart.notes;
    std::vector<std::vector<ChartNote>> candidates;
    for (int measure = 1; measure <= 7; ++measure)
    {
        for (int beat = 1; beat <= 4; ++beat)
        {
            for (int string = 1; string <= 6; ++string)
            {
                for (const Fraction sustain : {Fraction{1, 32}, Fraction{3, 2}})
                {
                    for (const int fret : {3, common::core::g_max_fret + 1})
                    {
                        candidates.push_back(withNote(
                            notes,
                            makeTestNote(
                                {.measure = measure, .beat = beat}, string, fret, sustain)));
                    }
                }
            }
        }
    }
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        candidates.push_back(without(notes, index));
        for (const int fret : {0, note.fret + 2, common::core::g_max_fret + 1})
        {
            candidates.push_back(
                rewritten(notes, index, [fret](ChartNote& edited) { edited.fret = fret; }));
        }
        for (const NoteAttack attack :
             {NoteAttack::Pick,
              NoteAttack::Legato,
              NoteAttack::Tap,
              NoteAttack::LeftTap,
              NoteAttack::Pinch})
        {
            candidates.push_back(
                rewritten(notes, index, [attack](ChartNote& edited) { edited.attack = attack; }));
        }
        for (const Fraction sustain : {Fraction{1, 32}, Fraction{3}})
        {
            candidates.push_back(rewritten(notes, index, [sustain](ChartNote& edited) {
                static_cast<void>(common::core::clipPayloadsToSustain(edited, sustain));
            }));
        }
        ChartNote later = note;
        later.position = beatAfter(note.position);
        candidates.push_back(withNote(without(notes, index), std::move(later)));
        for (const int string_step : {-1, 1})
        {
            ChartNote crossed = note;
            crossed.string += string_step;
            if (crossed.string >= 1 && crossed.string <= 6)
            {
                candidates.push_back(withNote(without(notes, index), std::move(crossed)));
            }
        }
        const GridPosition half_beat_in{
            .measure = note.position.measure,
            .beat = note.position.beat,
            .offset = Fraction{1, 2},
        };
        candidates.push_back(withNote(
            withNote(notes, makeTestNote(half_beat_in, note.string, 4)),
            makeTestNote(beatAfter(note.position), note.string, 6)));
    }
    return candidates;
}

// Every note of every fixture chart.
[[nodiscard]] std::vector<ChartNote> fixtureNotes()
{
    std::vector<ChartNote> notes;
    for (const FixtureChart& fixture : fixtureCharts())
    {
        notes.insert(notes.end(), fixture.chart.notes.begin(), fixture.chart.notes.end());
    }
    return notes;
}

} // namespace

// The gate judges only what a candidate touched and the rings its new onsets can shorten, which is
// sound only because the live chart is a fixpoint of the gate. Over every fixture chart, under a
// plain and a meter-changing map, and against both the live stream and an empty base, each
// candidate of the edit script must get exactly the whole-stream gate's answer: the same plan, or
// the same refusal with the same reason.
TEST_CASE("finalizePlan answers every edit exactly as the whole-stream gate", "[core][chart]")
{
    Outcomes outcomes;
    for (const TempoMap& tempo_map : {makeCommonTimeMap(), makeMeterChangeMap()})
    {
        for (const FixtureChart& fixture : fixtureCharts())
        {
            const Chart& chart = fixture.chart;
            // The precondition the touched-set gate stands on. A fixture chart stands for a live
            // chart, and every live chart is the gate's fixpoint (load and every edit leave one),
            // so a fixture that is not names itself here instead of testing an unreachable state.
            INFO("fixture " << fixture.name << " must be a fixpoint of the plan gate");
            const std::expected<std::vector<ChartNote>, ChartPlanRefusal> fixpoint =
                wholeStreamGate(chart, tempo_map, chart.notes, chart.notes);
            REQUIRE(
                (!fixpoint.has_value() &&
                 std::holds_alternative<ChartPlanNoChange>(fixpoint.error())));
            for (const std::vector<ChartNote>& candidate : editScript(chart))
            {
                requireSameAnswer(chart, tempo_map, chart.notes, candidate, outcomes);
                requireSameAnswer(chart, tempo_map, {}, candidate, outcomes);
            }
        }
    }
    // The script reaches every answer the gate can give.
    CHECK(outcomes.plans > 0);
    CHECK(outcomes.unchanged > 0);
    CHECK(outcomes.statements_lost > 0);
    CHECK(outcomes.duplicates > 0);
    CHECK(outcomes.refused > outcomes.statements_lost + outcomes.duplicates);
}

// The settle's Legato -> Pick retype writes the live chart without passing the gate, so it keeps
// the fixpoint only because no rule a note is judged by alone tells the two attacks apart. A rule
// that ever names one of them fails here instead of silently breaking the gate's invariant.
TEST_CASE("the Legato and Pick attacks share every validation verdict", "[core][chart]")
{
    const common::core::ChartTuning tuning = makeGateChart().tuning;
    for (const TempoMap& tempo_map : {makeCommonTimeMap(), makeMeterChangeMap()})
    {
        for (const ChartNote& note : fixtureNotes())
        {
            ChartNote picked = note;
            picked.attack = NoteAttack::Pick;
            ChartNote claimed = note;
            claimed.attack = NoteAttack::Legato;
            const std::expected<void, common::core::ChartError> picked_verdict =
                common::core::validateChartNoteAlone(
                    common::core::savedChartNote(picked), tuning, tempo_map);
            const std::expected<void, common::core::ChartError> claimed_verdict =
                common::core::validateChartNoteAlone(
                    common::core::savedChartNote(claimed), tuning, tempo_map);
            REQUIRE(picked_verdict.has_value() == claimed_verdict.has_value());
            if (!picked_verdict.has_value() && !claimed_verdict.has_value())
            {
                CHECK(picked_verdict.error().message == claimed_verdict.error().message);
            }
        }
    }
}

// dissolveSilentKeyframes writes the live chart without passing the gate, so it keeps the fixpoint
// only because the strip removes nothing but what the path already states: every fixture note,
// stripped, still validates and is still its own normal form.
TEST_CASE("stripping silent keyframes keeps every note valid and normal", "[core][chart]")
{
    const common::core::ChartTuning tuning = makeGateChart().tuning;
    const TempoMap tempo_map = makeCommonTimeMap();
    std::size_t stripped_any = 0;
    for (const ChartNote& note : fixtureNotes())
    {
        ChartNote stripped = note;
        if (common::core::stripSilentKeyframes(stripped))
        {
            ++stripped_any;
        }
        CHECK(
            common::core::validateChartNoteAlone(
                common::core::savedChartNote(stripped), tuning, tempo_map)
                .has_value());
        ChartNote normal = stripped;
        CHECK(common::core::normalizeChartNote(normal, tuning).empty());
        CHECK(normal == stripped);
    }
    // The fixtures carry a silent point, so the strip is exercised rather than vacuous.
    CHECK(stripped_any > 0);
}

} // namespace rock_hero::editor::core
