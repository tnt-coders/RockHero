#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
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

// Nullable-pointer view of the arrangement's optional chart, mirroring the editor harness's
// chartOrNull: the parameter-passed optional lets clang-tidy's unchecked-optional-access track
// the guard, which it cannot do across a Catch2 REQUIRE.
[[nodiscard]] Chart* chartOrNull(Arrangement& arrangement)
{
    return arrangement.chart.has_value() ? &*arrangement.chart : nullptr;
}

// Song-level section markers passed beside the arrangement, as the callers pass Song::sections.
[[nodiscard]] std::vector<SongSection> makeHighwaySections()
{
    return {
        SongSection{.position = GridPosition{.measure = 2, .beat = 1}, .name = "verse"},
    };
}

// Mirrors the editor tab-projection fixture (chord pair, sustained slide/bend note, shape spans,
// one FHP) plus a harmonic node for the highway-only fields.
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
        // Natural harmonic with a between-fret node the highway must carry through.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 3,
            .fret = 3,
            .harmonic_node = 3.2,
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

// A chart carrying every fact the shared scene holds: a strummed chord under a hand-shape span, a
// sustained note with a bend point and a pitched glide, a natural harmonic on a fractional node
// over a capo, a palm mute, a tremolo, a vibrato, an accent, a hammer-on with the pull-off that
// releases it, an arpeggio span, two fret-hand placements, and a pick slide with a turnaround plus
// its required unpitched terminal. Each technique sits on its own note so a composition that
// dropped one could not hide behind another.
[[nodiscard]] Chart makeAgreementChart()
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    chart.templates = {
        ChordTemplate{
            .name = "G#5",
            .frets = {4, 6, 6, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {1, 3, 4, std::nullopt, std::nullopt, std::nullopt},
        },
        ChordTemplate{
            .name = "D5",
            .frets = {std::nullopt, 5, 7, 7, std::nullopt, std::nullopt},
            .fingers = {std::nullopt, 1, 3, 4, std::nullopt, std::nullopt},
        },
    };
    chart.notes = {
        // Scrape from fret 17 down to 5 and back to 12, its terminal parked exactly on the
        // sustain. Outside every span, so it cannot flip a shape to arpeggio treatment.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 6,
            .fret = 17,
            .sustain = Fraction{1},
            .attack = NoteAttack::PickSlide,
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 5}},
            .slide_out = SlideOut{.offset = Fraction{1}, .fret = 12},
        },
        // Simultaneous chord at 2:1 covering the whole span's posture: reads as a chord box.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 4,
            .sustain = Fraction{1},
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 6,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 3,
            .fret = 6,
            .bend = {},
            .slides = {},
        },
        // One technique each, in order: palm mute, tremolo, vibrato, accent.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1, 2},
            .palm_mute = true,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 3},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1, 2},
            .tremolo = true,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 4},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1},
            .vibrato = true,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 4,
            .fret = 12,
            .emphasis = NoteEmphasis::Accent,
            .bend = {},
            .slides = {},
        },
        // Both payload kinds on one tail: a bend point mid-sustain and a pitched glide landing on
        // the sustain end. Ghosted, so the fixture carries BOTH ends of the emphasis axis and the
        // comparison below cannot pass by finding one value everywhere.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{2},
            .emphasis = NoteEmphasis::Ghost,
            .bend = {BendPoint{.offset = Fraction{1}, .semitones = 2.0}},
            .slides = {SlideWaypoint{.offset = Fraction{2}, .fret = 9}},
        },
        // Both mutes on one note: the palm is down AND this string is deadened. Two independent
        // flags, so a projection that collapsed them back onto one axis would drop one of them on
        // one surface and the comparison below would catch it.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 3},
            .string = 6,
            .fret = 5,
            .palm_mute = true,
            .dead = true,
            .bend = {},
            .slides = {},
        },
        // Fret 0 is the CAPO'd open string, so this node clears the capo rather than the nut: the
        // harmonic's legality depends on the tuning both surfaces also carry.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 4},
            .string = 5,
            .fret = 0,
            .sustain = Fraction{1, 2},
            .harmonic_node = 7.02,
            .bend = {},
            .slides = {},
        },
        // Fret 5 picked, then two connection claims: the first resolves upward to a hammer-on, the
        // second back down to a pull-off. The stored claim is identical in both — the direction is
        // the resolver's answer, which is exactly what the projection has to carry.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1},
            .attack = NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1},
            .attack = NoteAttack::Legato,
            .bend = {},
            .slides = {},
        },
        // Lone onset at the second span's start: reads as an arpeggio.
        ChartNote{
            .position = GridPosition{.measure = 5, .beat = 1},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{1, 2},
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
        ChartShape{
            .position = GridPosition{.measure = 5, .beat = 1},
            .sustain = Fraction{2},
            .chord = 1,
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 4, .width = 4},
        FretHandPosition{.position = GridPosition{.measure = 5, .beat = 1}, .fret = 5, .width = 4},
    };
    return chart;
}

} // namespace

// The capo rides the projection so the board can draw the clamp and its dead zone (25-Q6).
TEST_CASE("Highway projection carries the tuning's capo", "[core][highway]")
{
    Arrangement arrangement;
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    arrangement.chart = std::move(chart);
    CHECK(makeHighwayViewState(arrangement, makeHighwayTempoMap(), {}, {}).chart.capo == 2);
}

