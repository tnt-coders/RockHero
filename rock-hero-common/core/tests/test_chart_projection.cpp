#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// TEMPORARY (2026-09-01, goes with the F6 sighting rig): the cases below pin the accumulation
// law's MECHANICS with minimal two-member fixtures, so they run under an explicit two-member
// minimum regardless of the provisional three-member standard. The guard restores the prior
// value, so no case leaks the pin into its neighbours.
struct SightingMinimumGuard
{
    explicit SightingMinimumGuard(const std::size_t minimum)
        : m_previous{spanAccumulationMinimumForSighting()}
    {
        setSpanAccumulationMinimumForSighting(minimum);
    }
    ~SightingMinimumGuard()
    {
        setSpanAccumulationMinimumForSighting(m_previous);
    }
    SightingMinimumGuard(const SightingMinimumGuard&) = delete;
    SightingMinimumGuard& operator=(const SightingMinimumGuard&) = delete;

private:
    std::size_t m_previous;
};

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
        // Shift-slide pair: the glide is an ordinary pitched keyframe at the sustain end, the
        // minimum sustain distance before the re-picked landing on the same string, so the
        // projected segment must not be linked (the target's own head renders there).
        ChartNote{
            .position = GridPosition{.measure = 4, .beat = 1},
            .string = 5,
            .fret = 5,
            .sustain = Fraction{3, 4},
            .bend = {},
            .keyframes = {Keyframe{.offset = Fraction{3, 4}, .fret = 8}},
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

    // 4/4 at the default tempo: measure 2 beat 1 is beat index 4.
    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);
    CHECK(state.notes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(state.notes[1].start_seconds == Catch::Approx(4.0 * beat));
    // Its own eighth-of-a-beat ring is far under the kept-sustain bound, but its chord partner
    // reaches it, and rule 3's verdict is the GROUP's — one stroke sounds every string, so a lone
    // tail beside partners that look unsounded is a picture no strum makes.
    CHECK(state.notes[1].end_seconds == Catch::Approx(4.125 * beat));

    const NoteViewState& sliding = state.notes[3];
    CHECK(sliding.start_seconds == Catch::Approx(8.5 * beat));
    CHECK(sliding.end_seconds == Catch::Approx(10.5 * beat));
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
    // A keyframe at exactly the sustain end reads as a glide-end, not a continuation, so no
    // linked head renders at the tail tip.
    CHECK_FALSE(linkedKeyframe(sliding, sliding.slides[0]));

    // The shift glide ends at the sustain end, the minimum sustain distance before the re-picked
    // fret-8 landing; the segment is not linked (the landing's own head renders there).
    const NoteViewState& shift_slider = state.notes[5];
    REQUIRE(shift_slider.slides.size() == 1);
    CHECK(shift_slider.slides[0].seconds == Catch::Approx(12.75 * beat));
    CHECK(shift_slider.slides[0].fret == 8);
    CHECK_FALSE(linkedKeyframe(shift_slider, shift_slider.slides[0]));
    CHECK(shift_slider.end_seconds == Catch::Approx(12.75 * beat));

    // Both spans are DERIVED from the notes above — nothing in the chart authors one. The 2:1
    // pair strikes together and nothing rings across it, so it is a chord box; the 3:1+1/2 pair
    // strikes under string 2's still-sounding ring, so it is an arpeggio.
    //
    // The first is THE CONTINUITY LAW's box case (user ruling 2026-08-27, [D3]): its members ring
    // a beat and an eighth of a beat, and the first genuine stored gap ends the span at that
    // ring's end. Before the law the extent was the MAXIMUM of the members' rings, which read it
    // as a whole beat of held shape — a statement the chart does not make, since the hand has
    // demonstrably let one string go. The surviving long ring outlives the span, so it draws its
    // own whole tail and is untouched, which the note assertions above still pin.
    //
    // The second is TRAVEL SPLITTING under the LANDING split ([D2] amended 2026-08-29): its
    // string-4 member glides, and the span now COVERS that travel rather than stopping where the
    // hand departed — a chord slide keeps the fingers planted, so the continuity law itself
    // carries the transit. What bounds this span is therefore the OTHER member, whose eighth-beat
    // ring is the first coverage to run out. Nothing re-opens after it: the glide's arrival IS the
    // ring's end, so no member goes on ringing past the landing for a successor to state.
    //
    // Its FRONT is string 2's own onset, not the pair's (THE DATING RULE, user ruling 2026-08-31):
    // the ring the pair strikes under began at 8:0 and no preceding span covers that instant, so
    // the statement runs from there and the pair arrives inside it.
    REQUIRE(state.shapes.size() == 2);
    CHECK(state.shapes[0].start_seconds == Catch::Approx(4.0 * beat));
    CHECK(state.shapes[0].end_seconds == Catch::Approx(4.125 * beat));
    CHECK_FALSE(state.shapes[0].arpeggio);
    CHECK(state.shapes[1].start_seconds == Catch::Approx(8.0 * beat));
    CHECK(state.shapes[1].end_seconds == Catch::Approx(8.625 * beat));
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
    // THE DIGIT WINDOW (user ruling 2026-08-31): the window is the MARK'S OWN INSTANT, and heads
    // later in the span never suppress. String 2 is struck at the arpeggio's front, which is where
    // this bracket draws, so its own head states its 5 and the bracket prints nothing for it —
    // while strings 4 and 5 arrive half a beat LATER and therefore print in the opening bracket,
    // because the bracket is the span's chord frame and states the whole membership where the
    // reader meets it. The box-class span above prints nothing anywhere: it draws no bracket at
    // all, which is the empty slot for a different reason entirely.
    REQUIRE(state.shapes[0].strings.size() == 2);
    CHECK(
        state.shapes[0].strings[0] ==
        ShapeStringViewState{.string = 1, .fret = 1, .digit = std::nullopt});
    CHECK(
        state.shapes[0].strings[1] ==
        ShapeStringViewState{.string = 2, .fret = 3, .digit = std::nullopt});
    REQUIRE(state.shapes[1].strings.size() == 3);
    CHECK(
        state.shapes[1].strings[0] ==
        ShapeStringViewState{.string = 2, .fret = 5, .digit = std::nullopt});
    CHECK(
        state.shapes[1].strings[1] ==
        ShapeStringViewState{.string = 4, .fret = 7, .digit = StopMarkSlot::Bracket});
    CHECK(
        state.shapes[1].strings[2] ==
        ShapeStringViewState{.string = 5, .fret = 8, .digit = StopMarkSlot::Bracket});

    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].seconds == Catch::Approx(4.0 * beat));
}

