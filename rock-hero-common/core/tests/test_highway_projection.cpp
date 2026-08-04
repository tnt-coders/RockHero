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
    // No slide lands on this placement, so it morphs over the shared minimum-sustain-distance
    // margin (1/16 whole note — a quarter beat in 4/4).
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
// shared minimum-sustain-distance margin, crowded placements shorten against the previous
// arrival instead of overlapping it, and an unpitched slide-out never slide-matches a placement.
TEST_CASE("Highway projection derives hand-window ramps", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    // A sustained note whose tail trails off unpitched: a placement on its end takes the
    // margin morph, never the whole-sustain segment (a segment tie made the window creep
    // from the onset on long notes, sighted 2026-08-02).
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
        // Crowded: a sixteenth of a beat after the previous arrival — closer than the margin —
        // so the morph shortens against it.
        FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 16}},
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
        // Exactly where the unpitched slide-out ends (4:3 advanced one beat): the margin
        // morph, arriving with the release, never the whole-sustain segment.
        FretHandPosition{.position = GridPosition{.measure = 4, .beat = 4}, .fret = 9, .width = 4},
    };

    const HighwayViewState state = makeHighwayViewState(arrangement, tempo_map, {}, {});
    REQUIRE(state.fret_hand_positions.size() == 4);

    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));

    CHECK(state.fret_hand_positions[1].seconds == Catch::Approx(4.0625 * beat));
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(0.0625 * beat));

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