// Absolute anchors for the board's resolution: onsets, sustain ends, and intra-note payload
// offsets against the 4/4 default map, read through the composed chart scene.
TEST_CASE("Highway projection resolves chart positions to seconds", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const HighwayViewState state =
        makeHighwayViewState(makeArrangementWithChart(), tempo_map, makeHighwaySections(), {});

    CHECK(state.chart.string_count == 6);
    REQUIRE(state.chart.notes.size() == 4);

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.chart.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.chart.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.chart.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.chart.notes[1].end_seconds == Catch::Approx(state.chart.notes[1].start_seconds));

    const NoteViewState& sliding = state.chart.notes[2];
    CHECK(sliding.start_seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.end_seconds == Catch::Approx(10.5 * beat));
    REQUIRE(sliding.bend.size() == 1);
    CHECK(sliding.bend[0].seconds == Catch::Approx(9.5 * beat));
    CHECK(sliding.bend[0].semitones == Catch::Approx(2.0));
    REQUIRE(sliding.slides.size() == 1);
    CHECK(sliding.slides[0].seconds == Catch::Approx(10.5 * beat));
    CHECK(sliding.slides[0].fret == 9);

    // The between-fret harmonic node survives projection untouched, and its presence is what
    // makes the note a harmonic now.
    const NoteViewState& harmonic = state.chart.notes[3];
    CHECK(harmonic.attack == NoteAttack::Pick);
    REQUIRE(harmonic.harmonic_node.has_value());
    if (harmonic.harmonic_node.has_value())
    {
        CHECK(*harmonic.harmonic_node == Catch::Approx(3.2));
        CHECK(nodeIsOnNeck(harmonic.attack));
    }

    REQUIRE(state.chart.shapes.size() == 2);
    CHECK(state.chart.shapes[0].name == "F5");
    CHECK_FALSE(state.chart.shapes[0].arpeggio);
    CHECK(state.chart.shapes[1].arpeggio);
    // Posture entries carry the template's frets and fingerings (only strings in the posture).
    REQUIRE(state.chart.shapes[0].strings.size() == 3);
    CHECK(state.chart.shapes[0].strings[0].string == 1);
    CHECK(state.chart.shapes[0].strings[0].fret == 1);
    CHECK(state.chart.shapes[0].strings[0].finger == 1);
    CHECK(state.chart.shapes[0].strings[2].string == 3);
    CHECK(state.chart.shapes[0].strings[2].fret == 3);
    CHECK(state.chart.shapes[0].strings[2].finger == 4);

    REQUIRE(state.chart.fret_hand_positions.size() == 1);
    CHECK(state.chart.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    // No slide lands on this placement, so it morphs over the shared minimum-sustain-distance
    // margin (1/16 whole note — a quarter beat in 4/4).
    CHECK(state.chart.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));

    REQUIRE(state.sections.size() == 1);
    CHECK(state.sections[0].seconds == Catch::Approx(4.0 * beat));
    // Upper-cased by the projection, not the renderer: the board draws every section name that way,
    // and folding the case here keeps a pure function of the chart out of the per-frame path, where
    // it was allocating and transforming a string per visible section per frame. The authored name
    // is untouched in the song, and the 2D ruler still shows it as written.
    CHECK(state.sections[0].name == "VERSE");
}

// The board draws the same chart scene the lane draws because it COMPOSES the one projection, not
// because a second projection happens to agree with it. This pins that composition: the scene
// inside the highway state is the chart projection verbatim — no lane shift, no dropped field —
// over a chart that exercises every shared fact, with the board's own structure derived beside it.
TEST_CASE("Highway composes the chart projection unchanged", "[core][highway][chart]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const Chart chart = makeAgreementChart();
    // A fixture that rotted into an illegal chart would pin a scene no document can contain, so
    // its legality is a precondition.
    REQUIRE(validateChartRules(chart, tempo_map).has_value());

    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = chart;
    const ChartViewState scene = makeChartViewState(arrangement, tempo_map);
    // A display minimum wider than the chart, to prove the scene is never padded in the projection:
    // the renderer maps chart strings onto displayed lanes per frame.
    const HighwayViewState board = makeHighwayViewState(
        arrangement, tempo_map, {}, HighwayDisplayOptions{.minimum_string_count = 8});

    CHECK(board.chart == scene);
    CHECK(board.options.minimum_string_count == 8);

    // Non-vacuity: the equality above would pass just as happily over an empty scene, so every
    // technique the fixture carries has to be present in what was projected.
    REQUIRE(scene.notes.size() == chart.notes.size());
    REQUIRE(scene.display_hold_ends.size() == scene.notes.size());
    CHECK(scene.capo == 2);
    const auto any_note = [&scene](const auto& carries) {
        return std::ranges::any_of(scene.notes, carries);
    };
    CHECK(any_note([](const NoteViewState& note) { return note.palm_mute && note.dead; }));
    CHECK(any_note([](const NoteViewState& note) { return note.tremolo; }));
    CHECK(any_note([](const NoteViewState& note) { return note.vibrato; }));
    CHECK(
        any_note([](const NoteViewState& note) { return note.emphasis == NoteEmphasis::Accent; }));
    CHECK(any_note([](const NoteViewState& note) { return note.emphasis == NoteEmphasis::Ghost; }));
    CHECK(any_note([](const NoteViewState& note) { return note.harmonic_node.has_value(); }));
    CHECK(any_note([](const NoteViewState& note) { return !note.bend.empty(); }));
    CHECK(any_note([](const NoteViewState& note) { return note.attack == NoteAttack::PickSlide; }));
    CHECK(any_note([](const NoteViewState& note) { return note.legato == LegatoMotion::Hammer; }));
    CHECK(any_note([](const NoteViewState& note) { return note.legato == LegatoMotion::Pull; }));
    REQUIRE(scene.shapes.size() == 2);
    CHECK_FALSE(scene.shapes[0].arpeggio);
    CHECK(scene.shapes[1].arpeggio);
    CHECK(scene.shapes[1].strings.size() == 3);
    CHECK(scene.fret_hand_positions.size() == 2);

    // The continuation rule is a READ of the scene, shared by construction: the scrape's turnaround
    // continues the gesture, its terminal is where the pick leaves.
    const NoteViewState& scrape = scene.notes.front();
    REQUIRE(scrape.attack == NoteAttack::PickSlide);
    REQUIRE(scrape.slides.size() == 2);
    CHECK(linkedWaypoint(scrape, scrape.slides[0]));
    CHECK_FALSE(linkedWaypoint(scrape, scrape.slides[1]));

    // Board-only structure with no 2D counterpart — beat bars, camera framing zones, and the
    // picking-hand light the scrape drives — derived beside the scene, never inside it.
    CHECK_FALSE(board.beats.empty());
    CHECK_FALSE(board.camera_zone_starts.empty());
    CHECK(board.tap_onsets.size() == 1);
}

