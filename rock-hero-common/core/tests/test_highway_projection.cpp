#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_hit_glow.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/highway/highway_window.h>
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
    chart.notes = {
        // Simultaneous pair at 2:1: two strings struck together derive a chord-box span.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 1,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
        // Rings across the 3:1+1/2 strum without being re-struck there, so it joins that span's
        // posture and makes the span arrive arpeggio-style.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 2,
            .fret = 5,
            .sustain = Fraction{2},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{2},
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1}, .bend = 2.0},
                    Keyframe{.offset = Fraction{2}, .fret = 9},
                },
        },
        // The strum's second struck string: two members are what open a span at all.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .string = 5,
            .fret = 8,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
        // Natural harmonic with a between-fret node the highway must carry through.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 3,
            .fret = 3,
            .sustain = Fraction{1, 8},
            .harmonic_node = 3.2,
            .bend = {},
            .keyframes = {},
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
            .keyframes =
                {Keyframe{.offset = Fraction{1, 2}, .fret = 5},
                 Keyframe{.offset = Fraction{1}, .fret = 12}},
        },
        // Simultaneous chord at 2:1 covering the whole span's posture: reads as a chord box.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 4,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 6,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 3,
            .fret = 6,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
        // One technique each, in order: palm mute, tremolo, vibrato, accent.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1, 2},
            .palm_mute = true,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 3},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1, 2},
            .tremolo = true,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 4},
            .string = 5,
            .fret = 9,
            .sustain = Fraction{1},
            .vibrato = VibratoState::Narrow,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 4,
            .fret = 12,
            .sustain = Fraction{1, 8},
            .emphasis = NoteEmphasis::Accent,
            .bend = {},
            .keyframes = {},
        },
        // Both payload kinds on one tail: a bend point mid-sustain and a fret stated at the ring's
        // end, which is the SLIDE-OUT the hand slides out toward. Ghosted, so the fixture carries
        // BOTH ends of the emphasis axis and the comparison below cannot pass by finding one value
        // everywhere.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{2},
            .emphasis = NoteEmphasis::Ghost,
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1}, .bend = 2.0},
                    Keyframe{.offset = Fraction{2}, .fret = 9},
                },
        },
        // Both mutes on one note: the palm is down AND this string is deadened. Two independent
        // flags, so a projection that collapsed them back onto one axis would drop one of them on
        // one surface and the comparison below would catch it.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 3},
            .string = 6,
            .fret = 5,
            .sustain = Fraction{1, 8},
            .palm_mute = true,
            .dead = true,
            .bend = {},
            .keyframes = {},
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
            .keyframes = {},
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
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1},
            .attack = NoteAttack::Legato,
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1},
            .attack = NoteAttack::Legato,
            .bend = {},
            .keyframes = {},
        },
        // Held into the 5:1 strum without being re-struck there: the string that makes the second
        // derived span arrive arpeggio-style, and the third string of its posture.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 4},
            .string = 2,
            .fret = 5,
            .sustain = Fraction{2},
            .bend = {},
            .keyframes = {},
        },
        // The 5:1 pair: two strings struck together under the held one.
        ChartNote{
            .position = GridPosition{.measure = 5, .beat = 1},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 5, .beat = 1},
            .string = 4,
            .fret = 7,
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 4, .width = 4},
        FretHandPosition{.position = GridPosition{.measure = 5, .beat = 1}, .fret = 5, .width = 4},
    };
    return chart;
}

// One note of the strike-pop fixtures, a beat long unless the case shortens it so that no ring
// overlaps the next strike, with the keyframes the case gives it.
[[nodiscard]] ChartNote popNote(
    const GridPosition position, const int string, const int fret, const NoteAttack attack,
    std::vector<Keyframe> keyframes, const Fraction sustain = Fraction{1})
{
    return ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .attack = attack,
        .bend = {},
        .keyframes = std::move(keyframes),
    };
}

// The board a strike-pop fixture projects to: its notes as the chart, over the 120 BPM map.
[[nodiscard]] HighwayViewState popBoard(std::vector<ChartNote> notes)
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = std::move(notes);
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    return makeHighwayViewState(arrangement, makeHighwayTempoMap(), {}, {});
}

// A pop's release after THE POP CLAMP, for a partner `spacing` seconds later on the same strips.
[[nodiscard]] double clampedRelease(const double spacing)
{
    return highwayHitGlowRelease(
        g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing);
}

// Checks one derived pop against its expected onset, release and strips.
void checkPop(
    const HighwayStrikePop& pop, const double onset, const double release,
    const std::optional<int> fret)
{
    CHECK(pop.onset_seconds == Catch::Approx(onset));
    CHECK(pop.release_seconds == Catch::Approx(release));
    // Compared as plain ints, with -1 standing for the box sides, so a failure prints its values.
    CHECK(pop.fret.value_or(-1) == fret.value_or(-1));
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

    CHECK(state.chart.stringCount() == 6);
    REQUIRE(state.chart.notes.size() == 6);

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.chart.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.chart.notes[0].ring_end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.chart.notes[0].ink_end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.chart.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    // Rule 2's verdict is the GROUP's: its chord partner's whole-beat ring runs longer than the
    // kept-sustain bound, so this member draws its own eighth-of-a-beat tail rather than none.
    CHECK(state.chart.notes[1].ink_end_seconds == Catch::Approx(4.125 * beat));

    const NoteViewState& sliding = state.chart.notes[3];
    CHECK(sliding.start_seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.ring_end_seconds == Catch::Approx(10.5 * beat));
    CHECK(sliding.ink_end_seconds == Catch::Approx(10.5 * beat));
    // The curve opens at the ONSET: the note's own bend value is the channel's first statement,
    // so a bent note's polyline always starts at its head and the stated point follows.
    REQUIRE(sliding.bend.size() == 2);
    CHECK(sliding.bend[0].seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.bend[0].semitones == Catch::Approx(0.0));
    CHECK(sliding.bend[1].seconds == Catch::Approx(9.5 * beat));
    CHECK(sliding.bend[1].semitones == Catch::Approx(2.0));
    REQUIRE(sliding.slides.size() == 1);
    CHECK(sliding.slides[0].seconds == Catch::Approx(10.5 * beat));
    CHECK(sliding.slides[0].fret == 9);

    // The between-fret harmonic node survives projection untouched, and its presence is what
    // makes the note a harmonic now.
    const NoteViewState& harmonic = state.chart.notes[5];
    CHECK(harmonic.attack == NoteAttack::Pick);
    REQUIRE(harmonic.harmonic_node.has_value());
    if (harmonic.harmonic_node.has_value())
    {
        CHECK(*harmonic.harmonic_node == Catch::Approx(3.2));
        CHECK(nodeIsOnNeck(harmonic.attack));
    }

    // Both spans are DERIVED from the notes: the 2:1 pair is a chord box, and the 3:1+1/2 pair
    // strikes under string 2's still-sounding ring, which makes it an arpeggio.
    REQUIRE(state.chart.shapes.size() == 2);
    CHECK_FALSE(state.chart.shapes[0].arpeggio);
    CHECK(state.chart.shapes[1].arpeggio);
    // Posture entries carry the derived stops, one per string the posture holds.
    REQUIRE(state.chart.shapes[0].strings.size() == 2);
    CHECK(state.chart.shapes[0].strings[0].string == 1);
    CHECK(state.chart.shapes[0].strings[0].stop == frettedStop(1));
    CHECK(state.chart.shapes[0].strings[1].string == 2);
    CHECK(state.chart.shapes[0].strings[1].stop == frettedStop(3));
    REQUIRE(state.chart.shapes[1].strings.size() == 3);
    CHECK(state.chart.shapes[1].strings[2].string == 5);
    CHECK(state.chart.shapes[1].strings[2].stop == frettedStop(8));

    REQUIRE(state.chart.fret_hand_positions.size() == 1);
    CHECK(state.chart.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    // No slide lands on this placement, so it morphs over the shared minimum-sustain-distance
    // margin.
    CHECK(
        state.chart.fret_hand_positions[0].ramp_seconds ==
        Catch::Approx(g_minimum_sustain_distance_seconds));

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
    CHECK(any_note([](const NoteViewState& note) { return !note.vibrato.empty(); }));
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
    // continues the gesture, while its terminal — the slide-out, last of the same keyframe sequence
    // — sits exactly at the ring's end and so continues nothing.
    const NoteViewState& scrape = scene.notes.front();
    REQUIRE(scrape.attack == NoteAttack::PickSlide);
    REQUIRE(scrape.slides.size() == 2);
    CHECK(scrape.slides.back().slide_out);
    CHECK(linkedKeyframe(scrape.slides[0]));
    CHECK_FALSE(linkedKeyframe(scrape.slides[1]));
    CHECK(glideStopAt(scrape, 1).seconds == Catch::Approx(scrape.ring_end_seconds));

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
    chart->fret_hand_positions.clear();
    for (int measure = 1; measure <= 6; ++measure)
    {
        chart->notes.push_back(
            ChartNote{
                .position = GridPosition{.measure = measure, .beat = 1},
                .string = 1,
                .fret = 5,
                .sustain = Fraction{1, 8},
                .bend = {},
                .keyframes = {},
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
    CHECK(state.chart.stringCount() == 0);
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

    // THE ONE PLACEMENT AUTHORITY for a stop on the fret axis: a node sits on its own wire and a
    // fret at its slot's midpoint, which is what keeps a posture bracket glued to the node head
    // it brackets and the floor number under both.
    CHECK(
        highwayStopX(nodeStop(12.0), metrics, false) ==
        Catch::Approx(highwayFretLineX(12.0, metrics, false)));
    CHECK(
        highwayStopX(nodeStop(2.669), metrics, true) ==
        Catch::Approx(highwayFretLineX(2.669, metrics, true)));
    CHECK(
        highwayStopX(frettedStop(5), metrics, false) ==
        Catch::Approx(highwayNoteCenterX(5, metrics, false)));
    // A node past the drawn board is held at its edge on the way in, exactly as a head is.
    CHECK(highwayDrawnStop(nodeStop(40.0)) == nodeStop(static_cast<double>(g_highway_fret_count)));
    CHECK(highwayDrawnStop(frettedStop(5)) == frettedStop(5));

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
        note.ring_end_seconds = end;
        note.ink_end_seconds = end;
        notes.push_back(std::move(note));
    };
    add_note(0.0, 5.0); // Long sustain spanning most of the timeline.
    add_note(1.0, 1.2);
    add_note(2.0, 2.2);
    add_note(10.0, 11.0);

    const std::vector<double> prefix_max =
        makeSustainPrefixMax(notes | std::views::transform(&NoteViewState::ring_end_seconds));
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
            note.ring_end_seconds = start + 0.1;
            note.ink_end_seconds = start + 0.1;
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

// The projection RESOLVES the hold rule into seconds rather than restating it: the rule's own case
// matrix is pinned in beats beside chartHolds, and what matters here is that the resolution lands
// on the right second and that the result feeds the visible range. One derivation, so a defect
// cannot hide in a second copy.
TEST_CASE("Highway display hold ends resolve the chart holds", "[core][highway]")
{
    const TempoMap map = makeHighwayTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const auto strum_note = [](int string, GridPosition position) {
        return ChartNote{
            .position = position,
            .string = string,
            .fret = 5,
            // Half a beat, which at this tempo lasts exactly the kept-sustain bound and no longer,
            // so rule 2 drops it. The pair presents no tail, and the span rule is what answers how
            // long the hand stays down.
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        };
    };
    // One chugged pair at global beat 4 (2.0 seconds) and the same chug again at global beat 7.75
    // (3.875 seconds); the stored gap between them ends the first statement at its own rings, so
    // these are two spans. A single onset at global beat 8.25 closes the second one AT ITSELF — its
    // musical close, the trim living in the projection — which is 4.125 seconds. The hold is what
    // the HAND does, so it runs to that close while the pair itself presents no tail at all: the
    // hold outlasting the drawn tail is the whole reason the board reads a field of its own.
    //
    // The late pair sits where its half-beat rings still reach the closing onset. Any earlier and
    // the pair's statement would end at its own rings before that onset, leaving the closing arm of
    // this case testing nothing.
    const GridPosition early{.measure = 2, .beat = 1};
    const GridPosition late{.measure = 2, .beat = 4, .offset = Fraction{3, 4}};
    chart.notes = {
        strum_note(1, early),
        strum_note(2, early),
        strum_note(1, late),
        strum_note(2, late),
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 4}},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
    };

    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    const HighwayViewState state =
        makeHighwayViewState(arrangement, map, {}, HighwayDisplayOptions{});

    REQUIRE(state.chart.display_hold_ends.size() == state.chart.notes.size());
    REQUIRE(state.chart.notes.size() == 5);
    // Struck at 2.0 seconds and presenting no tail, so the heads stay pinned for what the strings
    // actually ring: half a beat, which the span outlasts.
    CHECK(state.chart.notes[0].ink_end_seconds == Catch::Approx(2.0));
    CHECK(state.chart.display_hold_ends[0] == Catch::Approx(2.25));
    CHECK(state.chart.display_hold_ends[1] == Catch::Approx(2.25));
    // The late pair is held to the span's musical close at 4.125 seconds, which is where the
    // closing onset stands — the shape is held right up to the statement that replaces it.
    CHECK(state.chart.display_hold_ends[2] == Catch::Approx(4.125));
    CHECK(state.chart.display_hold_ends[3] == Catch::Approx(4.125));

    // One authority, resolved identically for either surface: the 2D projection answers the same
    // seconds. What differs is how each SPENDS it — the board pins the heads here, while the lane
    // draws every tail to the note's ink end and so draws none for these chugs at all (the
    // chord box over the strum is what states the posture there). That division is ruling 3 of
    // `docs/plans/in-progress/note-sustain-model.md`; the lane's side is pinned in the tab paint
    // core's own suite.
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
    // And the hold genuinely outlasts the tail on every STRUM member, which is the whole reason
    // the two surfaces can read the same field and draw different lengths. The closing single
    // onset is not a strum, so nothing extends it and it holds exactly what it presents.
    for (std::size_t index = 0; index < 4; ++index)
    {
        CAPTURE(index);
        CHECK(lane.display_hold_ends[index] > lane.notes[index].ink_end_seconds);
    }

    // And the board's visible-range index is built from the holds, so a pinned strum stays in range
    // for as long as it is held: a window opening AFTER the late pair's onset still has to include
    // it, because the span holds its heads to 4.125.
    const std::vector<double> prefix_max = makeSustainPrefixMax(state.chart.display_hold_ends);
    REQUIRE(prefix_max.size() == 5);
    CHECK(prefix_max[3] == Catch::Approx(4.125));
    const auto visible = visibleEventRange(state.chart.notes, prefix_max, 3.9, 4.0);
    CHECK(visible.first == 2);
    CHECK(visible.second == 4);
}

// SURFACES MUST NOT DIVERGE, and the tail law is what makes that structural for a tail: there is
// one end per note and one verdict per note, and both surfaces can only read them — no second
// length anywhere. The law empties nothing: a taken member's ink_end_seconds is still its
// rules-1-to-3 end, and `rested` is what tells the board to REST that ribbon at distance while the
// lane draws it. The law's own arithmetic is pinned in the core presentation suite; what is pinned
// here is that the board and the lane resolve the same seconds, and the same verdict, from one
// derivation.
TEST_CASE("Both surfaces read the tail law's one end", "[core][highway]")
{
    const TempoMap map = makeHighwayTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // A four-beat ring on string 2 carrying into the strum two beats later: the carry joins that
    // shape's posture, which is what makes the span arrive as an ARPEGGIO rather than a box. The
    // string-4 ring beside it is the discriminator — it outlives the span by a beat, so it is the
    // one member the law may not take.
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 2,
            .fret = 7,
            .sustain = Fraction{4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 4,
            .fret = 3,
            .sustain = Fraction{4},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 3},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{2},
            .bend = {},
            .keyframes = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 3},
            .string = 3,
            .fret = 9,
            .sustain = Fraction{2},
            .bend = {},
            .keyframes = {},
        },
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const HighwayViewState state = makeHighwayViewState(arrangement, map, {}, {});

    REQUIRE(state.chart.shapes.size() == 1);
    CHECK(state.chart.shapes[0].arpeggio);
    REQUIRE(state.chart.notes.size() == 4);
    // The carry runs from the span's front to its close, so the span accounts for the whole of it
    // and the board RESTS the ribbon. The verdict empties no tail: the ink end stays the
    // rules-1-to-3 end rather than collapsing onto the onset. The carry passes both strum heads
    // and nothing binds it after, so its four beats run from 0.0 s to 2.0 s at this map's 120 BPM.
    CHECK(state.chart.notes[0].rested);
    CHECK(state.chart.notes[0].ink_end_seconds == Catch::Approx(2.0));
    // The strum's own rings die at that same close, so they go with it: a span that accounts for a
    // member's whole ring rests it whether or not anything sounds inside. Rested, not shortened —
    // struck at 1.0 s with nothing after them, both present their notated two beats out to 2.0 s.
    CHECK(state.chart.notes[2].rested);
    CHECK(state.chart.notes[3].rested);
    CHECK(state.chart.notes[2].ink_end_seconds == Catch::Approx(2.0));
    CHECK(state.chart.notes[3].ink_end_seconds == Catch::Approx(2.0));
    // And the member that OUTLIVES the span rests too, its whole ring the reveal's to show: the
    // resting remainder starts at its own head, and the ink end stays the rules-1-to-3 end —
    // nothing shortened.
    CHECK(state.chart.notes[1].rested);
    CHECK(state.chart.notes[1].reveal_from_seconds == Catch::Approx(0.5));
    CHECK(state.chart.notes[1].ink_end_seconds == Catch::Approx(2.5));

    // The 2D lane resolves the identical seconds, to the bit: one derivation, one end, no per-note
    // fact left for a surface to spend differently.
    const ChartViewState lane = makeChartViewState(arrangement, map);
    REQUIRE(lane.notes.size() == state.chart.notes.size());
    for (std::size_t index = 0; index < lane.notes.size(); ++index)
    {
        CAPTURE(index);
        CHECK_THAT(
            lane.notes[index].ink_end_seconds,
            Catch::Matchers::WithinULP(state.chart.notes[index].ink_end_seconds, 0));
        CHECK(lane.notes[index].rested == state.chart.notes[index].rested);
    }

    // And the reveal reads the same note: rule 1 binds none of these rings, so each ink end IS the
    // ring end the reveal draws to — the carry's stored four beats — and the verdict is the whole
    // of what differs between a plain paint and a reveal here.
    CHECK_THAT(
        state.chart.notes[0].ring_end_seconds,
        Catch::Matchers::WithinULP(state.chart.notes[0].ink_end_seconds, 0));
}

