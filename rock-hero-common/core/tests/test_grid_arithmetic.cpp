#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Measures 1-2 are 4/4 (quarter-note beats), measure 3 onward is 7/8 (eighth-note beats): the
// signature change exercises both the beats-per-measure carry and the note-value-to-beats
// conversion. Anchors only pin absolute time, which grid arithmetic never touches.
[[nodiscard]] TempoMap signatureChangeMap()
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
         TimeSignatureChange{.measure = 3, .numerator = 7, .denominator = 8}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 6, .beat = 1, .seconds = 20.0}},
    };
}

// A 4/4 map running at one steady tempo across five measures, so the margin walk has nothing but
// the rate to read. Sixteen beats separate the two anchors.
[[nodiscard]] TempoMap steadyMap(const double quarter_note_bpm)
{
    return TempoMap{
        {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 5, .beat = 1, .seconds = 16.0 * 60.0 / quarter_note_bpm}},
    };
}

// Seconds between two grid positions, the quantity the margin is stated in.
[[nodiscard]] double secondsBetween(
    const TempoMap& tempo_map, const GridPosition& from, const GridPosition& to)
{
    return tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, to)) -
           tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, from));
}

} // namespace

// THE MARGIN IS A DURATION: the same span of seconds at any tempo, which is the whole point of the
// law — the gap a player and a charter read on screen must not shrink because the song is fast.
// The beat counts differ (75 ms is 3/40 of a beat at 60 BPM and a quarter of one at 200), the
// seconds do not.
TEST_CASE("The minimum sustain distance spans one duration at every tempo", "[core][chart]")
{
    const GridPosition onset{.measure = 3, .beat = 2, .offset = {}};

    const TempoMap slow = steadyMap(60.0);
    const TempoMap fast = steadyMap(200.0);
    CHECK(minimumSustainDistanceBeats(slow, onset) == Fraction{3, 40});
    CHECK(minimumSustainDistanceBeats(fast, onset) == Fraction{1, 4});

    // Measured back through each map, both land on the one duration — within a tick, the lattice
    // the answer is floored onto.
    const double slow_tick_seconds = 60.0 / 60.0 * 4.0 / g_tick_quantum_denominator;
    const double fast_tick_seconds = 60.0 / 200.0 * 4.0 / g_tick_quantum_denominator;
    CHECK(
        secondsBetween(slow, marginBefore(slow, onset), onset) ==
        Catch::Approx(g_minimum_sustain_distance_seconds).margin(slow_tick_seconds));
    CHECK(
        secondsBetween(fast, marginBefore(fast, onset), onset) ==
        Catch::Approx(g_minimum_sustain_distance_seconds).margin(fast_tick_seconds));
}

// A tempo anchor standing INSIDE the margin is honoured exactly, because the walk leaves the beat
// axis for the map's time axis and comes back rather than scaling one local rate. Measure 1 runs
// at 60 BPM, then the beat into measure 2 is pinned twenty times faster: the margin before the
// downbeat therefore reaches back over the whole fast beat and on into the preceding one, which a
// single-rate margin could never produce.
TEST_CASE("The minimum sustain distance honours a tempo anchor inside it", "[core][chart]")
{
    const TempoMap map{
        {TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
         BeatAnchor{.measure = 1, .beat = 4, .seconds = 3.0},
         BeatAnchor{.measure = 2, .beat = 1, .seconds = 3.05},
         BeatAnchor{.measure = 5, .beat = 1, .seconds = 7.0}},
    };
    const GridPosition onset{.measure = 2, .beat = 1, .offset = {}};

    // 0.05 s of the 75 ms margin is spent on the fast beat and 0.025 s at one second per beat, so
    // the margin starts 39/40 of a beat into measure 1 beat 3 — on the tick lattice exactly.
    CHECK(
        marginBefore(map, onset) ==
        GridPosition{.measure = 1, .beat = 3, .offset = Fraction{39, 40}});
    CHECK(minimumSustainDistanceBeats(map, onset) == Fraction{41, 40});
    CHECK(
        secondsBetween(map, marginBefore(map, onset), onset) ==
        Catch::Approx(g_minimum_sustain_distance_seconds));
}

