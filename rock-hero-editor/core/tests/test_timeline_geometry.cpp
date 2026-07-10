#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>

namespace rock_hero::editor::core
{

namespace
{

// A ten-second window drawn 101 pixels wide, so positions map onto the [0, 100] pixel span.
constexpr common::core::TimeRange g_visible_window{
    .start = common::core::TimePosition{10.0},
    .end = common::core::TimePosition{20.0},
};
constexpr int g_window_width = 101;

} // namespace

// Verifies interior positions map proportionally onto the [0, width - 1] span at start, mid, and end.
TEST_CASE("Timeline geometry maps interior positions proportionally", "[core][timeline]")
{
    const std::optional<float> start_x = timelineXForPosition(
        common::core::TimePosition{10.0},
        g_visible_window,
        g_window_width,
        TimelinePositionClamping::RejectOutsideVisibleRange);
    const std::optional<float> mid_x = timelineXForPosition(
        common::core::TimePosition{15.0},
        g_visible_window,
        g_window_width,
        TimelinePositionClamping::RejectOutsideVisibleRange);
    const std::optional<float> end_x = timelineXForPosition(
        common::core::TimePosition{20.0},
        g_visible_window,
        g_window_width,
        TimelinePositionClamping::RejectOutsideVisibleRange);

    REQUIRE(start_x.has_value());
    REQUIRE(mid_x.has_value());
    REQUIRE(end_x.has_value());
    if (start_x.has_value() && mid_x.has_value() && end_x.has_value())
    {
        CHECK(*start_x == Catch::Approx(0.0F));
        CHECK(*mid_x == Catch::Approx(50.0F));
        CHECK(*end_x == Catch::Approx(100.0F));
    }
}

// Verifies the reject policy returns no coordinate for positions before or after the window.
TEST_CASE("Timeline geometry rejects out-of-range positions when configured", "[core][timeline]")
{
    CHECK_FALSE(timelineXForPosition(
                    common::core::TimePosition{5.0},
                    g_visible_window,
                    g_window_width,
                    TimelinePositionClamping::RejectOutsideVisibleRange)
                    .has_value());
    CHECK_FALSE(timelineXForPosition(
                    common::core::TimePosition{25.0},
                    g_visible_window,
                    g_window_width,
                    TimelinePositionClamping::RejectOutsideVisibleRange)
                    .has_value());
}

// Verifies the clamp policy pins out-of-range positions to the first and last pixel.
TEST_CASE("Timeline geometry clamps out-of-range positions when configured", "[core][timeline]")
{
    const std::optional<float> before_x = timelineXForPosition(
        common::core::TimePosition{5.0},
        g_visible_window,
        g_window_width,
        TimelinePositionClamping::ClampToVisibleRange);
    const std::optional<float> after_x = timelineXForPosition(
        common::core::TimePosition{25.0},
        g_visible_window,
        g_window_width,
        TimelinePositionClamping::ClampToVisibleRange);

    REQUIRE(before_x.has_value());
    REQUIRE(after_x.has_value());
    if (before_x.has_value() && after_x.has_value())
    {
        CHECK(*before_x == Catch::Approx(0.0F));
        CHECK(*after_x == Catch::Approx(100.0F));
    }
}

// Verifies a non-positive width or an empty visible range yields no coordinate.
TEST_CASE("Timeline geometry rejects degenerate width or range", "[core][timeline]")
{
    CHECK_FALSE(timelineXForPosition(
                    common::core::TimePosition{15.0},
                    g_visible_window,
                    0,
                    TimelinePositionClamping::ClampToVisibleRange)
                    .has_value());
    CHECK_FALSE(timelineXForPosition(
                    common::core::TimePosition{15.0},
                    g_visible_window,
                    -5,
                    TimelinePositionClamping::ClampToVisibleRange)
                    .has_value());

    constexpr common::core::TimeRange empty_window{
        .start = common::core::TimePosition{10.0},
        .end = common::core::TimePosition{10.0},
    };
    CHECK_FALSE(timelineXForPosition(
                    common::core::TimePosition{10.0},
                    empty_window,
                    g_window_width,
                    TimelinePositionClamping::ClampToVisibleRange)
                    .has_value());
}

} // namespace rock_hero::editor::core
