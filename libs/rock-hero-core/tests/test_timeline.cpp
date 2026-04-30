#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/timeline.h>

namespace rock_hero::core
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

} // namespace rock_hero::core