// The ACTUAL form draws each note at the ring the string really sounds for — the editor reveal's
// whole picture. Where no rule trimmed anything the two forms agree; where presentation dropped a
// tail outright, this form is the only one that has it.
TEST_CASE("Chart projection draws the actual form at each note's ring", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartViewState presented = makeChartViewState(makeArrangementWithChart(), tempo_map);
    const ChartViewState actual =
        makeChartViewState(makeArrangementWithChart(), tempo_map, ChartNoteForm::Actual);

    REQUIRE(actual.notes.size() == 7);
    REQUIRE(presented.notes.size() == actual.notes.size());

    const double beat = tempo_map.secondsAtBeat(1, 2) - tempo_map.secondsAtBeat(1, 1);

    // The fixture's last note is a lone eighth with no technique, so rule 3 presents it no tail at
    // all — bit-exactly its own onset, the way the projection assigns it across — while the actual
    // form draws the eighth it rings for.
    CHECK_THAT(
        presented.notes[6].end_seconds,
        Catch::Matchers::WithinULP(presented.notes[6].start_seconds, 0));
    CHECK(actual.notes[6].end_seconds == Catch::Approx(13.125 * beat));

    // A note no rule trimmed is the same note in both forms.
    CHECK(actual.notes[0].end_seconds == Catch::Approx(5.0 * beat));
    CHECK(presented.notes[0].end_seconds == Catch::Approx(5.0 * beat));
}

