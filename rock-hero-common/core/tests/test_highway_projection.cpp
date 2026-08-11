#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/tab/tab_projection.h>
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

// The ONE chart both surfaces project, carrying every fact their view types share: a strummed
// chord under a hand-shape span, a sustained note with a bend point and a pitched glide, a
// natural harmonic on a fractional node over a capo, a palm mute, a tremolo, a vibrato, an
// accent, a hammer-on with the pull-off that releases it, an arpeggio span, two fret-hand
// placements, and a pick slide with a turnaround plus its required unpitched terminal. Each
// technique sits on its own note so a projection that drops one cannot hide behind another.
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
            .mute = NoteMute::Palm,
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
            .accent = true,
            .bend = {},
            .slides = {},
        },
        // Both payload kinds on one tail: a bend point mid-sustain and a pitched glide landing on
        // the sustain end.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 2},
            .string = 3,
            .fret = 7,
            .sustain = Fraction{2},
            .bend = {BendPoint{.offset = Fraction{1}, .semitones = 2.0}},
            .slides = {SlideWaypoint{.offset = Fraction{2}, .fret = 9}},
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
        // Fret 5 picked, hammered up to 7, pulled back to 5: the legato pair plus the note it
        // releases from, which is what makes the pull-off a legal chart.
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
            .attack = NoteAttack::Hammer,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 4,
            .fret = 5,
            .sustain = Fraction{1},
            .attack = NoteAttack::Pull,
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
    CHECK(makeHighwayViewState(arrangement, makeHighwayTempoMap(), {}, {}).capo == 2);
}

// Absolute anchors for the board's own resolution: onsets, sustain ends, and intra-note payload
// offsets against the 4/4 default map. That the 2D lane resolves the same inputs to the same
// seconds is a separate claim, pinned mechanically by the agreement case below.
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

    // The between-fret harmonic node survives projection untouched, and its presence is what
    // makes the note a harmonic now.
    const HighwayNoteView& harmonic = state.notes[3];
    CHECK(harmonic.attack == NoteAttack::Pick);
    REQUIRE(harmonic.harmonic_node.has_value());
    if (harmonic.harmonic_node.has_value())
    {
        CHECK(*harmonic.harmonic_node == Catch::Approx(3.2));
        CHECK(nodeIsOnNeck(harmonic.attack));
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
    // Upper-cased by the projection, not the renderer: the board draws every section name that way,
    // and folding the case here keeps a pure function of the chart out of the per-frame path, where
    // it was allocating and transforming a string per visible section per frame. The authored name
    // is untouched in the song, and the 2D ruler still shows it as written.
    CHECK(state.sections[0].name == "VERSE");
}

