#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/timeline/timeline_x.h>

namespace rock_hero::common::core
{

// Verifies a range derives duration from its endpoints without introducing a new source of truth.
TEST_CASE("TimeRange duration is derived from endpoints", "[core][timeline]")
{
    const TimeRange range{
        .start = TimePosition{2.0},
        .end = TimePosition{7.5},
    };

    CHECK(range.duration() == TimeDuration{5.5});
}

// Verifies empty and inverted ranges report no usable duration.
TEST_CASE("TimeRange duration is non-negative", "[core][timeline]")
{
    const TimeRange empty_range{};
    const TimeRange inverted_range{
        .start = TimePosition{9.0},
        .end = TimePosition{4.0},
    };

    CHECK(empty_range.duration() == TimeDuration{});
    CHECK(inverted_range.duration() == TimeDuration{});
}

// Verifies endpoint containment is inclusive for editor hit-testing and cursor mapping.
TEST_CASE("TimeRange contains positions between endpoints", "[core][timeline]")
{
    const TimeRange range{
        .start = TimePosition{1.0},
        .end = TimePosition{4.0},
    };

    CHECK(range.contains(TimePosition{1.0}));
    CHECK(range.contains(TimePosition{2.5}));
    CHECK(range.contains(TimePosition{4.0}));
    CHECK_FALSE(range.contains(TimePosition{0.5}));
    CHECK_FALSE(range.contains(TimePosition{4.5}));
}

// Verifies seek-style clamping stays inside the range and handles zero-duration ranges.
TEST_CASE("TimeRange clamp keeps positions inside the range", "[core][timeline]")
{
    const TimeRange range{
        .start = TimePosition{3.0},
        .end = TimePosition{8.0},
    };

    CHECK(range.clamp(TimePosition{1.0}) == TimePosition{3.0});
    CHECK(range.clamp(TimePosition{5.0}) == TimePosition{5.0});
    CHECK(range.clamp(TimePosition{9.0}) == TimePosition{8.0});
    CHECK(TimeRange{}.clamp(TimePosition{9.0}) == TimePosition{});
}

// Verifies the time-to-x mapping spreads the visible range across the drawing's whole width: the
// start at x 0, the end at x == width (the right edge of the last column, so width / duration is
// the drawing's exact pixels per second), and times outside the range extrapolate past the edges.
TEST_CASE("The time-to-x mapping spans the drawing's whole width", "[core][timeline]")
{
    constexpr TimeRange visible{.start = TimePosition{10.0}, .end = TimePosition{14.0}};
    constexpr double width = 400.0;

    CHECK_THAT(timelineXForSeconds(10.0, visible, width), Catch::Matchers::WithinULP(0.0, 0));
    CHECK_THAT(timelineXForSeconds(11.25, visible, width), Catch::Matchers::WithinULP(125.0, 0));
    CHECK_THAT(timelineXForSeconds(14.0, visible, width), Catch::Matchers::WithinULP(400.0, 0));
    CHECK_THAT(timelineXForSeconds(9.0, visible, width), Catch::Matchers::WithinULP(-100.0, 0));
    CHECK_THAT(timelineXForSeconds(15.0, visible, width), Catch::Matchers::WithinULP(500.0, 0));
}

// Verifies the inverse returns every mapped x to the time it came from, edges and beyond included.
TEST_CASE("The time-to-x mapping's inverse returns each x to its time", "[core][timeline]")
{
    constexpr TimeRange visible{.start = TimePosition{10.0}, .end = TimePosition{14.0}};
    constexpr double width = 400.0;

    for (const double seconds : {9.0, 10.0, 11.25, 13.5, 14.0, 15.0})
    {
        CHECK_THAT(
            timelineSecondsForX(timelineXForSeconds(seconds, visible, width), visible, width),
            Catch::Matchers::WithinULP(seconds, 1));
    }
}

} // namespace rock_hero::common::core