// Payload is what a view-side end swap could never restore, and the reason the reveal asks for a
// whole projected form: the presentation trim CLIPS the points its shortened tail no longer
// contains, so the presented note is missing them for good. A trailing bend point and a trailing
// hold keyframe, both past the margin trim and neither changing anything, are exactly that case.
TEST_CASE("Chart projection keeps the payload a presented trim clipped", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Four beats of ring reaching exactly the next onset (so rule 1 trims to the margin rather
        // than presenting it whole), carrying two informative points early and two repeats late.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{4},
            .keyframes =
                {
                    Keyframe{.offset = Fraction{1}, .bend = 2.0},
                    Keyframe{.offset = Fraction{2}, .fret = 7},
                    // One moment, two repeats: the fret is a hold and the bend value is the same
                    // one already standing, so nothing here says anything new.
                    Keyframe{.offset = Fraction{39, 10}, .fret = 7, .bend = 2.0},
                },
        },
        // The binding onset the trim measures against.
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

    const ChartViewState presented = makeChartViewState(arrangement, tempo_map);
    const ChartViewState actual = makeChartViewState(arrangement, tempo_map, ChartNoteForm::Actual);
    REQUIRE(presented.notes.size() == 2);
    REQUIRE(actual.notes.size() == 2);

    // 120 BPM 4/4: a beat is half a second and the margin is a quarter of one, so the presented
    // tail stops at 3.75 beats and the ring runs the full four.
    CHECK(presented.notes[0].end_seconds == Catch::Approx(1.875));
    CHECK(actual.notes[0].end_seconds == Catch::Approx(2.0));

    // The statements past that trim left with the tail, and only the actual form still has them.
    // Both curves carry the onset point in front, which is the channel's opening value.
    CHECK(presented.notes[0].bend.size() == 2);
    REQUIRE(actual.notes[0].bend.size() == 3);
    CHECK(actual.notes[0].bend[2].seconds == Catch::Approx(1.95));
    REQUIRE(presented.notes[0].slides.size() == 1);
    REQUIRE(actual.notes[0].slides.size() == 2);
    CHECK(actual.notes[0].slides[1].seconds == Catch::Approx(1.95));
    // Each surviving keyframe carries the AUTHORED offset it was projected from, which is the
    // identity the editor's selection keys it by — and it survives the trim unchanged, unlike
    // the resolved second.
    CHECK(presented.notes[0].slides[0].offset == Fraction{2});
    CHECK(actual.notes[0].slides[0].offset == Fraction{2});
    CHECK(actual.notes[0].slides[1].offset == Fraction{39, 10});
}

// The form contract: the two states differ in their NOTES and in nothing else. Everything a
// surface draws besides the notes — the holds, the hand-shape spans and their arrival kinds, the
// fret-hand placements and their approach ramps, the string count, the capo — is derived from the
// presented stream whichever form is asked for, so the editor's reveal swaps note tails and moves
// no other mark on the lane.
TEST_CASE("Chart projection forms differ in notes and nothing else", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const Arrangement arrangement = makeArrangementWithChart();

    const ChartViewState presented = makeChartViewState(arrangement, tempo_map);
    const ChartViewState actual = makeChartViewState(arrangement, tempo_map, ChartNoteForm::Actual);

    CHECK(presented.stringCount() == actual.stringCount());
    CHECK(presented.capo == actual.capo);
    CHECK(presented.shapes == actual.shapes);
    CHECK(presented.fret_hand_positions == actual.fret_hand_positions);
    CHECK(presented.display_hold_ends == actual.display_hold_ends);
    // ... and the notes are genuinely a different picture, or the fixture would prove nothing.
    CHECK_FALSE(presented.notes == actual.notes);

    REQUIRE(presented.notes.size() == actual.notes.size());
    for (std::size_t index = 0; index < presented.notes.size(); ++index)
    {
        const NoteViewState& drawn = presented.notes[index];
        const NoteViewState& ring = actual.notes[index];
        // Presentation touches the tail alone, so every other per-note fact — the resolved legato
        // motion included, which is read off the saved stream in both forms — comes through equal.
        CHECK_THAT(ring.start_seconds, Catch::Matchers::WithinULP(drawn.start_seconds, 0));
        CHECK(ring.string == drawn.string);
        CHECK(ring.fret == drawn.fret);
        CHECK(ring.attack == drawn.attack);
        CHECK(ring.legato == drawn.legato);
        CHECK(ring.palm_mute == drawn.palm_mute);
        CHECK(ring.dead == drawn.dead);
        CHECK(ring.harmonic_node == drawn.harmonic_node);
        CHECK(ring.tremolo == drawn.tremolo);
        CHECK(ring.emphasis == drawn.emphasis);
        // No presentation rule ever lengthens a tail past its stored ring.
        CHECK(ring.end_seconds >= drawn.end_seconds);
        // The vibrato regions are tail payload like the bend curve and the slide keyframes, so
        // they belong to the FORM rather than to the invariant group above — a region running to
        // the ring's end runs to the end THIS form presents. What holds in both is that no region
        // leaves the tail it was clipped to.
        const auto regions_inside_tail = [](const NoteViewState& note) {
            for (const VibratoSpanViewState& span : note.vibrato)
            {
                CHECK(span.start_seconds >= note.start_seconds);
                CHECK(span.end_seconds <= note.end_seconds);
            }
        };
        regions_inside_tail(drawn);
        regions_inside_tail(ring);
    }
}