// The two surfaces may never disagree about the same chart fact, so one chart is projected both
// ways and every shared field is compared. Prose in a comment is what this used to be, while the
// two fixtures drifted onto different charts — leaving no chart in the tree projected twice, and a
// divergence with nowhere to show up.
TEST_CASE("Tab and highway projections agree on every shared chart fact", "[core][highway][tab]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const Chart chart = makeAgreementChart();
    // A fixture that rotted into an illegal chart would have the two surfaces agreeing about
    // something no document can contain, so its legality is a precondition of the comparison.
    REQUIRE(validateChartRules(chart, tempo_map).has_value());

    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = chart;
    const TabViewState flat = makeTabViewState(arrangement, tempo_map);
    // No display padding and no sections: the padding default is what the game ships and what the
    // editor's tab lane matches, and sections are song-level furniture the lane never draws.
    const HighwayViewState board = makeHighwayViewState(arrangement, tempo_map, {}, {});

    CHECK(flat.string_count == board.string_count);
    CHECK(flat.capo == board.capo);
    CHECK(flat.capo == 2);

    REQUIRE(flat.notes.size() == chart.notes.size());
    REQUIRE(board.notes.size() == chart.notes.size());

    // Non-vacuity: the field loop below would pass just as happily comparing defaults, so every
    // technique the two view types share has to be present in what was actually projected.
    const auto any_note = [&flat](const auto& carries) {
        return std::ranges::any_of(flat.notes, carries);
    };
    CHECK(any_note([](const TabNoteView& note) { return note.mute == NoteMute::Palm; }));
    CHECK(any_note([](const TabNoteView& note) { return note.tremolo; }));
    CHECK(any_note([](const TabNoteView& note) { return note.vibrato; }));
    CHECK(any_note([](const TabNoteView& note) { return note.accent; }));
    CHECK(any_note([](const TabNoteView& note) { return note.harmonic_node.has_value(); }));
    CHECK(any_note([](const TabNoteView& note) { return !note.bend.empty(); }));
    CHECK(any_note([](const TabNoteView& note) { return !note.slides.empty(); }));
    CHECK(any_note([](const TabNoteView& note) { return note.attack == NoteAttack::Hammer; }));
    CHECK(any_note([](const TabNoteView& note) { return note.attack == NoteAttack::Pull; }));
    CHECK(any_note([](const TabNoteView& note) { return note.attack == NoteAttack::PickSlide; }));

    for (std::size_t index = 0; index < flat.notes.size(); ++index)
    {
        CAPTURE(index);
        const TabNoteView& flat_note = flat.notes[index];
        const HighwayNoteView& board_note = board.notes[index];
        // Exact, not Approx: both projections put the same grid position through the same
        // tempo-map call, so any tolerance would accept precisely the drift this case exists to
        // catch. WithinULP(x, 0) matches the identical bit pattern and prints both values.
        CHECK_THAT(
            flat_note.start_seconds, Catch::Matchers::WithinULP(board_note.start_seconds, 0));
        CHECK_THAT(flat_note.end_seconds, Catch::Matchers::WithinULP(board_note.end_seconds, 0));
        // The board resolves displayed-lane padding inside the projection while the lane resolves
        // it up in the UI layer. With no padding configured — what both surfaces ship with — the
        // string numbers are the chart's own on both sides, so they must match exactly.
        CHECK(flat_note.string == board_note.string);
        CHECK(flat_note.fret == board_note.fret);
        CHECK(flat_note.attack == board_note.attack);
        CHECK(flat_note.mute == board_note.mute);
        // Compared as optionals, exactly as both view types' own operator== compares this field.
        CHECK(flat_note.harmonic_node == board_note.harmonic_node);
        CHECK(flat_note.vibrato == board_note.vibrato);
        CHECK(flat_note.tremolo == board_note.tremolo);
        CHECK(flat_note.accent == board_note.accent);

        REQUIRE(flat_note.bend.size() == board_note.bend.size());
        for (std::size_t point = 0; point < flat_note.bend.size(); ++point)
        {
            CAPTURE(point);
            CHECK_THAT(
                flat_note.bend[point].seconds,
                Catch::Matchers::WithinULP(board_note.bend[point].seconds, 0));
            CHECK_THAT(
                flat_note.bend[point].semitones,
                Catch::Matchers::WithinULP(board_note.bend[point].semitones, 0));
        }

        // One leg list per surface, the slide-out flattened onto the end of both.
        REQUIRE(flat_note.slides.size() == board_note.slides.size());
        for (std::size_t leg = 0; leg < flat_note.slides.size(); ++leg)
        {
            CAPTURE(leg);
            CHECK_THAT(
                flat_note.slides[leg].seconds,
                Catch::Matchers::WithinULP(board_note.slides[leg].seconds, 0));
            CHECK(flat_note.slides[leg].fret == board_note.slides[leg].fret);
            CHECK(flat_note.slides[leg].unpitched == board_note.slides[leg].unpitched);
        }
    }

    // The one shared-note field only the lane carries: `linked` decides whether a junction draws a
    // continuation head, a 2D notation question with no board counterpart — the rail runs through
    // the junction either way. So it is asserted on the tab side alone, in both its states: the
    // scrape's turnaround is a continuation, its terminal is where the pick leaves.
    const TabNoteView& scrape = flat.notes.front();
    REQUIRE(scrape.attack == NoteAttack::PickSlide);
    REQUIRE(scrape.slides.size() == 2);
    CHECK(scrape.slides[0].linked);
    CHECK_FALSE(scrape.slides[1].linked);

    REQUIRE(flat.shapes.size() == chart.shapes.size());
    REQUIRE(board.shapes.size() == chart.shapes.size());
    for (std::size_t index = 0; index < flat.shapes.size(); ++index)
    {
        CAPTURE(index);
        const TabShapeView& flat_shape = flat.shapes[index];
        const HighwayShapeView& board_shape = board.shapes[index];
        CHECK_THAT(
            flat_shape.start_seconds, Catch::Matchers::WithinULP(board_shape.start_seconds, 0));
        CHECK_THAT(flat_shape.end_seconds, Catch::Matchers::WithinULP(board_shape.end_seconds, 0));
        CHECK(flat_shape.name == board_shape.name);
        CHECK(flat_shape.arpeggio == board_shape.arpeggio);
    }
    // Both treatments are present, so the arrival flag is not agreeing against a constant.
    CHECK(flat.shapes[0].name == "G#5");
    CHECK_FALSE(flat.shapes[0].arpeggio);
    CHECK(flat.shapes[1].name == "D5");
    CHECK(flat.shapes[1].arpeggio);

    // Both surfaces read one posture out of one template and each adds the fact its own notation
    // needs — the lane which entries SOUND at the bracket start, the board which FINGER holds
    // them — so only the strings and frets are a shared fact. Only an arpeggio brackets in 2D, so
    // the lane leaves the chord-box span's list empty while the board fills it for the fingering
    // panel regardless.
    CHECK(flat.shapes[0].arpeggio_notes.empty());
    CHECK(board.shapes[0].strings.size() == 3);
    REQUIRE_FALSE(flat.shapes[1].arpeggio_notes.empty());
    REQUIRE(flat.shapes[1].arpeggio_notes.size() == board.shapes[1].strings.size());
    for (std::size_t entry = 0; entry < flat.shapes[1].arpeggio_notes.size(); ++entry)
    {
        CAPTURE(entry);
        CHECK(flat.shapes[1].arpeggio_notes[entry].string == board.shapes[1].strings[entry].string);
        CHECK(flat.shapes[1].arpeggio_notes[entry].fret == board.shapes[1].strings[entry].fret);
    }

    // Placements agree on where the hand arrives and what it covers; the board additionally
    // derives the eased approach (ramp_seconds, unpitched_ramp), which the static 2D marker has
    // no counterpart for.
    REQUIRE(flat.fret_hand_positions.size() == chart.fret_hand_positions.size());
    REQUIRE(board.fret_hand_positions.size() == chart.fret_hand_positions.size());
    REQUIRE_FALSE(flat.fret_hand_positions.empty());
    for (std::size_t index = 0; index < flat.fret_hand_positions.size(); ++index)
    {
        CAPTURE(index);
        const TabFhpView& flat_fhp = flat.fret_hand_positions[index];
        const HighwayFhpView& board_fhp = board.fret_hand_positions[index];
        CHECK_THAT(flat_fhp.seconds, Catch::Matchers::WithinULP(board_fhp.seconds, 0));
        CHECK(flat_fhp.fret == board_fhp.fret);
        CHECK(flat_fhp.width == board_fhp.width);
    }

    // Board-only structure with no 2D counterpart at all — beat bars, camera framing zones, and
    // the picking-hand light the scrape drives — so its absence from the lane is not a
    // disagreement about anything.
    CHECK_FALSE(board.beats.empty());
    CHECK_FALSE(board.camera_zone_starts.empty());
    CHECK(board.tap_onsets.size() == 1);
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
    // A sustained note whose tail trails off unpitched: a placement on its end rides the
    // trail-off's own segment with the unpitched curve, so the window travels exactly with the
    // drawn rail.
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

    // A placement on an unpitched trail-off's end rides that trail-off's OWN segment, exactly as a
    // pitched glide does, and carries the unpitched family so the window eases with the same curve
    // the rail is drawn with. The trail-off's segment runs from the note's onset (14 beats) to its
    // end (15 beats) because the note carries no pitched waypoints ahead of it; before this the
    // placement morphed over the metrical margin instead, leaving the window stationary for most of
    // the drawn glide and then sprinting to catch up.
    CHECK(state.fret_hand_positions[3].seconds == Catch::Approx(15.0 * beat));
    CHECK(state.fret_hand_positions[3].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK(state.fret_hand_positions[3].unpitched_ramp);
    // The pitched glide above keeps the pitched family.
    CHECK_FALSE(state.fret_hand_positions[2].unpitched_ramp);
}

// An equal-fret waypoint is a HOLD, not a glide: nothing travels across it, so a placement landing
// on one must take the short margin morph rather than a ramp spanning the held stretch. Holds are
// how a slide notated on a tied continuation records where it leaves from, so tying their span to
// the window made the hand drift across the whole tied group to arrive at a fret it never left —
// sighted at fret 11 of measure 50 of the acceptance song.
TEST_CASE("Highway projection gives a hold waypoint the margin morph", "[core][highway]")
{
    const TempoMap tempo_map = makeHighwayTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    // Four beats of held fret 5, then a one-beat glide up to fret 9: the hold pins the pitch at
    // beat 4 and the travel happens only over the final beat.
    chart.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{4},
            .bend = {},
            .slides = {
                SlideWaypoint{.offset = Fraction{3}, .fret = 5},
                SlideWaypoint{.offset = Fraction{4}, .fret = 9},
            },
        });
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 4}, .fret = 5, .width = 4},
        FretHandPosition{.position = GridPosition{.measure = 3, .beat = 1}, .fret = 9, .width = 4},
    };

    const HighwayViewState state = makeHighwayViewState(arrangement, tempo_map, {}, {});
    REQUIRE(state.fret_hand_positions.size() == 2);

    // The hold at beat 4 does NOT inherit the three-beat held stretch; it morphs over the margin.
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
    // The real glide that follows still rides its own one-beat segment.
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
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
    CHECK(state.string_count == 0);
    CHECK(state.notes.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
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

    const std::vector<double> prefix_max =
        makeSustainPrefixMax(notes | std::views::transform(&HighwayNoteView::end_seconds));
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

    REQUIRE(state.display_hold_ends.size() == state.notes.size());
    REQUIRE(state.notes.size() == 2);
    // Struck at 2.0 seconds with no sustain of their own, so both heads stay pinned until the span
    // ends at 4.0 seconds.
    CHECK(state.notes[0].end_seconds == Catch::Approx(2.0));
    CHECK(state.display_hold_ends[0] == Catch::Approx(4.0));
    CHECK(state.display_hold_ends[1] == Catch::Approx(4.0));

    // Which is what keeps a span-held strum inside the visible range for as long as it is drawn.
    const std::vector<double> prefix_max = makeSustainPrefixMax(state.display_hold_ends);
    REQUIRE(prefix_max.size() == 2);
    CHECK(prefix_max[1] == Catch::Approx(4.0));
    const auto visible = visibleEventRange(state.notes, prefix_max, 3.5, 3.9);
    CHECK(visible.first == 0);
    CHECK(visible.second == 2);
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
    // A tapped chord: its members share a grid position and so share a second exactly. The last one
    // is offset by a picosecond, which is the only kind of difference the tolerance is for — pure
    // arithmetic noise, orders below any grid the editor offers.
    add_note(3.0, 15, NoteAttack::Tap);
    add_note(3.0, 12, NoteAttack::Tap);
    add_note(3.000000000001, 17, NoteAttack::Tap);
    add_note(4.0, 9, NoteAttack::Hammer); // Left-hand tap imports as Hammer: no entry.

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
    HighwayNoteView tap;
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
    HighwayNoteView open_tap = tap;
    open_tap.harmonic_node.reset();
    CHECK(makeHighwayTapOnsets({open_tap}, std::vector<double>(1, 0.0)).empty());
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
    scrape.mute = NoteMute::Full;
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
    // Waypoint stations carry the unpitched flag so the light sweeps with the scrape's own
    // ease; the onset station arrives from no glide.
    CHECK_FALSE(light.path[0].unpitched);
    CHECK(light.path[1].unpitched);
    CHECK(light.path[2].unpitched);
    // The FHP on the waypoint's grid position ramps by the quarter-beat margin morph (0.125s at
    // the default tempo), not by the scrape leg's span back to the onset (which would be 0.25s).
    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.125));
}

} // namespace rock_hero::common::core