// The displayed-string minimum (the editor's "show at least N strings") is a lane-mapping question
// both surfaces answer per frame through the same two functions; the padding lanes sit below the
// chart's strings, so a chart string lands `extra_lanes` higher than its unpadded lane.
TEST_CASE("Displayed lanes pad below the chart's strings", "[core][highway][tab]")
{
    // Six chart strings shown in eight lanes: a shift of two.
    CHECK(displayedStringCount(6, 8) == 8);
    CHECK(displayedLane(1, 8 - 6) == 3);
    CHECK(displayedLane(6, 8 - 6) == 8);
    // A minimum at or below the chart count adds no lanes and shifts nothing.
    CHECK(displayedStringCount(6, 4) == 6);
    CHECK(displayedLane(1, 6 - 6) == 1);
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

// Camera framing zones quantize the camera's scan window: note-bearing measure runs split
// every two measures aligned to downbeats, empty runs collapse into one zone however long, and
// a section start forces a new zone (the derivation a standard automatic phrase generator uses).
TEST_CASE("Highway projection derives camera framing zones", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();

    // Fixture chart: measure 1 is empty, notes span measures 2-4, the tail is empty, and the
    // "verse" section starts at measure 2. Expect the empty intro zone, the section cut (also
    // the empty-to-notes transition) at 2.0 s, the two-measure split at 6.0 s, the
    // notes-to-empty transition at 8.0 s, and the whole empty tail merged into that zone.
    const HighwayViewState state =
        makeHighwayViewState(makeArrangementWithChart(), tempo_map, makeHighwaySections(), {});
    REQUIRE(state.camera_zone_starts.size() == 4);
    CHECK(state.camera_zone_starts[0] == Catch::Approx(0.0));
    CHECK(state.camera_zone_starts[1] == Catch::Approx(2.0));
    CHECK(state.camera_zone_starts[2] == Catch::Approx(6.0));
    CHECK(state.camera_zone_starts[3] == Catch::Approx(8.0));

    // A continuous run of note-bearing measures (1-6) splits every two measures: zones at
    // measures 1, 3, and 5, then the empty-tail transition at measure 7.
    Arrangement dense = makeArrangementWithChart();
    Chart* const chart = chartOrNull(dense);
    REQUIRE(chart != nullptr);
    chart->notes.clear();
    chart->shapes.clear();
    chart->fret_hand_positions.clear();
    for (int measure = 1; measure <= 6; ++measure)
    {
        chart->notes.push_back(
            ChartNote{
                .position = GridPosition{.measure = measure, .beat = 1},
                .string = 1,
                .fret = 5,
                .bend = {},
                .slides = {},
            });
    }
    const HighwayViewState dense_state = makeHighwayViewState(dense, tempo_map, {}, {});
    REQUIRE(dense_state.camera_zone_starts.size() == 4);
    CHECK(dense_state.camera_zone_starts[0] == Catch::Approx(0.0));
    CHECK(dense_state.camera_zone_starts[1] == Catch::Approx(4.0));
    CHECK(dense_state.camera_zone_starts[2] == Catch::Approx(8.0));
    CHECK(dense_state.camera_zone_starts[3] == Catch::Approx(12.0));
}

// Without a chart the projection returns an empty board (beat bars included: no chart, no
// board), but the song-level sections still resolve — they describe the song, not the chart.
TEST_CASE("Highway projection is empty without a chart", "[core][highway]")
{
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const HighwayViewState state =
        makeHighwayViewState(arrangement, makeHighwayTempoMap(), makeHighwaySections(), {});
    CHECK(state.chart.string_count == 0);
    CHECK(state.chart.notes.empty());
    CHECK(state.chart.shapes.empty());
    CHECK(state.chart.fret_hand_positions.empty());
    CHECK(state.beats.empty());
    CHECK(state.camera_zone_starts.empty());
    REQUIRE(state.sections.size() == 1);
    // Sections are song-level, so they survive a chartless arrangement — and arrive board-ready.
    CHECK(state.sections[0].name == "VERSE");
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

    // Lanes are centered on half-string offsets above the string grid's base (0.075, which the
    // renderer also reads as the chord-box frame thickness): the bottom lane sits the base plus
    // half a string spacing off the floor (0.075 + 0.175) so fret margins stay symmetric around
    // the grid while a chord box's bottom bar fills the below-grid gap.
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
    std::vector<NoteViewState> notes;
    const auto add_note = [&notes](double start, double end) {
        NoteViewState note;
        note.start_seconds = start;
        note.end_seconds = end;
        notes.push_back(std::move(note));
    };
    add_note(0.0, 5.0); // Long sustain spanning most of the timeline.
    add_note(1.0, 1.2);
    add_note(2.0, 2.2);
    add_note(10.0, 11.0);

    const std::vector<double> prefix_max =
        makeSustainPrefixMax(notes | std::views::transform(&NoteViewState::end_seconds));
    REQUIRE(prefix_max.size() == 4);
    CHECK(prefix_max[2] == Catch::Approx(5.0));

    // Span inside the long sustain: starts at the sustaining note, ends before the late note.
    const auto mid = visibleEventRange(notes, prefix_max, 3.0, 4.0);
    CHECK(mid.first == 0);
    CHECK(mid.second == 3);

    // Span between the sustain end and the late note: empty.
    const auto gap = visibleEventRange(notes, prefix_max, 6.0, 9.0);
    CHECK(gap.first == gap.second);

    // Span over the late note only.
    const auto late = visibleEventRange(notes, prefix_max, 10.5, 12.0);
    CHECK(late.first == 3);
    CHECK(late.second == 4);
}

// Node-series rules: a repeat of the same node extends the run, a fretting-hand non-natural
// breaks it, a picking-hand onset is invisible to it, and a re-established node starts a new
// series. The maker moved here from the renderer's per-frame path, so this pins the behavior
// the floor labels and the dotted-fret suppression read.
TEST_CASE("Highway node series derive from the note stream", "[core][highway]")
{
    std::vector<NoteViewState> notes;
    const auto add_note =
        [&notes](double start, std::optional<double> node, NoteAttack attack, int fret) {
            NoteViewState note;
            note.start_seconds = start;
            note.end_seconds = start + 0.1;
            note.harmonic_node = node;
            note.attack = attack;
            note.fret = fret;
            notes.push_back(std::move(note));
        };
    add_note(1.0, 12.0, NoteAttack::Pick, 0); // Establishes node 12.
    add_note(2.0, 12.0, NoteAttack::Pick, 0); // Repeat: extends the run, states nothing new.
    add_note(2.5, 7.0, NoteAttack::Tap, 0);   // Picking-hand onset: invisible, run unbroken.
    add_note(3.0, 12.0, NoteAttack::Pick, 0); // Still the established node: extends again.
    add_note(4.0, std::nullopt, NoteAttack::Pick, 5); // Fretted non-natural: the hand leaves.
    add_note(5.0, 12.0, NoteAttack::Pick, 0);         // Same node after a break: a NEW statement.

    const std::vector<HighwayNodeSeries> series = makeHighwayNodeSeries(notes);
    REQUIRE(series.size() == 2);
    CHECK(series[0].fret == 12); // The fret CONTAINING the node (the ceil law).
    CHECK(series[0].node == Catch::Approx(12.0));
    CHECK(series[0].begin_seconds == Catch::Approx(1.0));
    CHECK(series[0].end_seconds == Catch::Approx(3.0));
    CHECK(series[1].begin_seconds == Catch::Approx(5.0));
    CHECK(series[1].end_seconds == Catch::Approx(5.0));
}

// The projection RESOLVES the span-hold rule into seconds rather than restating it: the rule's own
// case matrix is pinned in beats beside chartEffectiveSustains, and what matters here is that the
// resolution lands on the right second and that the result feeds the visible range. Both used to be
// computed twice, and both copies carried the same defect.
TEST_CASE("Highway display hold ends resolve the effective sustains", "[core][highway]")
{
    const TempoMap map = makeHighwayTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // One span covering global beats 0 through 8, which at the default 120 BPM is 0.0 to 4.0
    // seconds.
    chart.shapes = {
        ChartShape{.position = GridPosition{.measure = 1, .beat = 1}, .sustain = Fraction{8}},
    };
    const auto strum_note = [](int string) {
        return ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = string,
            .fret = 5,
            .bend = {},
            .slides = {},
        };
    };
    // A sustainless pair at global beat 4 (2.0 seconds), inside the span.
    chart.notes = {strum_note(1), strum_note(2)};

    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    const HighwayViewState state =
        makeHighwayViewState(arrangement, map, {}, HighwayDisplayOptions{});

    REQUIRE(state.chart.display_hold_ends.size() == state.chart.notes.size());
    REQUIRE(state.chart.notes.size() == 2);
    // Struck at 2.0 seconds with no sustain of their own, so both heads stay pinned until the span
    // ends at 4.0 seconds.
    CHECK(state.chart.notes[0].end_seconds == Catch::Approx(2.0));
    CHECK(state.chart.display_hold_ends[0] == Catch::Approx(4.0));
    CHECK(state.chart.display_hold_ends[1] == Catch::Approx(4.0));

    // W9-A: the 2D lane resolves the same rule from the same authority, so one chart's tails end at
    // the same second on both surfaces. The lane drew bare heads with zero-width tails here until
    // the two were unified.
    const ChartViewState lane = makeChartViewState(arrangement, map);
    REQUIRE(lane.display_hold_ends.size() == lane.notes.size());
    REQUIRE(lane.notes.size() == state.chart.notes.size());
    for (std::size_t index = 0; index < lane.notes.size(); ++index)
    {
        CAPTURE(index);
        CHECK_THAT(
            lane.display_hold_ends[index],
            Catch::Matchers::WithinULP(state.chart.display_hold_ends[index], 0));
    }

    // Which is what keeps a span-held strum inside the visible range for as long as it is drawn.
    const std::vector<double> prefix_max = makeSustainPrefixMax(state.chart.display_hold_ends);
    REQUIRE(prefix_max.size() == 2);
    CHECK(prefix_max[1] == Catch::Approx(4.0));
    const auto visible = visibleEventRange(state.chart.notes, prefix_max, 3.5, 3.9);
    CHECK(visible.first == 0);
    CHECK(visible.second == 2);
}

