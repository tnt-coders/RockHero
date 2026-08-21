#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <rock_hero/common/core/shared/visible_events.h>
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

    // Highest string on top, lowest at the bottom, centers 40px apart — and each landing on a
    // pixel ROW CENTRE, hence the .5. A row spans [N, N+1], so a row is the mirror of another only
    // when 2 * center_y is whole; without the snap the shipped 39.5 px lane put every centre on a
    // quarter boundary and the tail's two rails rasterised to different row counts.
    CHECK(geometry.laneY(6) == Catch::Approx(20.5f));
    CHECK(geometry.laneY(1) == Catch::Approx(220.5f));

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
    // Chart string 1 renders on displayed lane 3 of 8: 40px lanes, center 220 from the top,
    // snapped to that row's centre.
    CHECK(extra.laneY(1) == Catch::Approx(220.5f));

    // The float lane-center helper matches the geometry's own stacking.
    CHECK(tabLaneCenterY(6, 6, 0.0f, 240.0f) == Catch::Approx(20.5f));
    CHECK(tabLaneCenterY(1, 6, 0.0f, 240.0f) == Catch::Approx(220.5f));

    // The snap's whole point, stated as the property rather than as sample values: 2 * center_y is
    // a whole number on EVERY lane, at every height and count, which is exactly the condition for
    // a pixel row to have a mirror row. This is what a lane height of 39.5 (the shipped editor's)
    // used to break on all six lanes at once.
    for (const float height : {240.0f, 237.0f, 235.0f, 238.0f, 246.0f})
    {
        for (int count = 1; count <= 8; ++count)
        {
            for (int lane = 1; lane <= count; ++lane)
            {
                CAPTURE(height, count, lane);
                const float doubled = 2.0f * tabLaneCenterY(lane, count, 0.0f, height);
                CHECK(doubled == Catch::Approx(std::floor(doubled)));
            }
        }
    }
}

// The tab surface reads the one shared visible-range search: the prefix table is the running
// maximum of sustain ends, aligned with the note order, and the query consumes it.
TEST_CASE("Shared sustain prefix and range queries work over tab notes", "[ui][tab-layout]")
{
    const std::vector<common::core::NoteViewState> notes{
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .end_seconds = 9.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
        },
    };

    const std::vector<double> prefix = common::core::makeSustainPrefixMax(notes);
    REQUIRE(prefix.size() == 3);
    CHECK_THAT(prefix[0], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[1], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[2], Catch::Matchers::WithinULP(12.0, 0));

    // The visibility query consumes the table exactly like the paint core does.
    const auto [first, last] = common::core::visibleEventRange(notes, prefix, 5.0, 6.0);
    CHECK(first == 0);
    CHECK(last == 2);
}

// The manifest mirrors the paint core's head and tail geometry for hit testing.
TEST_CASE("Tab note layout matches the painted head and tail geometry", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();

    const common::core::NoteViewState sustained{
        .start_seconds = 5.0,
        .end_seconds = 10.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
    };
    const TabNoteLayout layout = tabNoteLayout(geometry, sustained, sustained.end_seconds);

    // Onset at 5s across 20s of 400px is x = 100; string 1 is the bottom lane center, 220, on that
    // row's centre.
    CHECK(layout.onset_x == Catch::Approx(100.0f));
    CHECK(layout.center_y == Catch::Approx(220.5f));

    // Heads render one pixel larger than the note height, centered on the anchor.
    CHECK(layout.head_size == Catch::Approx(26.0f));
    CHECK(layout.head.x == Catch::Approx(87.0f));
    CHECK(layout.head.y == Catch::Approx(207.5f));
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
    const common::core::NoteViewState plain{
        .start_seconds = 5.0,
        .end_seconds = 5.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
    };
    const TabNoteLayout plain_layout = tabNoteLayout(geometry, plain, plain.end_seconds);
    CHECK_THAT(plain_layout.tail.width, Catch::Matchers::WithinULP(0.0f, 0));
    CHECK_FALSE(plain_layout.tail.contains(100.0f, 220.0f));

    // A span-held strum member stores no sustain but is DRAWN to its display hold end, so the tail
    // rectangle follows the ribbon rather than the stored value — the divergence that made a drawn
    // ribbon unclickable.
    const TabNoteLayout held_layout = tabNoteLayout(geometry, plain, 10.0);
    CHECK(held_layout.tail.width == Catch::Approx(100.0f));
    CHECK(held_layout.tail.contains(150.0f, 220.0f));
}

} // namespace rock_hero::common::ui
