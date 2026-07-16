#include "tab/tab_projection.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;

// A 4/4 default map: measure 1 beat 1 sits at zero and beats last half a second at 120 BPM.
[[nodiscard]] common::core::TempoMap makeTempoMap()
{
    return common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
}

[[nodiscard]] common::core::Arrangement makeArrangementWithChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.templates = {
        common::core::ChordTemplate{
            .name = "F5",
            .frets = {1, 3, 3, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {1, 3, 4, std::nullopt, std::nullopt, std::nullopt},
        },
        // Held posture for the arpeggio span; string 4 is struck at the bracket start, so its
        // entry is the only sounded one.
        common::core::ChordTemplate{
            .name = "Dm7",
            .frets = {std::nullopt, 5, std::nullopt, 7, 8, std::nullopt},
            .fingers = {std::nullopt, 1, std::nullopt, 3, 4, std::nullopt},
        },
    };
    chart.notes = {
        // Simultaneous pair at 2:1 under the shape span: reads as a chord box.
        common::core::ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 1,
            .sustain = Fraction{1},
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{2},
            .bend = {common::core::BendPoint{.offset = Fraction{1}, .semitones = 2.0}},
            .slides = {common::core::SlideWaypoint{.offset = Fraction{2}, .fret = 9}},
        },
    };
    chart.shapes = {
        common::core::ChartShape{
            .position = GridPosition{.measure = 2, .beat = 1},
            .sustain = Fraction{1},
            .chord = 0,
        },
        // Only one onset at 3:1+1/2, so this span reads as an arpeggio bracket.
        common::core::ChartShape{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .sustain = Fraction{2},
            .chord = 1,
        },
    };
    chart.fret_hand_positions = {
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1}, .fret = 1, .width = 4
        },
    };
    return common::core::Arrangement{
        .id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4",
        .part = common::core::Part::Lead,
        .difficulty = common::core::DifficultyRating{},
        .audio_asset = {},
        .audio_duration = common::core::TimeDuration{16.0},
        .tones = {},
        .tone_track = {},
        .tone_automation = {},
        .chart_ref = "charts/4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4.chart.json",
        .chart = std::move(chart),
    };
}

} // namespace

TEST_CASE("Tab projection resolves chart positions to seconds", "[editor-core][tab]")
{
    const common::core::TempoMap tempo_map = makeTempoMap();
    const TabViewState state = makeTabViewState(makeArrangementWithChart(), tempo_map);

    CHECK(state.string_count == 6);
    REQUIRE(state.notes.size() == 3);

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[1].end_seconds == Catch::Approx(state.notes[1].start_seconds));

    const TabNoteView& sliding = state.notes[2];
    CHECK(sliding.start_seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.end_seconds == Catch::Approx(10.5 * beat));
    REQUIRE(sliding.bend.size() == 1);
    CHECK(sliding.bend[0].seconds == Catch::Approx(9.5 * beat));
    CHECK(sliding.bend[0].semitones == Catch::Approx(2.0));
    REQUIRE(sliding.slides.size() == 1);
    CHECK(sliding.slides[0].seconds == Catch::Approx(10.5 * beat));
    CHECK(sliding.slides[0].fret == 9);

    REQUIRE(state.shapes.size() == 2);
    CHECK(state.shapes[0].name == "F5");
    CHECK_FALSE(state.shapes[0].arpeggio);
    CHECK(state.shapes[0].arpeggio_notes.empty());
    CHECK(state.shapes[1].arpeggio);

    // The arpeggio start brackets the whole held posture; string 4 is struck right at the
    // bracket start, so its entry is sounded and the template's other two entries are not.
    REQUIRE(state.shapes[1].arpeggio_notes.size() == 3);
    CHECK(
        state.shapes[1].arpeggio_notes[0] ==
        TabArpeggioNoteView{.string = 2, .fret = 5, .sounded = false});
    CHECK(
        state.shapes[1].arpeggio_notes[1] ==
        TabArpeggioNoteView{.string = 4, .fret = 7, .sounded = true});
    CHECK(
        state.shapes[1].arpeggio_notes[2] ==
        TabArpeggioNoteView{.string = 5, .fret = 8, .sounded = false});

    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
}

TEST_CASE("Tab projection is empty without a chart", "[editor-core][tab]")
{
    common::core::Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const TabViewState state = makeTabViewState(arrangement, makeTempoMap());
    CHECK(state.string_count == 0);
    CHECK(state.notes.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
}

} // namespace rock_hero::editor::core
