#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Sixteen seconds of default 4/4 — eight bars — which is all the timeline these cases need.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A picked note at a measure and beat, ringing a quarter unless a case says otherwise.
[[nodiscard]] ChartNote noteAt(
    const int measure, const int beat, const int string, const int fret,
    const Fraction ring = Fraction{1, 4})
{
    ChartNote note;
    note.position = GridPosition{.measure = measure, .beat = beat};
    note.string = string;
    note.fret = fret;
    note.sustain = ring;
    return note;
}

// A two-hand tap landing at `fret`.
[[nodiscard]] ChartNote tapAt(const int measure, const int beat, const int string, const int fret)
{
    ChartNote note = noteAt(measure, beat, string, fret);
    note.attack = NoteAttack::Tap;
    return note;
}

// A note claiming a hammer-on or pull-off from the note before it on its string.
[[nodiscard]] ChartNote legatoAt(
    const int measure, const int beat, const int string, const int fret)
{
    ChartNote note = noteAt(measure, beat, string, fret);
    note.attack = NoteAttack::Legato;
    return note;
}

// A note whose fret channel states `fret` at `offset` along its ring.
[[nodiscard]] ChartNote withFretAt(ChartNote note, const Fraction offset, const int fret)
{
    note.keyframes.push_back(Keyframe{.offset = offset, .fret = fret});
    return note;
}

// The hand events the 13 rules derive from one stream, under a capo where a case names one and
// around the authored positions where a case states any.
[[nodiscard]] std::vector<FretHandEvent> eventsOf(
    const std::vector<ChartNote>& notes, const int capo = 0,
    const std::vector<FretHandPosition>& authored = {})
{
    const TempoMap tempo_map = makeTempoMap();
    return deriveFretHandEvents(chartConnections(notes, tempo_map), tempo_map, capo, authored);
}

// An authored position at a measure and beat, with an end fret where a case states one.
[[nodiscard]] FretHandPosition positionAt(
    const int measure, const int beat, const int fret, const std::optional<int> end_fret = {})
{
    return FretHandPosition{
        .position = GridPosition{.measure = measure, .beat = beat},
        .fret = fret,
        .end_fret = end_fret,
    };
}

// An expected event: its front at a measure and beat of 4/4, and its window.
[[nodiscard]] FretHandEvent eventAt(
    const int measure, const int beat, const int fret, const int width = g_min_fret_hand_width)
{
    return FretHandEvent{
        .front = Fraction{measure - 1} + Fraction{beat - 1, 4},
        .window = FretWindow{.fret = fret, .width = width},
    };
}

// A picked note sliding out to `exit_fret` at the end of its ring, an eighth unless a case says
// otherwise.
[[nodiscard]] ChartNote slideOutAt(
    const int measure, const int beat, const int string, const int fret, const int exit_fret,
    const Fraction ring = Fraction{1, 8})
{
    return withFretAt(noteAt(measure, beat, string, fret, ring), ring, exit_fret);
}

// The slide-out rides over the hand events one stream derives, around the authored positions where
// a case states any.
[[nodiscard]] std::vector<FretHandEvent> ridesOf(
    const std::vector<ChartNote>& notes, const std::vector<FretHandPosition>& authored = {})
{
    const TempoMap tempo_map = makeTempoMap();
    const ChartConnections connections = chartConnections(notes, tempo_map);
    return deriveFretHandRides(
        connections, tempo_map, deriveFretHandEvents(connections, tempo_map, 0, authored));
}

// An expected ride arrival: its front in whole notes, and its window.
[[nodiscard]] FretHandEvent rideAt(
    const Fraction front, const int fret, const int width = g_min_fret_hand_width)
{
    return FretHandEvent{.front = front, .window = FretWindow{.fret = fret, .width = width}};
}

} // namespace

