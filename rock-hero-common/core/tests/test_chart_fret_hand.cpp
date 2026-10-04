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

// A placement whose index finger sits at `fret` from the start of `measure`.
[[nodiscard]] FretHandPosition placementAt(const int measure, const int fret)
{
    return FretHandPosition{.position = GridPosition{.measure = measure, .beat = 1}, .fret = fret};
}

// The widths the derivation gives one stream, each note holding the held fret the column names
// (\ref chartHeldStops is what production hands in): nothing where the column says nothing.
[[nodiscard]] std::vector<int> widthsOf(
    const std::vector<ChartNote>& notes, const std::vector<FretHandPosition>& placements,
    std::vector<std::optional<int>> held_frets = {})
{
    held_frets.resize(notes.size());
    return deriveFretHandWidths(notes, held_frets, placements, makeTempoMap());
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

// The hand events the 13 rules derive from one stream, under a capo where a case names one.
[[nodiscard]] std::vector<FretHandEvent> eventsOf(
    const std::vector<ChartNote>& notes, const int capo = 0)
{
    const TempoMap tempo_map = makeTempoMap();
    return deriveFretHandEvents(chartConnections(notes, tempo_map), tempo_map, capo);
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

} // namespace

// A placement nothing widens is the narrowest hand, whether nothing sounds under it at all or only
// what the fretting hand does not stop: the open string, and a tap with no held finger under it.
TEST_CASE("Fret-hand width floors at four frets with nothing stated", "[core][chart]")
{
    CHECK(widthsOf({}, {placementAt(1, 5)}) == std::vector<int>{4});
    CHECK(
        widthsOf({noteAt(1, 1, 1, 0), tapAt(1, 2, 2, 17)}, {placementAt(1, 5)}) ==
        std::vector<int>{4});
}

// A higher fretted onset stretches the window to reach it, from the authored index finger.
TEST_CASE("Fret-hand width reaches the highest stated onset fret", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 5), noteAt(1, 3, 2, 9)}, {placementAt(1, 5)}) ==
        std::vector<int>{5});
}

// An interior keyframe fret is a stop the finger slides to, so it widens; the statement at the
// ring's end does not — a slide-out is the hand leaving the board.
TEST_CASE("Fret-hand width reaches a pitched keyframe but not a slide-out", "[core][chart]")
{
    ChartNote glide = noteAt(1, 1, 1, 5, Fraction{1, 2});
    glide.keyframes = {
        Keyframe{.offset = Fraction{1, 4}, .fret = 10},
        Keyframe{.offset = Fraction{1, 2}, .fret = 14},
    };
    CHECK(widthsOf({glide}, {placementAt(1, 5)}) == std::vector<int>{6});
}

// Under a tap the fretting hand's stop is the held finger, and the tap's own landing fret belongs
// to the picking hand.
TEST_CASE("Fret-hand width reaches a tap's held stop, not the tap", "[core][chart]")
{
    CHECK(widthsOf({tapAt(1, 1, 1, 15)}, {placementAt(1, 5)}, {9}) == std::vector<int>{5});
}

// A natural harmonic presses nothing, but the fretting hand touches its node, so it counts at the
// node's fret (fretFor).
TEST_CASE("Fret-hand width reaches a natural harmonic's node fret", "[core][chart]")
{
    ChartNote harmonic = noteAt(1, 1, 1, 0);
    harmonic.harmonic_node = 12.0;
    CHECK(widthsOf({harmonic}, {placementAt(1, 8)}) == std::vector<int>{5});
}

// A stop below the index finger widens nothing: it stays outside the window, which is the honest
// report that the authored fret is wrong.
TEST_CASE("Fret-hand width ignores a stated fret below the index finger", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 3), noteAt(1, 2, 2, 8)}, {placementAt(1, 7)}) ==
        std::vector<int>{4});
}

