#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <limits>
#include <optional>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

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
        // Rings across the 3:1+1/2 strum without being re-struck there: it joins that span's
        // posture (rule 12) and makes the span arrive arpeggio-style.
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
        // Shift-slide pair, written the way the store holds it: the stop sits exactly AT the ring's
        // end, ON the re-picked landing's own onset, and what tells that statement from a slide-out
        // is the RELATION — it names the very stop the next head is struck at, at the same instant
        // (arrivesIntoNextHead). Rule 1 then stops the ink one margin before the landing, so the
        // arrival stands in the ending zone at its stored instant — LINKED, drawing its own head
        // there under a reveal — and only the resolved relation says the slide-out flag is false.
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 8}},
        },
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 2},
            .string = 5,
            .fret = 8,
            .sustain = Fraction{1, 8},
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

    CHECK(state.stringCount() == 6);
    REQUIRE(state.notes.size() == 7);
    // Sized like the notes because both painters index it by note index; its values are the
    // span-hold rule's, pinned below.
    CHECK(state.display_hold_ends.size() == state.notes.size());
    // The 2D lane's visible-range indexes: one entry per event, each the running maximum of the
    // ends so far — the ring end for a note, the musical close for a span.
    REQUIRE(state.ring_end_prefix_max.size() == state.notes.size());
    double ring_end_max = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < state.notes.size(); ++index)
    {
        ring_end_max = std::max(ring_end_max, state.notes[index].ring_end_seconds);
        CHECK_THAT(state.ring_end_prefix_max[index], Catch::Matchers::WithinULP(ring_end_max, 0));
    }
    REQUIRE(state.shape_close_prefix_max.size() == state.shapes.size());
    double close_max = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < state.shapes.size(); ++index)
    {
        close_max = std::max(close_max, state.shapes[index].close_seconds);
        CHECK_THAT(state.shape_close_prefix_max[index], Catch::Matchers::WithinULP(close_max, 0));
    }

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    // Nothing binds the pair's tails before the next onset four beats on, so both are free and
    // draw their whole rings (the first is pinned exactly in the ring-beside-ink case below).
    CHECK(state.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    // Its own eighth-of-a-beat ring is far under the kept-sustain bound, but its chord partner's
    // whole beat runs past it, and rule 2's verdict is the GROUP's — one stroke sounds every
    // string, so a lone tail beside partners that look unsounded is a picture no strum makes.
    CHECK(state.notes[1].ink_end_seconds == Catch::Approx(4.125 * beat));

    const NoteViewState& sliding = state.notes[3];
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
    // A fret stated at exactly the STORED ring's end is the SLIDE-OUT: the hand leaves toward it,
    // so it ends the gesture rather than continuing it and no linked head renders at the tail tip.
    CHECK(sliding.slides[0].slide_out);
    CHECK_FALSE(linkedKeyframe(sliding.slides[0]));
    // Nothing is struck on its string where its ring stops, so no mark of another note shares the
    // instant and the band conditional has nothing to do here.
    CHECK_FALSE(sliding.ends_on_next_head);

    // The shift glide STATES its arrival on the landing, and the ink stops the minimum sustain
    // distance before it, where rule 1 crops the tail. The arrival keeps its stored instant, in the
    // ending zone. It is NOT the slide-out, and the only thing that says so is the resolved
    // relation: the fret it names is the stop the next head is struck at, at the same instant. So
    // it is LINKED — under a reveal it draws its continuation head on the landing's own onset.
    const NoteViewState& shift_slider = state.notes[5];
    REQUIRE(shift_slider.slides.size() == 1);
    CHECK(shift_slider.slides[0].seconds == Catch::Approx(13.0 * beat));
    CHECK(shift_slider.slides[0].fret == 8);
    CHECK_FALSE(shift_slider.slides[0].slide_out);
    CHECK(linkedKeyframe(shift_slider.slides[0]));
    CHECK(shift_slider.ring_end_seconds == Catch::Approx(13.0 * beat));
    CHECK(shift_slider.ink_end_seconds == Catch::Approx(12.9 * beat));
    // A plain paint walks no stop — the arrival lies past the ink end — and a reveal walks it.
    CHECK_FALSE(keyframeDrawn(shift_slider.slides[0], shift_slider.ink_end_seconds));
    CHECK(keyframeDrawn(shift_slider.slides[0], shift_slider.ring_end_seconds));
    // The STORED ring lands on that head, which is what the band conditional keys on.
    CHECK(shift_slider.ends_on_next_head);
    // The keyframe's own stored offset — the ring's end, a whole beat in — which is the one name
    // every mapping back to the chart uses.
    CHECK(shift_slider.slides[0].offset == Fraction{1});

    // Both spans are DERIVED from the notes above — nothing in the chart authors one. The 2:1
    // pair strikes together and nothing rings across it, so it is a chord box; the 3:1+1/2 pair
    // strikes under string 2's still-sounding ring, so it is an arpeggio.
    //
    // The first is THE CONTINUITY LAW's box case ([D3]): its members ring a beat and an eighth of
    // a beat, and the first genuine stored gap ends the span at that ring's end — not the MAXIMUM
    // of the members' rings, which would read as a whole beat of held shape, a statement the chart
    // does not make since the hand has demonstrably let one string go. The surviving long ring
    // outlives the span, so it draws its own whole tail and is untouched, which the note
    // assertions above still pin.
    //
    // The second is TRAVEL SPLITTING under the LANDING split ([D2]): its string-4 member glides,
    // and the span COVERS that travel rather than stopping where the hand departed — a chord slide
    // keeps the fingers planted, so the continuity law itself carries the transit. What bounds this
    // span is therefore the OTHER member, whose eighth-beat ring is the first coverage to run out.
    // Nothing re-opens after it: the glide's arrival IS the ring's end, so no member goes on
    // ringing past the landing for a successor to state.
    //
    // Its FRONT is string 2's own onset, not the pair's (THE DATING RULE): the ring the pair
    // strikes under began at 8:0 and no preceding span covers that instant, so the statement runs
    // from there and the pair arrives inside it.
    REQUIRE(state.shapes.size() == 2);
    CHECK(state.shapes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(4.125 * beat));
    CHECK_FALSE(state.shapes[0].arpeggio);
    CHECK(state.shapes[1].start_seconds == Catch::Approx(8.0 * beat));
    CHECK(state.shapes[1].drawn_end_seconds == Catch::Approx(8.625 * beat));
    CHECK(state.shapes[1].arpeggio);

    // Every span carries its whole held posture, chord box and arpeggio alike: which entries a
    // surface draws is the painter's business (the lane brackets an arpeggio's). The arpeggio's
    // posture holds three strings although only two are struck at the bracket start — the third
    // is the one still ringing through it, which is what a posture means: where the fretting hand
    // is, not what sounds at that instant.
    //
    // Each entry also carries WHERE its digit prints, which is the rule answered here instead of
    // by each surface: a string whose own HEAD stands at the mark's instant already states its
    // fret with that head's number, so the posture prints nothing for it, while every other
    // posture string keeps the bracket's centre.
    //
    // THE DIGIT WINDOW: the window is the MARK'S OWN INSTANT, and heads later in the span never
    // suppress. String 2 is struck at the arpeggio's front, which is where this bracket draws, so
    // its own head states its 5 and the bracket prints nothing for it — while strings 4 and 5
    // arrive half a beat LATER and therefore print in the opening bracket, because the bracket is
    // the span's chord frame and states the whole membership where the reader meets it. The
    // box-class span above prints nothing anywhere: it draws no bracket at all, which is the empty
    // slot for a different reason entirely.
    REQUIRE(state.shapes[0].strings.size() == 2);
    CHECK(
        state.shapes[0].strings[0] ==
        ShapeStringViewState{.string = 1, .stop = frettedStop(1), .digit = std::nullopt});
    CHECK(
        state.shapes[0].strings[1] ==
        ShapeStringViewState{.string = 2, .stop = frettedStop(3), .digit = std::nullopt});
    REQUIRE(state.shapes[1].strings.size() == 3);
    CHECK(
        state.shapes[1].strings[0] ==
        ShapeStringViewState{.string = 2, .stop = frettedStop(5), .digit = std::nullopt});
    CHECK(
        state.shapes[1].strings[1] ==
        ShapeStringViewState{.string = 4, .stop = frettedStop(7), .digit = StopMarkSlot::Bracket});
    CHECK(
        state.shapes[1].strings[2] ==
        ShapeStringViewState{.string = 5, .stop = frettedStop(8), .digit = StopMarkSlot::Bracket});

    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
}

// Each note carries its two lengths: the ring the string really sounds for, which a reveal draws
// to, and the ink a plain paint stops at. Where no rule touched a tail the two are one number;
// where presentation dropped a tail outright, the ink stops on the onset and the ring stays.
TEST_CASE("Chart projection publishes each note's ring beside its ink", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartViewState state = makeChartViewState(makeArrangementWithChart(), tempo_map);

    REQUIRE(state.notes.size() == 7);

    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    // The fixture's last note is a lone eighth with no technique, so rule 2 draws it no tail at
    // all — bit-exactly its own onset, the way the projection assigns it across — while its ring
    // is the eighth it sounds for.
    CHECK_THAT(
        state.notes[6].ink_end_seconds,
        Catch::Matchers::WithinULP(state.notes[6].start_seconds, 0));
    CHECK(state.notes[6].ring_end_seconds == Catch::Approx(13.125 * beat));

    // A note no rule cropped draws its whole ring, and the two ends are one number exactly.
    CHECK(state.notes[0].ring_end_seconds == Catch::Approx(5.0 * beat));
    CHECK_THAT(
        state.notes[0].ink_end_seconds,
        Catch::Matchers::WithinULP(state.notes[0].ring_end_seconds, 0));
}

// The crop stops the ink and touches nothing else: every statement the note stores reaches the
// projection at its stored instant, whether it stands before the ink end or past it.
TEST_CASE("Chart projection crops the ink and keeps every keyframe", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Four beats of ring reaching exactly the next onset (so rule 1 crops to the margin rather
        // than drawing it whole), with statements before the crop and one past it.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{4},
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1}, .bend = 2.0},
                    Keyframe{.offset = Fraction{2}, .fret = 7},
                    // Past the crop at 39/10, in the ending zone.
                    Keyframe{.offset = Fraction{79, 20}, .bend = 1.0},
                },
        },
        // The binding onset the crop measures against.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.notes.size() == 2);
    const NoteViewState& ringing = state.notes[0];

    // 120 BPM 4/4: a beat is half a second and the margin is 0.05 s, a tenth of a beat here, so the
    // ink stops at 3.9 beats and the ring runs the full four.
    CHECK(ringing.ink_end_seconds == Catch::Approx(1.95));
    CHECK(ringing.ring_end_seconds == Catch::Approx(2.0));

    // The curve carries the onset point in front, which is the channel's opening value, and then
    // every stated point — the last at its stored 3.95 beats, past the ink end.
    REQUIRE(ringing.bend.size() == 3);
    CHECK(ringing.bend[2].seconds == Catch::Approx(1.975));
    CHECK(ringing.bend[2].seconds > ringing.ink_end_seconds);
    REQUIRE(ringing.slides.size() == 1);
    // Each keyframe carries the AUTHORED offset it was projected from, which is the identity the
    // editor's selection keys it by.
    CHECK(ringing.slides[0].offset == Fraction{2});
    CHECK(ringing.slides[0].seconds == Catch::Approx(1.0));
}

