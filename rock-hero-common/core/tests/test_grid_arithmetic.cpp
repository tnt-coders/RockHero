#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using rock_hero::common::core::testing::anchor;
using rock_hero::common::core::testing::commonTime;
using rock_hero::common::core::testing::metersAt;

// Measures 1-2 are 4/4 (quarter-note beats), measure 3 onward is 7/8 (eighth-note beats): the
// signature change exercises both the beats-per-measure carry and the note-value-to-beats
// conversion. Anchors only pin absolute time, which grid arithmetic never touches.
[[nodiscard]] TempoMap signatureChangeMap()
{
    return TempoMap{
        metersAt(
            {{.at = "1:1", .numerator = 4, .denominator = 4},
             {.at = "3:1", .numerator = 7, .denominator = 8}}),
        {anchor("1:1", 0), anchor("6:1", 20000)},
    };
}

// A 4/4 map running at one steady tempo across five measures, so the margin walk has nothing but
// the rate to read. Sixteen beats separate the two anchors.
[[nodiscard]] TempoMap steadyMap(const double quarter_note_bpm)
{
    return TempoMap{
        commonTime(),
        {anchor("1:1", 0),
         anchor("5:1", nearestAnchorMilliseconds(16.0 * 60.0 / quarter_note_bpm))},
    };
}

// Seconds between two whole-note places, the quantity the margin is stated in.
[[nodiscard]] double secondsBetween(
    const TempoMap& tempo_map, const Fraction from, const Fraction to)
{
    return tempo_map.secondsAtWholeNote(to) - tempo_map.secondsAtWholeNote(from);
}

} // namespace

// THE MARGIN IS A DURATION: the same span of seconds at any tempo, which is the whole point of the
// law — the gap a player and a charter read on screen must not shrink because the song is fast.
// The lengths differ (50 ms is an eightieth of a whole note at 60 BPM and a twenty-fourth at 200),
// the seconds do not.
TEST_CASE("The minimum sustain distance spans one duration at every tempo", "[core][chart]")
{
    const TempoMap slow = steadyMap(60.0);
    const TempoMap fast = steadyMap(200.0);
    const Fraction onset = slow.wholeNotePositionAt(GridPosition{.measure = 3, .beat = 2});
    CHECK(minimumSustainDistance(slow, onset) == Fraction{1, 80});
    CHECK(minimumSustainDistance(fast, onset) == Fraction{1, 24});

    // Measured back through each map, both land on the one duration — within a tick, the lattice
    // the answer is floored onto.
    const double slow_tick_seconds = 60.0 / 60.0 * 4.0 / g_tick_quantum_denominator;
    const double fast_tick_seconds = 60.0 / 200.0 * 4.0 / g_tick_quantum_denominator;
    CHECK(
        secondsBetween(slow, onset - minimumSustainDistance(slow, onset), onset) ==
        Catch::Approx(g_minimum_sustain_distance_seconds).margin(slow_tick_seconds));
    CHECK(
        secondsBetween(fast, onset - minimumSustainDistance(fast, onset), onset) ==
        Catch::Approx(g_minimum_sustain_distance_seconds).margin(fast_tick_seconds));
}

// A tempo anchor standing INSIDE the margin is honoured exactly, because the walk leaves the
// whole-note axis for the map's time axis and comes back rather than scaling one local rate.
// Measure 1 runs at 60 BPM, then the quarter into measure 2 is pinned forty times faster: the
// margin before the downbeat therefore reaches back over the whole fast quarter and on into the
// preceding one, which a single-rate margin could never produce.
TEST_CASE("The minimum sustain distance honours a tempo anchor inside it", "[core][chart]")
{
    const TempoMap map{
        commonTime(),
        {anchor("1:1", 0), anchor("1:4", 3000), anchor("2:1", 3025), anchor("5:1", 7000)},
    };
    const Fraction onset{1};

    // 0.025 s of the 50 ms margin is spent on the fast quarter and 0.025 s at one second per
    // quarter, so the margin starts 39/40 of a quarter into measure 1 beat 3 — on the tick lattice
    // exactly.
    const Fraction start = tickPlaceReached(map, onset, -g_minimum_sustain_distance_seconds);
    CHECK(
        map.gridPositionAtWholeNote(start) ==
        GridPosition{.measure = 1, .beat = 3, .offset = Fraction{39, 40}});
    CHECK(minimumSustainDistance(map, onset) == Fraction{41, 160});
    CHECK(secondsBetween(map, start, onset) == Catch::Approx(g_minimum_sustain_distance_seconds));
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

    for (const GridPosition& onset : onsets)
    {
        const Fraction place = map.wholeNotePositionAt(onset);
        const Fraction start = tickPlaceReached(map, place, -g_minimum_sustain_distance_seconds);
        CHECK(isOnTickLattice(start));
        CHECK(secondsBetween(map, start, place) >= g_minimum_sustain_distance_seconds);
    }
}