// Tapping-hand onsets (right-hand-tap-lighting plan): one derived entry per onset group that
// contains tapped notes, carrying the taps' fret extent and count. Non-tap notes sharing the
// onset contribute nothing, tap-free onsets derive no entry, and simultaneity follows the
// shared onset epsilon.
TEST_CASE("Highway tap onsets derive from tapped notes only", "[core][highway]")
{
    std::vector<NoteViewState> notes;
    const auto add_note = [&notes](double start, int fret, NoteAttack attack = NoteAttack::Pick) {
        NoteViewState note;
        note.start_seconds = start;
        note.end_seconds = start;
        note.fret = fret;
        note.attack = attack;
        notes.push_back(std::move(note));
    };
    add_note(0.0, 3);                   // Plain fretted onset: no entry.
    add_note(1.0, 12, NoteAttack::Tap); // Lone tap.
    add_note(2.0, 5); // Fretted note under a simultaneous tap: only the tap counts.
    add_note(2.0, 14, NoteAttack::Tap);
    // A tapped chord: its members share a grid position and so share a second exactly. The last one
    // is offset by a picosecond, which is the only kind of difference the tolerance is for — pure
    // arithmetic noise, orders below any grid the editor offers.
    add_note(3.0, 15, NoteAttack::Tap);
    add_note(3.0, 12, NoteAttack::Tap);
    add_note(3.000000000001, 17, NoteAttack::Tap);
    add_note(4.0, 9, NoteAttack::LeftTap); // The FRETTING hand's tap: no entry.

    const std::vector<HighwayTapOnsetView> onsets =
        makeHighwayTapOnsets(notes, std::vector<double>(notes.size(), 0.0));
    REQUIRE(onsets.size() == 3);
    CHECK(
        onsets[0] == HighwayTapOnsetView{
                         .seconds = 1.0,
                         .fret_low = 12,
                         .fret_high = 12,
                         .count = 1,
                         .path = {HighwayTapLightStation{
                             .seconds = 1.0, .fret_low = 12.0, .fret_high = 12.0, .unpitched = false
                         }},
                     });
    CHECK(
        onsets[1] == HighwayTapOnsetView{
                         .seconds = 2.0,
                         .fret_low = 14,
                         .fret_high = 14,
                         .count = 1,
                         .path = {HighwayTapLightStation{
                             .seconds = 2.0, .fret_low = 14.0, .fret_high = 14.0, .unpitched = false
                         }},
                     });
    CHECK(
        onsets[2] == HighwayTapOnsetView{
                         .seconds = 3.0,
                         .fret_low = 12,
                         .fret_high = 17,
                         .count = 3,
                         .path = {HighwayTapLightStation{
                             .seconds = 3.0, .fret_low = 12.0, .fret_high = 17.0, .unpitched = false
                         }},
                     });
}