// A drawn keyframe's `offset` is its IDENTITY — the STORED statement's own offset, which the
// editor keys a click, a caret and the accent ring by — and its `seconds` is that same statement's
// instant, since nothing presentation decides moves a statement. The fixture's note carries a
// statement AT its ring's end (a slide-out) bound by a head on another string: the ink stops one
// margin before that head and the slide-out stays where the chart states it, past the ink end.
TEST_CASE("Chart projection draws each keyframe at its stored instant", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{2},
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1, 2}, .fret = 7},
                    // A bend-only statement between the two stops: it draws no mark of its own, so
                    // the identities must survive the fret channel being a SUBSET of the keyframes.
                    Keyframe{.offset = Fraction{1}, .bend = 2.0},
                    // At the ring's end, stating a fret: the slide-out.
                    Keyframe{.offset = Fraction{2}, .fret = 9},
                },
        },
        // A head on another string, exactly where the ring ends, so rule 1 binds on it and the ink
        // stops one margin earlier.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 3},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.notes.size() == 2);
    const NoteViewState& note = state.notes[0];

    // The two fret-stating keyframes above, in the order the chart states them.
    const std::vector<Fraction> stored_offsets = {Fraction{1, 2}, Fraction{2}};
    REQUIRE(note.slides.size() == stored_offsets.size());
    for (std::size_t index = 0; index < stored_offsets.size(); ++index)
    {
        CHECK(note.slides[index].offset == stored_offsets[index]);
    }
    CHECK_FALSE(note.slides[0].slide_out);
    CHECK(note.slides[1].slide_out);
    // 120 BPM 4/4: the stop at half a beat draws a quarter second in, and the slide-out at the
    // ring's own end a second in — the ring the chart stores — while the ink stops one margin
    // (50 ms) before the head that binds.
    CHECK(note.slides[0].seconds == Catch::Approx(0.25));
    CHECK(note.slides[1].seconds == Catch::Approx(1.0));
    CHECK(note.ring_end_seconds == Catch::Approx(1.0));
    CHECK(note.ink_end_seconds == Catch::Approx(0.95));
    // A plain paint walks the stop before the ink end and the leg toward the slide-out as far as
    // the ink end; a reveal walks both stops.
    CHECK(keyframeDrawn(note.slides[0], note.ink_end_seconds));
    CHECK_FALSE(keyframeDrawn(note.slides[1], note.ink_end_seconds));
    CHECK(keyframeDrawn(note.slides[1], note.ring_end_seconds));
}

// The ink never runs past the ring: presentation only ever stops drawing early, so every note's
// ink end lies within its onset and its stored ring end.
TEST_CASE("Chart projection keeps every ink end within its ring", "[core][chart]")
{
    const ChartViewState state = makeChartViewState(makeArrangementWithChart(), makeTempoMap());
    REQUIRE(state.notes.size() == 7);

    std::size_t cropped = 0;
    std::size_t emptied = 0;
    for (const NoteViewState& note : state.notes)
    {
        CHECK(note.ring_end_seconds > note.start_seconds);
        CHECK(note.ink_end_seconds >= note.start_seconds);
        CHECK(note.ink_end_seconds <= note.ring_end_seconds);
        if (note.ink_end_seconds < note.ring_end_seconds)
        {
            ++cropped;
        }
        if (note.ink_end_seconds <= note.start_seconds)
        {
            ++emptied;
        }
    }
    // The fixture holds a cropped tail (the shift glide) and an emptied one (the lone eighth), so
    // the bounds above are asked of both kinds rather than only of whole rings.
    CHECK(cropped >= 1);
    CHECK(emptied >= 1);
}

// THE ONE TAIL-FADE RULE both surfaces read: a fraction of the INK's length, never less than a
// floor of time, and never more than the ink itself. Each case lands on a different arm of that
// clamp, and the long tail's ring runs past its ink so a rule measured on the ring would fail it.
TEST_CASE("Tail fade spans a fraction of the ink, floored and clamped to it", "[core][chart]")
{
    const auto note = [](const double start, const double ink_end, const double ring_end) {
        return NoteViewState{
            .start_seconds = start,
            .ring_end_seconds = ring_end,
            .ink_end_seconds = ink_end,
            .bend = {},
            .slides = {},
            .vibrato = {},
        };
    };

    // A long tail dissolves over 35% of its ink.
    CHECK_THAT(tailFadeSeconds(note(2.0, 12.0, 20.0)), Catch::Matchers::WithinRel(3.5, 1e-12));
    // A short one over the 0.25 s floor, where 35% would be only 0.175 s.
    CHECK_THAT(tailFadeSeconds(note(2.0, 2.5, 2.5)), Catch::Matchers::WithinRel(0.25, 1e-12));
    // One shorter than the floor over its whole length.
    CHECK_THAT(tailFadeSeconds(note(2.0, 2.2, 2.2)), Catch::Matchers::WithinRel(0.2, 1e-12));
    // And an ink that stops at its onset over nothing, however far its ring runs.
    CHECK_THAT(tailFadeSeconds(note(2.0, 2.0, 6.0)), Catch::Matchers::WithinULP(0.0, 0));
}

// The vibrato channel reaches both surfaces as the REGIONS it states rather than as a flag: each
// leg of the ring states its own width, so vibrato can begin at a glide's arrival, stop mid-hold,
// and begin again, and each region has to cover exactly the stretch the channel says it does. The
// onset-only case is the identity that keeps every chart written before the channel could say
// anything else drawing precisely what it drew.
TEST_CASE("Chart projection resolves the vibrato channel into regions", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Alone on the chart, so no binding onset crops the tail and the region ends are the channel's
    // own. 120 BPM 4/4: the onset sits at 0.0s, a beat lasts half a second, and four beats of ring
    // end at 2.0s.
    const auto project =
        [&tempo_map](const VibratoState onset_vibrato, std::vector<Keyframe> keyframes) {
            Chart chart;
            chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
            chart.notes = {
                ChartNote{
                    .position = GridPosition{.measure = 1, .beat = 1},
                    .string = 1,
                    .fret = 5,
                    .sustain = Fraction{4},
                    .vibrato = onset_vibrato,
                    .bend = {},
                    .keyframes = std::move(keyframes),
                },
            };
            Arrangement arrangement = makeArrangementWithChart();
            arrangement.chart = std::move(chart);
            return makeChartViewState(arrangement, tempo_map);
        };

    SECTION("vibrato stated at the onset alone covers the whole ring")
    {
        const ChartViewState state = project(VibratoState::Narrow, {});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        // Exactly the ring's own two ends, which is what makes this the drawing both surfaces
        // produced when the channel was one boolean.
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK_THAT(
            view.vibrato[0].end_seconds, Catch::Matchers::WithinULP(view.ring_end_seconds, 0));
    }

    SECTION("vibrato stated mid-ring begins at the statement, not at the onset")
    {
        const ChartViewState state = project(
            VibratoState::None, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK(view.vibrato[0].start_seconds == Catch::Approx(1.0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(2.0));
        // The discrimination the whole stage exists for: a region running from the onset would
        // draw vibrato across the two beats the chart says are steady.
        CHECK(view.vibrato[0].start_seconds > view.start_seconds);
    }

    SECTION("vibrato ends with its leg, not at the ring's end")
    {
        // The curl's keyframe begins a leg that states no width, so the vibrato stops there:
        // nothing carries across a keyframe, and ending needs no statement of its own.
        const ChartViewState state =
            project(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .bend = 1.0}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(1.0));
        CHECK(view.vibrato[0].end_seconds < view.ring_end_seconds);
    }

    SECTION("a channel that starts, stops and starts again states two regions")
    {
        const ChartViewState state = project(
            VibratoState::Narrow,
            {
                Keyframe{.offset = Fraction{1}, .bend = 0.5},
                Keyframe{.offset = Fraction{2}, .bend = 1.0, .vibrato = VibratoState::Narrow},
                Keyframe{.offset = Fraction{3}, .bend = 0.0},
            });
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 2);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(0.5));
        CHECK(view.vibrato[1].start_seconds == Catch::Approx(1.0));
        CHECK(view.vibrato[1].end_seconds == Catch::Approx(1.5));
    }

    SECTION("a width equal to the leg before it is no boundary")
    {
        // Restating the width the leg before already vibrates at says nothing, so the region stays
        // whole rather than being cut in two at an instant where the vibrato does not change.
        const ChartViewState state = project(
            VibratoState::Narrow,
            {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK_THAT(
            view.vibrato[0].end_seconds, Catch::Matchers::WithinULP(view.ring_end_seconds, 0));
    }

    SECTION("a slide stop whose leg states no width ends the vibrato there")
    {
        // The glide's stop begins a leg of its own, so vibrato on the first leg does not ride
        // through it.
        const ChartViewState state =
            project(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .fret = 7}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(1.0));
    }

    SECTION("a bare keyframe ends the vibrato there")
    {
        // The stored form of vibrato ending mid-hold (the importer's tie): the point states no
        // channel, but it begins an unvibrated leg.
        const ChartViewState state =
            project(VibratoState::Narrow, {Keyframe{.offset = Fraction{2}}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(1.0));
        // It states no position, so it draws no stop.
        CHECK(view.slides.empty());
    }

    SECTION("a step between the widths closes one region and opens the other")
    {
        // The case an either/or boundary test would hide: at this instant the vibrato neither
        // starts nor stops, so a reading that asked "did it turn on or off" would find neither and
        // draw the whole tail at the width the note opened with.
        const ChartViewState state = project(
            VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Wide}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 2);
        CHECK(view.vibrato[0].state == VibratoState::Narrow);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(1.0));
        // The wide region opens at the very instant the narrow one closes: one statement, two
        // regions, no gap the surfaces would draw as a pause in the vibrato.
        CHECK(view.vibrato[1].state == VibratoState::Wide);
        CHECK(view.vibrato[1].start_seconds == Catch::Approx(1.0));
        CHECK_THAT(
            view.vibrato[1].end_seconds, Catch::Matchers::WithinULP(view.ring_end_seconds, 0));
    }

    SECTION("every region carries the width it was stated at")
    {
        // A wide onset with no restatement is the whole-tail case at the other tier, which is the
        // one thing a region-carrying-the-width model has to get right before anything draws.
        const ChartViewState state = project(VibratoState::Wide, {});
        REQUIRE(state.notes.size() == 1);
        REQUIRE(state.notes.front().vibrato.size() == 1);
        CHECK(state.notes.front().vibrato[0].state == VibratoState::Wide);
    }

    SECTION("a note whose channel never speaks carries no region at all")
    {
        // Keyframes, but on another channel: a glide and a curl say nothing about vibrating.
        const ChartViewState state = project(
            VibratoState::None,
            {
                Keyframe{.offset = Fraction{1}, .bend = 2.0},
                Keyframe{.offset = Fraction{2}, .fret = 7},
            });
        REQUIRE(state.notes.size() == 1);
        CHECK(state.notes.front().vibrato.empty());
    }
}