// An onset standing closer to the chart's start than the margin has nowhere to reach back to, so
// the walk clamps at the grid origin and the margin is simply what room there was.
TEST_CASE("The minimum sustain distance clamps at the chart's start", "[core][chart]")
{
    const TempoMap map = steadyMap(60.0);

    CHECK(tickPlaceReached(map, Fraction{}, -g_minimum_sustain_distance_seconds) == Fraction{});
    CHECK(minimumSustainDistance(map, Fraction{}) == Fraction{});

    const Fraction early{1, 80};
    CHECK(tickPlaceReached(map, early, -g_minimum_sustain_distance_seconds) == Fraction{});
    CHECK(minimumSustainDistance(map, early) == Fraction{1, 80});
}

// Going FORWARD the reach is ceiled onto the lattice, so it is never shorter than the duration,
// and it is always at least one tick: the time axis clamps at the terminal anchor, so a place at
// or past it would otherwise reach nowhere at all.
TEST_CASE("A forward reach is at least one tick, even past the terminal", "[core][chart]")
{
    const TempoMap map = steadyMap(60.0);
    const Fraction one_tick = g_tick_quantum_note_value;

    // 50 ms at 60 BPM is exactly an eightieth of a whole note.
    CHECK(tickPlaceReached(map, Fraction{1}, 0.05) == Fraction{81, 80});
    // At 137 BPM the stretch lies between two ticks, so the reach takes the later one.
    const TempoMap odd = steadyMap(137.0);
    const Fraction reached = tickPlaceReached(odd, Fraction{1}, 0.05);
    CHECK(isOnTickLattice(reached));
    CHECK(secondsBetween(odd, Fraction{1}, reached) >= 0.05);
    CHECK(secondsBetween(odd, Fraction{1}, reached - one_tick) < 0.05);

    // The terminal anchor stands at 5:1, whole note 4. From it, and from past it, the time axis
    // has nowhere left to go, and the reach is the one tick a forward stretch never falls under.
    CHECK(tickPlaceReached(map, Fraction{4}, 0.05) == Fraction{4} + one_tick);
    CHECK(tickPlaceReached(map, Fraction{6}, 0.05) == Fraction{6} + one_tick);
    // One tick short of the terminal, the stretch is cut off at it — which is still one tick on.
    CHECK(tickPlaceReached(map, Fraction{4} - one_tick, 0.05) == Fraction{4});
}

// A whole-note place addresses across beat, measure and signature boundaries: the 4/4 measures
// hold four quarter-note beats and the 7/8 measures seven eighth-note ones.
TEST_CASE("A whole-note place addresses across measures and meters", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    CHECK(
        map.gridPositionAtWholeNote(Fraction{15, 8}) ==
        GridPosition{.measure = 2, .beat = 4, .offset = Fraction{1, 2}});
    CHECK(map.gridPositionAtWholeNote(Fraction{2}) == GridPosition{.measure = 3, .beat = 1});
    CHECK(
        map.gridPositionAtWholeNote(Fraction{2} + Fraction{7, 8}) ==
        GridPosition{.measure = 4, .beat = 1});
    CHECK(
        map.gridPositionAtWholeNote(Fraction{33, 16}) ==
        GridPosition{.measure = 3, .beat = 1, .offset = Fraction{1, 2}});
    // Nothing exists before the origin, so a place before it clamps there.
    CHECK(map.gridPositionAtWholeNote(Fraction{-1, 4}) == GridPosition{});
}