// The margin always lands ON the chart's tick lattice and is never SHORTER than the duration: the
// walk floors, so a tempo whose margin falls between two ticks gives the extra sliver away rather
// than taking it. 137 BPM is deliberately a rate no tick divides.
TEST_CASE("The minimum sustain distance lands on the tick lattice", "[core][chart]")
{
    const TempoMap map = steadyMap(137.0);
    const std::vector<GridPosition> onsets{
        GridPosition{.measure = 2, .beat = 1, .offset = {}},
        GridPosition{.measure = 2, .beat = 3, .offset = Fraction{1, 3}},
        GridPosition{.measure = 4, .beat = 4, .offset = Fraction{7, 16}},
    };

    // A 4/4 beat is a quarter of a whole note, so it holds a quarter of the ticks and an offset on
    // the lattice reduces to a denominator dividing that count.
    constexpr int ticks_per_beat = g_tick_quantum_denominator / 4;
    for (const GridPosition& onset : onsets)
    {
        const GridPosition start = marginBefore(map, onset);
        CHECK(ticks_per_beat % start.offset.denominator == 0);
        CHECK(secondsBetween(map, start, onset) >= g_minimum_sustain_distance_seconds);
    }
}

// An onset standing closer to the chart's start than the margin has nowhere to reach back to, so
// the walk clamps at the grid origin exactly as advanceGridPosition does and the margin is simply
// what room there was.
TEST_CASE("The minimum sustain distance clamps at the chart's start", "[core][chart]")
{
    const TempoMap map = steadyMap(60.0);

    CHECK(marginBefore(map, GridPosition{}) == GridPosition{});
    CHECK(minimumSustainDistanceBeats(map, GridPosition{}) == Fraction{});

    const GridPosition early{.measure = 1, .beat = 1, .offset = Fraction{1, 20}};
    CHECK(marginBefore(map, early) == GridPosition{});
    CHECK(minimumSustainDistanceBeats(map, early) == Fraction{1, 20});
}

// Advancement inside one beat accumulates the offset exactly; crossing the beat carries.
TEST_CASE("Grid advancement carries offsets across beats", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition base{.measure = 1, .beat = 1, .offset = Fraction{1, 4}};

    CHECK(
        advanceGridPosition(map, base, Fraction{1, 2}) ==
        GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
    CHECK(
        advanceGridPosition(map, base, Fraction{3, 4}) ==
        GridPosition{.measure = 1, .beat = 2, .offset = {}});
    CHECK(
        advanceGridPosition(map, base, Fraction{7, 4}) ==
        GridPosition{.measure = 1, .beat = 3, .offset = {}});
}

// Whole-beat carries cross measure and signature boundaries on the tempo map's beat axis: the
// 4/4 measures hold four beats and the 7/8 measures hold seven.
TEST_CASE("Grid advancement carries across measures and signature changes", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    CHECK(
        advanceGridPosition(
            map, GridPosition{.measure = 2, .beat = 4, .offset = Fraction{1, 2}}, Fraction{1}) ==
        GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}});
    CHECK(
        advanceGridPosition(
            map, GridPosition{.measure = 3, .beat = 1, .offset = {}}, Fraction{7}) ==
        GridPosition{.measure = 4, .beat = 1, .offset = {}});
    CHECK(
        advanceGridPosition(
            map, GridPosition{.measure = 1, .beat = 1, .offset = {}}, Fraction{8}) ==
        GridPosition{.measure = 3, .beat = 1, .offset = {}});
}

// Negative deltas move earlier, and results never go before the grid origin.
TEST_CASE("Grid advancement clamps negative results at the origin", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    CHECK(
        advanceGridPosition(
            map, GridPosition{.measure = 1, .beat = 2, .offset = {}}, Fraction{-1, 2}) ==
        GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}});
    CHECK(
        advanceGridPosition(
            map, GridPosition{.measure = 3, .beat = 1, .offset = {}}, Fraction{-1}) ==
        GridPosition{.measure = 2, .beat = 4, .offset = {}});
    CHECK(advanceGridPosition(map, GridPosition{}, Fraction{-3, 4}) == GridPosition{});
}

// Distance is the exact inverse of advancement, including tuplet fractions the corpus uses.
TEST_CASE("Beat distance inverts advancement exactly", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition base{.measure = 2, .beat = 3, .offset = Fraction{2, 5}};
    const std::vector<Fraction> deltas{
        Fraction{1, 5},
        Fraction{3, 7},
        Fraction{22, 9},
        Fraction{1, 12},
        Fraction{-7, 5},
        Fraction{-13, 7},
        Fraction{9},
    };

    for (const Fraction delta : deltas)
    {
        const GridPosition advanced = advanceGridPosition(map, base, delta);
        CHECK(beatDistance(map, base, advanced) == delta);
    }
    CHECK(beatDistance(map, base, base) == Fraction{});
}