// The projection publishes one chip per hand event, derived and authored alike, so both surfaces
// draw the windows the notes and the charter state, each chip carrying its front's grid position.
// Here a derived window stands before the authored position, the authored one reaches the top of
// the chord it heads, and a fret past its window at the next bar line ends it.
TEST_CASE("Chart projection publishes every hand event as a chip", "[core][chart]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        noteAt(1, 1, 1, 3),
        noteAt(1, 3, 2, 5),
        noteAt(1, 3, 3, 9),
        noteAt(2, 1, 4, 11),
    };
    chart.fret_hand_positions = {positionAt(1, 3, 5)};
    Arrangement arrangement;
    arrangement.chart = chart;

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.fret_hand_positions.size() == 3);
    const FhpViewState& before = state.fret_hand_positions[0];
    CHECK(before.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(before.fret == 3);
    CHECK(before.width == 4);
    const FhpViewState& stated = state.fret_hand_positions[1];
    CHECK(stated.position == GridPosition{.measure = 1, .beat = 3});
    CHECK(stated.fret == 5);
    CHECK(stated.width == 5);
    CHECK_FALSE(stated.end_authored);
    const FhpViewState& after = state.fret_hand_positions[2];
    CHECK(after.position == GridPosition{.measure = 2, .beat = 1});
    CHECK(after.fret == 11);

    // An authored end is the window's end, and published as authored, which is what the chip
    // prints as a range. The chord's fret 9 above it is struck at the position's own instant, which
    // the position owns, so it does not end the position.
    arrangement.chart->fret_hand_positions.front().end_fret = 7;
    const ChartViewState ended = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(ended.fret_hand_positions.size() == 3);
    CHECK(ended.fret_hand_positions[1].width == 3);
    CHECK(ended.fret_hand_positions[1].end_authored);
}

// A placement is a marker, so it may start only where every marker may: on the grid and strictly
// before the closing barline, which governs a passage of no length.
TEST_CASE("Fret-hand validation refuses a placement on the closing barline", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    const GridPosition terminal = terminalGridPosition(tempo_map);

    CHECK_FALSE(validateFretHandPositions(
                    {FretHandPosition{.position = terminal, .fret = 5}}, tuning, tempo_map)
                    .has_value());
    CHECK(validateFretHandPositions({positionAt(1, 1, 5)}, tuning, tempo_map).has_value());
}

// An authored end below the index finger is a window reaching nowhere, and no fit can say which of
// the two frets was meant, so it is refused; an end at the finger is a one-fret window and stands.
TEST_CASE("Fret-hand validation refuses an end fret below the index finger", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};

    CHECK_FALSE(validateFretHandPositions({positionAt(1, 1, 5, 4)}, tuning, tempo_map).has_value());
    CHECK(validateFretHandPositions({positionAt(1, 1, 5, 5)}, tuning, tempo_map).has_value());
}

// Rule 1: an open string and a tap constrain nothing; the window is the fretted notes' alone.
TEST_CASE("Hand derivation ignores open strings and taps", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), tapAt(1, 2, 2, 15), noteAt(1, 3, 3, 0)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(eventsOf({noteAt(1, 1, 1, 0), tapAt(1, 2, 2, 15)}).empty());
}

// Rule 1's fretted note is where the fretting finger is: a natural harmonic's is on its node.
TEST_CASE("Hand derivation places a natural harmonic at its node's fret", "[core][chart]")
{
    ChartNote harmonic = noteAt(1, 1, 1, 0);
    harmonic.harmonic_node = 12.0;
    CHECK(eventsOf({harmonic}) == std::vector{eventAt(1, 1, 12)});
}

// Rule 2: a pitched keyframe fret statement demands its fret at its own place; a slide-out, where
// pressure is already off, demands nothing.
TEST_CASE("Hand derivation follows a pitched keyframe but not a slide-out", "[core][chart]")
{
    const ChartNote slid = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 12);
    CHECK(eventsOf({slid}) == std::vector{eventAt(1, 1, 5), eventAt(1, 2, 12)});

    const ChartNote sliding_out =
        withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 2}, 12);
    CHECK(eventsOf({sliding_out}) == std::vector{eventAt(1, 1, 5)});
}

// A keyframe's demand lands on a struck instant without making it a chord: only strikes count, so
// the run keeps the single note and grows to cover both.
TEST_CASE("Hand derivation never counts a keyframe demand toward a chord", "[core][chart]")
{
    const ChartNote slid = withFretAt(noteAt(1, 1, 1, 5, Fraction{1, 2}), Fraction{1, 4}, 6);
    CHECK(eventsOf({slid, noteAt(1, 2, 2, 3)}) == std::vector{eventAt(1, 1, 3)});
}

// Rule 3: a run closes at a chord, at a bar line, and where the grip outgrows the hand.
TEST_CASE("Hand derivation closes a run at a chord, a bar line or an overflow", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 2, 2, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(2, 1, 1, 6), noteAt(2, 2, 1, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 9)});
}

// Rule 5: a one-fret run the standing window covers restates nothing; one it does not still moves.
TEST_CASE("Hand derivation keeps a covered one-fret run in place", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 7), noteAt(2, 1, 1, 6)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 7), noteAt(2, 1, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 12)});
}

