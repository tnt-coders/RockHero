#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A 4/4 default map: measure 1 beat 1 sits at zero and beats last half a second at 120 BPM.
[[nodiscard]] TempoMap makeHighwayTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// Song-level section markers passed beside the arrangement, as the callers pass Song::sections.
[[nodiscard]] std::vector<SongSection> makeHighwaySections()
{
    return {
        SongSection{.position = GridPosition{.measure = 2, .beat = 1}, .name = "verse"},
    };
}

// Mirrors the editor tab-projection fixture (chord pair, sustained slide/bend note, shape spans,
// one FHP) plus a harmonic touch position for the highway-only fields.
[[nodiscard]] Arrangement makeArrangementWithChart()
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.templates = {
        ChordTemplate{
            .name = "F5",
            .frets = {1, 3, 3, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {1, 3, 4, std::nullopt, std::nullopt, std::nullopt},
        },
    };
    chart.notes = {
        // Simultaneous pair at 2:1 under the shape span: reads as a chord box.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 1,
            .sustain = Fraction{1},
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{2},
            .bend = {BendPoint{.offset = Fraction{1}, .semitones = 2.0}},
            .slides = {SlideWaypoint{.offset = Fraction{2}, .fret = 9}},
        },
        // Natural harmonic with a between-fret touch position the highway must carry through.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 3,
            .fret = 3,
            .harmonic = NoteHarmonic::Natural,
            .touch = 3.2,
            .bend = {},
            .slides = {},
        },
    };
    chart.shapes = {
        ChartShape{
            .position = GridPosition{.measure = 2, .beat = 1},
            .sustain = Fraction{1},
            .chord = 0,
        },
        // Only one onset at 3:1+1/2, so this span reads as an arpeggio treatment.
        ChartShape{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .sustain = Fraction{2},
            .chord = 0,
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 1, .width = 4},
    };

    return Arrangement{
        .id = "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4",
        .part = Part::Lead,
        .difficulty = DifficultyRating{},
        .audio_asset = {},
        .audio_duration = TimeDuration{16.0},
        .tones = {},
        .tone_track = {},
        .tone_automation = {},
        .chart_ref = "charts/4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4.chart.json",
        .chart = std::move(chart),
    };
}

} // namespace

// The highway projection must resolve identical inputs to the identical seconds the editor's 2D
// projection produces: same tempo-map queries, same onset/sustain/payload discipline.
TEST_CASE("Highway projection resolves chart positions to seconds", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const HighwayViewState state =
        makeHighwayViewState(makeArrangementWithChart(), tempo_map, makeHighwaySections(), {});

    CHECK(state.string_count == 6);
    REQUIRE(state.notes.size() == 4);

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[1].end_seconds == Catch::Approx(state.notes[1].start_seconds));

    const HighwayNoteView& sliding = state.notes[2];
    CHECK(sliding.start_seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.end_seconds == Catch::Approx(10.5 * beat));
    REQUIRE(sliding.bend.size() == 1);
    CHECK(sliding.bend[0].seconds == Catch::Approx(9.5 * beat));
    CHECK(sliding.bend[0].semitones == Catch::Approx(2.0));
    REQUIRE(sliding.slides.size() == 1);
    CHECK(sliding.slides[0].seconds == Catch::Approx(10.5 * beat));
    CHECK(sliding.slides[0].fret == 9);

    // The between-fret harmonic touch position survives projection untouched.
    const HighwayNoteView& harmonic = state.notes[3];
    CHECK(harmonic.harmonic == NoteHarmonic::Natural);
    REQUIRE(harmonic.touch.has_value());
    if (harmonic.touch.has_value())
    {
        CHECK(*harmonic.touch == Catch::Approx(3.2));
    }

    REQUIRE(state.shapes.size() == 2);
    CHECK(state.shapes[0].name == "F5");
    CHECK_FALSE(state.shapes[0].arpeggio);
    CHECK(state.shapes[1].arpeggio);
    // Posture entries carry the template's frets and fingerings (only strings in the posture).
    REQUIRE(state.shapes[0].strings.size() == 3);
    CHECK(state.shapes[0].strings[0].string == 1);
    CHECK(state.shapes[0].strings[0].fret == 1);
    CHECK(state.shapes[0].strings[0].finger == 1);
    CHECK(state.shapes[0].strings[2].string == 3);
    CHECK(state.shapes[0].strings[2].fret == 3);
    CHECK(state.shapes[0].strings[2].finger == 4);

    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    // No slide lands on this placement, so it morphs over the 1/16-whole-note margin (a quarter
    // beat in 4/4).
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));

    REQUIRE(state.sections.size() == 1);
    CHECK(state.sections[0].seconds == Catch::Approx(4.0 * beat));
    CHECK(state.sections[0].name == "verse");
}