// Sustain endpoints are onset advancement; a zero sustain ends at the onset itself.
TEST_CASE("Sustain endpoints resolve through the tempo map", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    ChartNote note{};
    note.position = GridPosition{.measure = 1, .beat = 4, .offset = {}};
    note.sustain = Fraction{3, 2};
    CHECK(
        sustainEndPosition(map, note) ==
        GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 2}});

    note.sustain = Fraction{};
    CHECK(sustainEndPosition(map, note) == note.position);
}

// The 1/8 grid in 4/4 steps every half beat; positions snap to the nearest line and exact ties
// resolve to the earlier line, matching the editor timeline's snap rule.
TEST_CASE("Grid snapping picks the nearest note-value line", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const Fraction eighth_grid{1, 8};

    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 5}}, eighth_grid) ==
        GridPosition{.measure = 1, .beat = 1, .offset = {}});
    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 10}}, eighth_grid) ==
        GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 2}});
    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 4}}, eighth_grid) ==
        GridPosition{.measure = 1, .beat = 1, .offset = {}});
    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{4, 5}}, eighth_grid) ==
        GridPosition{.measure = 1, .beat = 3, .offset = {}});
}

// A position already on a line stays put, at any grid including tuplet note values.
TEST_CASE("Grid snapping keeps on-line positions", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 1, .beat = 3, .offset = {}}, Fraction{1, 4}) ==
        GridPosition{.measure = 1, .beat = 3, .offset = {}});
    CHECK(
        snapGridPosition(
            map,
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{2, 3}},
            Fraction{1, 12}) == GridPosition{.measure = 1, .beat = 1, .offset = Fraction{2, 3}});
}

// In 7/8 a 1/4-note grid steps every two eighth-note beats, so the measure length is not a
// multiple of the step; the next downbeat is still a line and wins when it is nearest.
TEST_CASE("Grid snapping treats every downbeat as a line", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const Fraction quarter_grid{1, 4};

    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 3, .beat = 7, .offset = Fraction{9, 10}}, quarter_grid) ==
        GridPosition{.measure = 4, .beat = 1, .offset = {}});
    CHECK(
        snapGridPosition(
            map, GridPosition{.measure = 3, .beat = 7, .offset = Fraction{1, 10}}, quarter_grid) ==
        GridPosition{.measure = 3, .beat = 7, .offset = {}});
}

// Note-value validity policy belongs to callers; a non-positive value is a documented no-op.
TEST_CASE("Grid snapping ignores non-positive note values", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition position{.measure = 2, .beat = 2, .offset = Fraction{1, 3}};

    CHECK(snapGridPosition(map, position, Fraction{}) == position);
    CHECK(snapGridPosition(map, position, Fraction{-1, 4}) == position);
}

// The adjacent line strictly beyond a position, either way: from a line its neighbour, from between
// lines the nearer line in the step direction — a step never overshoots the line it is next to.
TEST_CASE("Grid stepping reaches the adjacent note-value line", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const Fraction eighth_grid{1, 8};
    const GridPosition on_line{.measure = 1, .beat = 1, .offset = {}};
    const GridPosition between{.measure = 1, .beat = 1, .offset = Fraction{1, 5}};
    const GridPosition half{.measure = 1, .beat = 1, .offset = Fraction{1, 2}};

    CHECK(adjacentGridPosition(map, on_line, eighth_grid, true) == half);
    CHECK(adjacentGridPosition(map, between, eighth_grid, true) == half);
    CHECK(adjacentGridPosition(map, between, eighth_grid, false) == on_line);
    CHECK(adjacentGridPosition(map, half, eighth_grid, false) == on_line);
    // The next downbeat is the line after a measure's last one.
    CHECK(
        adjacentGridPosition(
            map,
            GridPosition{.measure = 1, .beat = 4, .offset = Fraction{1, 2}},
            eighth_grid,
            true) == GridPosition{.measure = 2, .beat = 1, .offset = {}});
}