// A stretch ends where the next placement begins: a fret stated at or after that instant is the
// next placement's, and a keyframe landing there is too, however early its note was struck. A note
// struck before the first placement belongs to none.
TEST_CASE("Fret-hand width reads only its own placement's stretch", "[core][chart]")
{
    // Struck under the first placement, gliding under the second: 3:4, then a hold at 4:1.
    ChartNote long_glide = noteAt(2, 2, 1, 5, Fraction{2});
    long_glide.keyframes = {
        Keyframe{.offset = Fraction{3, 2}, .fret = 11},
        Keyframe{.offset = Fraction{7, 4}, .fret = 11},
    };
    const std::vector<ChartNote> notes{
        noteAt(1, 1, 3, 20),
        long_glide,
        noteAt(3, 1, 2, 9),
        noteAt(4, 1, 2, 10),
    };
    CHECK(widthsOf(notes, {placementAt(2, 5), placementAt(3, 7)}) == std::vector<int>{4, 5});
}

// A finger struck before the placement and still down at its start is a stop the hand must cover,
// so its ring widens the window just as a strike inside the stretch would.
TEST_CASE("Fret-hand width reaches a ring struck before the placement", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 10, Fraction{2}), noteAt(2, 2, 2, 5)}, {placementAt(2, 5)}) ==
        std::vector<int>{6});
}

// A ring that has released by the placement's start holds nothing there — ending exactly on it
// included — and a ring still down below the index finger widens nothing, like any low stop.
TEST_CASE("Fret-hand width ignores released and lower carried rings", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 10, Fraction{1}), noteAt(2, 2, 2, 5)}, {placementAt(2, 5)}) ==
        std::vector<int>{4});
    CHECK(
        widthsOf({noteAt(1, 1, 1, 3, Fraction{2}), noteAt(2, 2, 2, 7)}, {placementAt(2, 7)}) ==
        std::vector<int>{4});
}

// A carried ring counts at the stop it holds when the stretch begins: a note that slid before the
// placement is down at the fret it reached, not the one it was struck at.
TEST_CASE("Fret-hand width carries a slid ring at the fret it reached", "[core][chart]")
{
    ChartNote slid = noteAt(1, 1, 1, 5, Fraction{2});
    slid.keyframes = {Keyframe{.offset = Fraction{1, 2}, .fret = 11}};
    CHECK(widthsOf({slid}, {placementAt(1, 5), placementAt(2, 6)}) == std::vector<int>{7, 6});
}

// A statement standing exactly on a placement's start holds from there on, so it is that
// placement's and not the one before; the statement it replaces ends there and reaches no
// further.
TEST_CASE("Fret-hand width splits a ring at a keyframe on a placement", "[core][chart]")
{
    ChartNote glide = noteAt(1, 1, 1, 5, Fraction{2});
    glide.keyframes = {Keyframe{.offset = Fraction{1}, .fret = 12}};
    CHECK(widthsOf({glide}, {placementAt(1, 5), placementAt(2, 8)}) == std::vector<int>{4, 5});
}

// A right-hand onset's keyframes are the picking hand's travel, so a tap's slide widens nothing;
// only the finger the fretting hand holds beneath it does.
TEST_CASE("Fret-hand width skips a tap's own keyframes", "[core][chart]")
{
    ChartNote tap = tapAt(1, 1, 1, 15);
    tap.sustain = Fraction{1, 2};
    tap.keyframes = {Keyframe{.offset = Fraction{1, 4}, .fret = 18}};
    CHECK(widthsOf({tap}, {placementAt(1, 5)}, {9}) == std::vector<int>{5});
}

// A tap struck before the placement and still ringing carries its held stop into it, never the
// fret the tapping finger landed on.
TEST_CASE("Fret-hand width carries a tap's held stop into the next placement", "[core][chart]")
{
    ChartNote tap = tapAt(1, 1, 1, 17);
    tap.sustain = Fraction{2};
    CHECK(widthsOf({tap}, {placementAt(2, 5)}, {9}) == std::vector<int>{5});
}

// A note struck before the first placement still states its keyframes where they land, so one
// landing after the first placement belongs to it.
TEST_CASE("Fret-hand width takes an early note's later keyframe", "[core][chart]")
{
    ChartNote early = noteAt(1, 1, 1, 3, Fraction{2});
    early.keyframes = {Keyframe{.offset = Fraction{3, 2}, .fret = 11}};
    CHECK(widthsOf({early}, {placementAt(2, 7)}) == std::vector<int>{5});
}

