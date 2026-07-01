#include <catch2/catch_test_macros.hpp>
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

} // namespace

// Verifies a full-width span returns every beat once, with downbeats flagged as measure starts.
TEST_CASE("Visible tempo grid returns all beats across the full width", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines =
        visibleTempoGridLines(map, one_measure_window, one_measure_width, 0, one_measure_width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .measure_start = true},
        {.x = 100, .measure = 1, .beat = 2, .measure_start = false},
        {.x = 200, .measure = 1, .beat = 3, .measure_start = false},
        {.x = 300, .measure = 1, .beat = 4, .measure_start = false},
        {.x = 400, .measure = 2, .beat = 1, .measure_start = true},
    };
    CHECK(lines == expected);
}

// Verifies only the lines inside the visible span are returned, leaving the rest culled.
TEST_CASE("Visible tempo grid culls lines outside the span", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines =
        visibleTempoGridLines(map, one_measure_window, one_measure_width, 150, 350);

    const std::vector<TempoGridLine> expected{
        {.x = 200, .measure = 1, .beat = 3, .measure_start = false},
        {.x = 300, .measure = 1, .beat = 4, .measure_start = false},
    };
    CHECK(lines == expected);
}

// Verifies the span is half-open: a line exactly on the left edge is kept, the right edge excluded.
TEST_CASE("Visible tempo grid treats the span as half-open", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    const std::vector<TempoGridLine> lines =
        visibleTempoGridLines(map, one_measure_window, one_measure_width, 100, 300);

    const std::vector<TempoGridLine> expected{
        {.x = 100, .measure = 1, .beat = 2, .measure_start = false},
        {.x = 200, .measure = 1, .beat = 3, .measure_start = false},
    };
    CHECK(lines == expected);
}

// Verifies a narrow window over a long song returns only the visible beats, with correct columns
// and measure flags. This exercises the binary search jumping past the leading off-screen beats.
TEST_CASE("Visible tempo grid scans only the visible run of a long song", "[core][tempo-grid]")
{
    // 100 measures at four seconds each: 400 beats, one per second, mapped one pixel per second.
    const common::core::TempoMap map = makeUniform44Map(100, 4.0);
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{400.0},
    };
    constexpr int width = 401;

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(map, window, width, 199, 202);

    // Global beat 200 is measure 51 beat 1, so it is the only downbeat in this span.
    const std::vector<TempoGridLine> expected{
        {.x = 199, .measure = 50, .beat = 4, .measure_start = false},
        {.x = 200, .measure = 51, .beat = 1, .measure_start = true},
        {.x = 201, .measure = 51, .beat = 2, .measure_start = false},
    };
    CHECK(lines == expected);
}

// Verifies beats that collapse onto one column when zoomed far out are merged to a single line per
// column, capping the output at the visible pixel count instead of the beat count.
TEST_CASE("Visible tempo grid merges beats sharing a column", "[core][tempo-grid]")
{
    // 400 beats squeezed into a five-pixel width: every column collects many beats, including
    // downbeats, so each surviving line keeps the measure colour.
    const common::core::TempoMap map = makeUniform44Map(100, 4.0);
    constexpr common::core::TimeRange window{
        .start = common::core::TimePosition{0.0},
        .end = common::core::TimePosition{400.0},
    };
    constexpr int width = 5;

    const std::vector<TempoGridLine> lines = visibleTempoGridLines(map, window, width, 0, width);

    const std::vector<TempoGridLine> expected{
        {.x = 0, .measure = 1, .beat = 1, .measure_start = true},
        {.x = 1, .measure = 14, .beat = 1, .measure_start = true},
        {.x = 2, .measure = 39, .beat = 1, .measure_start = true},
        {.x = 3, .measure = 64, .beat = 1, .measure_start = true},
        {.x = 4, .measure = 89, .beat = 1, .measure_start = true},
    };
    CHECK(lines == expected);
}

// Verifies degenerate inputs yield no lines rather than dividing by zero or scanning needlessly.
TEST_CASE("Visible tempo grid rejects degenerate inputs", "[core][tempo-grid]")
{
    const common::core::TempoMap map = makeUniform44Map(1, 4.0);

    CHECK(visibleTempoGridLines(map, one_measure_window, 0, 0, 10).empty());
    CHECK(visibleTempoGridLines(map, one_measure_window, -5, 0, 10).empty());

    constexpr common::core::TimeRange empty_window{
        .start = common::core::TimePosition{4.0},
        .end = common::core::TimePosition{4.0},
    };
    CHECK(
        visibleTempoGridLines(map, empty_window, one_measure_width, 0, one_measure_width).empty());

    // An inverted or empty visible span selects nothing.
    CHECK(visibleTempoGridLines(map, one_measure_window, one_measure_width, 200, 200).empty());
    CHECK(visibleTempoGridLines(map, one_measure_window, one_measure_width, 300, 100).empty());
}

} // namespace rock_hero::editor::core