// The reversibility the caret, the lane nudge, and the duration gesture all rely on, in the meter
// that broke it: a 1/4-note grid in 7/8 steps two beats, so the measure's last line (beat 7) sits
// one beat — exactly half a step — before the next downbeat. A walk that stepped two beats back
// from the downbeat and re-snapped to the nearest line landed halfway between beats 5 and 7 and
// resolved the tie to beat 5, skipping the line the forward walk had just visited.
TEST_CASE("Grid stepping is exactly reversible across an odd measure", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const Fraction quarter_grid{1, 4};

    // Forward from the 7/8 downbeat through every line of the measure onto the next downbeat.
    std::vector<GridPosition> forward{GridPosition{.measure = 3, .beat = 1, .offset = {}}};
    while (forward.back().measure == 3)
    {
        forward.push_back(adjacentGridPosition(map, forward.back(), quarter_grid, true));
    }
    REQUIRE(forward.size() == 5);
    CHECK(forward[1] == GridPosition{.measure = 3, .beat = 3, .offset = {}});
    CHECK(forward[2] == GridPosition{.measure = 3, .beat = 5, .offset = {}});
    CHECK(forward[3] == GridPosition{.measure = 3, .beat = 7, .offset = {}});
    CHECK(forward[4] == GridPosition{.measure = 4, .beat = 1, .offset = {}});

    // Back from the downbeat through the same lines in reverse, beat 7 included.
    for (std::size_t index = forward.size() - 1; index > 0; --index)
    {
        CAPTURE(index);
        CHECK(adjacentGridPosition(map, forward[index], quarter_grid, false) == forward[index - 1]);
    }
}

// Stepping earlier from a downbeat lands on the previous measure's last line on THAT measure's
// meter — here the 4/4 measure before the 7/8 one, whose quarter-note step is one beat — and the
// grid origin, which has no earlier line, answers with the position itself.
TEST_CASE("Grid stepping crosses a downbeat onto the previous meter's last line", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const Fraction quarter_grid{1, 4};
    const GridPosition downbeat{.measure = 3, .beat = 1, .offset = {}};
    const GridPosition last_line{.measure = 2, .beat = 4, .offset = {}};
    const GridPosition origin{.measure = 1, .beat = 1, .offset = {}};

    CHECK(adjacentGridPosition(map, downbeat, quarter_grid, false) == last_line);
    CHECK(adjacentGridPosition(map, last_line, quarter_grid, true) == downbeat);
    CHECK(adjacentGridPosition(map, origin, quarter_grid, false) == origin);
    // Note-value validity policy belongs to callers, as for the snap.
    CHECK(adjacentGridPosition(map, downbeat, Fraction{}, true) == downbeat);
}

// THE ONE ROUNDING RULE: the nearest whole tick, a tie going to the earlier one, and whole ticks
// passing through untouched.
TEST_CASE("The tick rounding rule takes the nearest tick and the earlier on a tie", "[core][chart]")
{
    CHECK(nearestTick(12, 5) == 2); // 2.4
    CHECK(nearestTick(13, 5) == 3); // 2.6
    CHECK(nearestTick(5, 2) == 2);  // 2.5, the tie: the earlier tick
    CHECK(nearestTick(7, 2) == 3);  // 3.5, the tie again
    CHECK(nearestTick(6, 3) == 2);  // a whole tick is its own nearest
    CHECK(nearestTick(0, 7) == 0);
    CHECK(nearestTick(-5, 2) == -3); // -2.5 ties to the earlier, which is further below zero
}

// A position is on the lattice when its offset is a whole number of ticks at its own meter: a
// quarter-note beat holds 960 ticks and an eighth-note beat 480.
TEST_CASE("A position lies on the tick lattice when its offset is whole ticks", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    CHECK(isOnTickLattice(map, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 3}}));
    CHECK(isOnTickLattice(map, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 960}}));
    CHECK_FALSE(
        isOnTickLattice(map, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 1920}}));
    CHECK_FALSE(
        isOnTickLattice(map, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{4, 7}}));
    // In 7/8 a tick is 1/480 of the eighth-note beat, so 1/960 of that beat is half a tick.
    CHECK(isOnTickLattice(map, GridPosition{.measure = 3, .beat = 2, .offset = Fraction{1, 480}}));
    CHECK_FALSE(
        isOnTickLattice(map, GridPosition{.measure = 3, .beat = 2, .offset = Fraction{1, 960}}));
}