// A shift slide's arrival head is its own note, so its width is its own first leg's: the head
// vibrates from its onset and the glide into it — the origin's leg — never does.
TEST_CASE("Chart projection vibrates a shift slide's arrival head, not its glide", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // The origin glides from 5 to 8, its stop ON the head struck at 8 a beat later.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 8}},
        },
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 2},
            .string = 1,
            .fret = 8,
            .sustain = Fraction{2},
            .vibrato = VibratoState::Narrow,
            .bend = {},
            .keyframes = {},
        },
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);
    const ChartViewState state = makeChartViewState(arrangement, tempo_map);

    REQUIRE(state.notes.size() == 2);
    CHECK(state.notes[0].vibrato.empty());
    const NoteViewState& head = state.notes[1];
    REQUIRE(head.vibrato.size() == 1);
    CHECK_THAT(head.vibrato[0].start_seconds, Catch::Matchers::WithinULP(head.start_seconds, 0));
    CHECK_THAT(head.vibrato[0].end_seconds, Catch::Matchers::WithinULP(head.ring_end_seconds, 0));
}

// A placement is authored at the instant the chart states a gesture end, and the ramp table files
// each glide's segment under that same stored instant — nothing presentation decides moves a
// statement, so a slide-out whose ink the crop stops a margin early is still ridden to where the
// chart states it. Every stored glide keyframe files a ramp, the one past the ink end included:
// the hand completes where the sound goes, not where the ink stops.
TEST_CASE("Chart projection ramps a cropped slide-out to its stored instant", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Four beats of ring ending exactly on the next onset — on ANOTHER string — trailing off
        // unpitched at its very end, so that head binds the drawn tail and the ink stops one
        // margin before the four beats the chart states the terminal at.
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
    // Exactly where the chart states the terminal, which is what a placement is authored against.
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 9, .width = 4},
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);

    REQUIRE(state.notes.size() == 2);
    // Bound once, so a count check and the access after it are provably the same object.
    const NoteViewState& glide = state.notes[0];
    REQUIRE(glide.slides.size() == 1);
    CHECK(glide.slides.back().slide_out);
    CHECK(glide.slides.back().fret == 12);
    CHECK(glide.slides.back().offset == Fraction{4});
    // The terminal stands at the ring's end, two seconds in, past the ink end one margin earlier:
    // a plain paint walks no stop, a reveal walks the terminal.
    CHECK(glide.ink_end_seconds == Catch::Approx(1.95));
    CHECK_FALSE(keyframeDrawn(glide.slides[0], glide.ink_end_seconds));
    REQUIRE(keyframeDrawn(glide.slides[0], glide.ring_end_seconds));
    CHECK(glideStopAt(glide, 0).seconds == Catch::Approx(2.0));
    CHECK(glideStopAt(glide, 0).unpitched);

    // The identity key finds the ramp: the hand rides the slide-out from the note's onset to the
    // terminal's stored instant, easing with the unpitched family.
    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(2.0));
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(2.0));
    CHECK(state.fret_hand_positions[0].unpitched_ramp);
}

TEST_CASE("Chart projection is empty without a chart", "[core][chart]")
{
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart.reset();

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    CHECK(state.stringCount() == 0);
    CHECK(state.notes.empty());
    CHECK(state.display_hold_ends.empty());
    CHECK(state.shapes.empty());
    CHECK(state.fret_hand_positions.empty());
}

// The two lengths a note has on screen, and where each comes from: `ink_end_seconds` is the DRAWN
// tail (what is drawn and scored) and `display_hold_ends` is the hold (how long the hand stays
// down, which a hand-shape span can outlive the tail by). Both are resolved from the one
// resolutions pass, so this pins the projection's wiring as much as the values.
TEST_CASE("Chart projection draws ink ends and holds the shape's chug", "[core][chart]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const auto note = [](int beat, int string, int fret, Fraction sustain) {
        return ChartNote{
            .position = GridPosition{.measure = 1, .beat = beat},
            .string = string,
            .fret = fret,
            .sustain = sustain,
            .bend = {},
            .keyframes = {},
        };
    };
    // A half-beat chug on two strings — which derives a span of its own — then the same chug alone
    // a beat later, then a note whose ring earns a real tail. At this tempo these rings last
    // exactly the kept-sustain bound and no longer, which leaves them tail-less; any longer and
    // they would earn tails and stop being chugs at all.
    chart.notes = {
        note(1, 1, 5, Fraction{1, 2}),
        note(1, 2, 7, Fraction{1, 2}),
        note(2, 1, 7, Fraction{1, 2}),
        note(3, 1, 9, Fraction{2}),
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    // 120 BPM 4/4: a beat is half a second, so the derived span runs 0.0s to 0.25s (the strum's
    // own ring) and the later string-1 onsets sit at 0.5s and 1.0s.
    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.notes.size() == 4);
    REQUIRE(state.display_hold_ends.size() == 4);
    // Every chug at or under the bound draws no tail at all: its ink stops on its onset.
    CHECK(state.notes[0].ink_end_seconds == Catch::Approx(0.0));
    CHECK(state.notes[1].ink_end_seconds == Catch::Approx(0.0));
    CHECK(state.notes[2].ink_end_seconds == Catch::Approx(0.5));
    // The one ring that runs longer than the kept-sustain bound draws its tail, cropped by nothing
    // (no later onset binds it).
    CHECK(state.notes[3].ink_end_seconds == Catch::Approx(2.0));

    // The strum's members are held while the shape is — capped at each one's own ring, which is
    // shorter than both the span's remainder and the restrike a beat later.
    CHECK(state.display_hold_ends[0] == Catch::Approx(0.25));
    CHECK(state.display_hold_ends[1] == Catch::Approx(0.25));
    // A single note is not a strum, so nothing extends it: it holds exactly to its ink end.
    CHECK(state.display_hold_ends[2] == Catch::Approx(0.5));
    CHECK(state.display_hold_ends[3] == Catch::Approx(2.0));
}

