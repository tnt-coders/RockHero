#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/tempo_grid_geometry.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Builds a uniform 4/4 map spanning the given measures, with beats evenly spaced in time. Two
// anchors pin the first and the terminal downbeat, so secondsAtBeat interpolates linearly between
// them and a beat at global index k lands at k * (measure_seconds / 4) seconds.
[[nodiscard]] common::core::TempoMap makeUniform44Map(int measures, double measure_seconds)
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
        },
        std::vector{
            common::core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            common::core::BeatAnchor{
                .measure = measures + 1,
                .beat = 1,
                .seconds = static_cast<double>(measures) * measure_seconds,
            },
        },
    };
}

// A four-second single measure mapped so every beat lands on a round hundred-pixel column: width
// 401 over a [0, 4] timeline gives x = seconds * 100.
constexpr common::core::TimeRange one_measure_window{
    .start = common::core::TimePosition{0.0},
    .end = common::core::TimePosition{4.0},
};
constexpr int one_measure_width = 401;

// Whole-beat grid step matching the editor's default spacing.
constexpr common::core::Fraction whole_beat_spacing{1, 1};

} // namespace

// Verifies a full-width span returns every beat once, with downbeats ranked as measures.
TEST_CASE("Visible tempo grid returns all beats across the full width", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, whole_beat_spacing, one_measure_window, one_measure_width, 0, one_measure_width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 100, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Beat},
        {.x = 200, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
        {.x = 300, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Beat},
        {.x = 400, .measure = 2, .beat = 1, .rank = TempoGridLineRank::Measure},
    };
    CHECK(lines == expected);
}

// Verifies only the lines inside the visible span are returned, leaving the rest culled.
TEST_CASE("Visible tempo grid culls lines outside the span", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, whole_beat_spacing, one_measure_window, one_measure_width, 150, 350);

    const std::vector<TempoGridLine> expected{
        {.x = 200, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
        {.x = 300, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Beat},
    };
    CHECK(lines == expected);
}

// Verifies the span is half-open: a line exactly on the left edge is kept, the right edge excluded.
TEST_CASE("Visible tempo grid treats the span as half-open", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, whole_beat_spacing, one_measure_window, one_measure_width, 100, 300);

    const std::vector<TempoGridLine> expected{
        {.x = 100, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Beat},
        {.x = 200, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
    };
    CHECK(lines == expected);
}

// Verifies a narrow window over a long song returns only the visible beats, with correct columns
// and ranks. This exercises the binary search jumping past the leading off-screen beats.
TEST_CASE("Visible tempo grid scans only the visible run of a long song", "[core][tempo-grid]")
{
    // 100 measures at four seconds each: 400 beats, one per second, mapped one pixel per second.
    const common::core::TempoMap map = makeUniform44Map(100, 4.0);
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{400.0},
    };
    constexpr int width = 401;

    const std::vector<TempoGridLine> lines =
        visibleTempoGridLines(map, whole_beat_spacing, window, width, 199, 202);

    // Global beat 200 is measure 51 beat 1, so it is the only downbeat in this span.
    const std::vector<TempoGridLine> expected{
        {.x = 199, .measure = 50, .beat = 4, .rank = TempoGridLineRank::Beat},
        {.x = 200, .measure = 51, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 201, .measure = 51, .beat = 2, .rank = TempoGridLineRank::Beat},
    };
    CHECK(lines == expected);
}

// Verifies beats that collapse onto one column when zoomed far out are merged to a single line per
// column, capping the output at the visible pixel count instead of the beat count.
TEST_CASE("Visible tempo grid merges beats sharing a column", "[core][tempo-grid]")
{
    // 400 beats squeezed into a five-pixel width: every column collects many beats, including
    // downbeats, so each surviving line keeps the measure rank.
    const common::core::TempoMap map = makeUniform44Map(100, 4.0);
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{400.0},
    };
    constexpr int width = 5;

    const std::vector<TempoGridLine> lines =
        visibleTempoGridLines(map, whole_beat_spacing, window, width, 0, width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 1, .measure = 14, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 2, .measure = 39, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 3, .measure = 64, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 4, .measure = 89, .beat = 1, .rank = TempoGridLineRank::Measure},
    };
    CHECK(lines == expected);
}

