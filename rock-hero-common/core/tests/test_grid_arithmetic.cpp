#include <catch2/catch_test_macros.hpp>
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

} // namespace

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

// The hold a strum inherits from a hand-shape span comes from the span that reaches FURTHEST, not
// the one that started last. Nothing forbids spans from overlapping, and a shape that began earlier
// and runs longer holds the same strum just as well — but a single cursor remembering the latest
// STARTING span let a short one beginning inside a long one shadow it, so the strum read as
// released and every hammer-on or pull-off it justified was repaired away.
TEST_CASE("A strum's inherited hold comes from the furthest-reaching span", "[core][chart]")
{
    const TempoMap map = TempoMap::defaultMap(TimeDuration{32.0});
    // Two sustainless notes at measure 3 beat 1, which is inside span A (measures 1 through 5) and
    // also inside span B (measure 2, one beat long) — B starts later but ends long before.
    const std::vector<ChartNote> notes = {
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        ChartNote{
            .position = GridPosition{.measure = 3, .beat = 1},
            .string = 2,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
    };
    const std::vector<ChartShape> shapes = {
        ChartShape{.position = GridPosition{.measure = 1, .beat = 1}, .sustain = Fraction{16}},
        ChartShape{.position = GridPosition{.measure = 2, .beat = 1}, .sustain = Fraction{1}},
    };

    const std::vector<Fraction> held = chartEffectiveSustains(notes, shapes, map);
    REQUIRE(held.size() == 2);
    // Span A runs 16 beats from measure 1 beat 1, so it ends at measure 5 beat 1 — eight beats past
    // the onset at measure 3 beat 1.
    CHECK(held[0] == Fraction{8});
    CHECK(held[1] == Fraction{8});

    // Order in the vector must not matter either: the same two spans listed the other way round
    // give the same answer, which a last-writer-wins cursor could not promise.
    const std::vector<ChartShape> reversed = {shapes[1], shapes[0]};
    const std::vector<Fraction> held_reversed = chartEffectiveSustains(notes, reversed, map);
    REQUIRE(held_reversed.size() == 2);
    CHECK(held_reversed[0] == Fraction{8});
}

} // namespace rock_hero::common::core