// The displayed-string minimum (the editor's "show at least N strings") raises the lane count and
// shifts every note and posture string into the padded range, so the shared palette anchors the
// chart's strings exactly as the 2D tab does. The game leaves it at zero (no shift).
TEST_CASE("Highway projection pads the displayed string count", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();

    // Chart has six strings; ask for eight displayed lanes → a shift of two.
    const HighwayViewState padded = makeHighwayViewState(
        makeArrangementWithChart(),
        tempo_map,
        {},
        HighwayDisplayOptions{.minimum_string_count = 8});
    CHECK(padded.string_count == 8);
    REQUIRE(padded.notes.size() == 4);
    // Chart strings 1 and 2 (the chord at measure 2) become displayed lanes 3 and 4.
    CHECK(padded.notes[0].string == 3);
    CHECK(padded.notes[1].string == 4);
    // Posture entries shift with the notes so brackets and fingering stay on the same lanes.
    REQUIRE(padded.shapes[0].strings.size() == 3);
    CHECK(padded.shapes[0].strings[0].string == 3);
    CHECK(padded.shapes[0].strings[2].string == 5);

    // A minimum at or below the chart count leaves everything unshifted.
    const HighwayViewState unshifted = makeHighwayViewState(
        makeArrangementWithChart(),
        tempo_map,
        {},
        HighwayDisplayOptions{.minimum_string_count = 4});
    CHECK(unshifted.string_count == 6);
    CHECK(unshifted.notes[0].string == 1);
}

// Ramp derivation for the hand window: a placement landing exactly on a pitched waypoint's grid
// position ramps over that glide segment (slide-locked), ordinary placements morph over the
// 1/16-whole-note margin, crowded placements shorten against the previous arrival instead of
// overlapping it, and an unpitched slide-out never slide-matches a placement.
TEST_CASE("Highway projection derives hand-window ramps", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    REQUIRE(arrangement.chart.has_value());
    Chart& chart = *arrangement.chart;
    // A sustained note whose tail trails off unpitched: its gesture must not feed a ramp.
    chart.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .slides = {},
            .slide_out = SlideOut{.offset = Fraction{1}, .fret = 12},
        });
    chart.fret_hand_positions = {
        // Ordinary move: the margin morph (a quarter beat in 4/4).
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 1, .width = 4},
        // Crowded: an eighth of a beat after the previous arrival, so the morph shortens.
        FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 8}},
            .fret = 2,
            .width = 4,
        },
        // Exactly on the fixture's pitched waypoint (3:1+1/2 advanced by its two-beat offset):
        // slide-locked to the glide segment.
        FretHandPosition{
            .position = GridPosition{.measure = 3, .beat = 3, .offset = Fraction{1, 2}},
            .fret = 6,
            .width = 4,
        },
        // Exactly where the unpitched slide-out ends (4:3 advanced one beat): still a margin
        // morph, never the slide-out's segment.
        FretHandPosition{.position = GridPosition{.measure = 4, .beat = 4}, .fret = 9, .width = 4},
    };

    const HighwayViewState state = makeHighwayViewState(arrangement, tempo_map, {}, {});
    REQUIRE(state.fret_hand_positions.size() == 4);

    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));

    CHECK(state.fret_hand_positions[1].seconds == Catch::Approx(4.125 * beat));
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(0.125 * beat));

    // The glide starts at the note onset (8.5 beats) and lands at the waypoint (10.5 beats).
    CHECK(state.fret_hand_positions[2].seconds == Catch::Approx(10.5 * beat));
    CHECK(state.fret_hand_positions[2].ramp_seconds == Catch::Approx(2.0 * beat));

    CHECK(state.fret_hand_positions[3].seconds == Catch::Approx(15.0 * beat));
    CHECK(state.fret_hand_positions[3].ramp_seconds == Catch::Approx(0.25 * beat));
}

// The beat list covers the whole song grid up to the terminal anchor with correct downbeat
// marks, so beat bars never query the tempo map at render time.
TEST_CASE("Highway projection resolves the beat grid with downbeats", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const HighwayViewState state =
        makeHighwayViewState(makeArrangementWithChart(), tempo_map, {}, {});

    const auto expected_count = static_cast<std::size_t>(tempo_map.terminalGlobalBeatIndex()) + 1;
    REQUIRE(state.beats.size() == expected_count);
    REQUIRE(state.beats.size() >= 5);

    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.beats[0].seconds == Catch::Approx(0.0));
    CHECK(state.beats[4].seconds == Catch::Approx(4.0 * beat));

    // 4/4 throughout: every fourth beat is a measure downbeat.
    for (std::size_t index = 0; index < state.beats.size(); ++index)
    {
        CHECK(state.beats[index].measure_downbeat == (index % 4 == 0));
    }
}