// RULE 12A LIVES HERE. The derivation stores THE MUSICAL CLOSE — the instant a span's statement
// ended — and the minimum sustain distance every drawn element keeps is taken off it exactly once,
// where the view state is built. Each section is one arm of that rule, and the last two are the
// arms a blanket "close minus a margin" would get wrong.
//
// BOTH ENDS are pinned in every section, because the view state publishes both and the editor's
// span reveal draws to the second. What separates them is the CLOSE CLASS and nothing else: where
// an EVENT closed the span a margin is owed and the drawn extent falls short of the close, and
// where the statement ran out, the rings died early, or the close sounds nothing, no margin is
// owed and the two ends are the same instant — so a reveal there must move nothing at all.
TEST_CASE(
    "Chart projection trims a span's drawn extent to the minimum sustain distance", "[core][chart]")
{
    // 120 BPM 4/4 throughout: a beat is half a second and the margin is 0.05 s, a tenth of a beat.
    const auto note =
        [](const GridPosition& position, const int string, const int fret, const Fraction sustain) {
            return ChartNote{
                .position = position,
                .string = string,
                .fret = fret,
                .sustain = sustain,
                .bend = {},
                .keyframes = {},
            };
        };
    // A bare tap claiming a stop the fretting hand takes without striking it: the picking hand
    // makes the onset, so the claim is the only thing the fretting hand states at that slot.
    const auto claim =
        [](const GridPosition& position, const int string, const int fret, const int held) {
            ChartNote claimed;
            claimed.position = position;
            claimed.string = string;
            claimed.fret = fret;
            claimed.sustain = Fraction{1, 4};
            claimed.attack = NoteAttack::Tap;
            claimed.held = held;
            return claimed;
        };
    const auto project = [](std::vector<ChartNote> notes) {
        Chart chart;
        chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
        std::ranges::sort(notes, chartNoteOrderLess);
        chart.notes = std::move(notes);
        Arrangement arrangement = makeArrangementWithChart();
        arrangement.chart = std::move(chart);
        return makeChartViewState(arrangement, makeTempoMap());
    };
    const GridPosition one{.measure = 1, .beat = 1};
    const GridPosition two{.measure = 1, .beat = 2};

    SECTION("the margin comes off the closing head")
    {
        // Two strums merging into one span, closed by a lone note an eighth after the second. The
        // statement runs to that note at beat 1.5 (0.75s) and the rails stop one margin short.
        const ChartViewState state = project({
            note(one, 1, 5, Fraction{1}),
            note(one, 2, 7, Fraction{1}),
            note(two, 1, 5, Fraction{1, 2}),
            note(two, 2, 7, Fraction{1, 2}),
            note(
                GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}},
                3,
                7,
                Fraction{1, 2}),
        });
        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].start_seconds == Catch::Approx(0.0));
        CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(0.7));
        // The close is that lone note's own onset — beat 1.5, which is where both the statement's
        // reach and the closing event land — so the drawn extent stops one margin inside it.
        CHECK(state.shapes[0].close_seconds == Catch::Approx(0.75));
    }

    SECTION("the trim never retreats behind the span's last statement")
    {
        // The same figure with the closing note a SIXTY-FOURTH after the restrike: the margin
        // alone would end the rails at 77/80 of a beat, in front of the beat-2 strum they are drawn
        // over. The floor keeps them on that strum, at 1.0 beat — half a second.
        const ChartViewState state = project({
            note(one, 1, 5, Fraction{1}),
            note(one, 2, 7, Fraction{1}),
            note(two, 1, 5, Fraction{1, 16}),
            note(two, 2, 7, Fraction{1, 16}),
            note(
                GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 16}},
                3,
                7,
                Fraction{1, 16}),
        });
        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(0.5));
        // The close is still the closing onset, a sixteenth of a beat past the restrike: 1.0625
        // beats from the front, or 0.53125 s. The floor is a DISPLAY floor, so it moves the drawn
        // extent alone and the reveal here reaches the whole 1/16 beat the trim gave back.
        CHECK(state.shapes[0].close_seconds == Catch::Approx(0.53125));
    }

    SECTION("a span crowded inside the margin keeps exact adjacency")
    {
        // Nothing is left after the trim and the floor, so the statement is drawn however crowded:
        // a sixteenth of a beat, ending exactly on the note that closed it.
        const ChartViewState state = project({
            note(one, 1, 3, Fraction{1, 16}),
            note(one, 2, 5, Fraction{1, 16}),
            note(
                GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 16}},
                3,
                7,
                Fraction{1, 16}),
        });
        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(0.03125));
        // Protected adjacency falls back to the musical close itself, so the two ends coincide and
        // the reveal has nothing to add: the rails already end on the note that closed the span.
        CHECK_THAT(
            state.shapes[0].close_seconds,
            Catch::Matchers::WithinULP(state.shapes[0].drawn_end_seconds, 0));
    }

    SECTION("a span whose rings died early is not pulled back from a head it never reached")
    {
        // The statement ends half a beat in, where its shorter member's ring gaps, and the note
        // that closes the span stands two and a half beats further on. The distance to that head is
        // already there, so nothing is taken off — a trim keyed on the close alone would shorten
        // this span by a margin it does not owe.
        const ChartViewState state = project({
            note(one, 1, 5, Fraction{1, 2}),
            note(one, 2, 7, Fraction{2}),
            note(GridPosition{.measure = 1, .beat = 4}, 3, 7, Fraction{1}),
        });
        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(0.25));
        // A REACH CLOSE owes no margin, so the two ends are one instant and the reveal moves
        // nothing — which is exactly what keeps it from implying a trim that never happened.
        CHECK_THAT(
            state.shapes[0].close_seconds,
            Catch::Matchers::WithinULP(state.shapes[0].drawn_end_seconds, 0));
    }

    SECTION("a close the fretting hand states nothing at owes no margin")
    {
        // A CLAIM contradicting a stated string breaks the grip at a slot the fretting hand
        // strikes nothing at (rule 8 reads a claim exactly as it reads a strike). The close states
        // no grip, so there is no head to keep clear of and the shape ends exactly where its
        // statement did.
        //
        // Three sounding strings rather than two, because under grip tenure a claim on a string
        // the grip does NOT state grows the span in place and splits nothing: the break has to be
        // a contradiction. What the claim opens on the far side of that break is nothing — the
        // rings crossing it belong to the span it broke (A RING BELONGS ONLY TO THE SPAN IT WAS
        // STRUCK IN) — so the figure is one shape closed at the claim.
        const ChartViewState state = project({
            note(one, 1, 5, Fraction{2}),
            note(one, 2, 7, Fraction{2}),
            note(one, 3, 9, Fraction{2}),
            claim(two, 1, 17, 12),
        });
        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].drawn_end_seconds == Catch::Approx(0.5));
        // A close where the picking hand alone sounds publishes no head to keep clear of, so the
        // two ends are one instant: the reveal moves nothing, which is exactly what keeps it from
        // implying a trim that never happened.
        CHECK_THAT(
            state.shapes[0].close_seconds,
            Catch::Matchers::WithinULP(state.shapes[0].drawn_end_seconds, 0));
    }
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
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.notes.size() == 1);
    const NoteViewState& view = state.notes.front();
    CHECK(view.attack == NoteAttack::PickSlide);
    CHECK_FALSE(view.palm_mute);
    CHECK_FALSE(view.dead);
    CHECK_FALSE(view.tremolo);
    CHECK(view.vibrato.empty());
    CHECK(view.bend.empty());
    // The turnaround and the terminal are both keyframes — the terminal being the SLIDE-OUT, the
    // last of them — so the stop walk reads one leg list, every stop unpitched because a scrape's
    // whole path is the PICK's travel. The turnaround is LINKED and the terminal is not: the pick
    // stays on the string through a direction change, so the junction carries a continuation head
    // (in the note's plectrum shape), while the terminal is where the pick leaves and only its
    // chip marks the position.
    REQUIRE(view.slides.size() == 2);
    CHECK(view.slides.back().slide_out);
    CHECK(view.slides.back().fret == 9);
    // Alone on the chart, so nothing crops it and the ink runs the whole ring.
    CHECK_THAT(view.ink_end_seconds, Catch::Matchers::WithinULP(view.ring_end_seconds, 0));
    REQUIRE(keyframeDrawn(view.slides.back(), view.ink_end_seconds));
    for (std::size_t index = 0; index < 2; ++index)
    {
        CHECK(glideStopAt(view, index).unpitched);
    }
    CHECK(linkedKeyframe(view.slides[0]));
    CHECK(glideStopAt(view, 1).seconds == Catch::Approx(view.ring_end_seconds));
}

// Ramp derivation for the fretting hand's approach: a placement landing exactly on a keyframe's
// grid position ramps over that glide segment (slide-locked), ordinary placements morph over the
// shared minimum-sustain-distance margin, and crowded placements shorten against the previous
// arrival instead of overlapping it. A segment ending at the RING's end is the slide-out, so its
// ramp carries the unpitched family; the pitched slide-lock is pinned by the hold-keyframe case
// below, whose arrival sits strictly inside its ring.
TEST_CASE("Chart projection derives hand-approach ramps", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    // A sustained note whose tail trails off unpitched: a placement on its end rides the
    // slide-out's own segment with the unpitched curve, so the window travels exactly with the
    // drawn rail.
    chart.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 3},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 12}},
        });
    chart.fret_hand_positions = {
        // Ordinary move: the margin morph.
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 1, .width = 4},
        // Crowded: a sixteenth of a beat after the previous arrival — closer than the margin —
        // so the morph shortens against it.
        FretHandPosition{
            .position = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 16}},
            .fret = 2,
            .width = 4,
        },
        // Exactly on the fixture's pitched keyframe (3:1+1/2 advanced by its two-beat offset):
        // slide-locked to the glide segment.
        FretHandPosition{
            .position = GridPosition{.measure = 3, .beat = 3, .offset = Fraction{1, 2}},
            .fret = 6,
            .width = 4,
        },
        // Exactly where the unpitched slide-out ends (4:3 advanced one beat): the margin
        // morph, arriving with the slide-out, never the whole-sustain segment.
        FretHandPosition{.position = GridPosition{.measure = 4, .beat = 4}, .fret = 9, .width = 4},
    };

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.fret_hand_positions.size() == 4);

    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
    CHECK(
        state.fret_hand_positions[0].ramp_seconds ==
        Catch::Approx(g_minimum_sustain_distance_seconds));

    CHECK(state.fret_hand_positions[1].seconds == Catch::Approx(4.0625 * beat));
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(0.0625 * beat));

    // The glide starts at the note onset (8.5 beats) and lands at the keyframe (10.5 beats), which
    // is the fixture ring's own end — the slide-out, so the family is the unpitched one.
    CHECK(state.fret_hand_positions[2].seconds == Catch::Approx(10.5 * beat));
    CHECK(state.fret_hand_positions[2].ramp_seconds == Catch::Approx(2.0 * beat));
    CHECK(state.fret_hand_positions[2].unpitched_ramp);

    // A placement on an unpitched slide-out's end rides that slide-out's OWN segment, exactly as a
    // pitched glide does, and carries the unpitched family so the window eases with the same curve
    // the rail is drawn with. The slide-out's segment runs from the note's onset (14 beats) to its
    // end (15 beats) because the note carries no pitched keyframes ahead of it; morphing over the
    // metrical margin instead would leave the window stationary for most of the drawn glide and
    // then sprinting to catch up.
    CHECK(state.fret_hand_positions[3].seconds == Catch::Approx(15.0 * beat));
    CHECK(state.fret_hand_positions[3].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK(state.fret_hand_positions[3].unpitched_ramp);
    // The two margin morphs keep the pitched family: only a slide-out ramp is unpitched.
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
}

// A shift slide's ARRIVAL is pitched even where the crop stops the ink before it, and only the
// resolved relation says so. Reading it as a slide-out eased the window — and every open-string
// band behind it — with the slide-out curve instead of the glide's.
TEST_CASE("Chart projection keeps a cropped shift slide's arrival ramp pitched", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    // Exactly on the fixture's shift-slide arrival, which the store places at the ring's END — the
    // landing's own onset — while the crop stops the ink one margin earlier. A placement is
    // authored against the chart, so it is the stored instant a ramp is filed under.
    chart.fret_hand_positions.push_back(
        FretHandPosition{
            .position = GridPosition{.measure = 4, .beat = 2},
            .fret = 8,
            .width = 4,
        });

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.fret_hand_positions.size() == 2);

    // The window completes WITH THE ARRIVAL: the glide segment starts at the onset (12 beats) and
    // ends at the 13 beats the chart states the arrival at, past the ink end a margin earlier. It
    // stays PITCHED, which is the other thing this case is about.
    REQUIRE(state.notes.size() == 7);
    const NoteViewState& shift = state.notes[5];
    REQUIRE(shift.slides.size() == 1);
    const KeyframeViewState& arrival = shift.slides.back();
    CHECK(arrival.seconds == Catch::Approx(13.0 * beat));
    CHECK(arrival.seconds > shift.ink_end_seconds);
    CHECK(state.fret_hand_positions[1].seconds == Catch::Approx(arrival.seconds));
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
}