// Rule 6: nothing moves unforced while a plain ring struck earlier sounds; a forced run still
// moves, and the next run after the ring ends re-derives.
TEST_CASE("Hand derivation holds still while a written ring sounds", "[core][chart]")
{
    const ChartNote ring = noteAt(1, 1, 1, 5, Fraction{3, 4});
    CHECK(
        eventsOf({ring, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) == std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf({ring, noteAt(1, 2, 2, 9), noteAt(1, 2, 3, 10)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 9)});
    CHECK(
        eventsOf({ring, noteAt(1, 4, 2, 6), noteAt(1, 4, 3, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 4, 6)});
}

// Rule 6's plain ring is any pressed stop: a pinch harmonic pins, a natural harmonic (fret 0) does
// not.
TEST_CASE("Hand derivation pins a pinch harmonic's ring but not a natural's", "[core][chart]")
{
    ChartNote pinch = noteAt(1, 1, 1, 5, Fraction{3, 4});
    pinch.attack = NoteAttack::Pinch;
    pinch.harmonic_node = 17.0;
    CHECK(
        eventsOf({pinch, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) == std::vector{eventAt(1, 1, 5)});

    ChartNote natural = noteAt(1, 1, 1, 0, Fraction{3, 4});
    natural.harmonic_node = 5.0;
    CHECK(
        eventsOf({natural, noteAt(1, 2, 2, 6), noteAt(1, 2, 3, 7)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 6)});
}

// Rule 6's pin runs from the last pitched stop: a slide-then-hold pins at its destination, while a
// slide-out departing straight from its arrival leaves nothing pinned.
TEST_CASE("Hand derivation pins a slide-then-hold at its destination", "[core][chart]")
{
    const ChartNote held = withFretAt(noteAt(1, 1, 1, 5, Fraction{1}), Fraction{1, 4}, 7);
    CHECK(
        eventsOf({held, noteAt(1, 3, 2, 6), noteAt(1, 3, 3, 8)}) == std::vector{eventAt(1, 1, 5)});

    const ChartNote slid_out = withFretAt(held, Fraction{1}, 3);
    CHECK(
        eventsOf({slid_out, noteAt(1, 3, 2, 6), noteAt(1, 3, 3, 8)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 3, 6)});
}

// Rule 7: the window sits on the run's lowest fret, wherever in the run it falls.
TEST_CASE("Hand derivation places the window on the run's floor", "[core][chart]")
{
    CHECK(eventsOf({noteAt(1, 1, 1, 7), noteAt(1, 2, 2, 5)}) == std::vector{eventAt(1, 1, 5)});
}

// Rule 8: the window spans the run's hull, never narrower than four frets.
TEST_CASE("Hand derivation widens the window to a wide grip", "[core][chart]")
{
    CHECK(eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 1, 2, 10)}) == std::vector{eventAt(1, 1, 5, 6)});
}

// Rule 9: the window fits on the board — never below the capo, never past the last fret — the
// floor winning.
TEST_CASE("Hand window fits above the capo and below the last fret", "[core][chart]")
{
    CHECK(fittedWindowFret(10, g_min_fret_hand_width, 0) == 10);
    CHECK(fittedWindowFret(3, g_min_fret_hand_width, 5) == 6);
    CHECK(fittedWindowFret(g_max_fret, g_min_fret_hand_width, 0) == 21);
    CHECK(fittedWindowFret(20, 6, 0) == 19);

    CHECK(eventsOf({noteAt(1, 1, 1, g_max_fret)}) == std::vector{eventAt(1, 1, 21)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 20), noteAt(1, 1, 2, g_max_fret)}) ==
        std::vector{eventAt(1, 1, 20, 5)});
    ChartNote bent_open = noteAt(1, 1, 1, 0);
    bent_open.bend = 1.0;
    CHECK(eventsOf({bent_open}, 5) == std::vector{eventAt(1, 1, 6)});
}

// Rule 10: a front reaches back over onsets that demand nothing, to the first after the previous
// run's last instant and the end of the last ring — the first run to the chart's first note.
TEST_CASE("Hand derivation fronts a run at its leading open strings", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 0), noteAt(1, 2, 2, 0), noteAt(1, 3, 1, 5)}) ==
        std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5, Fraction{1, 2}),
             noteAt(1, 2, 2, 0),
             noteAt(1, 3, 2, 0),
             noteAt(1, 4, 1, 12)}) == std::vector{eventAt(1, 1, 5), eventAt(1, 3, 12)});
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 3, 0), noteAt(1, 3, 1, 9), noteAt(1, 3, 2, 10)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 9)});
}