// Camera framing zones quantize the camera's scan window: note-bearing measure runs split
// every two measures aligned to downbeats, empty runs collapse into one zone however long, and
// a section start forces a new zone (the derivation a standard automatic phrase generator uses;
// user direction 2026-07-29).
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
    CHECK(state.string_count == 0);
    CHECK(state.notes.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
    CHECK(state.beats.empty());
    CHECK(state.camera_zone_starts.empty());
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

// Display hold ends: sustainless strum notes under a hand-shape span hold to the span end (the
// span states the chord's hold even though no tail is drawn), while single notes, sustained
// notes, dead chugs, and strums outside every span keep their own sustain end.
TEST_CASE("Highway display hold ends span-extend sustainless strums", "[core][highway]")
{
    std::vector<HighwayNoteView> notes;
    const auto add_note = [&notes](double start, double end, NoteMute mute = NoteMute::None) {
        HighwayNoteView note;
        note.start_seconds = start;
        note.end_seconds = end;
        note.mute = mute;
        notes.push_back(std::move(note));
    };
    add_note(0.99995, 0.99995); // Chord straddling the span start within the onset epsilon.
    add_note(1.0, 1.0);
    add_note(2.0, 2.0); // Single note under the span.
    add_note(3.0, 3.5); // Mixed chord: a real sustain next to a sustainless partner.
    add_note(3.0, 3.0);
    add_note(4.0, 4.0, NoteMute::Full); // Dead chug: choked, not held.
    add_note(4.0, 4.0, NoteMute::Full);
    add_note(7.0, 7.0); // Chord under the second span.
    add_note(7.0, 7.0);
    add_note(9.0, 9.0); // Chord past the second span's end.
    add_note(9.0, 9.0);

    std::vector<HighwayShapeView> shapes(2);
    shapes[0].start_seconds = 1.0;
    shapes[0].end_seconds = 5.0;
    shapes[1].start_seconds = 6.5;
    shapes[1].end_seconds = 8.0;

    const std::vector<double> hold_ends = highwayDisplayHoldEnds(notes, shapes);
    REQUIRE(hold_ends.size() == notes.size());
    CHECK(hold_ends[0] == Catch::Approx(5.0));
    CHECK(hold_ends[1] == Catch::Approx(5.0));
    CHECK(hold_ends[2] == Catch::Approx(2.0));
    CHECK(hold_ends[3] == Catch::Approx(3.5));
    CHECK(hold_ends[4] == Catch::Approx(5.0));
    CHECK(hold_ends[5] == Catch::Approx(4.0));
    CHECK(hold_ends[6] == Catch::Approx(4.0));
    CHECK(hold_ends[7] == Catch::Approx(8.0));
    CHECK(hold_ends[8] == Catch::Approx(8.0));
    CHECK(hold_ends[9] == Catch::Approx(9.0));
    CHECK(hold_ends[10] == Catch::Approx(9.0));

    // The hold ends feed the visible range through the end-time prefix-max overload.
    const std::vector<double> prefix_max = makeHighwaySustainPrefixMax(hold_ends);
    REQUIRE(prefix_max.size() == hold_ends.size());
    CHECK(prefix_max[2] == Catch::Approx(5.0));
    CHECK(prefix_max[7] == Catch::Approx(8.0));
}

// Tapping-hand onsets (right-hand-tap-lighting plan): one derived entry per onset group that
// contains tapped notes, carrying the taps' fret extent and count. Non-tap notes sharing the
// onset contribute nothing, tap-free onsets derive no entry, and simultaneity follows the
// shared onset epsilon.
TEST_CASE("Highway tap onsets derive from tapped notes only", "[core][highway]")
{
    std::vector<HighwayNoteView> notes;
    const auto add_note = [&notes](double start, int fret, NoteAttack attack = NoteAttack::Pick) {
        HighwayNoteView note;
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
    add_note(3.0, 15, NoteAttack::Tap); // Tapped chord within the onset epsilon.
    add_note(3.00005, 12, NoteAttack::Tap);
    add_note(3.00005, 17, NoteAttack::Tap);
    add_note(4.0, 9, NoteAttack::Hammer); // Left-hand tap imports as Hammer: no entry.

    const std::vector<HighwayTapOnsetView> onsets =
        makeHighwayTapOnsets(notes, std::vector<double>(notes.size(), 0.0));
    REQUIRE(onsets.size() == 3);
    CHECK(
        onsets[0] ==
        HighwayTapOnsetView{
            .seconds = 1.0,
            .fret_low = 12,
            .fret_high = 12,
            .count = 1,
            .path = {HighwayTapLightStation{.seconds = 1.0, .fret_low = 12.0, .fret_high = 12.0}},
        });
    CHECK(
        onsets[1] ==
        HighwayTapOnsetView{
            .seconds = 2.0,
            .fret_low = 14,
            .fret_high = 14,
            .count = 1,
            .path = {HighwayTapLightStation{.seconds = 2.0, .fret_low = 14.0, .fret_high = 14.0}},
        });
    CHECK(
        onsets[2] ==
        HighwayTapOnsetView{
            .seconds = 3.0,
            .fret_low = 12,
            .fret_high = 17,
            .count = 3,
            .path = {HighwayTapLightStation{.seconds = 3.0, .fret_low = 12.0, .fret_high = 17.0}},
        });
}

// A tap's light path follows sustained contact and pitched glides: a held tap keeps its light on
// through the sustain, a tapped slide adds a station per pitched waypoint so the light morphs
// with the glide, and an unpitched trail-off releases the light from the last pitched station.
TEST_CASE("Highway tap onsets carry the light path through glides", "[core][highway]")
{
    std::vector<HighwayNoteView> notes;

    // Held tap: sounding from 1.0 to 2.0 at fret 12, no glide.
    HighwayNoteView held;
    held.start_seconds = 1.0;
    held.end_seconds = 2.0;
    held.fret = 12;
    held.attack = NoteAttack::Tap;
    notes.push_back(held);

    // Tapped slide: fret 12 at 3.0 gliding to fret 15 at 4.0 (the sustain end).
    HighwayNoteView sliding;
    sliding.start_seconds = 3.0;
    sliding.end_seconds = 4.0;
    sliding.fret = 12;
    sliding.attack = NoteAttack::Tap;
    sliding.slides = {HighwaySlideView{.seconds = 4.0, .fret = 15, .unpitched = false}};
    notes.push_back(sliding);

    // Tapped slide with an unpitched trail-off: the pitched glide ends at 6.0; the trail to 6.5
    // is already releasing pressure, so the light must not follow it.
    HighwayNoteView trailing;
    trailing.start_seconds = 5.0;
    trailing.end_seconds = 6.5;
    trailing.fret = 10;
    trailing.attack = NoteAttack::Tap;
    trailing.slides = {
        HighwaySlideView{.seconds = 6.0, .fret = 13, .unpitched = false},
        HighwaySlideView{.seconds = 6.5, .fret = 8, .unpitched = true},
    };
    notes.push_back(trailing);

    const std::vector<HighwayTapOnsetView> onsets =
        makeHighwayTapOnsets(notes, std::vector<double>(notes.size(), 0.0));
    REQUIRE(onsets.size() == 3);

    REQUIRE(onsets[0].path.size() == 2);
    CHECK(
        onsets[0].path[0] ==
        HighwayTapLightStation{.seconds = 1.0, .fret_low = 12.0, .fret_high = 12.0});
    CHECK(
        onsets[0].path[1] ==
        HighwayTapLightStation{.seconds = 2.0, .fret_low = 12.0, .fret_high = 12.0});

    REQUIRE(onsets[1].path.size() == 2);
    CHECK(
        onsets[1].path[0] ==
        HighwayTapLightStation{.seconds = 3.0, .fret_low = 12.0, .fret_high = 12.0});
    CHECK(
        onsets[1].path[1] ==
        HighwayTapLightStation{.seconds = 4.0, .fret_low = 15.0, .fret_high = 15.0});

    REQUIRE(onsets[2].path.size() == 2);
    CHECK(
        onsets[2].path[0] ==
        HighwayTapLightStation{.seconds = 5.0, .fret_low = 10.0, .fret_high = 10.0});
    CHECK(
        onsets[2].path[1] ==
        HighwayTapLightStation{.seconds = 6.0, .fret_low = 13.0, .fret_high = 13.0});
}

// The light-rise ramp mirrors the fret-hand arrival rule: an onset takes its widest member's
// margin rise, and crowding clamps the rise so it never reaches backward past the previous tap
// onset's release — a dense run keeps its per-tap dips.
TEST_CASE("Highway tap onsets clamp light ramps against the previous release", "[core][highway]")
{
    std::vector<HighwayNoteView> notes;
    const auto add_tap = [&notes](double start, double end, int fret) {
        HighwayNoteView note;
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

// The pick-slide seam: latents suppressed, the path unpitched, no tap lighting, and the hand
// window's slide-locked ramps never tie to a scrape leg — an FHP sitting exactly on a scrape
// waypoint still gets the ordinary margin morph.
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
        .slides = {
            SlideWaypoint{.offset = Fraction{1, 2}, .fret = 3},
            SlideWaypoint{.offset = Fraction{1}, .fret = 9},
        },
    };
    scrape.mute = NoteMute::Full;
    scrape.tremolo = true;
    scrape.vibrato = true;
    scrape.slide_out = SlideOut{.offset = Fraction{1}, .fret = 1};
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
    REQUIRE(state.notes.size() == 1);
    const HighwayNoteView& view = state.notes.front();
    CHECK(view.attack == NoteAttack::PickSlide);
    CHECK(view.mute == NoteMute::None);
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
    // The FHP on the waypoint's grid position ramps by the quarter-beat margin morph (0.125s at
    // the default tempo), not by the scrape leg's span back to the onset (which would be 0.25s).
    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.125));
}

} // namespace rock_hero::common::core