// The repeat chain's pinned heads. A stored chug chain is strike-into-strike — every member's ring
// ends exactly where the next strike begins — and that adjacency is what merges the strums into ONE
// hand-shape span and makes the later ones repeat boxes. A repeat box draws no heads, so the
// chain's FIRST strum owns the only heads it has: they have to stay pinned at the fretboard for the
// whole chain, exactly as a plain chord box's duration keeps its own. Capping each hold at the
// note's own ring would end them at the second box's onset and drop the held shape one box into the
// chain.
TEST_CASE("Highway holds a repeat chain's heads through the whole chain", "[core][highway]")
{
    const TempoMap map = makeHighwayTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    // Half-beat chugs, struck a half beat apart: at this tempo that ring lasts exactly the
    // kept-sustain bound and no longer, so they present no tail at all and the span is what
    // answers how long the hand stays down.
    const auto chug = [](int string, int fret, GridPosition position) {
        return ChartNote{
            .position = position,
            .string = string,
            .fret = fret,
            .sustain = Fraction{1, 2},
            .bend = {},
            .keyframes = {},
        };
    };
    const GridPosition first{.measure = 1, .beat = 1};
    const GridPosition second{.measure = 1, .beat = 1, .offset = Fraction{1, 2}};
    const GridPosition third{.measure = 1, .beat = 2};
    // A different chord on the next measure's downbeat, ringing long enough to present its own
    // tail: it is both the chain's take-over (\ref HighwayChordGroupViewState::hold_cap_seconds)
    // and the control arm — a strum outside any chain must answer exactly what it always did.
    const auto ringing = [](int string, int fret) {
        return ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = string,
            .fret = fret,
            .sustain = Fraction{2},
            .bend = {},
            .keyframes = {},
        };
    };
    chart.notes = {
        chug(1, 5, first),
        chug(2, 7, first),
        chug(1, 5, second),
        chug(2, 7, second),
        chug(1, 5, third),
        chug(2, 7, third),
        ringing(3, 3),
        ringing(4, 5),
    };

    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    const HighwayViewState state =
        makeHighwayViewState(arrangement, map, {}, HighwayDisplayOptions{});

    REQUIRE(state.chart.notes.size() == 8);
    REQUIRE(state.chord_groups.size() == 4);
    // One strum showing its notes, then two boxes that draw none, then the fresh chord.
    CHECK(state.chord_groups[0].box_treatment == HighwayChordBoxTreatment::Full);
    CHECK(state.chord_groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    CHECK(state.chord_groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
    CHECK(state.chord_groups[3].box_treatment == HighwayChordBoxTreatment::Full);

    // ONE span over the whole chain — that merge is what the boxes are drawn under, and it is why
    // the first strum's own ring is shorter than the shape it belongs to. The fresh chord opens a
    // span of its own.
    REQUIRE(state.chart.shapes.size() == 2);
    CHECK(state.chart.shapes[0].start_seconds == Catch::Approx(0.0));
    CHECK(state.chart.shapes[0].drawn_end_seconds == Catch::Approx(0.75));

    // The chain's end is the LAST box's, not the first's: 120 BPM 4/4 puts the third strum at
    // 0.5 s and its half-beat ring closes the span at 0.75 s, and every member of the chain
    // resolves to that same one end. The value that would drop the shape mid-chain is 0.25 s —
    // the first strum's own ring, which stops exactly where the second box begins.
    REQUIRE(state.chart.display_hold_ends.size() == state.chart.notes.size());
    CHECK(state.chart.notes[0].ink_end_seconds == Catch::Approx(0.0));
    CHECK(state.chart.notes[2].start_seconds == Catch::Approx(0.25));
    for (std::size_t index = 0; index < 6; ++index)
    {
        CAPTURE(index);
        CHECK(state.chart.display_hold_ends[index] == Catch::Approx(0.75));
    }

    // And nothing clips the pin before then: the take-over is the next strum that SHOWS its notes,
    // which sits well past the chain's end. A box-only repeat never takes the display over,
    // because it has no head of its own to take it over with.
    CHECK(state.chord_groups[0].hold_cap_seconds == Catch::Approx(2.0));

    // THE PLAIN SUSTAINED CHORD outside the chain: both rings die at their own span's close, so the
    // strum RESTS RIBBONLESS. That is what grip tenure buys — at distance the box states the tenure
    // and no tail duplicates it.
    CHECK(state.chart.notes[6].rested);
    // The verdict empties no tail: the ink end stays the rules-1-to-3 end rather than collapsing
    // onto the onset. Nothing is struck after this chord, so rule 1 binds it nowhere and its two
    // beats run from 2.0 s out to 3.0 s — the ribbon the board reveals as the head nears the line,
    // and the one the lane draws throughout.
    CHECK(state.chart.notes[6].ink_end_seconds == Catch::Approx(3.0));
    // It is still the control arm this chain needs, because the two emptinesses remain different
    // things: the chain's members were emptied by RULE 2 before the law could look at them, so they
    // carry no verdict at all, while this chord's tail was judged and left standing. The hold and
    // the ribbon happen to agree here — a RESTED member holds its own stored ring — so the board
    // pins these heads for the whole two beats, while the chain's are held by the span rule
    // instead.
    CHECK(state.chart.display_hold_ends[6] == Catch::Approx(3.0));
    CHECK(state.chart.display_hold_ends[7] == Catch::Approx(3.0));
    CHECK_FALSE(state.chart.notes[0].rested);
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
        note.ring_end_seconds = start;
        note.ink_end_seconds = start;
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

    const std::vector<HighwayTapOnsetViewState> onsets = makeHighwayTapOnsets(notes);
    REQUIRE(onsets.size() == 3);
    CHECK(
        onsets[0] == HighwayTapOnsetViewState{
                         .start_seconds = 1.0,
                         .fret_low = 12,
                         .fret_high = 12,
                         .count = 1,
                         .release_seconds = 1.0
                     });
    CHECK(
        onsets[1] == HighwayTapOnsetViewState{
                         .start_seconds = 2.0,
                         .fret_low = 14,
                         .fret_high = 14,
                         .count = 1,
                         .release_seconds = 2.0
                     });
    CHECK(
        onsets[2] == HighwayTapOnsetViewState{
                         .start_seconds = 3.0,
                         .fret_low = 12,
                         .fret_high = 17,
                         .count = 3,
                         .release_seconds = 3.000000000001
                     });

    // The same groups light the picking hand: one arrival per onset on its one track (the release
    // a picosecond past the chord's onset is the onset's own instant, so it adds no arrival), and
    // one light per strike.
    const HighwayHandLight light = makePickHandLight(notes, std::vector<double>(notes.size(), 0.0));
    const auto settled = [](const double seconds, const double low, const double high) {
        return HighwayHandArrival{
            .seconds = seconds,
            .low_line = low,
            .high_line = high,
            .ramp_seconds = 0.0,
            .unpitched_ramp = false,
            .settle_seconds = 0.0,
        };
    };
    CHECK(
        light.track == std::vector<HighwayHandArrival>{
                           settled(1.0, 11.0, 12.0),
                           settled(2.0, 13.0, 14.0),
                           settled(3.0, 11.0, 17.0),
                       });
    CHECK(
        light.lit ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 1.0, .rise_seconds = 0.0},
            HighwayLitStretch{.start_seconds = 2.0, .release_seconds = 2.0, .rise_seconds = 0.0},
            HighwayLitStretch{
                .start_seconds = 3.0, .release_seconds = 3.000000000001, .rise_seconds = 0.0
            },
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
    tap.ring_end_seconds = 1.0;
    tap.ink_end_seconds = 1.0;
    tap.string = 3;
    tap.fret = 0;
    tap.attack = NoteAttack::Tap;
    tap.harmonic_node = 12.0;

    const std::vector<HighwayTapOnsetViewState> onsets = makeHighwayTapOnsets({tap});
    REQUIRE(onsets.size() == 1);
    CHECK(onsets.front().count == 1);
    CHECK(onsets.front().fret_low == 12);
    CHECK(onsets.front().fret_high == 12);
    // The track arrival reads the same sounding place through the light's own interpolation, so
    // it has to agree exactly (compared through the ordering query, which the project uses for an
    // exact floating compare that -Wfloat-equal accepts): the node's fret spans lines 11 to 12.
    const HighwayHandLight light = makePickHandLight({tap}, std::vector<double>(1, 0.0));
    REQUIRE_FALSE(light.track.empty());
    CHECK(std::is_eq(light.track.front().low_line <=> 11.0));
    CHECK(std::is_eq(light.track.front().high_line <=> 12.0));

    // An ordinary open string with no node still has nowhere to light, so the guard still holds
    // where it was meant to.
    NoteViewState open_tap = tap;
    open_tap.harmonic_node.reset();
    CHECK(makeHighwayTapOnsets({open_tap}).empty());
    const HighwayHandLight dark = makePickHandLight({open_tap}, std::vector<double>(1, 0.0));
    CHECK(dark.track.empty());
    CHECK(dark.lit.empty());
}

// A tap's light path follows sustained contact and pitched glides: a held tap keeps its light on
// through the sustain, a tapped slide adds an arrival per pitched keyframe so the light morphs
// with the glide, and an unpitched slide-out releases the light from the last pitched arrival.
TEST_CASE("Highway tap onsets carry the light path through glides", "[core][highway]")
{
    std::vector<NoteViewState> notes;

    // Held tap: sounding from 1.0 to 2.0 at fret 12, no glide.
    NoteViewState held;
    held.start_seconds = 1.0;
    held.ring_end_seconds = 2.0;
    held.ink_end_seconds = 2.0;
    held.fret = 12;
    held.attack = NoteAttack::Tap;
    notes.push_back(held);

    // Tapped slide: fret 12 at 3.0 gliding to fret 15 at 4.0 (the sustain end).
    NoteViewState sliding;
    sliding.start_seconds = 3.0;
    sliding.ring_end_seconds = 4.0;
    sliding.ink_end_seconds = 4.0;
    sliding.fret = 12;
    sliding.attack = NoteAttack::Tap;
    // Hand-built view states carry no authored offset: these fixtures resolve no tempo map, and
    // nothing on the highway path reads the field (it is the editor's selection identity).
    sliding.slides = {KeyframeViewState{.seconds = 4.0, .fret = 15, .offset = Fraction{}}};
    notes.push_back(sliding);

    // Tapped slide with an unpitched slide-out: the pitched glide ends at 6.0; the trail to 6.5
    // is already releasing pressure, so the light must not follow it.
    NoteViewState trailing;
    trailing.start_seconds = 5.0;
    trailing.ring_end_seconds = 6.5;
    trailing.ink_end_seconds = 6.5;
    trailing.fret = 10;
    trailing.attack = NoteAttack::Tap;
    trailing.slides = {
        KeyframeViewState{.seconds = 6.0, .fret = 13, .offset = Fraction{}},
        KeyframeViewState{.seconds = 6.5, .fret = 8, .offset = Fraction{}, .slide_out = true},
    };
    notes.push_back(trailing);

    const HighwayHandLight light = makePickHandLight(notes, std::vector<double>(notes.size(), 0.0));

    // The three onsets' paths concatenated on the one track, two arrivals each. Each later
    // arrival ramps over the leg from the arrival before it; an onset ramps over its light rise,
    // which is zero here.
    const std::vector<HighwayHandArrival>& track = light.track;
    REQUIRE(track.size() == 6);
    CHECK(
        track[0] == HighwayHandArrival{
                        .seconds = 1.0,
                        .low_line = 11.0,
                        .high_line = 12.0,
                        .ramp_seconds = 0.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });
    CHECK(
        track[1] == HighwayHandArrival{
                        .seconds = 2.0,
                        .low_line = 11.0,
                        .high_line = 12.0,
                        .ramp_seconds = 1.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });
    CHECK(
        track[2] == HighwayHandArrival{
                        .seconds = 3.0,
                        .low_line = 11.0,
                        .high_line = 12.0,
                        .ramp_seconds = 0.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });
    CHECK(
        track[3] == HighwayHandArrival{
                        .seconds = 4.0,
                        .low_line = 14.0,
                        .high_line = 15.0,
                        .ramp_seconds = 1.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });
    CHECK(
        track[4] == HighwayHandArrival{
                        .seconds = 5.0,
                        .low_line = 9.0,
                        .high_line = 10.0,
                        .ramp_seconds = 0.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });
    CHECK(
        track[5] == HighwayHandArrival{
                        .seconds = 6.0,
                        .low_line = 12.0,
                        .high_line = 13.0,
                        .ramp_seconds = 1.0,
                        .unpitched_ramp = false,
                        .settle_seconds = 0.0,
                    });

    // The held tap and the plain slide release at their ink ends; the drawn slide-out releases at
    // the last pitched keyframe, pressure already coming off. Each strike is its own light, and
    // each strike's hold end is the same release.
    REQUIRE(light.lit.size() == 3);
    CHECK(light.lit[0].release_seconds == Catch::Approx(held.ink_end_seconds));
    CHECK(light.lit[1].release_seconds == Catch::Approx(sliding.ink_end_seconds));
    CHECK(light.lit[2].release_seconds == Catch::Approx(trailing.slides.front().seconds));
    const std::vector<HighwayTapOnsetViewState> strikes = makeHighwayTapOnsets(notes);
    REQUIRE(strikes.size() == 3);
    for (std::size_t index = 0; index < strikes.size(); ++index)
    {
        CHECK(std::is_eq(strikes[index].release_seconds <=> light.lit[index].release_seconds));
    }
}

// The release is the hold end: the ink end, except where a DRAWN unpitched slide-out follows, when
// it is the last pitched keyframe. A slide-out past the ink end is not drawn, so the tail it would
// have released early simply ends — and releases — where its ink does.
TEST_CASE("Highway tap light releases at the hold end", "[core][highway]")
{
    NoteViewState trailing;
    trailing.start_seconds = 1.0;
    trailing.ring_end_seconds = 2.5;
    trailing.ink_end_seconds = 2.5;
    trailing.fret = 10;
    trailing.attack = NoteAttack::Tap;
    trailing.slides = {
        KeyframeViewState{.seconds = 1.5, .fret = 12, .offset = Fraction{}},
        KeyframeViewState{.seconds = 2.0, .fret = 13, .offset = Fraction{}},
        KeyframeViewState{.seconds = 2.5, .fret = 8, .offset = Fraction{}, .slide_out = true},
    };
    REQUIRE(keyframeDrawn(trailing.slides.back(), trailing.ink_end_seconds));

    const HighwayHandLight drawn = makePickHandLight({trailing}, std::vector<double>(1, 0.0));
    REQUIRE(drawn.lit.size() == 1);
    CHECK(drawn.lit.front().release_seconds == Catch::Approx(2.0));
    // The path ends at that last pitched keyframe: the slide-out adds nothing.
    REQUIRE(drawn.track.size() == 3);
    CHECK(drawn.track.back().seconds == Catch::Approx(2.0));

    // The same note with its ink cut after the pitched glide: the slide-out is not drawn, so the
    // light holds to the ink end and releases there, which extends the path by a release arrival.
    NoteViewState cut = trailing;
    cut.ink_end_seconds = 2.25;
    REQUIRE_FALSE(keyframeDrawn(cut.slides.back(), cut.ink_end_seconds));
    const HighwayHandLight undrawn = makePickHandLight({cut}, std::vector<double>(1, 0.0));
    REQUIRE(undrawn.lit.size() == 1);
    CHECK(undrawn.lit.front().release_seconds == Catch::Approx(cut.ink_end_seconds));
    REQUIRE(undrawn.track.size() == 4);
    const HighwayHandArrival& release = undrawn.track.back();
    CHECK(release.seconds == Catch::Approx(cut.ink_end_seconds));
    CHECK(release.low_line == Catch::Approx(12.0));
    CHECK(release.high_line == Catch::Approx(13.0));
    CHECK(release.ramp_seconds == Catch::Approx(0.25));
    CHECK_FALSE(release.unpitched_ramp);
    CHECK(std::is_eq(release.settle_seconds <=> 0.0));
}

// A pitched tap glide the ink end cuts: the light follows the cut leg on the rail's own curve to
// the stop the finger really reaches, and settles over the stretch past the ink end. That stop
// is the path's last arrival — later stops are not followed — and the release stays at the ink
// end, so the path runs past it and no release arrival is added.
TEST_CASE("Highway tap light follows a pitched glide the ink end cuts", "[core][highway]")
{
    // Fret 12 at 1.0 gliding to 15 at 2.0 and on to 17 at 2.5, the ink cut at 1.8 by a later head.
    NoteViewState tap;
    tap.start_seconds = 1.0;
    tap.ring_end_seconds = 2.5;
    tap.ink_end_seconds = 1.8;
    tap.fret = 12;
    tap.attack = NoteAttack::Tap;
    tap.slides = {
        KeyframeViewState{.seconds = 2.0, .fret = 15, .offset = Fraction{}},
        KeyframeViewState{.seconds = 2.5, .fret = 17, .offset = Fraction{}},
    };
    REQUIRE_FALSE(keyframeDrawn(tap.slides.front(), tap.ink_end_seconds));

    const HighwayHandLight light = makePickHandLight({tap}, std::vector<double>(1, 0.0));
    REQUIRE(light.lit.size() == 1);
    CHECK(light.lit.front().release_seconds == Catch::Approx(tap.ink_end_seconds));
    const std::vector<HighwayHandArrival>& path = light.track;
    REQUIRE(path.size() == 2);
    CHECK(
        path[0] == HighwayHandArrival{
                       .seconds = 1.0,
                       .low_line = 11.0,
                       .high_line = 12.0,
                       .ramp_seconds = 0.0,
                       .unpitched_ramp = false,
                       .settle_seconds = 0.0,
                   });
    CHECK(path[1].seconds == Catch::Approx(2.0));
    CHECK(path[1].low_line == Catch::Approx(14.0));
    CHECK(path[1].high_line == Catch::Approx(15.0));
    CHECK(path[1].ramp_seconds == Catch::Approx(1.0));
    CHECK_FALSE(path[1].unpitched_ramp);
    CHECK(path[1].settle_seconds == Catch::Approx(0.2));
}

// Between its own stops a member rides its rail's eased curve, not a straight line, so a chord
// arrival at one member's stop reads every other member where its own rail draws it then.
TEST_CASE("Highway tap light eases each chord member along its own rail", "[core][highway]")
{
    // Two taps at 3.0: fret 12 gliding to 15 at 4.0, and fret 14 gliding to 18 at 5.0.
    NoteViewState low;
    low.start_seconds = 3.0;
    low.ring_end_seconds = 4.0;
    low.ink_end_seconds = 4.0;
    low.fret = 12;
    low.attack = NoteAttack::Tap;
    low.slides = {KeyframeViewState{.seconds = 4.0, .fret = 15, .offset = Fraction{}}};
    NoteViewState high;
    high.start_seconds = 3.0;
    high.ring_end_seconds = 5.0;
    high.ink_end_seconds = 5.0;
    high.string = 1;
    high.fret = 14;
    high.attack = NoteAttack::Tap;
    high.slides = {KeyframeViewState{.seconds = 5.0, .fret = 18, .offset = Fraction{}}};

    const HighwayHandLight light = makePickHandLight({low, high}, std::vector<double>(2, 0.0));
    REQUIRE(light.lit.size() == 1);
    CHECK(light.lit.front().release_seconds == Catch::Approx(5.0));
    const std::vector<HighwayHandArrival>& path = light.track;
    REQUIRE(path.size() == 3);
    CHECK(path[0].low_line == Catch::Approx(11.0));
    CHECK(path[0].high_line == Catch::Approx(14.0));
    // Halfway along the upper member's leg: eased with the symmetric pitched curve.
    CHECK(path[1].seconds == Catch::Approx(4.0));
    CHECK(path[1].low_line == Catch::Approx(14.0));
    CHECK(path[1].high_line == Catch::Approx(14.0 + (4.0 * highwaySlideEaseWeight(0.5, false))));
    CHECK(path[1].high_line == Catch::Approx(16.0));
    CHECK(path[2].seconds == Catch::Approx(5.0));
    CHECK(path[2].low_line == Catch::Approx(14.0));
    CHECK(path[2].high_line == Catch::Approx(18.0));
}

// The light-rise ramp mirrors the fret-hand arrival rule: an onset takes its widest member's
// margin rise, and crowding clamps the rise so it never reaches backward past the previous tap
// onset's release — a dense run keeps its per-tap dips, each strike its own light.
TEST_CASE("Highway tap onsets clamp light ramps against the previous release", "[core][highway]")
{
    std::vector<NoteViewState> notes;
    const auto add_tap = [&notes](double start, double end, int fret) {
        NoteViewState note;
        note.start_seconds = start;
        note.ring_end_seconds = end;
        note.ink_end_seconds = end;
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
    const HighwayHandLight light = makePickHandLight(notes, rises);
    REQUIRE(light.lit.size() == 4);
    CHECK(light.lit[0].rise_seconds == Catch::Approx(0.25));
    CHECK(light.lit[1].rise_seconds == Catch::Approx(0.2));
    CHECK(light.lit[2].rise_seconds == Catch::Approx(0.25));
    CHECK(light.lit[3].rise_seconds == Catch::Approx(0.1));
}

// The pick-slide seam: latents suppressed, only the path's terminal unpitched, and the hand
// window's slide-locked ramps never tie to a scrape leg — an FHP sitting exactly on a scrape
// keyframe still gets the ordinary margin morph. The scrape DOES drive the moving right-hand light
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
        .keyframes = {
            Keyframe{.offset = Fraction{1, 4}, .bend = 1.0},
            Keyframe{.offset = Fraction{1, 2}, .fret = 3},
            Keyframe{.offset = Fraction{1}, .fret = 9},
        },
    };
    scrape.palm_mute = true;
    scrape.dead = true;
    scrape.tremolo = true;
    scrape.vibrato = VibratoState::Narrow;
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
    CHECK(view.vibrato.empty());
    CHECK(view.bend.empty());
    REQUIRE(view.slides.size() == 2);
    CHECK(view.slides.back().slide_out);
    REQUIRE(keyframeDrawn(view.slides.back(), view.ink_end_seconds));
    CHECK_FALSE(glideStopAt(view, 0).unpitched);
    CHECK(glideStopAt(view, 1).unpitched);
    // The right-hand light rides the scrape: one onset, and the picking hand's track follows the
    // traveled keyframes (17 at the onset, 3 at the reversal, 9 at the end), each fret's window
    // spanning lines fret - 1 to fret.
    REQUIRE(state.tap_onsets.size() == 1);
    const HighwayTapOnsetViewState& strike = state.tap_onsets.front();
    CHECK(strike.fret_low == 17);
    CHECK(strike.count == 1);
    const std::vector<HighwayHandArrival>& path = state.pick_hand.track;
    REQUIRE(path.size() == 3);
    CHECK(path[0].low_line == Catch::Approx(16.0));
    CHECK(path[1].low_line == Catch::Approx(2.0));
    CHECK(path[2].low_line == Catch::Approx(8.0));
    CHECK(path[2].high_line == Catch::Approx(9.0));
    // Each arrival carries its stop's family so the light sweeps with the rail's own ease — the
    // turnaround pitched, the terminal released — each over its own half-beat leg (0.25 s at 120
    // BPM); the onset arrival ramps over its light rise, which the grid origin clamps to nothing.
    CHECK_FALSE(path[0].unpitched_ramp);
    CHECK_FALSE(path[1].unpitched_ramp);
    CHECK(path[2].unpitched_ramp);
    CHECK(path[0].ramp_seconds == Catch::Approx(0.0));
    CHECK(path[1].ramp_seconds == Catch::Approx(0.25));
    CHECK(path[2].ramp_seconds == Catch::Approx(0.25));
    // A free scrape is drawn to its end, so no leg crosses an ink end and nothing settles.
    for (const HighwayHandArrival& arrival : path)
    {
        CHECK(std::is_eq(arrival.settle_seconds <=> 0.0));
    }
    // The FHP on the keyframe's grid position ramps by the margin morph — one duration at any
    // tempo — not by the scrape leg's span back to the onset (which would be 0.25s).
    REQUIRE(state.chart.fret_hand_positions.size() == 1);
    CHECK(
        state.chart.fret_hand_positions[0].ramp_seconds ==
        Catch::Approx(g_minimum_sustain_distance_seconds));
}