// Rule 10's reach-back stops at the bar line that broke the run: an open-string pickup before it
// stays with the old window, an open string after it fronts the new one.
TEST_CASE("Hand derivation never reaches back across the breaking bar line", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 4, 2, 0), noteAt(2, 1, 2, 0), noteAt(2, 2, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 12)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 4, 2, 0), noteAt(2, 2, 1, 12)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 2, 12)});
}

// Rule 11: a run whose window is the standing one emits nothing.
TEST_CASE("Hand derivation emits only where the window changes", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(2, 1, 1, 5), noteAt(2, 2, 1, 6)}) ==
        std::vector{eventAt(1, 1, 5)});
}

// Rule 12: a pull-off's plant is a demand at its source's onset — a tap's included, which demands
// nothing itself — so the hand arrives there rather than at the pull-off.
TEST_CASE("Hand derivation covers a plant from its tapped source's onset", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 2, 7, Fraction{1, 2}), tapAt(1, 2, 1, 15), legatoAt(1, 3, 1, 3)}) ==
        std::vector{eventAt(1, 1, 7), eventAt(1, 2, 3)});
}

// Rule 12: the plant also widens every instant up to its pull-off, so a chord struck over the
// waiting finger takes a window that still covers it.
TEST_CASE("Hand derivation widens the instants a plant waits under", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 3, 1, 7, Fraction{1, 2}),
             noteAt(1, 4, 2, 8),
             noteAt(1, 4, 3, 9),
             legatoAt(2, 1, 1, 5)}) == std::vector{eventAt(1, 3, 5), eventAt(1, 4, 5, 5)});
}

// Rule 13: an open string bent anywhere along its ring is bent behind the nut, so the hand is at
// the nut; an unbent open string asks nothing.
TEST_CASE("Hand derivation puts a behind-the-nut bend at fret one", "[core][chart]")
{
    ChartNote pre_bent = noteAt(1, 1, 1, 0);
    pre_bent.bend = 1.0;
    CHECK(eventsOf({pre_bent}) == std::vector{eventAt(1, 1, 1)});

    ChartNote bent_later = noteAt(1, 1, 1, 0, Fraction{1, 2});
    bent_later.keyframes.push_back(Keyframe{.offset = Fraction{1, 4}, .bend = 1.0});
    CHECK(eventsOf({bent_later}) == std::vector{eventAt(1, 1, 1)});

    CHECK(eventsOf({noteAt(1, 1, 1, 0)}).empty());
}

// An authored position is a row object of its own, so it is emitted even where the window it
// states is the one already standing — the one event rule 11 never drops.
TEST_CASE("An authored position is an event even where it restates the window", "[core][chart]")
{
    const std::vector notes{noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6)};
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5)});
    CHECK(
        eventsOf(notes, 0, {positionAt(1, 2, 5)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 2, 5)});
}

// A stated fret pins the floor and rule 8 gives the top from there: the top of the run the
// position heads, never under four frets.
TEST_CASE("An authored fret reaches the top of the run it heads", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 7), noteAt(1, 2, 1, 10)}, 0, {positionAt(1, 1, 5)}) ==
        std::vector{eventAt(1, 1, 5, 6)});
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5)}, 0, {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5)});
}

// A stated end fret IS the top, however narrow, and the derivation resumes at the first instant
// that falls outside the window.
TEST_CASE("An authored end fret is the window's top", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 3, 1, 7)},
            0,
            {positionAt(1, 1, 5, 6)}) == std::vector{eventAt(1, 1, 5, 2), eventAt(1, 3, 7)});
}

// The notes alone move the hand to a two-fret figure's floor in the next bar; an authored window
// that covers the figure stands through it, bar line and all.
TEST_CASE("An authored position stands through every figure that fits", "[core][chart]")
{
    const std::vector notes{noteAt(1, 1, 1, 5), noteAt(2, 1, 1, 7), noteAt(2, 2, 1, 8)};
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5), eventAt(2, 1, 7)});
    CHECK(eventsOf(notes, 0, {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5)});
}

TEST_CASE("The derivation resumes where an authored window is contradicted", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 7), noteAt(1, 3, 1, 12), noteAt(1, 4, 1, 13)},
            0,
            {positionAt(1, 1, 5)}) == std::vector{eventAt(1, 1, 5), eventAt(1, 3, 12)});
}

// What sounds at the position's own instant cannot end it: the window stands from there, and a
// stop below its floor stays outside it.
TEST_CASE("An authored position owns its own instant", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 8)}, 0, {positionAt(1, 1, 7)}) ==
        std::vector{eventAt(1, 1, 7)});
}