// A tap harmonic lights the NODE it strikes, even on an open string. E4 accepts a tap that strikes
// a node in place of a fret, and the tapping hand really does land on the node — so judging the
// light by `fret` dropped it entirely from a legal, matrix-listed note: the same tap one fret
// higher lit normally while the open-string one lit nowhere.
TEST_CASE("Highway tap onsets light an open-string tap harmonic at its node", "[core][highway]")
{
    NoteViewState tap;
    tap.start_seconds = 1.0;
    tap.end_seconds = 1.0;
    tap.string = 3;
    tap.fret = 0;
    tap.attack = NoteAttack::Tap;
    tap.harmonic_node = 12.0;

    const std::vector<HighwayTapOnsetView> onsets =
        makeHighwayTapOnsets({tap}, std::vector<double>(1, 0.0));
    REQUIRE(onsets.size() == 1);
    CHECK(onsets.front().count == 1);
    CHECK(onsets.front().fret_low == 12);
    CHECK(onsets.front().fret_high == 12);
    // The path station reads the same sounding place through the light's own interpolation, so it
    // has to agree exactly (compared through the ordering query, which the project uses for an
    // exact floating compare that -Wfloat-equal accepts).
    REQUIRE_FALSE(onsets.front().path.empty());
    CHECK(std::is_eq(onsets.front().path.front().fret_low <=> 12.0));

    // An ordinary open string with no node still has nowhere to light, so the guard still holds
    // where it was meant to.
    NoteViewState open_tap = tap;
    open_tap.harmonic_node.reset();
    CHECK(makeHighwayTapOnsets({open_tap}, std::vector<double>(1, 0.0)).empty());
}

// A tap's light path follows sustained contact and pitched glides: a held tap keeps its light on
// through the sustain, a tapped slide adds a station per pitched waypoint so the light morphs
// with the glide, and an unpitched trail-off releases the light from the last pitched station.
TEST_CASE("Highway tap onsets carry the light path through glides", "[core][highway]")
{
    std::vector<NoteViewState> notes;

    // Held tap: sounding from 1.0 to 2.0 at fret 12, no glide.
    NoteViewState held;
    held.start_seconds = 1.0;
    held.end_seconds = 2.0;
    held.fret = 12;
    held.attack = NoteAttack::Tap;
    notes.push_back(held);

    // Tapped slide: fret 12 at 3.0 gliding to fret 15 at 4.0 (the sustain end).
    NoteViewState sliding;
    sliding.start_seconds = 3.0;
    sliding.end_seconds = 4.0;
    sliding.fret = 12;
    sliding.attack = NoteAttack::Tap;
    sliding.slides = {SlideViewState{.seconds = 4.0, .fret = 15, .unpitched = false}};
    notes.push_back(sliding);

    // Tapped slide with an unpitched trail-off: the pitched glide ends at 6.0; the trail to 6.5
    // is already releasing pressure, so the light must not follow it.
    NoteViewState trailing;
    trailing.start_seconds = 5.0;
    trailing.end_seconds = 6.5;
    trailing.fret = 10;
    trailing.attack = NoteAttack::Tap;
    trailing.slides = {
        SlideViewState{.seconds = 6.0, .fret = 13, .unpitched = false},
        SlideViewState{.seconds = 6.5, .fret = 8, .unpitched = true},
    };
    notes.push_back(trailing);

    const std::vector<HighwayTapOnsetView> onsets =
        makeHighwayTapOnsets(notes, std::vector<double>(notes.size(), 0.0));
    REQUIRE(onsets.size() == 3);

    REQUIRE(onsets[0].path.size() == 2);
    CHECK(
        onsets[0].path[0] ==
        HighwayTapLightStation{
            .seconds = 1.0, .fret_low = 12.0, .fret_high = 12.0, .unpitched = false
        });
    CHECK(
        onsets[0].path[1] ==
        HighwayTapLightStation{
            .seconds = 2.0, .fret_low = 12.0, .fret_high = 12.0, .unpitched = false
        });

    REQUIRE(onsets[1].path.size() == 2);
    CHECK(
        onsets[1].path[0] ==
        HighwayTapLightStation{
            .seconds = 3.0, .fret_low = 12.0, .fret_high = 12.0, .unpitched = false
        });
    CHECK(
        onsets[1].path[1] ==
        HighwayTapLightStation{
            .seconds = 4.0, .fret_low = 15.0, .fret_high = 15.0, .unpitched = false
        });

    REQUIRE(onsets[2].path.size() == 2);
    CHECK(
        onsets[2].path[0] ==
        HighwayTapLightStation{
            .seconds = 5.0, .fret_low = 10.0, .fret_high = 10.0, .unpitched = false
        });
    CHECK(
        onsets[2].path[1] ==
        HighwayTapLightStation{
            .seconds = 6.0, .fret_low = 13.0, .fret_high = 13.0, .unpitched = false
        });
}