// Without a chart the projection returns an empty board (beat bars included: no chart, no
// board), but the song-level sections still resolve — they describe the song, not the chart.
TEST_CASE("Highway projection is empty without a chart", "[core][highway]")
{
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const HighwayViewState state =
        makeHighwayViewState(arrangement, makeHighwayTempoMap(), makeHighwaySections(), {});
    CHECK(state.string_count == 0);
    CHECK(state.notes.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
    CHECK(state.beats.empty());
    REQUIRE(state.sections.size() == 1);
    CHECK(state.sections[0].name == "verse");
}

// The lefty mirror is a pure fret-axis reflection: mirrored X is the negation of unmirrored X
// and mirroring twice is the identity. The string-order invert flips lane stacking exactly.
TEST_CASE("Highway geometry mirrors and inverts as pure reflections", "[core][highway]")
{
    const HighwayMetrics metrics{};

    CHECK(highwayFretLineX(0, metrics, false) == Catch::Approx(0.0));
    CHECK(highwayFretLineX(5, metrics, false) == Catch::Approx(5.5));
    CHECK(highwayFretLineX(5, metrics, true) == Catch::Approx(-5.5));
    CHECK(
        highwayFretLineX(5, metrics, true) == Catch::Approx(-highwayFretLineX(5, metrics, false)));
    CHECK(
        -(-highwayFretLineX(7, metrics, false)) ==
        Catch::Approx(highwayFretLineX(7, metrics, false)));

    CHECK(highwayNoteCenterX(1, metrics, false) == Catch::Approx(0.55));
    CHECK(highwayNoteCenterX(1, metrics, true) == Catch::Approx(-0.55));

    // Lanes are centered on half-string offsets above the string grid's base (0.075, the
    // chord-box frame thickness): the bottom lane sits the base plus half a string spacing off
    // the floor (0.075 + 0.175) so fret margins stay symmetric around the grid while a chord
    // box's bottom bar fills the below-grid gap.
    CHECK(highwayStringLaneY(1, 6, metrics, false) == Catch::Approx(0.25));
    CHECK(highwayStringLaneY(6, 6, metrics, false) == Catch::Approx(2.0));
    CHECK(highwayStringLaneY(1, 6, metrics, true) == Catch::Approx(2.0));
    CHECK(highwayStringLaneY(6, 6, metrics, true) == Catch::Approx(0.25));

    // Eight-string arrangements stack two more lanes above the standard six.
    CHECK(highwayStringLaneY(8, 8, metrics, false) == Catch::Approx(2.7));

    // The shared lane-to-Y seam that highwayStringLaneY delegates to.
    CHECK(highwayLaneToY(1, metrics) == Catch::Approx(0.25));
    CHECK(highwayLaneToY(6, metrics) == Catch::Approx(2.0));

    CHECK(highwayTimeToZ(1.0, 1.0, metrics) == Catch::Approx(20.0));
    CHECK(highwayTimeToZ(1.0, 2.0, metrics) == Catch::Approx(10.0));
    CHECK(highwayTimeToZ(-0.25, 1.0, metrics) == Catch::Approx(-5.0));
}

// Visible-range behavior: an early long sustain keeps its note in range, notes ending before the
// span drop out through the prefix maximum, and notes starting after the span end are excluded.
TEST_CASE("Highway visible-note range brackets a time span", "[core][highway]")
{
    std::vector<HighwayNoteView> notes;
    const auto add_note = [&notes](double start, double end) {
        HighwayNoteView note;
        note.start_seconds = start;
        note.end_seconds = end;
        notes.push_back(std::move(note));
    };
    add_note(0.0, 5.0); // Long sustain spanning most of the timeline.
    add_note(1.0, 1.2);
    add_note(2.0, 2.2);
    add_note(10.0, 11.0);

    const std::vector<double> prefix_max = makeHighwaySustainPrefixMax(notes);
    REQUIRE(prefix_max.size() == 4);
    CHECK(prefix_max[2] == Catch::Approx(5.0));

    // Span inside the long sustain: starts at the sustaining note, ends before the late note.
    const auto mid = highwayVisibleNoteRange(notes, prefix_max, 3.0, 4.0);
    CHECK(mid.first == 0);
    CHECK(mid.second == 3);

    // Span between the sustain end and the late note: empty.
    const auto gap = highwayVisibleNoteRange(notes, prefix_max, 6.0, 9.0);
    CHECK(gap.first == gap.second);

    // Span over the late note only.
    const auto late = highwayVisibleNoteRange(notes, prefix_max, 10.5, 12.0);
    CHECK(late.first == 3);
    CHECK(late.second == 4);
}

} // namespace rock_hero::common::core