// A grid no tick divides still names only ticks. A septuplet-sixteenth grid (1/28) steps a seventh
// of a 4/4 beat, 137.14 ticks: every line is the tick nearest its exact place, the lines stay
// strictly ascending, stepping walks them one by one and back, and snapping the EXACT septuplet
// instant lands on the same rounded line — which is what lets an imported septuplet note and this
// grid agree tick for tick.
TEST_CASE("A septuplet grid's lines are ticks that step and snap exactly", "[core][chart]")
{
    const TempoMap map = steadyMap(120.0);
    const Fraction septuplet_sixteenth{1, 28};
    const GridPosition downbeat{.measure = 2, .beat = 1, .offset = {}};

    std::vector<GridPosition> lines{downbeat};
    for (int step = 0; step < 28; ++step)
    {
        lines.push_back(adjacentGridPosition(map, lines.back(), septuplet_sixteenth, true));
    }
    // Twenty-eight steps cross exactly one 4/4 measure, landing on the next downbeat.
    CHECK(lines.back() == GridPosition{.measure = 3, .beat = 1, .offset = {}});
    for (std::size_t index = 1; index < lines.size(); ++index)
    {
        CHECK(isOnTickLattice(map, lines[index]));
        CHECK(lines[index - 1] < lines[index]);
        CHECK(
            adjacentGridPosition(map, lines[index], septuplet_sixteenth, false) ==
            lines[index - 1]);
    }

    // Line k sits at the tick nearest k * 3840/28 ticks from the downbeat.
    for (std::size_t line = 0; line < 28; ++line)
    {
        const auto exact_ticks = static_cast<std::int64_t>(line) * 3840;
        const std::int64_t tick = nearestTick(exact_ticks, 28);
        const GridPosition expected =
            advanceGridPosition(map, downbeat, Fraction{static_cast<int>(tick), 960});
        CHECK(lines[line] == expected);
        // The exact septuplet instant, snapped, is that same line.
        const GridPosition exact =
            advanceGridPosition(map, downbeat, Fraction{static_cast<int>(line), 7});
        CHECK(snapGridPosition(map, exact, septuplet_sixteenth) == expected);
        CHECK(snapGridPosition(map, exact, g_tick_quantum_note_value) == expected);
    }
}

// THE WHOLE-NOTE AXIS: the one axis a meter change does not bend. Against the 4/4-then-7/8 map a
// beat is a quarter note before measure 3 and an eighth note from it, so one whole-note step is a
// different beat count on each side, and a BEAT count carried across the change is not the same
// duration at all.
TEST_CASE("The whole-note axis measures the same duration in every meter", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    // Two 4/4 measures are two whole notes, and three and a half quarter-note beats are 7/8 of one.
    const GridPosition late_in_two{.measure = 2, .beat = 4, .offset = Fraction{1, 2}};
    CHECK(wholeNotePosition(map, late_in_two) == Fraction{15, 8});
    CHECK(wholeNotePosition(map, GridPosition{.measure = 3, .beat = 1}) == Fraction{2});
    // Past the last signature the 7/8 reign keeps going: seven more measures of 7/8.
    CHECK(wholeNotePosition(map, GridPosition{.measure = 10, .beat = 1}) == Fraction{65, 8});

    // A quarter note on from m2:4.5 lands one eighth-note beat into measure 3 — where one BEAT on
    // lands half an eighth later, the quarter-note beat re-read as an eighth-note one.
    const GridPosition a_quarter_on{.measure = 3, .beat = 2};
    CHECK(advanceGridPositionByWholeNotes(map, late_in_two, Fraction{1, 4}) == a_quarter_on);
    CHECK(
        advanceGridPosition(map, late_in_two, Fraction{1}) ==
        GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}});
    // Distance is the inverse, both ways.
    CHECK(wholeNoteDistance(map, late_in_two, a_quarter_on) == Fraction{1, 4});
    CHECK(advanceGridPositionByWholeNotes(map, a_quarter_on, Fraction{-1, 4}) == late_in_two);

    // A position on the lattice stays on it across the change when stepped by whole ticks, and
    // leaves it when the same step is taken as a beat count: an odd tick of a quarter-note beat
    // is half a tick of an eighth-note one.
    const GridPosition odd_tick{.measure = 2, .beat = 4, .offset = Fraction{959, 960}};
    const GridPosition carried = advanceGridPositionByWholeNotes(map, odd_tick, Fraction{1, 4});
    CHECK(carried == GridPosition{.measure = 3, .beat = 2, .offset = Fraction{479, 480}});
    CHECK(isOnTickLattice(map, carried));
    CHECK_FALSE(isOnTickLattice(map, advanceGridPosition(map, odd_tick, Fraction{1})));

    // The grid has nothing before its origin: a step back past it clamps there and reads as a
    // step that fell short, exactly as the beat axis clamps.
    const GridPosition second_beat{.measure = 1, .beat = 2};
    CHECK(advanceGridPositionByWholeNotes(map, second_beat, Fraction{-1}) == GridPosition{});
    CHECK(wholeNoteDistance(map, second_beat, GridPosition{}) == Fraction{-1, 4});
}

} // namespace rock_hero::common::core