// The light-rise ramp mirrors the fret-hand arrival rule: an onset takes its widest member's
// margin rise, and crowding clamps the rise so it never reaches backward past the previous tap
// onset's release — a dense run keeps its per-tap dips.
TEST_CASE("Highway tap onsets clamp light ramps against the previous release", "[core][highway]")
{
    std::vector<NoteViewState> notes;
    const auto add_tap = [&notes](double start, double end, int fret) {
        NoteViewState note;
        note.start_seconds = start;
        note.end_seconds = end;
        note.fret = fret;
        note.attack = NoteAttack::Tap;
        notes.push_back(std::move(note));
    };
    add_tap(1.0, 1.0, 12); // Roomy: keeps its full margin rise.
    add_tap(1.0, 1.0, 15); // Chord mate with a wider margin: the onset takes it.
    add_tap(1.2, 1.2, 14); // Crowded: only 0.2s of room after the previous release.
    add_tap(3.0, 3.5, 12); // Held tap whose release bounds the next rise.
    add_tap(3.6, 3.6, 15); // Rise clamps to 0.1s — the gap after the hold, not the onset gap.

    const std::vector<double> rises{0.2, 0.25, 0.25, 0.25, 0.25};
    const std::vector<HighwayTapOnsetView> onsets = makeHighwayTapOnsets(notes, rises);
    REQUIRE(onsets.size() == 4);
    CHECK(onsets[0].ramp_seconds == Catch::Approx(0.25));
    CHECK(onsets[1].ramp_seconds == Catch::Approx(0.2));
    CHECK(onsets[2].ramp_seconds == Catch::Approx(0.25));
    CHECK(onsets[3].ramp_seconds == Catch::Approx(0.1));
}

// The pick-slide seam: latents suppressed, the path unpitched, and the hand window's
// slide-locked ramps never tie to a scrape leg — an FHP sitting exactly on a scrape waypoint
// still gets the ordinary margin morph. The scrape DOES drive the moving right-hand light
// (asserted below) while contributing nothing to the hand window.
TEST_CASE("Highway projection suppresses pick-slide latents", "[core][highway]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    ChartNote scrape{
        .position = GridPosition{.measure = 1, .beat = 1},
        .string = 5,
        .fret = 17,
        .sustain = Fraction{1},
        .attack = NoteAttack::PickSlide,
        .bend = {BendPoint{.offset = Fraction{1, 4}, .semitones = 1.0}},
        .slides = {SlideWaypoint{.offset = Fraction{1, 2}, .fret = 3}},
        .slide_out = SlideOut{.offset = Fraction{1}, .fret = 9},
    };
    scrape.palm_mute = true;
    scrape.dead = true;
    scrape.tremolo = true;
    scrape.vibrato = true;
    chart.notes = {scrape};
    chart.fret_hand_positions = {
        FretHandPosition{
            .position = GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}},
            .fret = 3,
        },
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const HighwayViewState state = makeHighwayViewState(arrangement, makeHighwayTempoMap(), {}, {});
    REQUIRE(state.chart.notes.size() == 1);
    const NoteViewState& view = state.chart.notes.front();
    CHECK(view.attack == NoteAttack::PickSlide);
    CHECK_FALSE(view.palm_mute);
    CHECK_FALSE(view.dead);
    CHECK_FALSE(view.tremolo);
    CHECK_FALSE(view.vibrato);
    CHECK(view.bend.empty());
    REQUIRE(view.slides.size() == 2);
    CHECK(view.slides[0].unpitched);
    CHECK(view.slides[1].unpitched);
    // The right-hand light rides the scrape: one onset whose path stations follow the
    // traveled waypoints (17 at the onset, 3 at the reversal, 9 at the end).
    REQUIRE(state.tap_onsets.size() == 1);
    const HighwayTapOnsetView& light = state.tap_onsets.front();
    CHECK(light.fret_low == 17);
    CHECK(light.count == 1);
    REQUIRE(light.path.size() == 3);
    CHECK(light.path[0].fret_low == Catch::Approx(17.0));
    CHECK(light.path[1].fret_low == Catch::Approx(3.0));
    CHECK(light.path[2].fret_low == Catch::Approx(9.0));
    // Waypoint stations carry the unpitched flag so the light sweeps with the scrape's own
    // ease; the onset station arrives from no glide.
    CHECK_FALSE(light.path[0].unpitched);
    CHECK(light.path[1].unpitched);
    CHECK(light.path[2].unpitched);
    // The FHP on the waypoint's grid position ramps by the quarter-beat margin morph (0.125s at
    // the default tempo), not by the scrape leg's span back to the onset (which would be 0.25s).
    REQUIRE(state.chart.fret_hand_positions.size() == 1);
    CHECK(state.chart.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.125));
}

namespace
{

// A sustainless note for chord-group cases; onset equals end so nothing reads as held.
[[nodiscard]] NoteViewState chordNote(
    const double onset, const int string, const int fret,
    const NoteAttack attack = NoteAttack::Pick)
{
    NoteViewState note;
    note.start_seconds = onset;
    note.end_seconds = onset;
    note.string = string;
    note.fret = fret;
    note.attack = attack;
    return note;
}

// The two mutes as composable marks rather than parameters, so a row of chord members reads as the
// music it describes instead of as a row of bare booleans — and the both-muted member, which is
// the whole point of two independent flags, is just the two marks applied together.
[[nodiscard]] NoteViewState palmMuted(NoteViewState note)
{
    note.palm_mute = true;
    return note;
}

[[nodiscard]] NoteViewState deadened(NoteViewState note)
{
    note.dead = true;
    return note;
}

// A strummed-shape span holding the given posture, entries ascending by string.
[[nodiscard]] ShapeViewState chordShape(
    const double start, const double end, const std::vector<std::pair<int, int>>& posture)
{
    ShapeViewState shape;
    shape.start_seconds = start;
    shape.end_seconds = end;
    shape.arpeggio = false;
    for (const auto& [string, fret] : posture)
    {
        shape.strings.push_back(ShapeStringViewState{.string = string, .fret = fret});
    }
    return shape;
}

} // namespace