// A scrape the next head binds: its ink stops short of its last stop, and the light still follows
// the cut leg on the rail's own curve to the stop the pick really reaches — that stop is the path's
// last arrival, and the stretch of its leg past the ink end is the crop zone it settles over. The
// ink end cannot extend a path already running past it, so no hold-end arrival follows.
TEST_CASE("Highway tap light settles a bound scrape over its crop zone", "[core][highway]")
{
    // Scrape from fret 17 at 1.0 through fret 3 at 1.25 to its slide-out at fret 9 at 1.5, with
    // the ink cut at 1.4 — between the turnaround and the terminal.
    NoteViewState scrape;
    scrape.start_seconds = 1.0;
    scrape.ring_end_seconds = 1.5;
    scrape.ink_end_seconds = 1.4;
    scrape.fret = 17;
    scrape.attack = NoteAttack::PickSlide;
    scrape.slides = {
        KeyframeViewState{.seconds = 1.25, .fret = 3, .offset = Fraction{}},
        KeyframeViewState{.seconds = 1.5, .fret = 9, .offset = Fraction{}, .slide_out = true},
    };
    REQUIRE(keyframeDrawn(scrape.slides[0], scrape.ink_end_seconds));
    REQUIRE_FALSE(keyframeDrawn(scrape.slides[1], scrape.ink_end_seconds));

    const HighwayHandLight light = makePickHandLight({scrape}, std::vector<double>(1, 0.0));
    REQUIRE(light.lit.size() == 1);
    const std::vector<HighwayHandArrival>& path = light.track;
    REQUIRE(path.size() == 3);

    CHECK(path[0].seconds == Catch::Approx(1.0));
    CHECK(path[0].low_line == Catch::Approx(16.0));
    CHECK(path[0].high_line == Catch::Approx(17.0));
    CHECK_FALSE(path[0].unpitched_ramp);

    // The drawn turnaround: an ordinary pitched leg the pick arrives at and turns from, nothing to
    // settle.
    CHECK(path[1].seconds == Catch::Approx(1.25));
    CHECK(path[1].low_line == Catch::Approx(2.0));
    CHECK(path[1].high_line == Catch::Approx(3.0));
    CHECK(path[1].ramp_seconds == Catch::Approx(0.25));
    CHECK_FALSE(path[1].unpitched_ramp);
    CHECK(std::is_eq(path[1].settle_seconds <=> 0.0));

    // The cut leg: the terminal past the ink end is the last arrival, its whole leg is the ramp,
    // and the part of it past the ink end is the settle.
    CHECK(path[2].seconds == Catch::Approx(1.5));
    CHECK(path[2].low_line == Catch::Approx(8.0));
    CHECK(path[2].high_line == Catch::Approx(9.0));
    CHECK(path[2].ramp_seconds == Catch::Approx(0.25));
    CHECK(path[2].unpitched_ramp);
    CHECK(
        path[2].settle_seconds == Catch::Approx(scrape.slides[1].seconds - scrape.ink_end_seconds));
    CHECK(path[2].settle_seconds == Catch::Approx(0.1));
    // A scrape releases where its ink ends — the pick lifts at the crop — while the path runs on
    // to the cut leg's arrival.
    CHECK(light.lit.front().release_seconds == Catch::Approx(scrape.ink_end_seconds));
    CHECK(light.lit.front().release_seconds < path.back().seconds);
}