// Verifies degenerate inputs yield no lines rather than dividing by zero or scanning needlessly.
TEST_CASE("Visible tempo grid rejects degenerate inputs", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(visibleTempoGridLines(map, whole_beat_spacing, one_measure_window, 0, 0, 10).empty());
    CHECK(visibleTempoGridLines(map, whole_beat_spacing, one_measure_window, -5, 0, 10).empty());

    constexpr common::core::TimeRange empty_window{
        .start = common::core::TimePosition{4.0},
        .end = common::core::TimePosition{4.0},
    };
    CHECK(visibleTempoGridLines(
              map, whole_beat_spacing, empty_window, one_measure_width, 0, one_measure_width)
              .empty());

    // An inverted or empty visible span selects nothing.
    CHECK(visibleTempoGridLines(
              map, whole_beat_spacing, one_measure_window, one_measure_width, 200, 200)
              .empty());
    CHECK(visibleTempoGridLines(
              map, whole_beat_spacing, one_measure_window, one_measure_width, 300, 100)
              .empty());
}

// Verifies half-beat spacing interleaves subdivision lines between the whole-beat lines.
TEST_CASE("Visible tempo grid places half-beat subdivisions between beats", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map,
        common::core::Fraction{1, 2},
        one_measure_window,
        one_measure_width,
        0,
        one_measure_width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 50, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Subdivision},
        {.x = 100, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Beat},
        {.x = 150, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Subdivision},
        {.x = 200, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
        {.x = 250, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Subdivision},
        {.x = 300, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Beat},
        {.x = 350, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Subdivision},
        {.x = 400, .measure = 2, .beat = 1, .rank = TempoGridLineRank::Measure},
    };
    CHECK(lines == expected);
}

// Verifies quarter-beat spacing lands on expected columns inside a clipped visible span, keeping
// the bounded scan for fractional grids.
TEST_CASE("Visible tempo grid clips quarter-beat subdivisions to the span", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, common::core::Fraction{1, 4}, one_measure_window, one_measure_width, 140, 260);

    const std::vector<TempoGridLine> expected{
        {.x = 150, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Subdivision},
        {.x = 175, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Subdivision},
        {.x = 200, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
        {.x = 225, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Subdivision},
        {.x = 250, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Subdivision},
    };
    CHECK(lines == expected);
}

// Verifies non-power-of-two spacing such as beat thirds resolves exactly through Fraction offsets.
TEST_CASE("Visible tempo grid supports third-of-a-beat spacing", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, common::core::Fraction{1, 3}, one_measure_window, one_measure_width, 0, 110);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 33, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Subdivision},
        {.x = 67, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Subdivision},
        {.x = 100, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Beat},
    };
    CHECK(lines == expected);
}

// Verifies collapsed columns keep the strongest rank so beats and downbeats survive subdivisions.
TEST_CASE("Visible tempo grid merges collapsed subdivisions by rank", "[core][tempo-grid]")
{
    // A five-pixel width maps x to whole seconds, so each half-beat subdivision shares a column
    // with the beat or downbeat that follows it and must be promoted.
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);
    constexpr int width = 5;

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, common::core::Fraction{1, 2}, one_measure_window, width, 0, width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 1, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Beat},
        {.x = 2, .measure = 1, .beat = 3, .rank = TempoGridLineRank::Beat},
        {.x = 3, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Beat},
        {.x = 4, .measure = 2, .beat = 1, .rank = TempoGridLineRank::Measure},
    };
    CHECK(lines == expected);
}