// A tapped harmonic's light rides the NODE path: each waypoint station asks the drawn sounding
// position exactly like the onset seed, so a glide from fret 5 to 9 under a node at 17 walks the
// light 17 -> 21 — never 17 -> 9, the stop path the head does not draw.
TEST_CASE("Highway tap light glides a tapped harmonic along its node", "[core][highway]")
{
    NoteViewState note;
    note.start_seconds = 1.0;
    note.end_seconds = 2.0;
    note.string = 1;
    note.fret = 5;
    note.attack = NoteAttack::Tap;
    note.harmonic_node = 17.0;
    note.slides = {SlideViewState{.seconds = 2.0, .fret = 9, .unpitched = false}};

    const std::vector<NoteViewState> notes{note};
    const std::vector<HighwayTapOnsetView> onsets = makeHighwayTapOnsets(notes, {0.0});

    REQUIRE(onsets.size() == 1);
    REQUIRE_FALSE(onsets[0].path.empty());
    CHECK(onsets[0].path.front().fret_low == Catch::Approx(17.0));
    CHECK(onsets[0].path.back().fret_low == Catch::Approx(21.0));
}

// Membership and the per-group facts: contiguous same-onset notes form one group, right-hand
// onsets stay out of the fretting-hand count, a mute only part of the strum carries reaches no
// commonality, and each note indexes its own group.
TEST_CASE("Highway chord groups classify membership and mutes", "[core][highway]")
{
    std::vector<NoteViewState> notes{
        chordNote(1.0, 1, 3),
        palmMuted(chordNote(1.0, 2, 5)),
        chordNote(1.0, 3, 5, NoteAttack::Tap),
        chordNote(2.0, 1, 3),
    };
    notes[0].emphasis = NoteEmphasis::Accent;

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, {});

    REQUIRE(grouping.groups.size() == 2);
    REQUIRE(grouping.note_group.size() == notes.size());
    const HighwayChordGroupView& strum = grouping.groups[0];
    CHECK(strum.first == 0);
    CHECK(strum.count == 3);
    CHECK(strum.fretting_hand_count == 2);
    // One accented member makes the strum accented; the other two are normal.
    CHECK(strum.emphasis == NoteEmphasis::Accent);
    CHECK_FALSE(strum.all_palm_muted);
    CHECK_FALSE(strum.all_dead);
    CHECK_FALSE(strum.box_only);
    CHECK(grouping.note_group == std::vector<std::size_t>({0, 0, 0, 1}));
    CHECK(grouping.groups[1].count == 1);
}

// Two independent flags fold into two independent unanimities, and the group can be unanimous in
// one while split in the other. Each commonality means "every member carries this flag", so one
// dead string inside a palm-muted chord leaves the dead unanimity FALSE and the box goes on
// reading as the palm mute it is; a strum unanimous in both is unanimous in both, and the box
// wears both marks stacked rather than picking one.
TEST_CASE("Highway chord groups fold the two mutes independently", "[core][highway]")
{
    const auto mutesOf = [](std::vector<NoteViewState> notes) {
        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, {});
        REQUIRE(grouping.groups.size() == 1);
        const HighwayChordGroupView& group = grouping.groups.front();
        return std::pair{group.all_palm_muted, group.all_dead};
    };

    // Unanimously palm muted, unanimously dead, and unanimously both.
    CHECK(
        mutesOf({palmMuted(chordNote(1.0, 1, 3)), palmMuted(chordNote(1.0, 2, 5))}) ==
        std::pair{true, false});
    CHECK(
        mutesOf({deadened(chordNote(1.0, 1, 3)), deadened(chordNote(1.0, 2, 5))}) ==
        std::pair{false, true});
    CHECK(
        mutesOf(
            {palmMuted(deadened(chordNote(1.0, 1, 3))),
             palmMuted(deadened(chordNote(1.0, 2, 5)))}) == std::pair{true, true});

    // A dead string inside a palm-muted chord: every member is palmed, only one is dead. The palm
    // unanimity survives the split the old single mute axis would have collapsed to nothing.
    CHECK(
        mutesOf({palmMuted(chordNote(1.0, 1, 3)), palmMuted(deadened(chordNote(1.0, 2, 5)))}) ==
        std::pair{true, false});

    // And a strum every member of which is dead while only one is palmed stays a dead strum.
    CHECK(
        mutesOf({deadened(chordNote(1.0, 1, 3)), palmMuted(deadened(chordNote(1.0, 2, 5)))}) ==
        std::pair{false, true});
}

// The group's emphasis is what a box STANDING IN for the heads states, so the two folds differ on
// purpose: loud is existential, quiet unanimous. A strum is not played softly while part of it is
// struck normally, and one accent among ghosts still makes the strum an accented one.
TEST_CASE("Highway chord groups fold emphasis loud-wins, quiet-unanimous", "[core][highway]")
{
    const auto groupEmphasis = [](const std::vector<NoteEmphasis>& members) {
        std::vector<NoteViewState> notes;
        notes.reserve(members.size());
        for (std::size_t index = 0; index < members.size(); ++index)
        {
            notes.push_back(chordNote(1.0, static_cast<int>(index) + 1, 3));
            notes.back().emphasis = members[index];
        }
        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, {});
        REQUIRE(grouping.groups.size() == 1);
        return grouping.groups.front().emphasis;
    };

    using enum NoteEmphasis;
    CHECK(groupEmphasis({Normal, Normal}) == Normal);
    CHECK(groupEmphasis({Ghost, Ghost, Ghost}) == Ghost);
    // A part-ghosted strum is not a quiet strum.
    CHECK(groupEmphasis({Ghost, Normal}) == Normal);
    CHECK(groupEmphasis({Accent, Normal}) == Accent);
    // Loud outranks quiet the way it does on a single note carrying both claims.
    CHECK(groupEmphasis({Accent, Ghost}) == Accent);
}

