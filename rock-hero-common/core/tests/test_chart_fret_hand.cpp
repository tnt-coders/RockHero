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

// Sixteen seconds of default 4/4 — eight bars — which is all the beat axis these cases need.
[[nodiscard]] TempoMap makeTempoMap()
{
    return TempoMap::defaultMap(TimeDuration{16.0});
}

// A picked note at a measure and beat, ringing one beat unless a case says otherwise.
[[nodiscard]] ChartNote noteAt(
    const int measure, const int beat, const int string, const int fret,
    const Fraction ring = Fraction{1})
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
    ChartNote glide = noteAt(1, 1, 1, 5, Fraction{2});
    glide.keyframes = {
        Keyframe{.offset = Fraction{1}, .fret = 10},
        Keyframe{.offset = Fraction{2}, .fret = 14},
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
    ChartNote long_glide = noteAt(2, 2, 1, 5, Fraction{8});
    long_glide.keyframes = {
        Keyframe{.offset = Fraction{6}, .fret = 11},
        Keyframe{.offset = Fraction{7}, .fret = 11},
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
        widthsOf({noteAt(1, 1, 1, 10, Fraction{8}), noteAt(2, 2, 2, 5)}, {placementAt(2, 5)}) ==
        std::vector<int>{6});
}

// A ring that has released by the placement's start holds nothing there — ending exactly on it
// included — and a ring still down below the index finger widens nothing, like any low stop.
TEST_CASE("Fret-hand width ignores released and lower carried rings", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 10, Fraction{4}), noteAt(2, 2, 2, 5)}, {placementAt(2, 5)}) ==
        std::vector<int>{4});
    CHECK(
        widthsOf({noteAt(1, 1, 1, 3, Fraction{8}), noteAt(2, 2, 2, 7)}, {placementAt(2, 7)}) ==
        std::vector<int>{4});
}

// A carried ring counts at the stop it holds when the stretch begins: a note that slid before the
// placement is down at the fret it reached, not the one it was struck at.
TEST_CASE("Fret-hand width carries a slid ring at the fret it reached", "[core][chart]")
{
    ChartNote slid = noteAt(1, 1, 1, 5, Fraction{8});
    slid.keyframes = {Keyframe{.offset = Fraction{2}, .fret = 11}};
    CHECK(widthsOf({slid}, {placementAt(1, 5), placementAt(2, 6)}) == std::vector<int>{7, 6});
}

// A statement standing exactly on a placement's start holds from there on, so it is that
// placement's and not the one before; the statement it replaces ends there and reaches no
// further.
TEST_CASE("Fret-hand width splits a ring at a keyframe on a placement", "[core][chart]")
{
    ChartNote glide = noteAt(1, 1, 1, 5, Fraction{8});
    glide.keyframes = {Keyframe{.offset = Fraction{4}, .fret = 12}};
    CHECK(widthsOf({glide}, {placementAt(1, 5), placementAt(2, 8)}) == std::vector<int>{4, 5});
}

// A right-hand onset's keyframes are the picking hand's travel, so a tap's slide widens nothing;
// only the finger the fretting hand holds beneath it does.
TEST_CASE("Fret-hand width skips a tap's own keyframes", "[core][chart]")
{
    ChartNote tap = tapAt(1, 1, 1, 15);
    tap.sustain = Fraction{2};
    tap.keyframes = {Keyframe{.offset = Fraction{1}, .fret = 18}};
    CHECK(widthsOf({tap}, {placementAt(1, 5)}, {9}) == std::vector<int>{5});
}

// A tap struck before the placement and still ringing carries its held stop into it, never the
// fret the tapping finger landed on.
TEST_CASE("Fret-hand width carries a tap's held stop into the next placement", "[core][chart]")
{
    ChartNote tap = tapAt(1, 1, 1, 17);
    tap.sustain = Fraction{8};
    CHECK(widthsOf({tap}, {placementAt(2, 5)}, {9}) == std::vector<int>{5});
}

// A note struck before the first placement still states its keyframes where they land, so one
// landing after the first placement belongs to it.
TEST_CASE("Fret-hand width takes an early note's later keyframe", "[core][chart]")
{
    ChartNote early = noteAt(1, 1, 1, 3, Fraction{8});
    early.keyframes = {Keyframe{.offset = Fraction{6}, .fret = 11}};
    CHECK(widthsOf({early}, {placementAt(2, 7)}) == std::vector<int>{5});
}

// The last placement's stretch runs to the end of the chart.
TEST_CASE("Fret-hand width of the last placement runs to the chart's end", "[core][chart]")
{
    CHECK(
        widthsOf({noteAt(1, 1, 1, 5), noteAt(8, 4, 2, 10)}, {placementAt(1, 5)}) ==
        std::vector<int>{6});
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

} // namespace rock_hero::common::core