// Verifies steps larger than one beat skip beats for both generation and snapping.
TEST_CASE("Visible tempo grid supports steps larger than one beat", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);
    constexpr common::core::Fraction three_half_beats{3, 2};

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(
        map, three_half_beats, one_measure_window, one_measure_width, 0, one_measure_width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .rank = TempoGridLineRank::Measure},
        {.x = 150, .measure = 1, .beat = 2, .rank = TempoGridLineRank::Subdivision},
        {.x = 300, .measure = 1, .beat = 4, .rank = TempoGridLineRank::Beat},
    };
    CHECK(lines == expected);

    CHECK(
        nearestTempoGridTime(map, three_half_beats, common::core::TimePosition{2.2}) ==
        common::core::TimePosition{1.5});
}

// Verifies corrupt spacing degrades to the whole-beat grid instead of blanking the timeline.
TEST_CASE("Visible tempo grid falls back to whole beats for invalid spacing", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> whole_beat_lines = visibleTempoGridLines(
        map, whole_beat_spacing, one_measure_window, one_measure_width, 0, one_measure_width);

    // The Fraction default of 0/1 and an over-bound denominator are both rejected.
    CHECK(
        visibleTempoGridLines(
            map,
            common::core::Fraction{},
            one_measure_window,
            one_measure_width,
            0,
            one_measure_width) == whole_beat_lines);
    CHECK(
        visibleTempoGridLines(
            map,
            common::core::Fraction{1, 2048},
            one_measure_window,
            one_measure_width,
            0,
            one_measure_width) == whole_beat_lines);
}

// Verifies snap lookup picks the nearest beat time without scanning the whole range.
TEST_CASE("Nearest tempo grid time picks the closest beat", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{1.49}) ==
        common::core::TimePosition{1.0});
    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{1.51}) ==
        common::core::TimePosition{2.0});
}

// Verifies targets exactly halfway between beats prefer the earlier line for stable snapping.
TEST_CASE("Nearest tempo grid time resolves ties to the earlier beat", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{1.5}) ==
        common::core::TimePosition{1.0});
}

// Verifies out-of-range targets snap to the first or terminal beat instead of extrapolating.
TEST_CASE("Nearest tempo grid time clamps to the authored beat range", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{-3.0}) ==
        common::core::TimePosition{0.0});
    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{9.0}) ==
        common::core::TimePosition{4.0});
}

// Verifies a degenerate anchorless map resolves to its clamp position instead of failing.
TEST_CASE("Nearest tempo grid time tolerates an anchorless map", "[core][tempo-grid]")
{
    const common::core::TempoMap map{{}, {}};

    CHECK(
        nearestTempoGridTime(map, whole_beat_spacing, common::core::TimePosition{2.0}) ==
        common::core::TimePosition{0.0});
}

// Verifies snapping selects fractional grid lines and keeps the earlier-line tie rule.
TEST_CASE("Nearest tempo grid time snaps to subdivisions", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);
    constexpr common::core::Fraction half_beat{1, 2};

    CHECK(
        nearestTempoGridTime(map, half_beat, common::core::TimePosition{0.74}) ==
        common::core::TimePosition{0.5});
    CHECK(
        nearestTempoGridTime(map, half_beat, common::core::TimePosition{0.76}) ==
        common::core::TimePosition{1.0});
    CHECK(
        nearestTempoGridTime(map, half_beat, common::core::TimePosition{0.75}) ==
        common::core::TimePosition{0.5});
}

// Verifies corrupt spacing snaps on the whole-beat grid, matching the rendering fallback.
TEST_CASE("Nearest tempo grid time falls back to whole beats", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(
        nearestTempoGridTime(map, common::core::Fraction{}, common::core::TimePosition{0.74}) ==
        common::core::TimePosition{1.0});
}