// The light eases each scrape leg in the rail's own family: a turnaround is a stop the pick
// reaches and turns from, so the light arrives there on the pitched curve exactly as the rail
// does, and only the terminal — where the pick lifts — keeps the release curve.
TEST_CASE("Highway tap light arrives pitched at a scrape's turnarounds", "[core][highway]")
{
    // Down from fret 12 to 3, up to 10, then the terminal down toward 2, a quarter second a leg.
    NoteViewState scrape;
    scrape.start_seconds = 1.0;
    scrape.ring_end_seconds = 1.75;
    scrape.ink_end_seconds = 1.75;
    scrape.fret = 12;
    scrape.attack = NoteAttack::PickSlide;
    scrape.slides = {
        KeyframeViewState{.seconds = 1.25, .fret = 3, .offset = Fraction{}},
        KeyframeViewState{.seconds = 1.5, .fret = 10, .offset = Fraction{}},
        KeyframeViewState{.seconds = 1.75, .fret = 2, .offset = Fraction{}, .slide_out = true},
    };

    const HighwayHandLight light = makePickHandLight({scrape}, std::vector<double>(1, 0.0));
    const std::vector<HighwayHandArrival>& path = light.track;
    REQUIRE(path.size() == 4);

    // The onset comes from no glide; each turnaround arrives pitched, the terminal released.
    CHECK_FALSE(path[0].unpitched_ramp);
    CHECK_FALSE(path[1].unpitched_ramp);
    CHECK_FALSE(path[2].unpitched_ramp);
    CHECK(path[3].unpitched_ramp);
    for (std::size_t index = 1; index < path.size(); ++index)
    {
        CHECK(path[index].ramp_seconds == Catch::Approx(0.25));
        CHECK(std::is_eq(path[index].settle_seconds <=> 0.0));
    }
    CHECK(path[1].low_line == Catch::Approx(2.0));
    CHECK(path[2].low_line == Catch::Approx(9.0));
    CHECK(path[3].low_line == Catch::Approx(1.0));

    // Halfway along each leg the light's window stands where that leg's own curve puts it: the
    // pitched weight into each turnaround, the release weight into the terminal.
    const auto low_line_at = [&path](const double seconds) {
        return highwayHandWindowAt(path, seconds).low_line;
    };
    CHECK(
        low_line_at(1.125) ==
        Catch::Approx(11.0 + ((2.0 - 11.0) * highwaySlideEaseWeight(0.5, false))));
    CHECK(
        low_line_at(1.375) ==
        Catch::Approx(2.0 + ((9.0 - 2.0) * highwaySlideEaseWeight(0.5, false))));
    CHECK(
        low_line_at(1.625) ==
        Catch::Approx(9.0 + ((1.0 - 9.0) * highwaySlideEaseWeight(0.5, true))));
}