// Advancement inside one beat accumulates the offset exactly; crossing the beat carries.
TEST_CASE("Grid advancement carries offsets across beats", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition base{.measure = 1, .beat = 1, .offset = Fraction{1, 4}};

    CHECK(
        advanceGridPositionByWholeNotes(map, base, Fraction{1, 8}) ==
        GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
    CHECK(
        advanceGridPositionByWholeNotes(map, base, Fraction{3, 16}) ==
        GridPosition{.measure = 1, .beat = 2, .offset = {}});
    CHECK(
        advanceGridPositionByWholeNotes(map, base, Fraction{7, 16}) ==
        GridPosition{.measure = 1, .beat = 3, .offset = {}});
}

// Distance is the exact inverse of advancement, including tuplet fractions the corpus uses and
// steps that cross the meter change.
TEST_CASE("Whole-note distance inverts advancement exactly", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition base{.measure = 2, .beat = 3, .offset = Fraction{2, 5}};
    const std::vector<Fraction> deltas{
        Fraction{1, 20},
        Fraction{3, 28},
        Fraction{11, 18},
        Fraction{1, 48},
        Fraction{-7, 20},
        Fraction{-13, 28},
        Fraction{9, 4},
    };

    for (const Fraction delta : deltas)
    {
        const GridPosition advanced = advanceGridPositionByWholeNotes(map, base, delta);
        CHECK(wholeNoteDistance(map, base, advanced) == delta);
    }
    CHECK(wholeNoteDistance(map, base, base) == Fraction{});
}

// A ring's end is its onset advanced by the ring; the endpoint is exact across a meter change.
TEST_CASE("Sustain endpoints resolve through the tempo map", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();

    ChartNote note{};
    note.position = GridPosition{.measure = 1, .beat = 4, .offset = {}};
    note.sustain = Fraction{3, 8};
    CHECK(
        advanceGridPositionByWholeNotes(map, note.position, note.sustain) ==
        GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 2}});

    // A dotted quarter from the last quarter of measure 2 ends one eighth-note beat into the 7/8.
    note.position = GridPosition{.measure = 2, .beat = 4};
    CHECK(
        advanceGridPositionByWholeNotes(map, note.position, note.sustain) ==
        GridPosition{.measure = 3, .beat = 2});
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

// A step under a tick has no lattice of distinct lines to read — rounded onto ticks, its lines
// would collapse onto one another — so both lattice queries return the position unchanged, the
// same refusal a non-positive note value gets.
TEST_CASE("Grid queries refuse a note value under a tick", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    const GridPosition between_lines{.measure = 1, .beat = 2, .offset = Fraction{1, 3}};
    const Fraction half_tick = g_tick_quantum_note_value * Fraction{1, 2};

    CHECK(snapGridPosition(map, between_lines, half_tick) == between_lines);
    CHECK(adjacentGridPosition(map, between_lines, half_tick, true) == between_lines);
    CHECK(snapGridPosition(map, between_lines, Fraction{}) == between_lines);
    // A tick itself is the finest grid there is, and it still answers.
    CHECK(
        snapGridPosition(map, between_lines, g_tick_quantum_note_value) ==
        GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 3}});
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
// that tests it hardest: a 1/4-note grid in 7/8 steps two beats, so the measure's last line (beat
// 7) sits one beat — exactly half a step — before the next downbeat. A walk that stepped two beats
// back from the downbeat and re-snapped to the nearest line would land halfway between beats 5 and
// 7 and resolve the tie to beat 5, skipping the line the forward walk had just visited.
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