// The vibrato channel reaches both surfaces as the REGIONS it states rather than as a flag: it
// holds from each statement until the next, so a shake can begin at a glide's arrival, stop
// mid-hold, and begin again, and each region has to cover exactly the stretch the channel says it
// does. The onset-only case is the identity that keeps every chart written before the channel
// could say anything else drawing precisely what it drew.
TEST_CASE("Chart projection resolves the vibrato channel into regions", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    // Alone on the chart, so no binding onset trims the tail and the region ends are the channel's
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

    SECTION("a shake stated at the onset alone covers the whole presented tail")
    {
        const ChartViewState state = project(VibratoState::Narrow, {});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        // Exactly the tail's own two ends, which is what makes this the drawing both surfaces
        // produced when the channel was one boolean.
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK_THAT(view.vibrato[0].end_seconds, Catch::Matchers::WithinULP(view.end_seconds, 0));
    }

    SECTION("a shake stated mid-ring begins at the statement, not at the onset")
    {
        const ChartViewState state = project(
            VibratoState::Off, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK(view.vibrato[0].start_seconds == Catch::Approx(1.0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(2.0));
        // The discrimination the whole stage exists for: a region running from the onset would
        // draw a shake across the two beats the chart says are steady.
        CHECK(view.vibrato[0].start_seconds > view.start_seconds);
    }

    SECTION("a shake ended mid-ring stops at the statement, not at the ring's end")
    {
        const ChartViewState state = project(
            VibratoState::Narrow, {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Off}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK(view.vibrato[0].end_seconds == Catch::Approx(1.0));
        CHECK(view.vibrato[0].end_seconds < view.end_seconds);
    }

    SECTION("a channel that starts, stops and starts again states two regions")
    {
        const ChartViewState state = project(
            VibratoState::Narrow,
            {
                Keyframe{.offset = Fraction{1}, .vibrato = VibratoState::Off},
                Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow},
                Keyframe{.offset = Fraction{3}, .vibrato = VibratoState::Off},
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

    SECTION("a statement equal to the state in force is no boundary")
    {
        // Restating what already stands says nothing, so the region stays whole rather than being
        // cut in two at an instant where nothing changes.
        const ChartViewState state = project(
            VibratoState::Narrow,
            {Keyframe{.offset = Fraction{2}, .vibrato = VibratoState::Narrow}});
        REQUIRE(state.notes.size() == 1);
        const NoteViewState& view = state.notes.front();
        REQUIRE(view.vibrato.size() == 1);
        CHECK_THAT(
            view.vibrato[0].start_seconds, Catch::Matchers::WithinULP(view.start_seconds, 0));
        CHECK_THAT(view.vibrato[0].end_seconds, Catch::Matchers::WithinULP(view.end_seconds, 0));
    }

    SECTION("a step between the widths closes one region and opens the other")
    {
        // The case an either/or boundary test would hide: at this instant the shake neither starts
        // nor stops, so a reading that asked "did it turn on or off" would find neither and draw
        // the whole tail at the width the note opened with.
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
        // regions, no gap the surfaces would draw as a pause in the shake.
        CHECK(view.vibrato[1].state == VibratoState::Wide);
        CHECK(view.vibrato[1].start_seconds == Catch::Approx(1.0));
        CHECK_THAT(view.vibrato[1].end_seconds, Catch::Matchers::WithinULP(view.end_seconds, 0));
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
        // Keyframes, but on another channel: a glide and a curl say nothing about shaking.
        const ChartViewState state = project(
            VibratoState::Off,
            {
                Keyframe{.offset = Fraction{1}, .bend = 2.0},
                Keyframe{.offset = Fraction{2}, .fret = 7},
            });
        REQUIRE(state.notes.size() == 1);
        CHECK(state.notes.front().vibrato.empty());
    }
}

// The hand's approach ramps are the derivation most exposed to the swap, because a placement can
// be slide-locked to a gesture end that PRESENTATION MOVED: a slide-out compresses back with the
// trimmed tail, so its grid position — the ramp table's key — is one position in the presented
// stream and another in the saved one. The table is built from the presented stream in either
// form, so both answer with the same margin morph; read it off the drawn stream instead and the
// actual form alone locks this placement to a four-beat unpitched glide, and the hand marker
// visibly jumps the moment the reveal is held.
TEST_CASE("Chart projection ramps a moved slide-out the same in both forms", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        // Four beats of ring ending exactly on the next onset, trailing off unpitched at its very
        // end: the margin trim pulls the tail to 3.75 beats and the terminal compresses with it.
        ChartNote{
            .position = GridPosition{.measure = 1, .beat = 1},
            .string = 1,
            .fret = 5,
            .sustain = Fraction{4},
            .bend = {},
            .keyframes = {},
            .slide_out = 12,
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
    // Exactly where the STORED terminal lands, which is where the presented one no longer is.
    chart.fret_hand_positions = {
        FretHandPosition{.position = GridPosition{.measure = 2, .beat = 1}, .fret = 9, .width = 4},
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    const ChartViewState presented = makeChartViewState(arrangement, tempo_map);
    const ChartViewState actual = makeChartViewState(arrangement, tempo_map, ChartNoteForm::Actual);

    // The trim is real: the terminal moved in the presented form and stayed put in the actual one.
    // The terminal is the note's own field rather than a keyframe (W9-L), so it states a fret and
    // takes its time from the ring's end — which is exactly the value presentation moved.
    REQUIRE(presented.notes.size() == 2);
    REQUIRE(actual.notes.size() == 2);
    // Each note bound once, so the guard below and the access after it are provably the same
    // object — two `notes[0]` subscripts are two calls the optional checker cannot tie together.
    const NoteViewState& presented_glide = presented.notes[0];
    const NoteViewState& actual_glide = actual.notes[0];
    CHECK(presented_glide.slides.empty());
    CHECK(actual_glide.slides.empty());
    REQUIRE(presented_glide.slide_out.has_value());
    REQUIRE(actual_glide.slide_out.has_value());
    CHECK(*presented_glide.slide_out == 12);
    CHECK(*actual_glide.slide_out == 12);
    REQUIRE(glideStopCount(presented_glide) == 1);
    REQUIRE(glideStopCount(actual_glide) == 1);
    CHECK(glideStopAt(presented_glide, 0).seconds == Catch::Approx(1.875));
    CHECK(glideStopAt(actual_glide, 0).seconds == Catch::Approx(2.0));
    CHECK(glideStopAt(presented_glide, 0).unpitched);

    // 120 BPM 4/4: the margin morph is a quarter beat, an eighth of a second — in both forms.
    REQUIRE(presented.fret_hand_positions.size() == 1);
    CHECK(presented.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.125));
    CHECK_FALSE(presented.fret_hand_positions[0].unpitched_ramp);
    CHECK(presented.fret_hand_positions == actual.fret_hand_positions);
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

    // The form does not reach the no-chart exit, so the reveal's state is empty exactly when the
    // lane's is — the editor publishes them together and neither can be the odd one out.
    CHECK(makeChartViewState(arrangement, makeTempoMap(), ChartNoteForm::Actual) == state);
}

// The two lengths a note has on screen, and where each comes from: `end_seconds` is the PRESENTED
// tail (what is drawn and scored) and `display_hold_ends` is the hold (how long the hand stays
// down, which a hand-shape span can outlive the tail by). Both are resolved from the one
// resolutions pass, so this pins the projection's wiring as much as the values.
TEST_CASE("Chart projection draws presented tails and holds the shape's chug", "[core][chart]")
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
    // A three-quarter-beat chug on two strings — which derives a span of its own — then the same
    // chug alone a beat later, then a note whose ring earns a real tail.
    chart.notes = {
        note(1, 1, 5, Fraction{3, 4}),
        note(1, 2, 7, Fraction{3, 4}),
        note(2, 1, 7, Fraction{3, 4}),
        note(3, 1, 9, Fraction{2}),
    };
    Arrangement arrangement = makeArrangementWithChart();
    arrangement.chart = std::move(chart);

    // 120 BPM 4/4: a beat is half a second, so the derived span runs 0.0s to 0.375s (the strum's
    // own ring) and the later string-1 onsets sit at 0.5s and 1.0s.
    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.notes.size() == 4);
    REQUIRE(state.display_hold_ends.size() == 4);
    // Every sub-quarter chug presents no tail at all, however long it rings.
    CHECK(state.notes[0].end_seconds == Catch::Approx(0.0));
    CHECK(state.notes[1].end_seconds == Catch::Approx(0.0));
    CHECK(state.notes[2].end_seconds == Catch::Approx(0.5));
    // The one ring that reaches the kept-sustain bound draws its tail, trimmed by nothing (no
    // later onset binds it).
    CHECK(state.notes[3].end_seconds == Catch::Approx(2.0));

    // The strum's members are held while the shape is — capped at each one's own ring, which is
    // shorter than both the span's remainder and the restrike a beat later.
    CHECK(state.display_hold_ends[0] == Catch::Approx(0.375));
    CHECK(state.display_hold_ends[1] == Catch::Approx(0.375));
    // A single note is not a strum, so nothing extends it: it holds exactly what it presents.
    CHECK(state.display_hold_ends[2] == Catch::Approx(0.5));
    CHECK(state.display_hold_ends[3] == Catch::Approx(2.0));
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
        .keyframes =
            {
                Keyframe{.offset = Fraction{1, 4}, .bend = 1.0},
                Keyframe{.offset = Fraction{1, 2}, .fret = 3},
            },
        .slide_out = 9,
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
    // The turnaround is a keyframe and the slide-out is the terminal; the two read as one leg
    // list through the shared stop walk, both unpitched because a scrape's whole path is the
    // PICK's travel. The turnaround is LINKED and the terminal is not: the pick stays on the
    // string through a direction change, so the junction carries a continuation head (in the
    // note's plectrum shape), while the terminal is where the pick leaves and only its chip
    // marks the position.
    REQUIRE(view.slides.size() == 1);
    REQUIRE(view.slide_out.has_value());
    CHECK(*view.slide_out == 9);
    REQUIRE(glideStopCount(view) == 2);
    for (std::size_t index = 0; index < glideStopCount(view); ++index)
    {
        CHECK(glideStopAt(view, index).unpitched);
    }
    CHECK(linkedKeyframe(view, view.slides[0]));
    CHECK(glideStopAt(view, 1).seconds == Catch::Approx(view.end_seconds));
}

// Ramp derivation for the fretting hand's approach: a placement landing exactly on a pitched
// keyframe's grid position ramps over that glide segment (slide-locked), ordinary placements morph
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
            .keyframes = {},
            .slide_out = 12,
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
        // Exactly on the fixture's pitched keyframe (3:1+1/2 advanced by its two-beat offset):
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

    // The glide starts at the note onset (8.5 beats) and lands at the keyframe (10.5 beats).
    CHECK(state.fret_hand_positions[2].seconds == Catch::Approx(10.5 * beat));
    CHECK(state.fret_hand_positions[2].ramp_seconds == Catch::Approx(2.0 * beat));

    // A placement on an unpitched trail-off's end rides that trail-off's OWN segment, exactly as a
    // pitched glide does, and carries the unpitched family so the window eases with the same curve
    // the rail is drawn with. The trail-off's segment runs from the note's onset (14 beats) to its
    // end (15 beats) because the note carries no pitched keyframes ahead of it; before this the
    // placement morphed over the metrical margin instead, leaving the window stationary for most of
    // the drawn glide and then sprinting to catch up.
    CHECK(state.fret_hand_positions[3].seconds == Catch::Approx(15.0 * beat));
    CHECK(state.fret_hand_positions[3].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK(state.fret_hand_positions[3].unpitched_ramp);
    // The pitched glide above keeps the pitched family.
    CHECK_FALSE(state.fret_hand_positions[2].unpitched_ramp);
}

// An equal-fret keyframe is a HOLD, not a glide: nothing travels across it, so a placement landing
// on one must take the short margin morph rather than a ramp spanning the held stretch. Holds are
// how a slide notated on a tied continuation records where it leaves from, so tying their span to
// the window made the hand drift across the whole tied group to arrive at a fret it never left —
// sighted at fret 11 of measure 50 of the acceptance song.
TEST_CASE("Chart projection gives a hold keyframe the margin morph", "[core][chart]")
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
    CHECK(state.fret_hand_positions[0].ramp_seconds == Catch::Approx(0.25 * beat));
    CHECK_FALSE(state.fret_hand_positions[0].unpitched_ramp);
    // The real glide that follows still rides its own one-beat segment.
    CHECK(state.fret_hand_positions[1].ramp_seconds == Catch::Approx(1.0 * beat));
    CHECK_FALSE(state.fret_hand_positions[1].unpitched_ramp);
}

// A silently-held stop reaches the projection with ONE fact a sounding note does not carry: where
// the bracket that states its stop draws. That is the START of the span the derivation resolved it
// into, not the slot it was authored at, because the bracket IS its face — and one that resolved
// into no span carries no instant at all, which is what makes it undrawable and unclickable by
// construction. Its fret is not repeated there: the posture already carries it, and a second copy
// would be one stop drawn from two places.
TEST_CASE("Chart projection places silent holds at their posture brackets", "[core][chart]")
{
    Arrangement arrangement = makeArrangementWithChart();
    Chart* const chart = chartOrNull(arrangement);
    REQUIRE(chart != nullptr);
    // The fixture's span opens at measure 2 beat 1 (2.0s) and its statement stops an eighth of a
    // beat later (2.0625s), where its shorter member's stored ring gaps — THE CONTINUITY LAW's box
    // case, which is why the hold "inside" below sits a sixteenth in rather than a quarter.
    const auto hold = [](const GridPosition& position, const int string, const int fret) {
        ChartNote note;
        note.position = position;
        note.string = string;
        note.fret = fret;
        note.attack = NoteAttack::None;
        return note;
    };
    chart->notes.push_back(hold(GridPosition{.measure = 2, .beat = 1}, 3, 9));
    chart->notes.push_back(
        hold(GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 16}}, 5, 5));
    chart->notes.push_back(hold(GridPosition{.measure = 2, .beat = 3}, 6, 7));
    std::ranges::sort(chart->notes, chartNoteOrderLess);

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    // The face of the hold on one string, found by that string alone: each of the three sits on
    // its own, so nothing here needs to re-state the slot the chart was built with.
    const auto faceOf = [&state](const int string) {
        std::optional<double> face;
        bool found = false;
        for (const NoteViewState& note : state.notes)
        {
            if (!found && note.string == string && silentHold(note.attack))
            {
                const std::optional<StopMarkViewState>& mark = note.stop_mark;
                face = mark.has_value() ? std::optional<double>{mark->seconds}
                                        : std::optional<double>{};
                found = true;
            }
        }
        REQUIRE(found);
        return face;
    };
    // Each instant is bound to a named value before it is read, so the guard and the access are
    // provably one object (a REQUIRE alone is not visible to that analysis).
    const std::optional<double> at_start = faceOf(3);
    const std::optional<double> inside = faceOf(5);
    const std::optional<double> past_end = faceOf(6);
    // Authored AT the span start, so both readings agree here.
    REQUIRE(at_start.has_value());
    if (at_start.has_value())
    {
        CHECK_THAT(*at_start, Catch::Matchers::WithinAbs(2.0, 1e-9));
    }
    // Authored a sixteenth of a beat INSIDE the span (2.03125s), on a string the shape does not
    // state: that is GROWTH, so it opens the grown shape at its own instant and its bracket draws
    // there. The discrimination is the line above rather than this one — the hold at the span start
    // keeps its face at 2.0 even though the grown span reaches it too, which is the derivation
    // publishing the FIRST span a stop reaches rather than the last.
    REQUIRE(inside.has_value());
    if (inside.has_value())
    {
        CHECK_THAT(*inside, Catch::Matchers::WithinAbs(2.03125, 1e-9));
    }
    // Authored at 3.0s, past the span's own end: it joins no posture and so states no place.
    CHECK_FALSE(past_end.has_value());

    // Every sounding note that claims no stop leaves the field absent: its face is its own head at
    // its own instant, and this figure holds nothing under anything.
    for (const NoteViewState& note : state.notes)
    {
        if (!silentHold(note.attack))
        {
            CHECK_FALSE(note.held.has_value());
            CHECK_FALSE(note.stop_mark.has_value());
        }
    }
}

// THE DIGIT WINDOW and the mark that rides it (user ruling 2026-08-31). A bracket is the span's
// CHORD FRAME: it states every member's fret AT THE INSTANT IT DRAWS, and only a head standing
// right there takes a number out of it. A held stop is one of those members, so it prints in the
// frame like any other — displaced into the satellite column only where its own tap head occupies
// the string's centre right there. The note's own mark rides that same entry, and only the
// SATELLITE column gives it one: a digit standing in the bracket's own column is the span's
// furniture, while the note's OWN face is its satellite, published for every held stop and shown on
// the terms its authorship earns (THE SATELLITE REVEAL, same day). Two facts in two inks for a
// mid-span tap, and a drawn digit is clickable and an undrawn one unreachable by construction.
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
    // A silently-held stop: the fretting hand's grip on a string, with no onset and no ring of its
    // own. What the DEFAULT cases below need is a grip that a tap on the same string cannot clamp,
    // and a hold rings for nothing.
    const auto hold = [](const int beat, const int string, const int fret) {
        ChartNote note;
        note.position = GridPosition{.measure = 1, .beat = beat};
        note.string = string;
        note.fret = fret;
        note.attack = NoteAttack::None;
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
        // both print in the frame. Under the span-wide window that stood before the ruling, string
        // 2's own later head suppressed its digit and the frame fell SILENT about a member the
        // span states.
        REQUIRE(state.shapes[0].strings.size() == 3);
        CHECK(
            state.shapes[0].strings[0] ==
            ShapeStringViewState{.string = 1, .fret = 5, .digit = std::nullopt});
        CHECK(
            state.shapes[0].strings[1] ==
            ShapeStringViewState{.string = 2, .fret = 7, .digit = StopMarkSlot::Bracket});
        CHECK(
            state.shapes[0].strings[2] ==
            ShapeStringViewState{.string = 3, .fret = 9, .digit = StopMarkSlot::Bracket});

        // AND THE TAP WEARS ITS OWN FACE BESIDE THAT. Two facts, two inks (user ruling
        // 2026-08-31): the digit above is the span's furniture stating MEMBERSHIP, and this is the
        // note-scoped satellite — what a press addresses and a typed digit retypes. AUTHORED here,
        // so it STANDS, and it stands at the tap's own slot (0.5 s) rather than at the bracket the
        // membership digit printed in (0.0 s). Under the superseded gating — publish only where the
        // span's digit went to the satellite column — this tap had no mark at all.
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
                CHECK(mark->slot == StopMarkSlot::Satellite);
                CHECK_THAT(mark->seconds, Catch::Matchers::WithinAbs(0.5, 1e-9));
            }
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
            ShapeStringViewState{.string = 1, .fret = 5, .digit = std::nullopt});
        CHECK(
            state.shapes[0].strings[1] ==
            ShapeStringViewState{.string = 3, .fret = 9, .digit = StopMarkSlot::Satellite});

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
                CHECK(mark->slot == StopMarkSlot::Satellite);
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
            // truth about it at once. This is the discrimination against a law that stood every
            // satellite: the same figure with the stop AUTHORED stands (the section above).
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
            CHECK(stated->fret == 9);
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
        // FAILS UNDER PRE-CHANGE CODE, deliberately: a tap that stated no held stop used to carry
        // no `held` and no mark at all, so this section's subject did not exist. THE DEFAULT FACT
        // (user ruling 2026-09-02) is what gives it one.
        //
        // The grip is a silent hold on string 3 at fret 7; the sounding note beside it gives the
        // span its extent; and the tap at beat 3 states nothing of its own, so what is under it is
        // that grip.
        const ChartViewState state = project(
            {strike(1, 1, 5, Fraction{4}),
             hold(1, 3, 7),
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
                CHECK(mark->slot == StopMarkSlot::Satellite);
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

        // THE DISCRIMINATION the default makes necessary: the same VALUE, authored. Zero is now
        // also what a default answers, so a value comparison can no longer tell a charter who typed
        // the open string from a tap holding nothing — only the face can, and it does.
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
}

// [D2]'s amendment 2, projected. A span an event states keeps its bracket at its own start, because
// there the start IS the statement. A LANDING-OPENED span states nothing at its landing — a chord
// slide keeps the fingers planted, so all that happens there is the fingers arriving — and its
// bracket defers to the span's first interior sounding, where the ink follows the sound.
TEST_CASE("A landing-opened span defers its bracket to its first sounding", "[core][chart]")
{
    const SightingMinimumGuard sighting_guard{2};
    const TempoMap tempo_map = makeTempoMap();
    // Two fingers hold a grip, glide, and come to rest at 1:3 (1.0s at 120 BPM), ringing on. The
    // lone re-pick at 2:1 (2.0s) is the successor's first interior sounding.
    const auto slideInto = [](const std::vector<ChartNote>& extra) {
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
            };
            chart->notes.insert(chart->notes.end(), extra.begin(), extra.end());
            chart->fret_hand_positions = {};
        }
        return arrangement;
    };

    SECTION("the bracket anchors at the first interior sounding")
    {
        const Arrangement arrangement = slideInto({
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
        // all: an anchor is arpeggio furniture, and the strum's own box is its whole statement
        // (user ruling 2026-08-30). Its posture is still stated — the class rule and the box
        // identity both read it — which is what makes the empty optional a statement about INK.
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
        const Arrangement arrangement = slideInto({});

        const ChartViewState state = makeChartViewState(arrangement, tempo_map);

        REQUIRE(state.shapes.size() == 2);
        // BOX AT BOTH ENDS (user ruling 2026-08-30): the departing grip is struck whole and
        // nothing strikes the successor at all, so neither is an arpeggio and neither draws an
        // opening
        // mark. What the reader sees is the two boxes and the members' sliding tails between them
        // — amendment 2's seamless picture, falling out of the class law rather than a carve-out.
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
        Arrangement arrangement = slideInto({});
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