// The fretting hand's track is an arrival per chart placement, with the placement's settled window
// as fret lines and the scene's ramp. A placement landing where a slide-out arrives gets the crop
// zone as its settle — the slide-out's rail is cut one margin before the next head while the hand
// completes at the head — and a margin-morph placement settles over nothing.
TEST_CASE("Highway fret hand settles a slide-out arrival over its crop zone", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Four beats ringing to the next onset on another string, trailing off unpitched at the
        // very end: that head binds the tail, so the ink stops one margin before the terminal.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{4},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{4}, .fret = 12}},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
    };
    const GridPosition slide_out_arrival{.measure = 2, .beat = 1};
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 1, .beat = 1}, .fret = 5, .width = 4},
        // Exactly on the slide-out's terminal: rides the slide-out's own segment, unpitched.
        FretHandPosition{.position = slide_out_arrival, .fret = 9, .width = 4},
        // An ordinary move with no glide under it: the margin morph.
        FretHandPosition{.position = GridPosition{.measure = 3, .beat = 1}, .fret = 2, .width = 5},
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const HighwayViewState state = makeHighwayViewState(arrangement, tempo_map, {}, {});
    REQUIRE(state.chart.fret_hand_positions.size() == 3);
    REQUIRE(state.fret_hand.track.size() == 3);

    // One arrival per placement, at the placement's instant with its ramp and family.
    for (std::size_t index = 0; index < state.fret_hand.track.size(); ++index)
    {
        const HighwayHandArrival& arrival = state.fret_hand.track[index];
        const FhpViewState& placement = state.chart.fret_hand_positions[index];
        CHECK(arrival.seconds == Catch::Approx(placement.seconds));
        CHECK(arrival.ramp_seconds == Catch::Approx(placement.ramp_seconds));
        CHECK(arrival.unpitched_ramp == placement.unpitched_ramp);
    }
    // Its lines are the settled window's edges: frets [fret, fret + width - 1] sit on lines
    // fret - 1 to fret + width - 1.
    CHECK(state.fret_hand.track[0].low_line == Catch::Approx(4.0));
    CHECK(state.fret_hand.track[0].high_line == Catch::Approx(8.0));
    CHECK(state.fret_hand.track[1].low_line == Catch::Approx(8.0));
    CHECK(state.fret_hand.track[1].high_line == Catch::Approx(12.0));
    CHECK(state.fret_hand.track[2].low_line == Catch::Approx(1.0));
    CHECK(state.fret_hand.track[2].high_line == Catch::Approx(6.0));

    // The slide-out arrival settles over the crop zone: from where the margin before the head
    // begins — the lattice-floored margin, which at 120 BPM lands on the lattice exactly — to the
    // arrival. The zone begins exactly where the gliding note's ink stops.
    const HighwayHandArrival& slide_out = state.fret_hand.track[1];
    REQUIRE(slide_out.unpitched_ramp);
    const double crop_seconds = tempo_map.secondsAtGlobalBeatPosition(
        globalBeatPosition(tempo_map, marginBefore(tempo_map, slide_out_arrival)));
    CHECK(slide_out.settle_seconds == Catch::Approx(slide_out.seconds - crop_seconds));
    CHECK(slide_out.settle_seconds == Catch::Approx(g_minimum_sustain_distance_seconds));
    REQUIRE_FALSE(state.chart.notes.empty());
    CHECK(
        slide_out.settle_seconds ==
        Catch::Approx(slide_out.seconds - state.chart.notes.front().ink_end_seconds));
    CHECK(slide_out.settle_seconds <= slide_out.ramp_seconds);

    // The margin morphs settle over nothing: their ramp is already the pitched curve's tangential
    // arrival.
    CHECK_FALSE(state.fret_hand.track[0].unpitched_ramp);
    CHECK_FALSE(state.fret_hand.track[2].unpitched_ramp);
    CHECK(std::is_eq(state.fret_hand.track[0].settle_seconds <=> 0.0));
    CHECK(std::is_eq(state.fret_hand.track[2].settle_seconds <=> 0.0));
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
    note.ring_end_seconds = onset;
    note.ink_end_seconds = onset;
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

// The emphasis axis, composable with the two mutes above for the same reason: a repeat box renders
// every mute profile at every loudness, so a case sweeping both reads as the grid it is.
[[nodiscard]] NoteViewState struckAt(NoteViewState note, const NoteEmphasis emphasis)
{
    note.emphasis = emphasis;
    return note;
}

// A strummed-shape span holding the given posture, entries ascending by string.
//
// Both ends take the one value: the board reads the drawn extent alone, and a fixture whose close
// sat at the default would be a span that closed before it was drawn — a state no projection emits.
[[nodiscard]] ShapeViewState chordShape(
    const double start, const double end, const std::vector<std::pair<int, int>>& posture)
{
    ShapeViewState shape;
    shape.start_seconds = start;
    shape.drawn_end_seconds = end;
    shape.close_seconds = end;
    shape.arpeggio = false;
    for (const auto& [string, fret] : posture)
    {
        shape.strings.push_back(ShapeStringViewState{.string = string, .stop = frettedStop(fret)});
    }
    return shape;
}

} // namespace

// A tapped harmonic's light rides the NODE path: each keyframe arrival asks the drawn sounding
// position exactly like the onset seed, so a glide from fret 5 to 9 under a node at 17 walks the
// light 17 -> 21 — never 17 -> 9, the stop path the head does not draw.
TEST_CASE("Highway tap light glides a tapped harmonic along its node", "[core][highway]")
{
    NoteViewState note;
    note.start_seconds = 1.0;
    note.ring_end_seconds = 2.0;
    note.ink_end_seconds = 2.0;
    note.string = 1;
    note.fret = 5;
    note.attack = NoteAttack::Tap;
    note.harmonic_node = 17.0;
    note.slides = {KeyframeViewState{.seconds = 2.0, .fret = 9, .offset = Fraction{}}};

    const std::vector<NoteViewState> notes{note};
    const HighwayHandLight light = makePickHandLight(notes, std::vector<double>(1, 0.0));

    REQUIRE_FALSE(light.track.empty());
    // The node's fret spans lines node - 1 to node.
    CHECK(light.track.front().high_line == Catch::Approx(17.0));
    CHECK(light.track.back().high_line == Catch::Approx(21.0));
    CHECK(light.track.back().low_line == Catch::Approx(20.0));
}

namespace
{

// A chart with nothing but the notes a floor-light case needs and no placements: the default map
// is 4/4 at 120 BPM, so a beat is half a second, a measure two, and the arrival margin an eighth.
[[nodiscard]] Arrangement makeLightArrangement(std::vector<ChartNote> notes)
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = std::move(notes);
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    return arrangement;
}

// One note of a floor-light case, the fields such a case varies named and the rest at rest.
[[nodiscard]] ChartNote lightNote(
    const GridPosition position, const int string, const int fret, const Fraction sustain,
    const NoteAttack attack = NoteAttack::Pick)
{
    return ChartNote{
        .position = position,
        .string = string,
        .fret = fret,
        .sustain = sustain,
        .attack = attack,
        .bend = {},
        .keyframes = {},
    };
}

// A sustainless tap for the picking hand's cases, released where it starts.
[[nodiscard]] NoteViewState tapAt(const double start, const int string, const int fret)
{
    NoteViewState note;
    note.start_seconds = start;
    note.ring_end_seconds = start;
    note.ink_end_seconds = start;
    note.string = string;
    note.fret = fret;
    note.attack = NoteAttack::Tap;
    return note;
}

// The instant one arrival margin before a grid position: where a light rising into a note there
// begins.
[[nodiscard]] double marginStartSeconds(const TempoMap& tempo_map, const GridPosition position)
{
    return tempo_map.secondsAtGlobalBeatPosition(
        globalBeatPosition(tempo_map, marginBefore(tempo_map, position)));
}

// THE SPAN NEVER OPENS A LIGHT: every lit stretch of the fretting hand starts at the onset of a
// note that is evidence for it, never at a span's own start with nothing struck there. A span's
// evidence carries no rise, so a stretch it opened would switch on with no approach at all.
void checkEveryStretchOpensOnANote(const HighwayViewState& state)
{
    for (const HighwayLitStretch& stretch : state.fret_hand.lit)
    {
        CHECK(std::ranges::any_of(state.chart.notes, [&stretch](const NoteViewState& note) {
            const bool evidence = !rightHandOnset(note.attack) || note.held.value_or(0) > 0;
            return evidence &&
                   std::abs(note.start_seconds - stretch.start_seconds) < g_onset_match_epsilon;
        }));
    }
}

} // namespace

// An open string is evidence by ruling: it is drawn as a bar spanning the window, so a dark window
// under it would read as a floating bar. Its light rises over the arrival margin before the note —
// the same marginBefore the hand's own morph is led by — and releases at the drawn end.
TEST_CASE("Fret-hand light lights a lone open note with a margin rise", "[core][highway][light]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const GridPosition onset{.measure = 2, .beat = 1};
    const HighwayViewState state = makeHighwayViewState(
        makeLightArrangement({lightNote(onset, 1, 0, Fraction{1})}), tempo_map, {}, {});

    REQUIRE(state.chart.notes.size() == 1);
    const NoteViewState& open = state.chart.notes.front();
    REQUIRE(state.fret_hand.lit.size() == 1);
    const HighwayLitStretch& lit = state.fret_hand.lit.front();
    CHECK(lit.start_seconds == Catch::Approx(open.start_seconds));
    CHECK(lit.release_seconds == Catch::Approx(open.ink_end_seconds));
    CHECK(
        lit.rise_seconds ==
        Catch::Approx(open.start_seconds - marginStartSeconds(tempo_map, onset)));
    CHECK(lit.rise_seconds == Catch::Approx(g_minimum_sustain_distance_seconds));
}

// A bare tap proves nothing about the fretting hand, so it leaves that hand dark while the tap
// itself still strikes and lights the picking hand. A tap whose held stop is pressed is the
// fretting hand holding that stop, lit through the claim.
TEST_CASE("Fret-hand light ignores a bare tap and lights a claimed one", "[core][highway][light]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    ChartNote tap =
        lightNote(GridPosition{.measure = 2, .beat = 1}, 1, 12, Fraction{1}, NoteAttack::Tap);

    const HighwayViewState bare =
        makeHighwayViewState(makeLightArrangement({tap}), tempo_map, {}, {});
    CHECK(bare.fret_hand.lit.empty());
    CHECK(bare.tap_onsets.size() == 1);
    CHECK(bare.pick_hand.lit.size() == 1);

    tap.held = 5;
    const HighwayViewState claimed =
        makeHighwayViewState(makeLightArrangement({tap}), tempo_map, {}, {});
    REQUIRE(claimed.chart.notes.size() == 1);
    REQUIRE(claimed.fret_hand.lit.size() == 1);
    CHECK(
        claimed.fret_hand.lit.front().start_seconds ==
        Catch::Approx(claimed.chart.notes.front().start_seconds));
    CHECK(
        claimed.fret_hand.lit.front().release_seconds ==
        Catch::Approx(claimed.chart.notes.front().ink_end_seconds));
}

// A pick slide is the picking hand dragging across the strings, and without a claim it says
// nothing about the fretting hand — while a left tap and a natural harmonic are that hand's own.
TEST_CASE("Fret-hand light reads the fretting hand's techniques only", "[core][highway][light]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    ChartNote scrape =
        lightNote(GridPosition{.measure = 2, .beat = 1}, 5, 17, Fraction{1}, NoteAttack::PickSlide);
    scrape.keyframes = {Keyframe{.offset = Fraction{1}, .fret = 9}};
    const HighwayViewState scraped =
        makeHighwayViewState(makeLightArrangement({scrape}), tempo_map, {}, {});
    CHECK(scraped.fret_hand.lit.empty());
    CHECK_FALSE(scraped.pick_hand.lit.empty());

    // A left tap and, two measures later, a natural harmonic on a between-fret node: two lights,
    // each opened by its own note.
    ChartNote harmonic = lightNote(GridPosition{.measure = 4, .beat = 1}, 3, 3, Fraction{1, 8});
    harmonic.harmonic_node = 3.2;
    const HighwayViewState state = makeHighwayViewState(
        makeLightArrangement(
            {lightNote(
                 GridPosition{.measure = 2, .beat = 1}, 4, 7, Fraction{1}, NoteAttack::LeftTap),
             harmonic}),
        tempo_map,
        {},
        {});
    REQUIRE(state.chart.notes.size() == 2);
    REQUIRE(state.fret_hand.lit.size() == 2);
    CHECK(
        state.fret_hand.lit[0].start_seconds == Catch::Approx(state.chart.notes[0].start_seconds));
    CHECK(
        state.fret_hand.lit[1].start_seconds == Catch::Approx(state.chart.notes[1].start_seconds));
}

