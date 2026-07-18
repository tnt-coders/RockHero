#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// A 20-second window across a 400px-wide, 240px-high six-lane row (40px lanes).
[[nodiscard]] TabLaneGeometry makeReferenceGeometry()
{
    return makeTabLaneGeometry(
        0.0f,
        0.0f,
        400.0f,
        240.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
}

} // namespace

// Lane height fixes the note height at Charter's 1.5 ratio, capped at the style's ceiling.
TEST_CASE("Tab lane geometry derives Charter's sizes from the lane height", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();
    CHECK(geometry.lane_height == Catch::Approx(40.0f));
    // 40 / 1.5 = 26.67 exceeds the 25px cap: full-size lanes render at exactly Charter's scale.
    CHECK(geometry.note_height == Catch::Approx(25.0f));
    // Tail height rounds to odd so the tail centers on the string line: 25 * 3/4 = 18.75 -> 19.
    CHECK(geometry.tail_height == Catch::Approx(19.0f));
    CHECK(geometry.draw_text);

    // A 120px row yields 20px lanes and proportional notes below the cap.
    const TabLaneGeometry small = makeTabLaneGeometry(
        0.0f,
        0.0f,
        200.0f,
        120.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
    CHECK(small.lane_height == Catch::Approx(20.0f));
    CHECK(small.note_height == Catch::Approx(20.0f / 1.5f));
    CHECK(small.draw_text);

    // Lanes too small for readable fret numbers drop to bare markers.
    const TabLaneGeometry tiny = makeTabLaneGeometry(
        0.0f,
        0.0f,
        200.0f,
        48.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
    CHECK_FALSE(tiny.draw_text);

    // The style scale knob raises the ceiling; the default reproduces the editor lane.
    const TabLaneGeometry scaled = makeTabLaneGeometry(
        0.0f,
        0.0f,
        400.0f,
        240.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6,
        TabLaneStyle{.max_note_height = 50.0f});
    CHECK(scaled.note_height == Catch::Approx(240.0f / 6.0f / 1.5f));
}

// The geometry maps time linearly across the width and stacks lanes in tablature orientation.
TEST_CASE("Tab lane geometry maps time and strings to pixels", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();
    CHECK(geometry.x(0.0) == Catch::Approx(0.0f));
    CHECK(geometry.x(10.0) == Catch::Approx(200.0f));
    CHECK(geometry.x(20.0) == Catch::Approx(400.0f));

    // Highest string on top, lowest at the bottom, centers 40px apart.
    CHECK(geometry.laneY(6) == Catch::Approx(20.0f));
    CHECK(geometry.laneY(1) == Catch::Approx(220.0f));

    // Extra user lanes below the chart push chart strings upward.
    const TabLaneGeometry extra = makeTabLaneGeometry(
        0.0f,
        0.0f,
        400.0f,
        320.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        8,
        6);
    CHECK(extra.extra_lanes == 2);
    // Chart string 1 renders on displayed lane 3 of 8: 40px lanes, center 220 from the top.
    CHECK(extra.laneY(1) == Catch::Approx(220.0f));

    // The float lane-center helper matches the geometry's own stacking.
    CHECK(tabLaneCenterY(6, 6, 0.0f, 240.0f) == Catch::Approx(20.0f));
    CHECK(tabLaneCenterY(1, 6, 0.0f, 240.0f) == Catch::Approx(220.0f));
}

// The prefix table is the running maximum of sustain ends, aligned with the note order.
TEST_CASE("Tab prefix table tracks the running sustain maximum", "[ui][tab-layout]")
{
    const std::vector<common::core::TabNoteView> notes{
        common::core::TabNoteView{
            .start_seconds = 1.0,
            .end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 2.0,
            .end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        common::core::TabNoteView{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
    };

    const std::vector<double> prefix = tabPrefixMaxEndSeconds(notes);
    REQUIRE(prefix.size() == 3);
    CHECK_THAT(prefix[0], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[1], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[2], Catch::Matchers::WithinULP(12.0, 0));

    // The visibility query consumes the table exactly like the paint core does.
    const auto [first, last] = tabVisibleNoteRange(notes, prefix, 5.0, 6.0);
    CHECK(first == 0);
    CHECK(last == 2);
}

// The manifest mirrors the paint core's head and tail geometry for hit testing.
TEST_CASE("Tab note layout matches the painted head and tail geometry", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();

    const common::core::TabNoteView sustained{
        .start_seconds = 5.0,
        .end_seconds = 10.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
    };
    const TabNoteLayout layout = tabNoteLayout(geometry, sustained);

    // Onset at 5s across 20s of 400px is x = 100; string 1 is the bottom lane center, 220.
    CHECK(layout.onset_x == Catch::Approx(100.0f));
    CHECK(layout.center_y == Catch::Approx(220.0f));

    // Heads render one pixel larger than the note height, centered on the anchor.
    CHECK(layout.head_size == Catch::Approx(26.0f));
    CHECK(layout.head.x == Catch::Approx(87.0f));
    CHECK(layout.head.y == Catch::Approx(207.0f));
    CHECK(layout.head.width == Catch::Approx(26.0f));
    CHECK(layout.head.height == Catch::Approx(26.0f));
    CHECK(layout.head.contains(100.0f, 220.0f));
    CHECK_FALSE(layout.head.contains(100.0f, 240.0f));

    // The tail spans onset to sustain end across Charter's tail top/bottom.
    const TailSpan span = tailSpan(geometry, layout.center_y);
    CHECK(layout.tail.x == Catch::Approx(100.0f));
    CHECK(layout.tail.width == Catch::Approx(100.0f));
    CHECK(layout.tail.y == Catch::Approx(span.top));
    CHECK(layout.tail.height == Catch::Approx(span.bottom - span.top));
    CHECK(layout.tail.contains(150.0f, 220.0f));

    // A note without a sustain has an empty tail rectangle that contains nothing.
    const common::core::TabNoteView plain{
        .start_seconds = 5.0,
        .end_seconds = 5.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
    };
    const TabNoteLayout plain_layout = tabNoteLayout(geometry, plain);
    CHECK_THAT(plain_layout.tail.width, Catch::Matchers::WithinULP(0.0f, 0));
    CHECK_FALSE(plain_layout.tail.contains(100.0f, 220.0f));
}

} // namespace rock_hero::common::ui