// The settle is the stretch of a slide-matched ramp past its rail's ink end: a placement on a glide
// stop the ink end cuts settles from the ink end to the arrival, while one on a stop the rail
// reaches, and a margin morph, settle over nothing.
TEST_CASE("Chart projection settles a placement past its glide's ink end", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    chart.notes = {
        // A shift slide into its re-picked landing: the landing's head crops the ink one margin
        // before the arrival.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 7}},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 2,
            .fret = 7,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
        // A glide whose ring runs half a beat past its arrival, alone on the board after it: the
        // rail reaches the stop.
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{3, 2},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 9}},
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 2}, .fret = 7, .width = 4},
        FretHandPosition{.position = GridPosition{.measure = 3, .beat = 2}, .fret = 9, .width = 4},
        // An ordinary move with no glide under it: the margin morph.
        FretHandPosition{.position = GridPosition{.measure = 4, .beat = 1}, .fret = 2, .width = 4},
    };

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.notes.size() == 3);
    REQUIRE(state.fret_hand_positions.size() == 3);

    // The cut glide: the arrival (5 beats) lies past the shift note's ink end, and the settle is
    // exactly that overhang — inside the one-beat slide-matched ramp.
    const NoteViewState& shift = state.notes[0];
    REQUIRE(shift.slides.size() == 1);
    REQUIRE(shift.slides.front().seconds > shift.ink_end_seconds);
    const FhpViewState& cut = state.fret_hand_positions[0];
    CHECK(cut.seconds == Catch::Approx(5.0 * beat));
    CHECK(cut.ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK(cut.settle_seconds == Catch::Approx(cut.seconds - shift.ink_end_seconds));
    CHECK(cut.settle_seconds > 0.0);
    CHECK(cut.settle_seconds < cut.ramp_seconds);

    // The reached glide: slide-matched over its own one-beat segment, nothing to settle.
    const NoteViewState& reached = state.notes[2];
    REQUIRE(reached.slides.size() == 1);
    REQUIRE(keyframeDrawn(reached.slides.front(), reached.ink_end_seconds));
    const FhpViewState& drawn = state.fret_hand_positions[1];
    CHECK(drawn.seconds == Catch::Approx(9.0 * beat));
    CHECK(drawn.ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_THAT(drawn.settle_seconds, Catch::Matchers::WithinULP(0.0, 0));

    // The margin morph settles over nothing.
    const FhpViewState& morph = state.fret_hand_positions[2];
    CHECK(morph.ramp_seconds == Catch::Approx(g_minimum_sustain_distance_seconds));
    CHECK_THAT(morph.settle_seconds, Catch::Matchers::WithinULP(0.0, 0));
}

// Two rings can end on ONE head from two strings, and the ramp table is keyed by instant alone: the
// placement standing there is the hand LANDING, so the pitched arrival outranks the slide-out
// leaving beside it whichever note the walk reaches first.
TEST_CASE("Chart projection prefers a pitched ramp at a shared instant", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    chart.notes = {
        // String 1 trails off unpitched onto the shared instant, and comes FIRST in slot order, so
        // it is the note whose ramp the table would keep under a first-in-order rule.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 12}},
        },
        // String 2 shift-slides into its own re-picked landing at that same instant.
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 2,
            .fret = 3,
            .sustain = Fraction{1},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{1}, .fret = 7}},
        },
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 2},
            .string = 2,
            .fret = 7,
            .sustain = Fraction{1, 8},
            .bend = {},
            .keyframes = {},
        },
    };
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 2}, .fret = 7, .width = 4},
    };

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.fret_hand_positions.size() == 1);

    // The arrival's own segment: from the onset (4 beats) to the arrival's stored instant, the
    // 5 beats both rings end at.
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(5.0 * beat));
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
}

// An equal-fret keyframe is a HOLD, not a glide: nothing travels across it, so a placement landing
// on one must take the short margin morph rather than a ramp spanning the held stretch. Holds are
// how a slide notated on a tied continuation records where it leaves from, so tying their span to
// the window drifts the hand across the whole tied group to arrive at a fret it never left — the
// picture at fret 11 of measure 50 of the acceptance song.
TEST_CASE("Chart projection gives a hold keyframe the margin morph", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart_ptr = chartOrNull(arrangement);
    REQUIRE(chart_ptr != nullptr);
    Chart& chart = *chart_ptr;
    // Four beats of held fret 5, then a one-beat glide up to fret 9: the hold pins the pitch at
    // beat 4 and the travel happens only over the beat that follows. The ring runs half a beat
    // past that arrival, which is what keeps the arrival a PITCHED stop — a fret stated at the
    // ring's own end would be the slide-out instead.
    chart.notes.push_back(
        ChartNote{
            .position = GridPosition{.measure = 2, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{9, 2},
            .bend = {},
            .keyframes = {
                Keyframe{.offset = Fraction{3}, .fret = 5},
                Keyframe{.offset = Fraction{4}, .fret = 9},
            },
        });
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 4}, .fret = 5, .width = 4},
        FretHandPosition{.position = GridPosition{.measure = 3, .beat = 1}, .fret = 9, .width = 4},
    };

    const ChartViewState state = makeChartViewState(arrangement, tempo_map);
    REQUIRE(state.fret_hand_positions.size() == 2);

    // The hold at beat 4 does NOT inherit the three-beat held stretch; it morphs over the margin.
    CHECK(
        state.fret_hand_positions[0].ramp_seconds ==
        Catch::Approx(g_minimum_sustain_distance_seconds));
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
    // The real glide that follows still rides its own one-beat segment, and the arrival is drawn
    // exactly where the chart states it, so the window completes there.
    CHECK(state.fret_hand_positions[1].seconds == Catch::Approx(8.0 * beat));
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
}