// The next authored position ends the one before it, and no run crosses it: the first window's
// top is read from the notes before the second position, not from the ones it takes.
TEST_CASE("No run crosses an authored position", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 5), noteAt(1, 2, 1, 6), noteAt(1, 3, 1, 9), noteAt(1, 4, 1, 12)},
            0,
            {positionAt(1, 1, 5), positionAt(1, 3, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(1, 3, 9)});
}

// Rule 10 reaches a derived front back over leading open strings, but never past an authored
// position: one standing over silence keeps every onset up to its own instant.
TEST_CASE("A derived front never reaches back past an authored position", "[core][chart]")
{
    CHECK(
        eventsOf(
            {noteAt(1, 1, 1, 3, Fraction{1, 8}), noteAt(1, 2, 2, 0), noteAt(1, 4, 1, 12)},
            0,
            {positionAt(1, 3, 5, 8)}) ==
        std::vector{eventAt(1, 1, 3), eventAt(1, 3, 5), eventAt(1, 4, 12)});
}

TEST_CASE("An authored position after the last note is still an event", "[core][chart]")
{
    CHECK(
        eventsOf({noteAt(1, 1, 1, 5)}, 0, {positionAt(2, 1, 9)}) ==
        std::vector{eventAt(1, 1, 5), eventAt(2, 1, 9)});
}

// The hand travels with a slide-out, so the window arrives covering the exit fret where the slide
// ends and is back at the standing window by the next instant the hand covers. That is motion
// beside the hand events, which stay exactly what the notes derive.
TEST_CASE("The window rides a slide-out to its exit and back", "[core][chart]")
{
    const std::vector notes{slideOutAt(1, 1, 1, 5, 1), noteAt(1, 3, 1, 5)};
    CHECK(ridesOf(notes) == std::vector{rideAt(Fraction{1, 8}, 1), rideAt(Fraction{1, 2}, 5)});
    CHECK(eventsOf(notes) == std::vector{eventAt(1, 1, 5)});
}

// A window already covering the exit fret does not move, and neither does the fret hand for a
// tap's slide-out, which is the picking hand's travel.
TEST_CASE("The window does not ride an exit it covers or a tap's slide-out", "[core][chart]")
{
    CHECK(ridesOf({slideOutAt(1, 1, 1, 8, 6), noteAt(1, 2, 1, 5)}).empty());
    CHECK(ridesOf({withFretAt(tapAt(1, 1, 1, 5), Fraction{1, 4}, 1), noteAt(1, 1, 2, 5)}).empty());
}

// A slide-out with no room before the next demand stays planted: one ringing past it, and one
// reaching exactly to it, since a ring is half-open and is not there at its end.
TEST_CASE("A slide-out without room before the next demand stays planted", "[core][chart]")
{
    CHECK(ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{1, 2}), noteAt(1, 2, 2, 5)}).empty());
    CHECK(ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{1, 4}), noteAt(1, 2, 2, 5)}).empty());
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 1, Fraction{3, 16}), noteAt(1, 2, 2, 5)}) ==
        std::vector{rideAt(Fraction{3, 16}, 1), rideAt(Fraction{1, 4}, 5)});
}

// The way back is a hand event wherever one fronts first, and a hand event fronting while the slide
// travels is the hand moving on, so that slide-out is not ridden at all.
TEST_CASE("A hand event is the window's way back from a ride, or its departure", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 1), noteAt(2, 1, 1, 12)}) ==
        std::vector{rideAt(Fraction{1, 8}, 1)});
    CHECK(
        ridesOf(
            {slideOutAt(1, 1, 1, 5, 12, Fraction{1, 2}), noteAt(2, 1, 1, 5)}, {positionAt(1, 2, 9)})
            .empty());
}

// Strings sliding out together ride one window, shifted the least distance that covers every exit.
TEST_CASE("Strings sliding out together ride one window", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 5, 2), slideOutAt(1, 1, 2, 7, 4), noteAt(1, 3, 1, 5)}) ==
        std::vector{rideAt(Fraction{1, 8}, 2), rideAt(Fraction{1, 2}, 5)});
}

// An authored window is ridden like a derived one, keeping its width.
TEST_CASE("An authored position standing over a slide-out gets the same ride", "[core][chart]")
{
    CHECK(
        ridesOf({slideOutAt(1, 1, 1, 7, 12), noteAt(1, 3, 1, 6)}, {positionAt(1, 1, 5, 9)}) ==
        std::vector{rideAt(Fraction{1, 8}, 8, 5), rideAt(Fraction{1, 2}, 5, 5)});
}

} // namespace rock_hero::common::core
