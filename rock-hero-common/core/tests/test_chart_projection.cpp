#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/song/arrangement.h>
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

// Nullable-pointer view of the arrangement's optional chart, mirroring the editor harness's
// chartOrNull: the parameter-passed optional lets clang-tidy's unchecked-optional-access track
// the guard, which it cannot do across a Catch2 REQUIRE.
[[nodiscard]] Chart* chartOrNull(Arrangement& arrangement)
{
    return arrangement.chart.has_value() ? &*arrangement.chart : nullptr;
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
        // Held posture for the arpeggio span; string 4 is struck at the bracket start and the
        // other two are not, which the posture entries are expected to be blind to.
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

// The capo rides the projection so a surface can indicate the string floor (roadmap 25-Q6).
TEST_CASE("Chart projection carries the tuning's capo", "[core][chart]")
{
    Arrangement arrangement;
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.tuning.capo = 2;
    arrangement.chart = std::move(chart);
    CHECK(makeChartViewState(arrangement, makeTempoMap()).capo == 2);
}

TEST_CASE("Chart projection resolves chart positions to seconds", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartViewState state = makeChartViewState(makeArrangementWithChart(), tempo_map);

    CHECK(state.string_count == 6);
    REQUIRE(state.notes.size() == 5);
    // Sized like the notes because both painters index it by note index; its values are the
    // span-hold rule's, pinned below.
    CHECK(state.display_hold_ends.size() == state.notes.size());

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[1].end_seconds == Catch::Approx(state.notes[1].start_seconds));

    const NoteViewState& sliding = state.notes[2];
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
    CHECK_FALSE(linkedWaypoint(sliding, sliding.slides[0]));

    // The shift glide ends at the sustain end, the minimum sustain distance before the re-picked
    // fret-8 landing; the segment is not linked (the landing's own head renders there).
    const NoteViewState& shift_slider = state.notes[3];
    REQUIRE(shift_slider.slides.size() == 1);
    CHECK(shift_slider.slides[0].seconds == Catch::Approx(12.75 * beat));
    CHECK(shift_slider.slides[0].fret == 8);
    CHECK_FALSE(linkedWaypoint(shift_slider, shift_slider.slides[0]));
    CHECK(shift_slider.end_seconds == Catch::Approx(12.75 * beat));

    REQUIRE(state.shapes.size() == 2);
    CHECK(state.shapes[0].name == "F5");
    CHECK_FALSE(state.shapes[0].arpeggio);
    CHECK(state.shapes[1].arpeggio);

    // Every span carries its whole held posture, chord box and arpeggio alike: which entries a
    // surface draws is the painter's business (the lane brackets an arpeggio's, the board's
    // fingering panel reads them all). String 4 is struck right at the arpeggio's bracket start
    // and strings 2 and 5 are not, and the entries are identical either way: a posture states
    // where the fretting hand is, never what sounds there, so the projection asks the notes
    // nothing.
    REQUIRE(state.shapes[0].strings.size() == 3);
    CHECK(state.shapes[0].strings[0] == ShapeStringViewState{.string = 1, .fret = 1, .finger = 1});
    REQUIRE(state.shapes[1].strings.size() == 3);
    CHECK(state.shapes[1].strings[0] == ShapeStringViewState{.string = 2, .fret = 5, .finger = 1});
    CHECK(state.shapes[1].strings[1] == ShapeStringViewState{.string = 4, .fret = 7, .finger = 3});
    CHECK(state.shapes[1].strings[2] == ShapeStringViewState{.string = 5, .fret = 8, .finger = 4});

    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
}

TEST_CASE("Chart projection is empty without a chart", "[core][chart]")
{
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
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
TEST_CASE("Chart projection caps a span hold at the next same-string onset", "[core][chart]")
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
    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.display_hold_ends.size() == 4);
    // String 1's member stops where the string is struck again, never at the span's end.
    CHECK(state.display_hold_ends[0] == Catch::Approx(0.5));
    // String 2 is never restruck, so its member inherits the whole span.
    CHECK(state.display_hold_ends[1] == Catch::Approx(2.0));
    // The later single notes are not strums, so nothing extends them past their own onsets.
    CHECK(state.display_hold_ends[2] == Catch::Approx(0.5));
    CHECK(state.display_hold_ends[3] == Catch::Approx(1.0));
}

// The pick-slide seam: latent overridden techniques never reach the view, and the path is
// unpitched end to end.
TEST_CASE("Chart projection suppresses pick-slide latents", "[core][chart]")
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
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.notes.size() == 1);
    const NoteViewState& view = state.notes.front();
    CHECK(view.attack == NoteAttack::PickSlide);
    CHECK_FALSE(view.palm_mute);
    CHECK_FALSE(view.dead);
    CHECK_FALSE(view.tremolo);
    CHECK_FALSE(view.vibrato);
    CHECK(view.bend.empty());
    // The turnaround waypoint and the slide-out terminal flatten into one leg list, both
    // unpitched — but the turnaround is LINKED and the terminal is not: the pick stays on the
    // string through a direction change, so the junction carries a continuation head (in the
    // note's plectrum shape), while the terminal is where the pick leaves and only its chip
    // marks the position.
    REQUIRE(view.slides.size() == 2);
    for (const SlideViewState& leg : view.slides)
    {
        CHECK(leg.unpitched);
    }
    CHECK(linkedWaypoint(view, view.slides[0]));
    CHECK_FALSE(linkedWaypoint(view, view.slides[1]));
}

// Ramp derivation for the fretting hand's approach: a placement landing exactly on a pitched
// waypoint's grid position ramps over that glide segment (slide-locked), ordinary placements morph
// over the shared minimum-sustain-distance margin, crowded placements shorten against the previous
// arrival instead of overlapping it, and an unpitched slide-out never slide-matches a placement.
TEST_CASE("Chart projection derives hand-approach ramps", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
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

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
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
TEST_CASE("Chart projection gives a hold waypoint the margin morph", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
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

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.fret_hand_positions.size() == 2);

    // The hold at beat 4 does NOT inherit the three-beat held stretch; it morphs over the margin.
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
    // The real glide that follows still rides its own one-beat segment.
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
}

} // namespace rock_hero::common::core