// The repeat chain (Charter's chord visibility rules): under a covering shape, a strum that
// restates the posture of an earlier non-muted run renders as the repeat box alone. Two recorded
// regressions are pinned here because they used to live untestable inside the renderer: the
// chain's first strum sitting a rounding epsilon BELOW the shape start must still anchor the walk
// (the classic repeat-box flicker), and a strum at the shape's END is still under the span (a
// strict comparison once dropped the last strum from repeat treatment).
TEST_CASE("Highway chord groups give repeating strums the box treatment", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
    const double epsilon_below = 1.0 - (g_onset_match_epsilon / 2.0);
    std::vector<NoteViewState> notes{
        chordNote(epsilon_below, 1, 3),
        chordNote(epsilon_below, 2, 5),
        chordNote(2.0, 1, 3),
        chordNote(2.0, 2, 5),
        chordNote(3.0, 1, 3),
        chordNote(3.0, 2, 5),
    };

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

    REQUIRE(grouping.groups.size() == 3);
    // The chain's first strum shows its notes; the restatements — including the one exactly at
    // the shape's end — are boxes.
    CHECK_FALSE(grouping.groups[0].box_only);
    CHECK(grouping.groups[1].box_only);
    CHECK(grouping.groups[2].box_only);
}

// The repeat rule asks what is DRAWN, not what is stored. Inside the connection family the mark is
// the RESOLVED motion, so a claim nothing justifies carries no mark and must not hold the box off —
// it is pixel-identical to the plain pick beside it. A claim that resolves carries its triangle,
// and a marked chord always shows its notes.
TEST_CASE("Highway chord groups judge repeat marks by the resolved motion", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
    const auto grouped = [&](const LegatoMotion motion) {
        std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            chordNote(2.0, 1, 3),
            chordNote(2.0, 2, 5),
        };
        // The repeat candidate carries a STORED claim in both runs; only its resolution differs.
        notes[2].attack = NoteAttack::Legato;
        notes[2].legato = motion;
        return makeHighwayChordGroups(notes, shapes);
    };

    const HighwayChordGrouping broken = grouped(LegatoMotion::Unjustified);
    REQUIRE(broken.groups.size() == 2);
    CHECK(broken.groups[1].box_only);

    const HighwayChordGrouping resolved = grouped(LegatoMotion::Hammer);
    REQUIRE(resolved.groups.size() == 2);
    CHECK_FALSE(resolved.groups[1].box_only);
}

// A dead chug (every member dead) earns the X repeat box only when it restates the nearest
// preceding chord's posture; with fresh frets it shows its notes and their mute crosses — the
// third recorded regression (Charter blanks every dead chug; this board does not).
TEST_CASE("Highway chord groups blank a dead chug only when it restates", "[core][highway]")
{
    std::vector<NoteViewState> restating{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        deadened(chordNote(2.0, 1, 3)),
        deadened(chordNote(2.0, 2, 5)),
    };
    const HighwayChordGrouping restated = makeHighwayChordGroups(restating, {});
    REQUIRE(restated.groups.size() == 2);
    CHECK(restated.groups[1].all_dead);
    CHECK(restated.groups[1].box_only);

    std::vector<NoteViewState> fresh{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        deadened(chordNote(2.0, 1, 7)),
        deadened(chordNote(2.0, 2, 9)),
    };
    const HighwayChordGrouping shown = makeHighwayChordGroups(fresh, {});
    REQUIRE(shown.groups.size() == 2);
    CHECK(shown.groups[1].all_dead);
    CHECK_FALSE(shown.groups[1].box_only);

    // The palm resting on the strings does not stop a dead chug being one: the chug rule reads the
    // dead flag alone, so a both-muted restatement blanks exactly as the plain dead one does.
    std::vector<NoteViewState> palmed{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        palmMuted(deadened(chordNote(2.0, 1, 3))),
        palmMuted(deadened(chordNote(2.0, 2, 5))),
    };
    const HighwayChordGrouping palmed_chug = makeHighwayChordGroups(palmed, {});
    REQUIRE(palmed_chug.groups.size() == 2);
    CHECK(palmed_chug.groups[1].all_dead);
    CHECK(palmed_chug.groups[1].all_palm_muted);
    CHECK(palmed_chug.groups[1].box_only);
}

// The span-hold take-over cap resolves over the WHOLE song: each group's cap is the next
// note-showing strum wherever it is, and box-only repeats, dead chugs, and single notes continue
// the hold rather than taking it over. Deriving this inside the renderer's window used to leave
// the last visible group capped at infinity even when the taking-over strum sat just past the
// window's edge.
TEST_CASE("Highway chord group hold caps resolve over the whole song", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 2.5, posture)};
    std::vector<NoteViewState> notes{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        chordNote(2.0, 1, 3),
        chordNote(2.0, 2, 5),
        chordNote(3.0, 1, 0),
        chordNote(9.0, 1, 8),
        chordNote(9.0, 2, 10),
    };

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

    REQUIRE(grouping.groups.size() == 4);
    CHECK(grouping.groups[1].box_only);
    // The box-only repeat at 2.0 and the single note at 3.0 pass the hold through, so the strum
    // at 1.0 is capped by the shown strum at 9.0 — far beyond any drawing window.
    CHECK(grouping.groups[0].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(grouping.groups[1].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(grouping.groups[2].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(std::isinf(grouping.groups[3].hold_cap_seconds));
}

} // namespace rock_hero::common::core
