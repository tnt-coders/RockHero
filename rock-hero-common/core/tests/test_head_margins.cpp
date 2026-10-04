#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using Catch::Matchers::WithinULP;
using rock_hero::common::core::testing::anchor;

// A map every margin round trip has something to cross: a 7/8 bar between two 4/4 stretches,
// anchors at odd milliseconds on both sides of it, and a sub-beat anchor a third of a beat into
// measure 4.
[[nodiscard]] TempoMap denseMap()
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .meter = {.numerator = 4, .denominator = 4}},
         TimeSignatureChange{.measure = 3, .meter = {.numerator = 7, .denominator = 8}},
         TimeSignatureChange{.measure = 4, .meter = {.numerator = 4, .denominator = 4}}},
        {anchor("1:1", 0),
         anchor("2:3", 2777),
         anchor("3:4", 4321),
         anchor("4:2+1/3", 5913),
         anchor("9:1", 13381)},
    };
}

// One note of the dense chart.
[[nodiscard]] ChartNote denseNote(
    const GridPosition position, const int string, const int fret, const NoteAttack attack)
{
    return ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = Fraction{1, 3},
        .attack = attack,
        .bend = {},
        .keyframes = {},
    };
}

// Every beat of measures 1-8 carries a three-string chord on the beat and a legato chain on string
// 4 at its thirds, so chords share a head, a chain binds every ring, and an onset stands on each
// anchor — the sub-beat one included. One onset a sliver past the origin exercises the clamp.
[[nodiscard]] std::vector<ChartNote> denseNotes(const TempoMap& tempo_map)
{
    std::vector<ChartNote> notes;
    notes.push_back(
        denseNote(GridPosition{.measure = 1, .beat = 1, .offset = {}}, 1, 3, NoteAttack::Pick));
    notes.push_back(denseNote(
        GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 40}}, 2, 5, NoteAttack::Pick));
    for (int measure = 1; measure <= 8; ++measure)
    {
        const int beats = tempo_map.timeSignatureAt(measure).meter.numerator;
        for (int beat = 1; beat <= beats; ++beat)
        {
            if (measure > 1 || beat > 1)
            {
                for (const int string : std::array{1, 2, 3})
                {
                    notes.push_back(denseNote(
                        GridPosition{.measure = measure, .beat = beat, .offset = {}},
                        string,
                        3 + string,
                        NoteAttack::Pick));
                }
            }
            notes.push_back(denseNote(
                GridPosition{.measure = measure, .beat = beat, .offset = Fraction{1, 3}},
                4,
                5,
                NoteAttack::Pick));
            notes.push_back(denseNote(
                GridPosition{.measure = measure, .beat = beat, .offset = Fraction{2, 3}},
                4,
                7,
                NoteAttack::Legato));
        }
    }
    return notes;
}

} // namespace

// The table asks the authority once per head and must answer exactly what the authority answers:
// GridPosition and Fraction are exact, so equality here is identity, and a margin read off the
// table can never differ from one asked directly.
TEST_CASE("Head margins answer exactly what the margin authority answers", "[core][chart]")
{
    const TempoMap tempo_map = denseMap();
    const std::vector<ChartNote> notes = denseNotes(tempo_map);
    const HeadMargins margins{notes, tempo_map};

    REQUIRE(notes.size() > std::size_t{150});
    for (const ChartNote& note : notes)
    {
        CHECK(margins.before(note.position) == marginBefore(tempo_map, note.position));
        CHECK(
            margins.beats(note.position) == minimumSustainDistanceBeats(tempo_map, note.position));
    }
}

// What the scene carries per note is the authority's margin start resolved onto the clock, and the
// board composes that scene unchanged — so the board's rise is the one the margin states.
TEST_CASE("The scene carries each head's margin start for the board", "[core][chart][highway]")
{
    const TempoMap tempo_map = denseMap();
    const std::vector<ChartNote> notes = denseNotes(tempo_map);
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = notes;
    Arrangement arrangement;
    arrangement.chart = std::move(chart);

    const ChartViewState scene = makeChartViewState(arrangement, tempo_map);
    REQUIRE(scene.notes.size() == notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        CHECK_THAT(
            scene.notes[index].margin_start_seconds,
            WithinULP(tempo_map.secondsAt(marginBefore(tempo_map, notes[index].position)), 0));
    }

    const HighwayViewState board = makeHighwayViewState(scene, tempo_map, {});
    CHECK(board.chart == scene);
}

} // namespace rock_hero::common::core