// The last placement's stretch runs to the end of the chart.
TEST_CASE("Fret-hand width of the last placement runs to the chart's end", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 5), noteAt(8, 4, 2, 10)}, {placementAt(1, 5)}) ==
        std::vector<int>{6});
}

// An authored end fret is the window's end, whatever the notes hold: narrower than the derived
// floor (a pinky on the third fret reaching down from it), and not widened by a stop above it,
// which stays outside the window as a stop below the index finger does.
TEST_CASE("Fret-hand width reaches exactly an authored end fret", "[core][chart]")
{
    FretHandPosition narrow = placementAt(1, 1);
    narrow.end_fret = 3;
    CHECK(widthsOf({noteAt(1, 1, 1, 3)}, {narrow}) == std::vector<int>{3});

    FretHandPosition capped = placementAt(1, 5);
    capped.end_fret = 7;
    CHECK(widthsOf({noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 11)}, {capped}) == std::vector<int>{3});

    FretHandPosition wide = placementAt(1, 5);
    wide.end_fret = 10;
    CHECK(widthsOf({}, {wide}) == std::vector<int>{6});
}

// The projection publishes the derived reach, so both surfaces draw the window the notes prove.
TEST_CASE("Chart projection publishes the derived fret-hand width", "[core][chart]")
{
    Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {noteAt(1, 1, 1, 5), noteAt(1, 2, 2, 11)};
    chart.fret_hand_positions = {placementAt(1, 5)};
    Arrangement arrangement;
    arrangement.chart = chart;

    const ChartViewState state = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(state.fret_hand_positions.size() == 1);
    CHECK(state.fret_hand_positions[0].fret == 5);
    CHECK(state.fret_hand_positions[0].width == 7);
    CHECK_FALSE(state.fret_hand_positions[0].end_authored);

    // An authored end is published as authored, which is what the chip prints as a range.
    arrangement.chart->fret_hand_positions.front().end_fret = 8;
    const ChartViewState authored = makeChartViewState(arrangement, makeTempoMap());
    REQUIRE(authored.fret_hand_positions.size() == 1);
    CHECK(authored.fret_hand_positions[0].width == 4);
    CHECK(authored.fret_hand_positions[0].end_authored);
}

// The one fold reports both ends of what each stretch holds: its LOWEST stop is the default an
// inserted placement's fret takes. An open string holds nothing and bounds nothing, and a stretch
// holding no stop reports none.
TEST_CASE("Held fret range spans a stretch's stops without the open string", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    const std::vector<ChartNote> notes{noteAt(1, 1, 1, 0), noteAt(1, 2, 2, 9), noteAt(1, 3, 3, 7)};
    const std::vector<std::optional<HeldFretRange>> ranges = deriveHeldFretRanges(
        notes,
        std::vector<std::optional<int>>(notes.size()),
        {placementAt(1, 5), placementAt(3, 5)},
        tempo_map);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0] == std::optional{HeldFretRange{.lowest = 7, .highest = 9}});
    CHECK_FALSE(ranges[1].has_value());
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
    CHECK(validateFretHandPositions({placementAt(1, 5)}, tuning, tempo_map).has_value());
}

// An authored end below the index finger is a window reaching nowhere, and no fit can say which of
// the two frets was meant, so it is refused; an end at the finger is a one-fret window and stands.
TEST_CASE("Fret-hand validation refuses an end fret below the index finger", "[core][chart]")
{
    const TempoMap tempo_map = makeTempoMap();
    ChartTuning tuning;
    tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};

    FretHandPosition below = placementAt(1, 5);
    below.end_fret = 4;
    CHECK_FALSE(validateFretHandPositions({below}, tuning, tempo_map).has_value());

    FretHandPosition single = placementAt(1, 5);
    single.end_fret = 5;
    CHECK(validateFretHandPositions({single}, tuning, tempo_map).has_value());
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

} // namespace rock_hero::common::core