// THE DIGIT WINDOW and the mark that rides it. A bracket is the span's CHORD FRAME: it states
// every member's fret AT THE INSTANT IT DRAWS, and only a head standing right there takes a number
// out of it. A held stop is one of those members, so it prints in the frame like any other —
// displaced into the satellite column only where its own tap head occupies the string's centre
// right there. The note's own mark rides that same entry: a digit standing in the bracket's own
// column is the span's furniture, while the note's OWN face is its satellite, published for every
// held stop and shown on the terms its authorship earns (THE SATELLITE REVEAL). Two facts in two
// inks for a mid-span tap, and a drawn digit is clickable and an undrawn one unreachable by
// construction.
TEST_CASE("A held stop prints in its span's opening bracket", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Measure 1 of the default map: beat 1 sits at 0.0s and each beat lasts half a second.
    const auto strike =
        [](const int beat, const int string, const int fret, const Fraction sustain) {
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = beat};
            note.string = string;
            note.fret = fret;
            note.sustain = sustain;
            return note;
        };
    const auto tap = [](const int beat,
                        const int string,
                        const int fret,
                        const std::optional<int>
                            held,
                        const Fraction sustain) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        note.sustain = sustain;
        note.attack = NoteAttack::Tap;
        note.held = held;
        return note;
    };
    const auto project = [&tempo_map](std::vector<ChartNote> notes) {
        Arrangement arrangement = makeArrangementWithChart();
        Chart* const chart = chartOrNull(arrangement);
        REQUIRE(chart != nullptr);
        if (chart != nullptr)
        {
            std::ranges::sort(notes, chartNoteOrderLess);
            chart->notes = std::move(notes);
        }
        return makeChartViewState(arrangement, tempo_map);
    };
    // The projection keeps the chart's note order, and each figure below carries exactly one tap.
    const auto tap_view = [](const ChartViewState& state) -> const NoteViewState* {
        for (const NoteViewState& note : state.notes)
        {
            if (note.attack == NoteAttack::Tap)
            {
                return &note;
            }
        }
        return nullptr;
    };

    SECTION("a mid-span authored tap prints in the frame AND stands its own satellite")
    {
        // String 1 rings from beat 1; the strum at beat 2 arrives under it, so the span's FRONT
        // backdates to that ring's onset and the bracket draws there. The tap's held stop joins at
        // the strum's slot, which is INSIDE the frame rather than at it.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{4}),
             strike(2, 2, 7, Fraction{3}),
             tap(2, 3, 12, 9, Fraction{1, 4})});

        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].arpeggio);
        const std::optional<double>& bracket = state.shapes[0].bracket_seconds;
        REQUIRE(bracket.has_value());
        if (bracket.has_value())
        {
            CHECK_THAT(*bracket, Catch::Matchers::WithinAbs(0.0, 1e-9));
        }
        // String 1's own head stands at the bracket and prints its 5, so the frame stays quiet
        // there. String 2's strum and string 3's tapped stop both ACCUMULATE IN at the second
        // beat, past the bracket's own instant — neither heads its string where the mark draws, so
        // both print in the frame. Under a span-wide window, string 2's own later head would
        // suppress its digit and the frame would fall SILENT about a member the span states.
        REQUIRE(state.shapes[0].strings.size() == 3);
        CHECK(
            state.shapes[0].strings[0] ==
            ShapeStringViewState{.string = 1, .stop = frettedStop(5), .digit = std::nullopt});
        CHECK(
            state.shapes[0].strings[1] ==
            ShapeStringViewState{
                .string = 2, .stop = frettedStop(7), .digit = StopMarkSlot::Bracket
            });
        CHECK(
            state.shapes[0].strings[2] ==
            ShapeStringViewState{
                .string = 3, .stop = frettedStop(9), .digit = StopMarkSlot::Bracket
            });

        // AND THE TAP WEARS ITS OWN FACE BESIDE THAT. Two facts, two inks: the digit above is the
        // span's furniture stating MEMBERSHIP, and this is the note-scoped satellite — what a press
        // addresses and a typed digit retypes. AUTHORED here, so it STANDS, and it stands at the
        // tap's own slot (0.5 s) rather than at the bracket the membership digit printed in
        // (0.0 s). Gating publication on the span's digit reaching the satellite column would leave
        // this tap no mark at all.
        const NoteViewState* const tapped = tap_view(state);
        REQUIRE(tapped != nullptr);
        if (tapped != nullptr)
        {
            CHECK(tapped->held == std::optional{9});
            const std::optional<StopMarkViewState>& mark = tapped->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Standing);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
            }
        }
    }

    SECTION("a node head over a node grip suppresses; the members arriving later print their node")
    {
        // THE NODE GRIP at the digit rule: the comparison is on PLACES. Three naturals accumulating
        // at the twelfth-partial node found a parts span whose posture holds nodes, and the bracket
        // prints each entry through the one label authority — "12", never the 0 the notes store.
        // String 4's own diamond head stands at the bracket and sounds that very node, so its digit
        // is suppressed; strings 5 and 6 arrive later and print in the frame.
        const auto natural = [&strike](const int beat, const int string, const Fraction sustain) {
            ChartNote note = strike(beat, string, 0, sustain);
            note.harmonic_node = 12.0;
            return note;
        };
        const ChartViewState state = project({
            natural(1, 4, Fraction{4}),
            natural(2, 5, Fraction{3}),
            natural(3, 6, Fraction{2}),
        });

        REQUIRE(state.shapes.size() == 1);
        CHECK(state.shapes[0].arpeggio);
        REQUIRE(state.shapes[0].strings.size() == 3);
        CHECK(
            state.shapes[0].strings[0] ==
            ShapeStringViewState{.string = 4, .stop = nodeStop(12.0), .digit = std::nullopt});
        CHECK(
            state.shapes[0].strings[1] ==
            ShapeStringViewState{
                .string = 5, .stop = nodeStop(12.0), .digit = StopMarkSlot::Bracket
            });
        CHECK(
            state.shapes[0].strings[2] ==
            ShapeStringViewState{
                .string = 6, .stop = nodeStop(12.0), .digit = StopMarkSlot::Bracket
            });
    }

    SECTION("an artificial harmonic's pressed stop prints once, in the note's own satellite")
    {
        // A fret-5 head damped at node 17 prints "17" — it sounds at the node — while the fretting
        // hand presses 5, which is the grip the span states. The PLACE test cannot suppress that 5:
        // two different places are not one number, and 17 and 5 are not the same number. What
        // suppresses it is the arm below, the one every fretting-hand head takes: the note carries
        // the pressed stop as its own held stop and prints it beside its own head, so a bracket
        // digit would be that same 5 twice on one string.
        //
        // Which hand struck is no part of either test. A TAP holding the same 5 keeps its bracket
        // digit (the sections around this one), because there the span's number is the only
        // statement that the fretting hand is on the string at all.
        ChartNote artificial = strike(1, 1, 5, Fraction{4});
        artificial.harmonic_node = 17.0;
        const ChartViewState state = project({
            artificial,
            strike(2, 2, 7, Fraction{3}),
            strike(3, 3, 9, Fraction{2}),
        });

        REQUIRE(state.shapes.size() == 1);
        REQUIRE(state.shapes[0].strings.size() == 3);
        CHECK(
            state.shapes[0].strings[0] ==
            ShapeStringViewState{.string = 1, .stop = frettedStop(5), .digit = std::nullopt});
        // WHERE THE 5 WENT, asserted beside the silence so the two are read together: the note's
        // own satellite, standing, which is the whole ground for the suppression above.
        REQUIRE(!state.notes.empty());
        CHECK(state.notes.front().held == std::optional{5});
        const std::optional<StopMarkViewState>& mark = state.notes.front().stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            CHECK(mark->face == StopMarkFace::Standing);
            CHECK(stopMarkShown(*mark, false));
        }
    }

    SECTION("a tap AT the bracket displaces its stop into the satellite column")
    {
        // The one tap that displaces anything: its head occupies the string's centre exactly where
        // the mark draws, so the stop the other hand holds takes the column beside the bars.
        const ChartViewState state =
            project({strike(1, 1, 5, Fraction{4}), tap(1, 3, 12, 9, Fraction{1, 4})});

        REQUIRE(state.shapes.size() == 1);
        REQUIRE(state.shapes[0].strings.size() == 2);
        CHECK(
            state.shapes[0].strings[0] ==
            ShapeStringViewState{.string = 1, .stop = frettedStop(5), .digit = std::nullopt});
        CHECK(
            state.shapes[0].strings[1] ==
            ShapeStringViewState{
                .string = 3, .stop = frettedStop(9), .digit = StopMarkSlot::Satellite
            });

        // THE BRACKET OWES THE STATEMENT here, and the face says so: the displaced digit above IS
        // this tap's, drawn by the span's own ink at the bracket's instant, so the tap draws
        // nothing of its own beside it and the stop stands whatever its authorship.
        const NoteViewState* const tapped = tap_view(state);
        REQUIRE(tapped != nullptr);
        if (tapped != nullptr)
        {
            const std::optional<StopMarkViewState>& mark = tapped->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Posture);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.0, 1e-9));
            }
        }
    }

    SECTION("a DERIVED held stop is one of the frame's members like any other")
    {
        // Nothing authors the stop here: the tap is pulled off onto fret 9, and you cannot pull
        // off onto a fret unless a finger was already waiting on it, so the notation itself states
        // what the hand held (\ref chartClaimedStops). The projection reads that one resolution,
        // so the frame prints 9 exactly as it does for an authored claim.
        ChartNote pull;
        pull.position = GridPosition{.measure = 1, .beat = 3};
        pull.string = 3;
        pull.fret = 9;
        pull.sustain = Fraction{1};
        pull.attack = NoteAttack::Legato;
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{4}),
             strike(2, 2, 7, Fraction{3}),
             tap(2, 3, 12, std::nullopt, Fraction{1}),
             pull});

        const NoteViewState* const tapped = tap_view(state);
        REQUIRE(tapped != nullptr);
        if (tapped != nullptr)
        {
            CHECK(tapped->held == std::optional{9});
            // AND ITS OWN FACE WAITS FOR THE REVEAL. The pull-off already prints that 9, so a
            // standing satellite would state it twice; revealing the note is what shows the whole
            // truth about it at once. This is the discrimination against a law that would stand
            // every satellite: the same figure with the stop AUTHORED stands (the section above).
            const std::optional<StopMarkViewState>& mark = tapped->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Revealed);
                CHECK_FALSE(stopMarkShown(*mark, false));
                CHECK(stopMarkShown(*mark, true));
            }
        }
        REQUIRE_FALSE(state.shapes.empty());
        const auto stated =
            std::ranges::find(state.shapes.front().strings, 3, &ShapeStringViewState::string);
        REQUIRE(stated != state.shapes.front().strings.end());
        if (stated != state.shapes.front().strings.end())
        {
            CHECK(stated->stop == frettedStop(9));
            CHECK(stated->digit == std::optional{StopMarkSlot::Bracket});
        }
    }

    SECTION("a LONE claim follows the same two rules, span or no span")
    {
        // Nothing else sounds with it, so the tap founds no span and its claim reaches none: the
        // face is its own either way, which is exactly the point — a held stop's satellite does not
        // depend on a bracket existing. AUTHORED here.
        const ChartViewState authored = project({tap(2, 3, 12, 9, Fraction{1})});
        const NoteViewState* const lone = tap_view(authored);
        REQUIRE(lone != nullptr);
        if (lone != nullptr)
        {
            const std::optional<StopMarkViewState>& mark = lone->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Standing);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
            }
        }

        // The same figure with the stop DERIVED instead: the tap is pulled off onto fret 9, so the
        // notation states it and the face waits for the reveal.
        ChartNote pull;
        pull.position = GridPosition{.measure = 1, .beat = 3};
        pull.string = 3;
        pull.fret = 9;
        pull.sustain = Fraction{1};
        pull.attack = NoteAttack::Legato;
        const ChartViewState derived = project({tap(2, 3, 12, std::nullopt, Fraction{1}), pull});
        const NoteViewState* const pulled = tap_view(derived);
        REQUIRE(pulled != nullptr);
        if (pulled != nullptr)
        {
            CHECK(pulled->held == std::optional{9});
            const std::optional<StopMarkViewState>& mark = pulled->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Revealed);
            }
        }
    }

    SECTION("a BARE tap in a span wears THE DEFAULT, on the reveal's terms")
    {
        // THE DEFAULT FACT: a tap that states no held stop of its own still carries a `held` and a
        // mark — whatever the fretting hand has under it answers the question.
        //
        // The grip states fret 7 on string 3 and rings up to the tap; the longer note beside it
        // gives the span its extent; and the tap at beat 3 states nothing of its own, so what is
        // under it is that grip, held on under tenure after its own ring has ended.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{4}),
             strike(1, 3, 7, Fraction{2}),
             tap(3, 3, 12, std::nullopt, Fraction{1})});

        const NoteViewState* const tapped = tap_view(state);
        REQUIRE(tapped != nullptr);
        if (tapped != nullptr)
        {
            CHECK(tapped->held == std::optional{7});
            const std::optional<StopMarkViewState>& mark = tapped->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                // Not the charter's ink, so it shows on the reveal's terms exactly as a derived
                // stop does — and it wears the note's OWN satellite at the note's own instant
                // (1.0s), never the bracket's face, even though the posture prints the same 7.
                CHECK(mark->face == StopMarkFace::Revealed);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(1.0, 1e-9));
                CHECK_FALSE(stopMarkShown(*mark, false));
                CHECK(stopMarkShown(*mark, true));
            }
        }
    }

    SECTION("a span-less bare tap defaults to the open string, and an AUTHORED zero still stands")
    {
        // Nothing covers this tap, so nothing is held under it: zero, the open string. The face is
        // still its own, because the question arose and was answered.
        const ChartViewState bare = project({tap(2, 3, 12, std::nullopt, Fraction{1})});
        const NoteViewState* const untold = tap_view(bare);
        REQUIRE(untold != nullptr);
        if (untold != nullptr)
        {
            CHECK(untold->held == std::optional{0});
            const std::optional<StopMarkViewState>& mark = untold->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Revealed);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
            }
        }

        // THE DISCRIMINATION the default makes necessary: the same VALUE, authored. Zero is also
        // what a default answers, so a value comparison cannot tell a charter who typed the open
        // string from a tap holding nothing — only the face can, and it does.
        const ChartViewState authored = project({tap(2, 3, 12, 0, Fraction{1})});
        const NoteViewState* const stated = tap_view(authored);
        REQUIRE(stated != nullptr);
        if (stated != nullptr)
        {
            CHECK(stated->held == std::optional{0});
            const std::optional<StopMarkViewState>& mark = stated->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Standing);
                CHECK(stopMarkShown(*mark, false));
            }
        }
    }

    // THE PLANT'S FACE. A fretting-hand head at the bracket states the left hand's presence on its
    // string with its own number, so the stop a pull-off lands on beneath it is the refinement the
    // notation already prints in the pull-off: the note wears it as its own reveal-only satellite,
    // exactly as a tap wears a derived stop, and the bracket prints nothing on that string. Under a
    // RIGHT-hand head the bracket's number is the one statement that the hand is there at all,
    // which is why that face stands (the sections above).
    //
    // A SOURCE CAN ONLY HEAD A SPAN WHERE A GRIP ALREADY STOOD ON ITS STRING, which is what shapes
    // the two figures below. A pull-off states a grip beneath the fret it sounds only where a span
    // is standing and gripping the landing stop when the source speaks; over any other ground the
    // source states what it sounds, and the slide-out begins a statement of its own. And a span
    // still standing would simply carry on, so the span whose bracket the source heads is always
    // the SUCCESSOR of an emitted one, fronted at the coverage frontier: the figures open with a
    // stroke that grips the landing stop, and close it on another string at the source's own
    // instant.
    const auto pull_to =
        [](const int beat, const int string, const int fret, const Fraction sustain) {
            ChartNote note;
            note.position = GridPosition{.measure = 1, .beat = beat};
            note.string = string;
            note.fret = fret;
            note.sustain = sustain;
            note.attack = NoteAttack::Legato;
            return note;
        };
    const auto source_view = [](const ChartViewState& state) -> const NoteViewState* {
        for (const NoteViewState& note : state.notes)
        {
            if (note.string == 3 && note.fret == 7)
            {
                return &note;
            }
        }
        return nullptr;
    };

    SECTION("a pull-off source AT the bracket wears its landing stop as its own reveal satellite")
    {
        // The opening stroke grips 5 on strings 1 and 3 for a beat. At beat two string 1 moves to
        // 9, which closes that span there, and the source is struck in the same slot: the grip was
        // standing on string 3's 5, so the source states that 5 beneath the 7 it sounds and the
        // successor's posture holds it. The successor fronts at the frontier — the source's own
        // instant — so the source HEADS its string exactly where the bracket draws; string 2
        // arrives a beat later, and the sequential arrival is what makes it an arpeggio.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{1}),
             strike(1, 3, 5, Fraction{1}),
             strike(2, 1, 9, Fraction{3}),
             strike(2, 3, 7, Fraction{1}),
             pull_to(3, 3, 5, Fraction{2}),
             strike(3, 2, 7, Fraction{2})});

        REQUIRE(state.shapes.size() == 2);
        const ShapeViewState& span = state.shapes[1];
        const NoteViewState* const source = source_view(state);
        REQUIRE(source != nullptr);
        if (source == nullptr)
        {
            return;
        }
        // The bracket draws at the source's own onset, so this IS the fronting figure.
        REQUIRE(span.bracket_seconds.has_value());
        if (span.bracket_seconds.has_value())
        {
            CHECK_THAT(
                *span.bracket_seconds, Catch::Matchers::WithinAbs(source->start_seconds, 1e-9));
        }
        // The posture holds the LANDING STOP on string 3, and the bracket prints nothing there:
        // the note owns that number. The strings arriving later keep their centred digits.
        const auto planted = std::ranges::find(span.strings, 3, &ShapeStringViewState::string);
        REQUIRE(planted != span.strings.end());
        CHECK(planted->stop == frettedStop(5));
        CHECK_FALSE(planted->digit.has_value());
        const auto later = std::ranges::find(span.strings, 2, &ShapeStringViewState::string);
        REQUIRE(later != span.strings.end());
        CHECK(later->digit == std::optional{StopMarkSlot::Bracket});

        CHECK(source->held == std::optional{5});
        const std::optional<StopMarkViewState>& mark = source->stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            // The note's OWN face at its own instant, on the reveal's terms — never the bracket's
            // Posture face, which is a right-hand head's alone.
            CHECK(mark->face == StopMarkFace::Revealed);
            CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(source->start_seconds, 1e-9));
            CHECK_FALSE(stopMarkShown(*mark, false));
            CHECK(stopMarkShown(*mark, true));
        }
    }

    SECTION("a pull-off source MID-span wears the same face, and the frame still prints its stop")
    {
        // The source arrives after the bracket, over a string the span put on 5 a beat earlier, so
        // it restates that 5 and the figure stays one span. The chord frame at the front prints
        // string 3's member centred, as it prints every member that accumulates in; the note's own
        // satellite is a second fact stated in a second ink, at the note's own instant.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{4}),
             strike(2, 2, 7, Fraction{3}),
             strike(2, 3, 5, Fraction{1}),
             strike(3, 3, 7, Fraction{1}),
             pull_to(4, 3, 5, Fraction{1})});

        REQUIRE(state.shapes.size() == 1);
        const ShapeViewState& span = state.shapes.front();
        const auto planted = std::ranges::find(span.strings, 3, &ShapeStringViewState::string);
        REQUIRE(planted != span.strings.end());
        CHECK(planted->stop == frettedStop(5));
        CHECK(planted->digit == std::optional{StopMarkSlot::Bracket});

        const NoteViewState* const source = source_view(state);
        REQUIRE(source != nullptr);
        if (source == nullptr)
        {
            return;
        }
        CHECK(source->held == std::optional{5});
        const std::optional<StopMarkViewState>& mark = source->stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            CHECK(mark->face == StopMarkFace::Revealed);
            CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(source->start_seconds, 1e-9));
        }
    }

    SECTION("a DERIVED tap stop AT the bracket still stands: the bracket owes the number")
    {
        // The tap half of the ruling on the derived tier, which every derived fixture above asks
        // mid-span or alone: at the bracket the tap's head is the OTHER hand, so nothing but the
        // bracket's satellite says the left hand holds a fret there, and it stands whatever its
        // authorship. The stop is derived by the pull-off onto 9 a beat after the tap — and the
        // derivation states it only because a span is standing on string 3's 9 when the tap
        // speaks, gripped by the opening stroke. String 1 moves to 7 in the tap's own slot, which
        // closes that span there and puts the successor's front on the tap.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{1}),
             strike(1, 3, 9, Fraction{1}),
             strike(2, 1, 7, Fraction{3}),
             tap(2, 3, 12, std::nullopt, Fraction{1}),
             pull_to(3, 3, 9, Fraction{1})});

        REQUIRE(state.shapes.size() == 2);
        const auto fronted =
            std::ranges::find(state.shapes[1].strings, 3, &ShapeStringViewState::string);
        REQUIRE(fronted != state.shapes[1].strings.end());
        CHECK(fronted->stop == frettedStop(9));
        CHECK(fronted->digit == std::optional{StopMarkSlot::Satellite});
        const NoteViewState* const tapped = tap_view(state);
        REQUIRE(tapped != nullptr);
        if (tapped != nullptr)
        {
            CHECK(tapped->held == std::optional{9});
            const std::optional<StopMarkViewState>& mark = tapped->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Posture);
                CHECK(stopMarkShown(*mark, false));
            }
        }
    }

    SECTION("a plant on the OPEN string is a held 0 with the same face")
    {
        // Every fret derives alike, the open string included: a pull onto 0 plants the open string
        // beneath the source, so its satellite reads "0" on the reveal and the bracket prints
        // nothing on that string, exactly as for a pressed plant.
        const ChartViewState state = project(
            {strike(1, 3, 7, Fraction{1}),
             pull_to(2, 3, 0, Fraction{3}),
             strike(3, 1, 5, Fraction{2}),
             strike(4, 2, 7, Fraction{1})});

        REQUIRE(state.shapes.size() == 1);
        const auto planted =
            std::ranges::find(state.shapes.front().strings, 3, &ShapeStringViewState::string);
        REQUIRE(planted != state.shapes.front().strings.end());
        CHECK(planted->stop == frettedStop(0));
        CHECK_FALSE(planted->digit.has_value());
        const NoteViewState* const source = source_view(state);
        REQUIRE(source != nullptr);
        if (source != nullptr)
        {
            CHECK(source->held == std::optional{0});
            const std::optional<StopMarkViewState>& mark = source->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Revealed);
            }
        }
    }

    SECTION("a tapped harmonic's PRESSED stop stands, because its head prints the node instead")
    {
        // The tapping finger only touches the node here, so the stop the fretting hand presses is
        // the note's own fret and the claim is read straight off it. The head prints the node, so
        // that 5 has no other ink at all — which is why it stands rather than waiting for a reveal,
        // exactly as a typed held stop does under a plain tap.
        ChartNote tapped = tap(2, 3, 5, std::nullopt, Fraction{1});
        tapped.harmonic_node = 17.0;
        const ChartViewState state = project({tapped});

        const NoteViewState* const touched = tap_view(state);
        REQUIRE(touched != nullptr);
        if (touched != nullptr)
        {
            CHECK(touched->held == std::optional{5});
            const std::optional<StopMarkViewState>& mark = touched->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Standing);
                CHECK(stopMarkShown(*mark, false));
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
            }
        }
    }

    SECTION("an open-string tapped harmonic states no stop beside its head")
    {
        // The other form of the same record, and it is a NATURAL harmonic whose node the picking
        // hand touches: the fretting hand presses nothing, so there is no second stop to state and
        // the head's node is the whole of what the note says. A "0" beside it would claim a finger
        // on the nut that no hand is holding — and a natural harmonic prints none either.
        ChartNote touched_open = tap(2, 3, 0, std::nullopt, Fraction{1});
        touched_open.harmonic_node = 12.0;
        const ChartViewState state = project({touched_open});

        const NoteViewState* const touched = tap_view(state);
        REQUIRE(touched != nullptr);
        if (touched != nullptr)
        {
            CHECK_FALSE(touched->held.has_value());
            CHECK_FALSE(touched->stop_mark.has_value());
        }
    }

    SECTION("an artificial harmonic's PRESSED stop stands on the very same terms")
    {
        // The two hands part company here exactly as they do above — the head prints the node the
        // picking hand touches, the fretting hand is on the 5 it presses — and only WHICH hand
        // sounded the string differs. The displaced stop is the fretting hand's either way, so it
        // wears the same standing face at the same instant: a note with one number of its own has
        // no second ink to wait for.
        ChartNote artificial = strike(2, 3, 5, Fraction{1});
        artificial.harmonic_node = 17.0;
        const ChartViewState state = project({artificial});

        REQUIRE(state.notes.size() == 1);
        const NoteViewState& pressed = state.notes.front();
        CHECK(pressed.held == std::optional{5});
        const std::optional<StopMarkViewState>& mark = pressed.stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            CHECK(mark->face == StopMarkFace::Standing);
            CHECK(stopMarkShown(*mark, false));
            CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(pressed.start_seconds, 1e-9));
            CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
        }
    }

    SECTION("a plant beneath an artificial harmonic never reaches the bracket")
    {
        // THE BRACKET STATES THE PRESSED FRET under such a harmonic and never the finger a pull-off
        // derives beneath it: the node the head prints is MEASURED from that stop, so the pressed
        // fret is the grip the figure needs, and it is the very number the head's own satellite
        // prints. The planted finger stays true in the wide table because it is real — it is the
        // hand window's to reach, not the bracket's to print.
        //
        // Here that shows as the SPAN'S FRONT. The harmonic states 5 and the slide-out states 3, so
        // the two statements differ and the slide-out begins its own: the span fronts at the
        // pull-off rather than inheriting the harmonic's beginning, and the head standing right
        // there states the 3 itself, so the bracket prints no digit on the string at all. The
        // harmonic's own satellite is then the one ink in that column, which is what the ruling
        // bought — before it, the bracket printed the planted 3 into the same satellite column at
        // the same instant as the head's 5. The span's entry is that 3 because the harmonic's ring
        // is long over by the time three rings found the span: what the hand is on there is the
        // pulled note.
        ChartNote artificial = strike(1, 3, 5, Fraction{1});
        artificial.harmonic_node = 17.0;
        const ChartViewState state = project(
            {artificial,
             pull_to(2, 3, 3, Fraction{3}),
             strike(3, 1, 5, Fraction{2}),
             strike(4, 2, 7, Fraction{1})});

        REQUIRE(state.shapes.size() == 1);
        const ShapeViewState& span = state.shapes.front();
        const auto planted = std::ranges::find(span.strings, 3, &ShapeStringViewState::string);
        REQUIRE(planted != span.strings.end());
        CHECK(planted->stop == frettedStop(3));
        CHECK_FALSE(planted->digit.has_value());
        // Beat 2 at 120 BPM: the pull-off's own onset, not the harmonic's.
        REQUIRE(span.bracket_seconds.has_value());
        if (span.bracket_seconds.has_value())
        {
            CHECK_THAT(*span.bracket_seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
        }

        const auto touched = std::ranges::find_if(state.notes, [](const NoteViewState& note) {
            return note.string == 3 && note.harmonic_node.has_value();
        });
        REQUIRE(touched != state.notes.end());
        if (touched == state.notes.end())
        {
            return;
        }
        CHECK(touched->held == std::optional{5});
        const std::optional<StopMarkViewState>& mark = touched->stop_mark;
        REQUIRE(mark.has_value());
        if (mark.has_value())
        {
            CHECK(mark->face == StopMarkFace::Standing);
            CHECK(stopMarkShown(*mark, false));
        }
    }

    SECTION("the tapped twin derives the same span, digit for digit")
    {
        // ONE FAMILY, one answer. The two forms differ only in which hand sounds the string — the
        // tapping finger touches the node the picking hand otherwise touches — so the figure above
        // must project identically here: the same one span, the same bracket at the pull-off's own
        // onset, the same empty digit on the string, and the same standing 5 beside the head that
        // prints the node. Before the ruling the two disagreed, the artificial form putting the
        // planted 3 in the bracket's satellite column while the tapped one never did.
        ChartNote tapped = tap(1, 3, 5, std::nullopt, Fraction{1});
        tapped.harmonic_node = 17.0;
        const ChartViewState state = project(
            {tapped,
             pull_to(2, 3, 3, Fraction{3}),
             strike(3, 1, 5, Fraction{2}),
             strike(4, 2, 7, Fraction{1})});

        REQUIRE(state.shapes.size() == 1);
        const ShapeViewState& span = state.shapes.front();
        const auto planted = std::ranges::find(span.strings, 3, &ShapeStringViewState::string);
        REQUIRE(planted != span.strings.end());
        CHECK(planted->stop == frettedStop(3));
        CHECK_FALSE(planted->digit.has_value());
        REQUIRE(span.bracket_seconds.has_value());
        if (span.bracket_seconds.has_value())
        {
            CHECK_THAT(*span.bracket_seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
        }

        const NoteViewState* const touched = tap_view(state);
        REQUIRE(touched != nullptr);
        if (touched != nullptr)
        {
            CHECK(touched->held == std::optional{5});
            const std::optional<StopMarkViewState>& mark = touched->stop_mark;
            REQUIRE(mark.has_value());
            if (mark.has_value())
            {
                CHECK(mark->face == StopMarkFace::Standing);
                CHECK(stopMarkShown(*mark, false));
            }
        }
    }

    SECTION("a co-struck harmonic's own span prints the pressed stop, not the plant")
    {
        // THE CO-STRUCK FIGURE, which is where the two-digit column could still have arisen: the
        // harmonic struck INSIDE a chord that goes on ringing past the slide-out, so the span it
        // fronts does not end with its ring. The slide-out is then a statement made inside that
        // span — and while the hold-under law read the plant bare, it counted as the span's own
        // finger lifting and wrote the 3 into a bracket drawn at the harmonic's own onset, into the
        // very satellite column the head's standing 5 paints over. Now the slide-out states a grip
        // the harmonic never did, so the span CLOSES there and keeps the pressed 5. The slide-out
        // opens nothing of its own: the chord's rings belong to the span it closed (A RING BELONGS
        // ONLY TO THE SPAN IT WAS STRUCK IN), so its 3 never reaches a bracket at all.
        //
        // The digit is absent for the same reason it is absent everywhere a head stands at the
        // bracket: the harmonic's head is right there on the string, so what states the 5 is the
        // head's own satellite and the frame states nothing.
        //
        // The third string joins a beat later so the span SOUNDS IN PARTS and therefore draws a
        // bracket at all: a box-class span states itself with its strums' own boxes and opens no
        // mark, which would leave the digit empty for a reason that has nothing to do with this.
        ChartNote artificial = strike(1, 4, 5, Fraction{2});
        artificial.harmonic_node = 17.0;
        const ChartViewState state = project(
            {artificial,
             strike(1, 3, 5, Fraction{5}),
             strike(2, 5, 5, Fraction{4}),
             pull_to(3, 4, 3, Fraction{2})});

        REQUIRE(state.shapes.size() == 1);
        const ShapeViewState& fronted = state.shapes.front();
        // Beat 1 at 120 BPM: the span's own front, where the harmonic's head stands.
        REQUIRE(fronted.bracket_seconds.has_value());
        if (fronted.bracket_seconds.has_value())
        {
            CHECK_THAT(*fronted.bracket_seconds, Catch::Matchers::WithinAbs(0.0, 1e-9));
        }
        const auto pressed = std::ranges::find(fronted.strings, 4, &ShapeStringViewState::string);
        REQUIRE(pressed != fronted.strings.end());
        if (pressed != fronted.strings.end())
        {
            CHECK(pressed->stop == frettedStop(5));
            CHECK(pressed->digit == std::nullopt);
        }
        const auto touched = std::ranges::find_if(state.notes, [](const NoteViewState& note) {
            return note.string == 4 && note.harmonic_node.has_value();
        });
        REQUIRE(touched != state.notes.end());
        if (touched != state.notes.end())
        {
            CHECK(touched->held == std::optional{5});
        }
    }
}

// [D2]'s amendment 2, projected. A span an event states keeps its bracket at its own start, because
// there the start IS the statement. A LANDING-OPENED span states nothing at its landing — a chord
// slide keeps the fingers planted, so all that happens there is the fingers arriving — and its
// bracket defers to the span's first interior sounding, where the ink follows the sound.
TEST_CASE("A landing-opened span defers its bracket to its first sounding", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Three fingers hold a grip, glide, and come to rest at 1:3 (1.0s at 120 BPM), ringing on. The
    // lone re-pick at 2:1 (2.0s) is the successor's first interior sounding.
    //
    // Three rather than two because the successor a landing opens is the opening law asked at a
    // boundary: its surviving members must reach the accumulation minimum, so a two-string
    // glide would land in no stated grip and there would be no successor to defer anything.
    const auto slide_into = [](const std::vector<ChartNote>& extra) {
        Arrangement arrangement = makeArrangementWithChart();
        Chart* const chart = chartOrNull(arrangement);
        REQUIRE(chart != nullptr);
        if (chart != nullptr)
        {
            chart->notes = {
                ChartNote{
                    .position = GridPosition{.measure = 1, .beat = 1},
                    .string = 1,
                    .fret = 5,
                    .sustain = Fraction{4},
                    .bend = {},
                    .keyframes = {Keyframe{.offset = Fraction{2}, .fret = 7}},
                },
                ChartNote{
                    .position = GridPosition{.measure = 1, .beat = 1},
                    .string = 2,
                    .fret = 7,
                    .sustain = Fraction{6},
                    .bend = {},
                    .keyframes = {Keyframe{.offset = Fraction{2}, .fret = 9}},
                },
                // String FOUR, leaving string three free for the tap the last section adds: that
                // onset has to land on a string the shape never held.
                ChartNote{
                    .position = GridPosition{.measure = 1, .beat = 1},
                    .string = 4,
                    .fret = 11,
                    .sustain = Fraction{6},
                    .bend = {},
                    .keyframes = {Keyframe{.offset = Fraction{2}, .fret = 13}},
                },
            };
            chart->notes.insert(chart->notes.end(), extra.begin(), extra.end());
            chart->fret_hand_positions = {};
        }
        return arrangement;
    };

    SECTION("the bracket anchors at the first interior sounding")
    {
        const Arrangement arrangement = slide_into({
            ChartNote{
                .position = GridPosition{.measure = 2, .beat = 1},
                .string = 1,
                .fret = 7,
                .sustain = Fraction{2},
                .bend = {},
                .keyframes = {},
            },
        });

        const ChartViewState state = makeChartViewState(arrangement, tempo_map);

        REQUIRE(state.shapes.size() == 2);
        // Each span's mark bound ONCE to a name, so the guard and the read are provably the same
        // object — the shape lint cannot tie two indexings of the same vector together.
        const std::optional<double>& departing = state.shapes[0].bracket_seconds;
        const std::optional<double>& successor = state.shapes[1].bracket_seconds;
        // The departing grip is struck WHOLE, so it is a box and publishes no bracket instant at
        // all: an anchor is arpeggio furniture, and the strum's own box is its whole statement. Its
        // posture is still stated — the class rule and the box identity both read it — which is
        // what makes the empty optional a statement about INK.
        CHECK_FALSE(state.shapes[0].arpeggio);
        CHECK_FALSE(departing.has_value());
        // The successor opens at the landing (1.0s) and draws NOTHING there: its mark waits for
        // the re-pick at 2.0s, which is the discrimination — the span's own start is 1.0. The
        // re-pick reaches only part of the landed grip, which is what makes this successor an
        // arpeggio at all and therefore what makes it draw a bracket.
        CHECK(state.shapes[1].arpeggio);
        CHECK_THAT(state.shapes[1].start_seconds, Catch::Matchers::WithinAbs(1.0, 1e-9));
        REQUIRE(successor.has_value());
        if (successor.has_value())
        {
            CHECK_THAT(*successor, Catch::Matchers::WithinAbs(2.0, 1e-9));
        }
    }

    SECTION("a successor that never sounds interiorly draws no bracket at all")
    {
        const Arrangement arrangement = slide_into({});

        const ChartViewState state = makeChartViewState(arrangement, tempo_map);

        REQUIRE(state.shapes.size() == 2);
        // BOX AT BOTH ENDS: the departing grip is struck whole and nothing strikes the successor at
        // all, so neither is an arpeggio and neither draws an opening mark. What the reader sees is
        // the two boxes and the members' sliding tails between them — [D2] amendment 2's seamless
        // picture, falling out of the class law rather than a carve-out.
        CHECK_FALSE(state.shapes[0].arpeggio);
        CHECK_FALSE(state.shapes[1].arpeggio);
        CHECK_FALSE(state.shapes[0].bracket_seconds.has_value());
        CHECK_FALSE(state.shapes[1].bracket_seconds.has_value());
        // A span with no bracket prints no digit either — the posture entries stay, because the
        // posture is a fact of its own that the class rule and the box identity both read.
        CHECK_FALSE(state.shapes[1].strings.empty());
        for (const ShapeStringViewState& entry : state.shapes[1].strings)
        {
            CHECK_FALSE(entry.digit.has_value());
        }
    }

    SECTION("a right-hand onset inside the successor anchors nothing")
    {
        // The bracket states the FRETTING hand's grip, and a tap says nothing about where those
        // fingers are — so it cannot be the sounding the mark follows.
        Arrangement arrangement = slide_into({});
        Chart* const chart = chartOrNull(arrangement);
        REQUIRE(chart != nullptr);
        if (chart != nullptr)
        {
            // Inside the successor's own extent (1.0s to 2.0s), so the span really is sounded
            // from above rather than merely followed by a tap.
            ChartNote tap{
                .position = GridPosition{.measure = 1, .beat = 4},
                .string = 3,
                .fret = 12,
                .sustain = Fraction{1, 2},
                .bend = {},
                .keyframes = {},
            };
            tap.attack = NoteAttack::Tap;
            chart->notes.push_back(tap);
        }

        const ChartViewState state = makeChartViewState(arrangement, tempo_map);

        REQUIRE(state.shapes.size() == 2);
        // The tap flips the successor's CLASS — a held shape sounded from above is trigger (d) —
        // so this span really would draw a bracket if anything anchored one. Nothing does: the
        // anchor follows the fretting hand's soundings, and there are none inside.
        CHECK(state.shapes[1].arpeggio);
        CHECK_FALSE(state.shapes[1].bracket_seconds.has_value());
    }
}

} // namespace rock_hero::common::core