// Both hands release by one rule: a DRAWN unpitched slide-out lets go at the last pitched keyframe
// (pressure is already coming off), while one past the ink end is not drawn, so the note holds to
// its ink end and releases there.
TEST_CASE("Fret-hand light releases a slide-out at its last pitch", "[core][highway][light]")
{
    NoteViewState trailing;
    trailing.start_seconds = 1.0;
    trailing.ring_end_seconds = 2.5;
    trailing.ink_end_seconds = 2.5;
    trailing.fret = 10;
    trailing.slides = {
        KeyframeViewState{.seconds = 1.5, .fret = 12, .offset = Fraction{}},
        KeyframeViewState{.seconds = 2.0, .fret = 13, .offset = Fraction{}},
        KeyframeViewState{.seconds = 2.5, .fret = 8, .offset = Fraction{}, .slide_out = true},
    };
    REQUIRE(keyframeDrawn(trailing.slides.back(), trailing.ink_end_seconds));
    ChartViewState scene;
    scene.notes = {trailing};
    const std::vector<double> rise{0.125};
    CHECK(
        makeFretHandLight(scene, rise) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.125},
        });

    scene.notes.front().ink_end_seconds = 2.25;
    REQUIRE_FALSE(keyframeDrawn(scene.notes.front().slides.back(), 2.25));
    CHECK(
        makeFretHandLight(scene, rise) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.25, .rise_seconds = 0.125},
        });
}

// A span is evidence over its drawn extent: chord heads whose tails the repeat treatment emptied
// still hold the grip, so the light stays lit to the span's drawn end — and the span, starting at
// the chord's onset, adds no rise of its own.
TEST_CASE("Fret-hand light holds through a span over emptied tails", "[core][highway][light]")
{
    ChartViewState scene;
    scene.notes = {chordNote(1.0, 1, 5), chordNote(1.0, 2, 7)};
    scene.shapes = {chordShape(1.0, 3.0, {{1, 5}, {2, 7}})};
    CHECK(
        makeFretHandLight(scene, std::vector<double>{0.125, 0.125}) ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 3.0, .rise_seconds = 0.125},
        });
}

// THE SPAN NEVER OPENS A LIGHT, over a real derivation: three fingers glide into a landing and
// ring on, which opens a carry-opened SUCCESSOR span at the landing — a span that starts where no
// note does. It tiles onto its predecessor and the gliding rings cover it, so it never opens a
// stretch; a rise taken from nothing would switch the light on with no approach.
TEST_CASE("Fret-hand light is never opened by a carry-opened span", "[core][highway][light]")
{
    const auto glide = [](const int string, const int fret, const Fraction sustain) {
        ChartNote note = lightNote(GridPosition{.measure = 1, .beat = 1}, string, fret, sustain);
        note.keyframes = {Keyframe{.offset = Fraction{2}, .fret = fret + 2}};
        return note;
    };
    const HighwayViewState state = makeHighwayViewState(
        makeLightArrangement(
            {glide(1, 5, Fraction{4}),
             glide(2, 7, Fraction{6}),
             glide(4, 11, Fraction{6}),
             lightNote(GridPosition{.measure = 2, .beat = 1}, 1, 7, Fraction{2})}),
        makeHighwayTempoMap(),
        {},
        {});

    // The departing grip and the landing's successor, which starts at the landing (1.0 s) where
    // no note is struck.
    REQUIRE(state.chart.shapes.size() == 2);
    const double landing = state.chart.shapes[1].start_seconds;
    CHECK(std::ranges::none_of(state.chart.notes, [landing](const NoteViewState& note) {
        return std::abs(note.start_seconds - landing) < g_onset_match_epsilon;
    }));
    CHECK(state.fret_hand.lit.size() == 1);
    checkEveryStretchOpensOnANote(state);
}

// THE 9731dcee DISCRIMINATOR, the user's repro: a chord under a span in measure 1, measure 2
// emptied of notes, and the chord returning in measure 3. The rest is longer than the tolerance,
// so the light goes out and comes back: TWO stretches, the second starting at the returning chord
// with a one-margin rise that never reaches back through the rest. It guards the order of the
// merge: the silence in front of a statement is measured before the span the statement opens is
// folded in, so that span can never erase the rest.
TEST_CASE(
    "Fret-hand light goes out through a measure a chord returns from", "[core][highway][light]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const auto chord_note = [](const int measure, const int string) {
        return lightNote(GridPosition{.measure = measure, .beat = 1}, string, 5, Fraction{1});
    };
    const HighwayViewState state = makeHighwayViewState(
        makeLightArrangement(
            {chord_note(1, 1), chord_note(1, 2), chord_note(3, 1), chord_note(3, 2)}),
        tempo_map,
        {},
        {});

    REQUIRE(state.chart.notes.size() == 4);
    REQUIRE(state.chart.shapes.size() == 2);
    // The first light releases at the later of the first chord's ink ends and its span's drawn
    // end; the second opens at the returning chord.
    const double first_release = std::max(
        {state.chart.notes[0].ink_end_seconds,
         state.chart.notes[1].ink_end_seconds,
         state.chart.shapes[0].drawn_end_seconds});
    const double returning = state.chart.notes[2].start_seconds;
    REQUIRE(returning - first_release >= g_hand_rest_seconds);

    REQUIRE(state.fret_hand.lit.size() == 2);
    CHECK(state.fret_hand.lit[0].release_seconds == Catch::Approx(first_release));
    CHECK(state.fret_hand.lit[1].start_seconds == Catch::Approx(returning));
    CHECK(
        state.fret_hand.lit[1].rise_seconds ==
        Catch::Approx(
            returning - marginStartSeconds(tempo_map, GridPosition{.measure = 3, .beat = 1})));
    CHECK(
        state.fret_hand.lit[1].start_seconds - state.fret_hand.lit[1].rise_seconds > first_release);
    checkEveryStretchOpensOnANote(state);
}

// The picking hand pulses: every strike of a dense run is its own light over the one track, the
// dip between strikes mirroring the finger lifting — spaced here past the picking hand's own
// tolerance, whatever its value. Only strikes that overlap merge, their gap being negative.
TEST_CASE("Pick-hand light lights each strike of a dense run on its own", "[core][highway][light]")
{
    const double step = g_pick_light_rest_seconds + 0.25;
    const std::vector<NoteViewState> run{
        tapAt(1.0, 1, 12),
        tapAt(1.0 + step, 1, 14),
        tapAt(1.0 + (2.0 * step), 1, 12),
        tapAt(1.0 + (3.0 * step), 1, 15),
    };
    const HighwayHandLight light = makePickHandLight(run, std::vector<double>(run.size(), 0.125));
    CHECK(light.track.size() == run.size());
    REQUIRE(light.lit.size() == run.size());
    for (std::size_t index = 0; index < run.size(); ++index)
    {
        CHECK(std::is_eq(light.lit[index].start_seconds <=> run[index].start_seconds));
    }

    // A held tap and a second strike landing inside its hold: one light.
    NoteViewState held = tapAt(1.0, 1, 12);
    held.ring_end_seconds = 2.0;
    held.ink_end_seconds = 2.0;
    const std::vector<NoteViewState> overlapping{held, tapAt(1.5, 2, 14)};
    const HighwayHandLight one =
        makePickHandLight(overlapping, std::vector<double>(overlapping.size(), 0.125));
    CHECK(
        one.lit ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.125},
        });
}

// ONE TRACK: when a later strike lands while an earlier one is still travelling, the hand has
// moved on — the earlier strike's arrivals at or after the later onset are dropped, and the later
// strike takes the window from its own onset.
TEST_CASE("Pick-hand track leaves an earlier strike at the next onset", "[core][highway][light]")
{
    // A tapped glide from fret 12 at 1.0 to fret 15 at 2.0, and a tap at fret 17 on another string
    // at 1.5, halfway along it.
    NoteViewState gliding = tapAt(1.0, 1, 12);
    gliding.ring_end_seconds = 2.0;
    gliding.ink_end_seconds = 2.0;
    gliding.slides = {KeyframeViewState{.seconds = 2.0, .fret = 15, .offset = Fraction{}}};
    const std::vector<NoteViewState> notes{gliding, tapAt(1.5, 2, 17)};

    const HighwayHandLight light = makePickHandLight(notes, std::vector<double>(2, 0.125));
    REQUIRE(light.track.size() == 2);
    CHECK(light.track[0].seconds == Catch::Approx(1.0));
    CHECK(light.track[0].low_line == Catch::Approx(11.0));
    // The glide's landing at 2.0 is gone: the later strike holds the window from 1.5 on, arriving
    // there instantly as every strike does.
    CHECK(light.track[1].seconds == Catch::Approx(1.5));
    CHECK(light.track[1].low_line == Catch::Approx(16.0));
    CHECK(light.track[1].high_line == Catch::Approx(17.0));
    CHECK(std::is_eq(light.track[1].ramp_seconds <=> 0.0));
    // The later strike lands inside the glide's hold, so the two overlap and are one light, lit
    // through the glide's release.
    CHECK(
        light.lit ==
        std::vector<HighwayLitStretch>{
            HighwayLitStretch{.start_seconds = 1.0, .release_seconds = 2.0, .rise_seconds = 0.125},
        });
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
    const HighwayChordGroupViewState& strum = grouping.groups[0];
    CHECK(strum.first == 0);
    CHECK(strum.count == 3);
    CHECK(strum.fretting_hand_count == 2);
    // One accented member makes the strum accented; the other two are normal.
    CHECK(strum.emphasis == NoteEmphasis::Accent);
    CHECK_FALSE(strum.all_palm_muted);
    CHECK_FALSE(strum.all_dead);
    // A strum no span covers states its own chord: the full box, never the repeat treatment.
    CHECK(strum.box_treatment == HighwayChordBoxTreatment::Full);
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
    const auto mutes_of = [](const std::vector<NoteViewState>& notes) {
        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, {});
        REQUIRE(grouping.groups.size() == 1);
        const HighwayChordGroupViewState& group = grouping.groups.front();
        return std::pair{group.all_palm_muted, group.all_dead};
    };

    // Unanimously palm muted, unanimously dead, and unanimously both.
    CHECK(
        mutes_of({palmMuted(chordNote(1.0, 1, 3)), palmMuted(chordNote(1.0, 2, 5))}) ==
        std::pair{true, false});
    CHECK(
        mutes_of({deadened(chordNote(1.0, 1, 3)), deadened(chordNote(1.0, 2, 5))}) ==
        std::pair{false, true});
    CHECK(
        mutes_of(
            {palmMuted(deadened(chordNote(1.0, 1, 3))),
             palmMuted(deadened(chordNote(1.0, 2, 5)))}) == std::pair{true, true});

    // A dead string inside a palm-muted chord: every member is palmed, only one is dead. Palm and
    // dead are separate axes, so the palm unanimity survives — a single mute axis would collapse it
    // to nothing.
    CHECK(
        mutes_of({palmMuted(chordNote(1.0, 1, 3)), palmMuted(deadened(chordNote(1.0, 2, 5)))}) ==
        std::pair{true, false});

    // And a strum every member of which is dead while only one is palmed stays a dead strum.
    CHECK(
        mutes_of({deadened(chordNote(1.0, 1, 3)), palmMuted(deadened(chordNote(1.0, 2, 5)))}) ==
        std::pair{false, true});
}