// Verifies display and entry conversions share note-value units and reduce exactly.
TEST_CASE("Tempo grid note-value conversions reduce exactly", "[core][tempo-grid]")
{
    CHECK(
        displayedTempoGridNoteValue(common::core::Fraction{1, 1}, 4) ==
        common::core::Fraction{1, 4});
    CHECK(
        displayedTempoGridNoteValue(common::core::Fraction{1, 2}, 4) ==
        common::core::Fraction{1, 8});
    CHECK(
        displayedTempoGridNoteValue(common::core::Fraction{3, 4}, 4) ==
        common::core::Fraction{3, 16});
    CHECK(
        displayedTempoGridNoteValue(common::core::Fraction{1, 3}, 4) ==
        common::core::Fraction{1, 12});
    CHECK(
        displayedTempoGridNoteValue(common::core::Fraction{1, 1}, 8) ==
        common::core::Fraction{1, 8});

    CHECK(
        tempoGridSpacingFromNoteValue(common::core::Fraction{1, 16}, 4) ==
        common::core::Fraction{1, 4});
    CHECK(
        tempoGridSpacingFromNoteValue(common::core::Fraction{1, 16}, 8) ==
        common::core::Fraction{1, 2});
    CHECK(
        tempoGridSpacingFromNoteValue(common::core::Fraction{3, 16}, 4) ==
        common::core::Fraction{3, 4});
    CHECK(
        tempoGridSpacingFromNoteValue(common::core::Fraction{1, 12}, 4) ==
        common::core::Fraction{1, 3});

    // The conversions are inverses, so entry always round-trips back to the same display text.
    CHECK(
        tempoGridSpacingFromNoteValue(
            displayedTempoGridNoteValue(common::core::Fraction{1, 3}, 4), 4) ==
        common::core::Fraction{1, 3});
}

// Verifies oversized entries collapse to the invalid 0/1 spacing instead of overflowing the
// widened numerator product back into int.
TEST_CASE("Tempo grid note-value conversion rejects oversized entries", "[core][tempo-grid]")
{
    const common::core::Fraction spacing =
        tempoGridSpacingFromNoteValue(common::core::Fraction{600000000, 1}, 4);

    CHECK(spacing == common::core::Fraction{0, 1});
    CHECK_FALSE(isValidTempoGridSpacing(spacing));
}

// Verifies free placement keeps the exact click time while snap placement resolves to the
// nearest grid line's exact time. Width 401 over [0, 4] makes column x equal x / 100 seconds.
TEST_CASE("Timeline cursor placement snaps or keeps the click point by mode", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const auto free_position = timelineCursorPlacementTime(
        map,
        whole_beat_spacing,
        one_measure_window,
        one_measure_width,
        150.0f,
        TimelineCursorPlacementMode::Free);
    REQUIRE(free_position.has_value());
    CHECK(free_position->seconds == 1.5);

    const auto snapped_position = timelineCursorPlacementTime(
        map,
        whole_beat_spacing,
        one_measure_window,
        one_measure_width,
        140.0f,
        TimelineCursorPlacementMode::SnapToGrid);
    REQUIRE(snapped_position.has_value());
    CHECK(snapped_position->seconds == 1.0);
}

// Verifies a click exactly halfway between two grid lines snaps to the earlier line, so repeated
// clicks on the same pixel place the cursor stably instead of jumping forward.
TEST_CASE(
    "Timeline cursor placement resolves halfway clicks to the earlier line", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const auto position = timelineCursorPlacementTime(
        map,
        whole_beat_spacing,
        one_measure_window,
        one_measure_width,
        150.0f,
        TimelineCursorPlacementMode::SnapToGrid);
    REQUIRE(position.has_value());
    CHECK(position->seconds == 1.0);
}

// Verifies degenerate timeline geometry yields no placement instead of a fabricated position.
TEST_CASE("Timeline cursor placement rejects invalid geometry", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const auto position = timelineCursorPlacementTime(
        map,
        whole_beat_spacing,
        one_measure_window,
        0,
        140.0f,
        TimelineCursorPlacementMode::SnapToGrid);
    CHECK_FALSE(position.has_value());
}

} // namespace rock_hero::editor::core