// The chart ends at its terminal anchor, the mirror of the origin: a later step lands on the
// terminal where the next line would pass it — mid-measure here, between two quarter lines on an
// eighth — and from the terminal itself there is nothing later. A terminal on a downbeat is reached
// as the line it is.
TEST_CASE("Grid stepping stops at the terminal anchor", "[core][chart]")
{
    const Fraction quarter_grid{1, 4};
    const GridPosition last_line{.measure = 5, .beat = 3, .offset = {}};

    const GridPosition mid_terminal{.measure = 5, .beat = 3, .offset = Fraction{1, 2}};
    const TempoMap mid = TempoMap{commonTime(), {anchor("1:1", 0), anchor("5:3+1/2", 9250)}};
    CHECK(adjacentGridPosition(mid, last_line, quarter_grid, true) == mid_terminal);
    CHECK(adjacentGridPosition(mid, mid_terminal, quarter_grid, true) == mid_terminal);
    CHECK(adjacentGridPosition(mid, mid_terminal, quarter_grid, false) == last_line);

    const GridPosition downbeat_terminal{.measure = 6, .beat = 1, .offset = {}};
    const TempoMap whole = TempoMap{commonTime(), {anchor("1:1", 0), anchor("6:1", 10000)}};
    const GridPosition last_beat{.measure = 5, .beat = 4, .offset = {}};
    CHECK(adjacentGridPosition(whole, last_beat, quarter_grid, true) == downbeat_terminal);
    CHECK(adjacentGridPosition(whole, downbeat_terminal, quarter_grid, true) == downbeat_terminal);
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

// A tick count read as a double rounds by the same rule, so a decimal source (an imported sync
// point's fraction of a bar) lands on the tick an exact rational of the same value would.
TEST_CASE("The tick rounding rule is the same for a tick count read as a double", "[core][chart]")
{
    CHECK(nearestTick(2.5) == 2);   // the tie: the earlier tick, not away from zero
    CHECK(nearestTick(-2.5) == -3); // the earlier tick below zero too
    CHECK(nearestTick(548.5714) == 549);
    CHECK(nearestTick(1151.9999) == 1152);
    for (const std::int64_t numerator : {-7, -5, 0, 5, 7, 12, 13, 1097})
    {
        const double exact_ticks = static_cast<double>(numerator) / 2.0;
        CHECK(nearestTick(exact_ticks) == nearestTick(numerator, 2));
    }
}

// The lattice rule lives on the whole-note axis: a place is on it when it is a whole number of
// 1/3840 whole notes, and the nearest lattice place keeps one already there and rounds any other by
// the tick rule, a tie going to the earlier tick.
TEST_CASE("A whole-note place rounds onto the tick lattice only when off it", "[core][chart]")
{
    CHECK(isOnTickLattice(Fraction{3, 8}));
    CHECK(isOnTickLattice(Fraction{1, 3840}));
    CHECK_FALSE(isOnTickLattice(Fraction{1, 7680}));
    CHECK(nearestTickPlace(Fraction{3, 8}) == Fraction{3, 8});
    CHECK(nearestTickPlace(Fraction{1, 7680}) == Fraction{});
    CHECK(nearestTickPlace(Fraction{3, 7680}) == Fraction{1, 3840});
    CHECK(nearestTickPlace(Fraction{5, 7}) == Fraction{2743, 3840}); // 2742.857 ticks
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
        const GridPosition expected = advanceGridPositionByWholeNotes(
            map, downbeat, Fraction{static_cast<int>(tick), g_tick_quantum_denominator});
        CHECK(lines[line] == expected);
        // The exact septuplet instant, snapped, is that same line.
        const GridPosition exact =
            advanceGridPositionByWholeNotes(map, downbeat, Fraction{static_cast<int>(line), 28});
        CHECK(snapGridPosition(map, exact, septuplet_sixteenth) == expected);
        CHECK(snapGridPosition(map, exact, g_tick_quantum_note_value) == expected);
    }
}

// THE WHOLE-NOTE AXIS: the one axis a meter change does not bend. Against the 4/4-then-7/8 map a
// beat is a quarter note before measure 3 and an eighth note from it, so the one note value spans
// a different number of beats on each side, and the axis is what keeps it one length.
TEST_CASE("The whole-note axis measures the same duration in every meter", "[core][chart]")
{
    const TempoMap map = signatureChangeMap();
    // Two 4/4 measures are two whole notes, and three and a half quarter-note beats are 7/8 of one.
    const GridPosition late_in_two{.measure = 2, .beat = 4, .offset = Fraction{1, 2}};
    CHECK(map.wholeNotePositionAt(late_in_two) == Fraction{15, 8});
    CHECK(map.wholeNotePositionAt(GridPosition{.measure = 3, .beat = 1}) == Fraction{2});
    // Past the last signature the 7/8 reign keeps going: seven more measures of 7/8.
    CHECK(map.wholeNotePositionAt(GridPosition{.measure = 10, .beat = 1}) == Fraction{65, 8});

    // A quarter note on from m2:4.5 lands one eighth-note beat into measure 3.
    const GridPosition a_quarter_on{.measure = 3, .beat = 2};
    CHECK(advanceGridPositionByWholeNotes(map, late_in_two, Fraction{1, 4}) == a_quarter_on);
    // Distance is the inverse, both ways.
    CHECK(wholeNoteDistance(map, late_in_two, a_quarter_on) == Fraction{1, 4});
    CHECK(advanceGridPositionByWholeNotes(map, a_quarter_on, Fraction{-1, 4}) == late_in_two);

    // A position on the lattice stays on it across the change when stepped by whole ticks: an odd
    // tick of a quarter-note beat lands on an odd tick of an eighth-note one.
    const GridPosition odd_tick{.measure = 2, .beat = 4, .offset = Fraction{959, 960}};
    const GridPosition carried = advanceGridPositionByWholeNotes(map, odd_tick, Fraction{1, 4});
    CHECK(carried == GridPosition{.measure = 3, .beat = 2, .offset = Fraction{479, 480}});
    CHECK(isOnTickLattice(map, carried));

    // The grid has nothing before its origin: a step back past it clamps there and reads as a
    // step that fell short.
    const GridPosition second_beat{.measure = 1, .beat = 2};
    CHECK(advanceGridPositionByWholeNotes(map, second_beat, Fraction{-1}) == GridPosition{});
    CHECK(wholeNoteDistance(map, second_beat, GridPosition{}) == Fraction{-1, 4});
}

} // namespace rock_hero::common::core