// The group's emphasis is what a box STANDING IN for the heads states, so the two folds differ on
// purpose: loud is existential, quiet unanimous. A strum is not played softly while part of it is
// struck normally, and one accent among ghosts still makes the strum an accented one.
TEST_CASE("Highway chord groups fold emphasis loud-wins, quiet-unanimous", "[core][highway]")
{
    const auto group_emphasis = [](const std::vector<NoteEmphasis>& members) {
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
    CHECK(group_emphasis({Normal, Normal}) == Normal);
    CHECK(group_emphasis({Ghost, Ghost, Ghost}) == Ghost);
    // A part-ghosted strum is not a quiet strum.
    CHECK(group_emphasis({Ghost, Normal}) == Normal);
    CHECK(group_emphasis({Accent, Normal}) == Accent);
    // Loud outranks quiet the way it does on a single note carrying both claims.
    CHECK(group_emphasis({Accent, Ghost}) == Accent);
}

// The repeat chain (Charter's chord visibility rules): under a covering shape, a strum that
// restates the posture of an earlier non-muted run renders as the repeat box alone. Two boundary
// cases are pinned here, both untestable while this decision lived inside the renderer: the chain's
// first strum sitting a rounding epsilon BELOW the shape start must still anchor the walk (else the
// box flickers), and a strum at the shape's END is still under the span (a strict comparison would
// drop the last strum from repeat treatment).
TEST_CASE("Highway chord groups give repeating strums the box treatment", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
    const double epsilon_below = 1.0 - (g_onset_match_epsilon / 2.0);
    const std::vector<NoteViewState> notes{
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
    CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
    CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    CHECK(grouping.groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
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
    CHECK(broken.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);

    const HighwayChordGrouping resolved = grouped(LegatoMotion::Hammer);
    REQUIRE(resolved.groups.size() == 2);
    CHECK(resolved.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
}

// A BOX MARKS SIMULTANEITY (LAW IV): any two-or-more-string strike wears one, inside a span and
// outside one alike. A partial restrike is still two strings struck together, so it states its own
// chord — and it wears THE STANDARD CHORD BOX (Q2), never a narrowed one, because the arpeggio
// context is already carried by the span's borders and the brackets standing on the fretboard. What
// keeps it from LYING is the identity law, not a missing box: a repeat only ever follows an
// IDENTICAL preceding onset, and a partial is not the same notes as the whole.
TEST_CASE("Highway chord groups box a partial strike with the standard box", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}, {3, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 4.0, posture)};
    const std::vector<NoteViewState> notes{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        chordNote(1.0, 3, 5),
        // A partial restrike: two of the three strings the shape sounds.
        chordNote(2.0, 1, 3),
        chordNote(2.0, 2, 5),
        // The same partial again, with nothing between.
        chordNote(3.0, 1, 3),
        chordNote(3.0, 2, 5),
        chordNote(4.0, 1, 3),
        chordNote(4.0, 2, 5),
        chordNote(4.0, 3, 5),
    };

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

    REQUIRE(grouping.groups.size() == 4);
    CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
    // The partial gets its OWN full box, not a repeat of the whole shape's: different notes are a
    // different onset however little sits between them.
    CHECK(grouping.groups[1].fretting_hand_count == 2);
    CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    // THE STANDARD box, not a narrowed one (Q2): the arpeggio context is already carried by the
    // span's borders and the brackets on the fretboard, so the box restates nothing by matching the
    // shape's width. The struck count still differs, because the count is what the box treatment is
    // decided FROM.
    CHECK(grouping.groups[0].fretting_hand_count == 3);
    // An identical partial after that partial DOES repeat: same struck strings, same frets,
    // nothing between, one span.
    CHECK(grouping.groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
    // And the whole shape after the partials re-heads, because the onset before it differs.
    CHECK(grouping.groups[3].fretting_hand_count == 3);
    CHECK(grouping.groups[3].box_treatment == HighwayChordBoxTreatment::Full);
}

// EVERY QUESTION HERE IS THE FRETTING HAND'S (review N3/N4). A right-hand onset is the other hand,
// so it never counts toward the strum, folds into its unanimities, states a fret its identity
// compares, or is scanned by the capability gate. A mixed reading gets it wrong in both
// directions, and this pins both.
TEST_CASE("Highway chord groups read the fretting hand alone", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 5.0, posture)};

    SECTION("a tap over identical chugs keeps the repeat, riding on top")
    {
        // Two identical dead chugs with a TAP on a third string over the second. Letting the tap's
        // own fret into the repeat identity would compare the second onset DIFFERENT and re-head
        // it; letting its sustain reach the has_tails scan would force a full box even without
        // that.
        std::vector<NoteViewState> notes{
            deadened(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            deadened(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
            chordNote(2.0, 3, 12, NoteAttack::Tap),
        };
        notes[4].ring_end_seconds = 2.5;
        notes[4].ink_end_seconds = 2.5;

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        // The chug run CONTINUES with the tap riding over it: two fretting-hand members, the same
        // strings at the same frets, and a profile the repeat box draws itself.
        CHECK(grouping.groups[1].fretting_hand_count == 2);
        CHECK(grouping.groups[1].all_dead);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("a tap REPLACING a member breaks the repeat, because the fretting set shrank")
    {
        // The retreat the exclusion leaves standing, and it falls out of the exact string-set
        // comparison rather than needing a rule: swap one chug member for a tap and the fretting
        // content is a different onset, so it wears its own full box.
        const std::vector<NoteViewState> notes{
            deadened(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            deadened(chordNote(2.0, 1, 3)),
            chordNote(2.0, 2, 5, NoteAttack::Tap),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[1].fretting_hand_count == 1);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::None);
    }

    SECTION("a group of TAPS alone is no strum at all")
    {
        // Letting taps fill the identity would make two tapped groups compare equal, and the second
        // would draw a repeat box for a strum nobody played.
        const std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 12, NoteAttack::Tap),
            chordNote(1.0, 2, 12, NoteAttack::Tap),
            chordNote(2.0, 1, 12, NoteAttack::Tap),
            chordNote(2.0, 2, 12, NoteAttack::Tap),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::None);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::None);
    }
}

// THE GATE's two remaining halves, on figures nothing else exercises. A returning chord after a
// chug run must re-head — it is a different PROFILE only, so the identity lets it repeat and the
// TAIL it presents is what refuses it — and two identical chugs under NO span never repeat at all,
// because a span boundary is the only thing that can separate two statements the onsets compare
// equal.
TEST_CASE("Highway chord groups gate a returning chord and an unspanned pair", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};

    SECTION("a SUSTAINED chord returning after dead chugs re-heads on its tail")
    {
        // The profile is free, so the identity alone would repeat this — same strings, same frets.
        // The capability gate is what refuses it, and honestly: a box is drawn at an instant and
        // has nowhere to put a sustain. The chord SUSTAINS, which is what makes the check real
        // rather than a bare re-statement of the identity law.
        std::vector<NoteViewState> notes{
            deadened(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            chordNote(2.0, 1, 3),
            chordNote(2.0, 2, 5),
        };
        notes[2].ring_end_seconds = 3.5;
        notes[2].ink_end_seconds = 3.5;
        notes[3].ring_end_seconds = 3.5;
        notes[3].ink_end_seconds = 3.5;
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 4.0, posture)};

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        // Nothing precedes the chug, so it opens the run with a full box of its own.
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);

        // The discriminator: the SAME return with no tail repeats, wearing its own plain profile.
        notes[2].ring_end_seconds = 2.0;
        notes[2].ink_end_seconds = 2.0;
        notes[3].ring_end_seconds = 2.0;
        notes[3].ink_end_seconds = 2.0;
        const HighwayChordGrouping tailless = makeHighwayChordGroups(notes, shapes);
        REQUIRE(tailless.groups.size() == 2);
        CHECK(tailless.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("two identical chugs under NO span are two statements")
    {
        const std::vector<NoteViewState> notes{
            deadened(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            deadened(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, {});

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }
}

// A lone note is not simultaneous, so it wears no box wherever it falls — inside an arpeggio span
// included. The one case \ref HighwayChordBoxTreatment::None is left with.
TEST_CASE("Highway chord groups leave a lone note inside a span boxless", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}, {3, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
    const std::vector<NoteViewState> notes{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        chordNote(1.0, 3, 5),
        chordNote(2.0, 2, 5),
    };

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

    REQUIRE(grouping.groups.size() == 2);
    CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
    CHECK(grouping.groups[1].fretting_hand_count == 1);
    CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::None);
}

// THE CONSECUTIVENESS LAW's other half: an onset of ANY kind between two identical chords breaks
// the run, which is where "repeats look odd in arpeggio spans" actually lived.
TEST_CASE("Highway chord groups break a repeat run on any interleaved onset", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
    const std::vector<NoteViewState> notes{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        // A single pick of one member, between the two identical strums.
        chordNote(2.0, 1, 3),
        chordNote(3.0, 1, 3),
        chordNote(3.0, 2, 5),
    };

    const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

    REQUIRE(grouping.groups.size() == 3);
    CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
    CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::None);
    // NOT a repeat: the immediately preceding onset is the interleaved pick, not the chord.
    CHECK(grouping.groups[2].box_treatment == HighwayChordBoxTreatment::Full);

    // The control, one note apart: with nothing between, the same two strums repeat.
    const std::vector<NoteViewState> adjacent{
        chordNote(1.0, 1, 3),
        chordNote(1.0, 2, 5),
        chordNote(3.0, 1, 3),
        chordNote(3.0, 2, 5),
    };
    const HighwayChordGrouping unbroken = makeHighwayChordGroups(adjacent, shapes);
    REQUIRE(unbroken.groups.size() == 2);
    CHECK(unbroken.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
}

// THE IDENTITY COMPARES WHERE THE HEADS SOUND (soundingStopAt), not the fret column and not the
// grip: a repeat box draws no heads, so it may only stand in for an onset whose heads are the ones
// the onset before it drew. Both halves of the harmonic defect are pinned — a node chord read as
// an open chord through `fret`, and an artificial-harmonic chord read as its pressed frets through
// the grip. The node chord itself never repeats (the capability gate folds a node into its marks
// scan), so in both figures the FOLLOWER is where the false box appeared.
TEST_CASE("Highway chord groups compare sounding places, not frets or grips", "[core][highway]")
{
    SECTION("an open chord after a natural-harmonic chord on the same strings re-heads")
    {
        // Through `fret` both onsets are 0/0 on strings one and two, and the open chord following
        // the chime drew a headless repeat box for a strum that never repeated.
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, {{1, 0}, {2, 0}})};
        std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 0),
            chordNote(1.0, 2, 0),
            chordNote(2.0, 1, 0),
            chordNote(2.0, 2, 0),
        };
        notes[0].harmonic_node = 12.0;
        notes[1].harmonic_node = 12.0;

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }

    SECTION("a plain chord after an artificial-harmonic chord at the same frets re-heads")
    {
        // The half a GRIP reading leaves live: a fret-5 chord damped at node 17 presses the same
        // stops as the plain fret-5 chord after it, so the grips compare identical and the plain
        // chord wore a repeat box implying the squeal repeats. Its heads sound twelve frets from
        // where the first chord's did, so they are not heads a repeat box may stand in for.
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, {{1, 5}, {2, 5}})};
        std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 5),
            chordNote(1.0, 2, 5),
            chordNote(2.0, 1, 5),
            chordNote(2.0, 2, 5),
        };
        notes[0].harmonic_node = 17.0;
        notes[1].harmonic_node = 17.0;

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }
}

