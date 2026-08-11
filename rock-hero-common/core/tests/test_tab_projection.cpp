#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/tab/tab_projection.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

namespace
{

// A 4/4 default map: measure 1 beat 1 sits at zero and beats last half a second at 120 BPM.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

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
        // Held posture for the arpeggio span; string 4 is struck at the bracket start, so its
        // entry is the only sounded one.
        ChordTemplate{
            .name = "Dm7",
            .frets = {std::nullopt, 5, std::nullopt, 7, 8, std::nullopt},
            .fingers = {std::nullopt, 1, std::nullopt, 3, 4, std::nullopt},
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
        // Shift-slide pair: the glide is an ordinary pitched waypoint at the sustain end, the
        // minimum sustain distance before the re-picked landing on the same string, so the
        // projected segment must not be linked (the target's own head renders there).
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{3, 4},
            .bend = {},
            .slides = {SlideWaypoint{.offset = Fraction{3, 4}, .fret = 8}},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 5,
            .fret = 8,
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
        // Only one onset at 3:1+1/2, so this span reads as an arpeggio bracket.
        ChartShape{
            .position = GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}},
            .sustain = Fraction{2},
            .chord = 1,
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

// The capo rides the projection so the lane can indicate the string floor (roadmap 25-Q6).
TEST_CASE("Tab projection carries the tuning's capo", "[core][tab]")
{
    Arrangement arrangement;
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    arrangement.chart = std::move(chart);
    CHECK(makeTabViewState(arrangement, makeTempoMap()).capo == 2);
}

TEST_CASE("Tab projection resolves chart positions to seconds", "[core][tab]")
{
    const TempoMap tempo_map = makeTempoMap();
    const TabViewState state = makeTabViewState(makeArrangementWithChart(), tempo_map);

    CHECK(state.string_count == 6);
    REQUIRE(state.notes.size() == 5);
    // Sized like the notes because the paint core indexes it by note index; its values are the
    // span-hold rule's, pinned against the board's in test_highway_projection.
    CHECK(state.display_hold_ends.size() == state.notes.size());

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
    // A waypoint at exactly the sustain end reads as a glide-end, not a continuation, so no
    // linked head renders at the tail tip.
    CHECK_FALSE(sliding.slides[0].linked);

    // The shift glide ends at the sustain end, the minimum sustain distance before the re-picked
    // fret-8 landing; the segment is not linked (the landing's own head renders there).
    const TabNoteView& shift_slider = state.notes[3];
    REQUIRE(shift_slider.slides.size() == 1);
    CHECK(shift_slider.slides[0].seconds == Catch::Approx(12.75 * beat));
    CHECK(shift_slider.slides[0].fret == 8);
    CHECK_FALSE(shift_slider.slides[0].linked);
    CHECK(shift_slider.end_seconds == Catch::Approx(12.75 * beat));

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

TEST_CASE("Tab projection is empty without a chart", "[core][tab]")
{
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const TabViewState state = makeTabViewState(arrangement, makeTempoMap());
    CHECK(state.string_count == 0);
    CHECK(state.notes.empty());
    CHECK(state.display_hold_ends.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
}

// The exact geometry the lane used to draw through: a sustainless chord under a four-beat span with
// the same string restruck twice inside it. The lane draws a ribbon to `display_hold_ends`, so an
// uncapped span hold put string 1's tail straight under the later heads and out the far side — a
// picture 40-Q2-B guarantees no stored sustain can produce.
TEST_CASE("Tab projection caps a span hold at the next same-string onset", "[core][tab]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.templates = {
        ChordTemplate{
            .name = "A5",
            .frets = {5, 7, std::nullopt, std::nullopt, std::nullopt, std::nullopt},
            .fingers = {1, 3, std::nullopt, std::nullopt, std::nullopt, std::nullopt},
        },
    };
    const auto note = [](int beat, int string, int fret) {
        return ChartNote{
            .position = GridPosition{.measure = 1, .beat = beat},
            .string = string,
            .fret = fret,
            .bend = {},
            .slides = {},
        };
    };
    chart.notes = {note(1, 1, 5), note(1, 2, 7), note(2, 1, 7), note(3, 1, 9)};
    chart.shapes = {ChartShape{
        .position = GridPosition{.measure = 1, .beat = 1}, .sustain = Fraction{4}, .chord = 0
    }};
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    // 120 BPM 4/4: a beat is half a second, so the span runs 0.0s to 2.0s and the later string-1
    // onsets sit at 0.5s and 1.0s.
    const TabViewState state = makeTabViewState(arrangement, makeTempoMap());
    REQUIRE(state.display_hold_ends.size() == 4);
    // String 1's member stops where the string is struck again, never at the span's end.
    CHECK(state.display_hold_ends[0] == Catch::Approx(0.5));
    // String 2 is never restruck, so its member inherits the whole span.
    CHECK(state.display_hold_ends[1] == Catch::Approx(2.0));
    // The later single notes are not strums, so nothing extends them past their own onsets.
    CHECK(state.display_hold_ends[2] == Catch::Approx(0.5));
    CHECK(state.display_hold_ends[3] == Catch::Approx(1.0));
}

// The pick-slide seam: latent overridden techniques never reach the view, and the path renders
// unpitched with no linked continuation heads.
TEST_CASE("Tab projection suppresses pick-slide latents", "[core][tab]")
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
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const TabViewState state = makeTabViewState(arrangement, makeTempoMap());
    REQUIRE(state.notes.size() == 1);
    const TabNoteView& view = state.notes.front();
    CHECK(view.attack == NoteAttack::PickSlide);
    CHECK(view.mute == NoteMute::None);
    CHECK_FALSE(view.tremolo);
    CHECK_FALSE(view.vibrato);
    CHECK(view.bend.empty());
    // The turnaround waypoint and the slide-out terminal flatten into one leg list, both
    // unpitched — but the turnaround is LINKED and the terminal is not: the pick stays on the
    // string through a direction change, so the junction carries a continuation head (in the
    // note's plectrum shape), while the terminal is where the pick leaves and only its chip
    // marks the position.
    REQUIRE(view.slides.size() == 2);
    for (const TabSlideView& leg : view.slides)
    {
        CHECK(leg.unpitched);
    }
    CHECK(view.slides[0].linked);
    CHECK_FALSE(view.slides[1].linked);
}

} // namespace rock_hero::common::core