// THE CONSECUTIVENESS LAW: an onset wears a repeat box iff it is identical to the IMMEDIATELY
// PRECEDING onset, within the same span, with no onset of any kind between — where identical means
// the same struck strings at the same frets, PROFILE FREE. Every re-head below is that one rule: a
// rest is a span boundary, a fresh grip is a span boundary, and a differing onset before it is
// simply not the same onset. Nothing may be skipped over on the way to a matching run further away
// (the rule F10 asked for, and the ruleset's dead list refuses).
TEST_CASE("Highway chord groups repeat only after the identical onset", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};

    SECTION("a dead chug chain shows its first strum and blanks the rest")
    {
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
        const std::vector<NoteViewState> chugs{
            deadened(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            deadened(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
            deadened(chordNote(3.0, 1, 3)),
            deadened(chordNote(3.0, 2, 5)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(chugs, shapes);

        REQUIRE(grouping.groups.size() == 3);
        CHECK(grouping.groups[0].all_dead);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
        CHECK(grouping.groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("a dead chug after a ringing chord of the same frets REPEATS")
    {
        // THE DISCRIMINATOR for a profile-free identity. Rule 11 makes the chord and its chugs ONE
        // span — a change in articulation does not split it — and the identity ignores the profile,
        // so the first chug is an X'd REPEAT box wearing its own mark rather than a full re-head.
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 2.5, posture)};
        const std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            deadened(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
            deadened(chordNote(2.5, 1, 3)),
            deadened(chordNote(2.5, 2, 5)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 3);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].all_dead);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
        // And the chugs after it go on repeating: nothing about the run's head is special.
        CHECK(grouping.groups[2].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("a dead chug at FRESH frets is a new position and shows its heads")
    {
        // The discrimination the identity turns on: with the hand somewhere else, a chug states a
        // chord the reader has not been shown, so it heads its own run and its X'd heads print
        // (this board does not blank every dead chug). The section above is its partner — profile
        // free, POSITION strict.
        const std::vector<std::pair<int, int>> moved{{1, 7}, {2, 9}};
        const std::vector<ShapeViewState> shapes{
            chordShape(1.0, 1.5, posture), chordShape(2.0, 2.5, moved)
        };
        const std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            deadened(chordNote(2.0, 1, 7)),
            deadened(chordNote(2.0, 2, 9)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].all_dead);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }

    SECTION("the palm resting on the strings does not stop a chug chain repeating")
    {
        // Both mute flags at once is one statement's own articulation, so the chugs merge into one
        // span and the box carries BOTH marks for the strums it stands in for.
        const std::vector<ShapeViewState> shapes{chordShape(1.0, 3.0, posture)};
        const std::vector<NoteViewState> notes{
            palmMuted(deadened(chordNote(1.0, 1, 3))),
            palmMuted(deadened(chordNote(1.0, 2, 5))),
            palmMuted(deadened(chordNote(2.0, 1, 3))),
            palmMuted(deadened(chordNote(2.0, 2, 5))),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].all_dead);
        CHECK(grouping.groups[1].all_palm_muted);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("silence between two soundings of one grip re-heads the chain")
    {
        // The motivating figure, and the gap law's own consequence: a chord whose stored ring stops
        // short of the next one leaves a REST, a rest is the hand free to lift and mute, and rule
        // 11a therefore ends the statement there. The identical chord after it is a new statement,
        // so it draws its FULL box with heads — across silence the grip is unstated, and a headless
        // repeat box would be a guess about a hand nobody watched.
        const std::vector<ShapeViewState> shapes{
            chordShape(1.0, 1.5, posture), chordShape(9.0, 9.5, posture)
        };
        const std::vector<NoteViewState> notes{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            chordNote(9.0, 1, 3),
            chordNote(9.0, 2, 5),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(notes, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }
}

// THE DISPLAY-CAPABILITY GATE, which the identity law above leaves exactly as it was — and which
// now carries more weight, since a profile CHANGE reaches it instead of re-heading. A repeat
// box draws no heads, so it may only stand in for a strum whose entire statement the box itself
// carries: one of the four mute profiles it has a mark for, composed with the emphasis it already
// carries. Every combination of the two axes is a valid repeat render, and a profile the box cannot
// draw falls back to the full box, which keeps its heads and therefore keeps every mark on them.
TEST_CASE("Highway repeat boxes render every mute profile at every emphasis", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 2.0, posture)};
    // Two strums of one shape under one span: the first heads the chain, the second is the repeat
    // candidate the gate judges.
    const auto chained =
        [&shapes](NoteViewState (*const mark)(const NoteViewState&), const NoteEmphasis emphasis) {
            std::vector<NoteViewState> notes;
            for (const double onset : {1.0, 2.0})
            {
                notes.push_back(struckAt(mark(chordNote(onset, 1, 3)), emphasis));
                notes.push_back(struckAt(mark(chordNote(onset, 2, 5)), emphasis));
            }
            return makeHighwayChordGroups(notes, shapes);
        };

    const std::array<NoteViewState (*)(const NoteViewState&), 4> profiles{
        [](const NoteViewState& note) { return note; },
        [](const NoteViewState& note) { return palmMuted(note); },
        [](const NoteViewState& note) { return deadened(note); },
        [](const NoteViewState& note) { return palmMuted(deadened(note)); },
    };
    const std::array<NoteEmphasis, 3> emphases{
        NoteEmphasis::Normal, NoteEmphasis::Accent, NoteEmphasis::Ghost
    };
    for (std::size_t profile = 0; profile < profiles.size(); ++profile)
    {
        for (std::size_t loudness = 0; loudness < emphases.size(); ++loudness)
        {
            CAPTURE(profile, loudness);
            const HighwayChordGrouping grouping =
                chained(profiles.at(profile), emphases.at(loudness));
            REQUIRE(grouping.groups.size() == 2);
            CHECK(grouping.groups[0].box_treatment == HighwayChordBoxTreatment::Full);
            CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
            // The box stands in for the heads, so it has to carry what they would have said.
            CHECK(grouping.groups[1].emphasis == emphases.at(loudness));
        }
    }

    SECTION("a mute the strum does not share falls back to the full box")
    {
        // One palm-muted member and one dead one: neither unanimity holds, so the box has no
        // single mark to draw and the heads keep theirs.
        const std::vector<NoteViewState> mixed{
            palmMuted(chordNote(1.0, 1, 3)),
            deadened(chordNote(1.0, 2, 5)),
            palmMuted(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(mixed, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK_FALSE(grouping.groups[1].all_palm_muted);
        CHECK_FALSE(grouping.groups[1].all_dead);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }

    SECTION("a profile CHANGE reaches the gate instead of re-heading")
    {
        // The identity is profile free, so a plain chord followed by the same frets in a profile no
        // box can draw passes the comparison and is refused HERE, by the one rule that is about
        // drawing. That is the whole of what the profile ruling moved: from a re-head decided by
        // the identity to a fallback decided by capability.
        const std::vector<NoteViewState> plain_then_mixed{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            palmMuted(chordNote(2.0, 1, 3)),
            deadened(chordNote(2.0, 2, 5)),
        };

        const HighwayChordGrouping grouping = makeHighwayChordGroups(plain_then_mixed, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);

        // The control, one flag apart: a profile the box CAN draw repeats through the same path.
        const std::vector<NoteViewState> plain_then_palm{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            palmMuted(chordNote(2.0, 1, 3)),
            palmMuted(chordNote(2.0, 2, 5)),
        };
        const HighwayChordGrouping drawable = makeHighwayChordGroups(plain_then_palm, shapes);
        REQUIRE(drawable.groups.size() == 2);
        CHECK(drawable.groups[1].all_palm_muted);
        CHECK(drawable.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    }

    SECTION("a mark outside the mute axis falls back the same way")
    {
        std::vector<NoteViewState> harmonics{
            chordNote(1.0, 1, 3),
            chordNote(1.0, 2, 5),
            chordNote(2.0, 1, 3),
            chordNote(2.0, 2, 5),
        };
        harmonics[2].harmonic_node = 12.0;

        const HighwayChordGrouping grouping = makeHighwayChordGroups(harmonics, shapes);

        REQUIRE(grouping.groups.size() == 2);
        CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Full);
    }
}

// The span-hold take-over cap resolves over the WHOLE song: each group's cap is the next
// note-showing strum wherever it is, and box-only repeats, dead chugs, and single notes continue
// the hold rather than taking it over. Deriving this inside the renderer's window instead would cap
// the last visible group at infinity whenever the taking-over strum sits just past the window's
// edge.
TEST_CASE("Highway chord group hold caps resolve over the whole song", "[core][highway]")
{
    const std::vector<std::pair<int, int>> posture{{1, 3}, {2, 5}};
    const std::vector<ShapeViewState> shapes{chordShape(1.0, 2.5, posture)};
    const std::vector<NoteViewState> notes{
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
    CHECK(grouping.groups[1].box_treatment == HighwayChordBoxTreatment::Repeat);
    // The box-only repeat at 2.0 and the single note at 3.0 pass the hold through, so the strum
    // at 1.0 is capped by the shown strum at 9.0 — far beyond any drawing window.
    CHECK(grouping.groups[0].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(grouping.groups[1].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(grouping.groups[2].hold_cap_seconds == Catch::Approx(9.0));
    CHECK(std::isinf(grouping.groups[3].hold_cap_seconds));
}

// Every pop lands on its strips and belongs to its NOTE's hand: a fretted single pops its fret, a
// slide landing and a bend arrival theirs, a strummed chord its box sides, all on the fretting
// hand; a single tap pops its fret, a tapped chord its box sides, and a tapped glide's landing pops
// on the PICKING hand. Only the bend's strike clamps, against its own arrival on the same fret.
TEST_CASE("Highway strike pops land on their strips, in their note's hand", "[core][highway]")
{
    const HighwayViewState board = popBoard({
        // A fretted slide from 5 to 7: its strike, and its landing half a beat later.
        popNote(
            GridPosition{.measure = 1, .beat = 1},
            3,
            5,
            NoteAttack::Pick,
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7}}),
        // A bend on fret 8: its strike, and its arrival half a beat later on the same fret.
        popNote(
            GridPosition{.measure = 1, .beat = 3},
            2,
            8,
            NoteAttack::Pick,
            {Keyframe{.offset = Fraction{1, 2}, .bend = 1.0}}),
        // A tapped glide from 12 to 14.
        popNote(
            GridPosition{.measure = 2, .beat = 1},
            1,
            12,
            NoteAttack::Tap,
            {Keyframe{.offset = Fraction{1, 2}, .fret = 14}}),
        // A tapped chord.
        popNote(GridPosition{.measure = 2, .beat = 3}, 1, 12, NoteAttack::Tap, {}),
        popNote(GridPosition{.measure = 2, .beat = 3}, 2, 14, NoteAttack::Tap, {}),
        // A strummed chord.
        popNote(GridPosition{.measure = 3, .beat = 1}, 3, 5, NoteAttack::Pick, {}),
        popNote(GridPosition{.measure = 3, .beat = 1}, 4, 5, NoteAttack::Pick, {}),
    });

    const std::vector<HighwayStrikePop>& fretting = board.fret_hand.pops;
    REQUIRE(fretting.size() == 5);
    checkPop(fretting[0], 0.0, g_hit_glow_release_seconds, 5);
    checkPop(fretting[1], 0.25, g_hit_glow_release_seconds, 7);
    checkPop(fretting[2], 1.0, clampedRelease(0.25), 8);
    checkPop(fretting[3], 1.25, g_hit_glow_release_seconds, 8);
    checkPop(fretting[4], 4.0, g_hit_glow_release_seconds, std::nullopt);

    const std::vector<HighwayStrikePop>& picking = board.pick_hand.pops;
    REQUIRE(picking.size() == 3);
    checkPop(picking[0], 2.0, g_hit_glow_release_seconds, 12);
    checkPop(picking[1], 2.25, g_hit_glow_release_seconds, 14);
    checkPop(picking[2], 3.0, g_hit_glow_release_seconds, std::nullopt);
}

// THE POP CLAMP: a pop clamps against the next pop of its hand on the SAME strips only — never
// against a pop on another fret in between; two pops at one instant are one strike and never clamp
// each other; and every box pop of a hand lands on its live window's sides, so box pops clamp each
// other whatever their chords' frets.
TEST_CASE("Highway strike pops clamp against the next pop on the same strips", "[core][highway]")
{
    // Singles an eighth of a beat long, so no ring is still held at the next strike.
    const Fraction short_ring{1, 8};
    const HighwayViewState board = popBoard({
        // Fret 5 at 0.0, fret 9 at 0.125 on another fret, fret 5 again at 0.1875.
        popNote(GridPosition{.measure = 1, .beat = 1}, 3, 5, NoteAttack::Pick, {}, short_ring),
        popNote(
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 4}},
            1,
            9,
            NoteAttack::Pick,
            {},
            short_ring),
        popNote(
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 8}},
            4,
            5,
            NoteAttack::Pick,
            {},
            short_ring),
        // A chord at 1.0 whose two notes both slide onto fret 7 at 1.25: one instant, one strips.
        popNote(
            GridPosition{.measure = 1, .beat = 3},
            3,
            5,
            NoteAttack::Pick,
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7}}),
        popNote(
            GridPosition{.measure = 1, .beat = 3},
            4,
            6,
            NoteAttack::Pick,
            {Keyframe{.offset = Fraction{1, 2}, .fret = 7}}),
        // Two strummed chords a half beat apart on different frets.
        popNote(GridPosition{.measure = 2, .beat = 1}, 3, 5, NoteAttack::Pick, {}),
        popNote(GridPosition{.measure = 2, .beat = 1}, 4, 5, NoteAttack::Pick, {}),
        popNote(
            GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 2}},
            3,
            9,
            NoteAttack::Pick,
            {}),
        popNote(
            GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 2}},
            4,
            10,
            NoteAttack::Pick,
            {}),
    });

    const std::vector<HighwayStrikePop>& pops = board.fret_hand.pops;
    REQUIRE(pops.size() == 8);
    checkPop(pops[0], 0.0, clampedRelease(0.1875), 5);
    checkPop(pops[1], 0.125, g_hit_glow_release_seconds, 9);
    checkPop(pops[2], 0.1875, g_hit_glow_release_seconds, 5);
    checkPop(pops[3], 1.0, g_hit_glow_release_seconds, std::nullopt);
    checkPop(pops[4], 1.25, g_hit_glow_release_seconds, 7);
    checkPop(pops[5], 1.25, g_hit_glow_release_seconds, 7);
    checkPop(pops[6], 2.0, clampedRelease(0.25), std::nullopt);
    checkPop(pops[7], 2.25, g_hit_glow_release_seconds, std::nullopt);
    CHECK(board.pick_hand.pops.empty());
}

} // namespace rock_hero::common::core
