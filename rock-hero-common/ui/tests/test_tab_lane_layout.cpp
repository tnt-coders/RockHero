#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <optional>
#include <ranges>
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
    // a pixel row to have a mirror row. Without the snap a lane height of 39.5 — the shipped
    // editor's — breaks it on all six lanes at once.
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
// maximum of RING ends — the furthest a note can be drawn — aligned with the note order, and the
// query consumes it. The first note's ink stops at 3s, so only its ring keeps it in the 5-6s
// window.
TEST_CASE("Shared sustain prefix and range queries work over tab notes", "[ui][tab-layout]")
{
    const std::vector<common::core::NoteViewState> notes{
        common::core::NoteViewState{
            .start_seconds = 1.0,
            .ring_end_seconds = 9.0,
            .ink_end_seconds = 3.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 2.5,
            .ink_end_seconds = 2.5,
            .string = 4,
            .fret = 7,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 12.5,
            .ink_end_seconds = 12.0,
            .string = 6,
            .fret = 0,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };

    const std::vector<double> prefix = common::core::makeSustainPrefixMax(
        notes | std::views::transform(&common::core::NoteViewState::ring_end_seconds));
    REQUIRE(prefix.size() == 3);
    CHECK_THAT(prefix[0], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[1], Catch::Matchers::WithinULP(9.0, 0));
    CHECK_THAT(prefix[2], Catch::Matchers::WithinULP(12.5, 0));

    // The visibility query consumes the table exactly like the paint core does.
    const auto [first, last] = common::core::visibleEventRange(notes, prefix, 5.0, 6.0);
    CHECK(first == 0);
    CHECK(last == 2);
}

// The manifest mirrors the paint core's HEAD geometry for hit testing, and publishes nothing for
// the tail: heads are targets, tails are testimony.
TEST_CASE("Tab note layout matches the painted head geometry", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();

    const common::core::NoteViewState sustained{
        .start_seconds = 5.0,
        .ring_end_seconds = 10.0,
        .ink_end_seconds = 10.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
        .keyframes = {},
        .vibrato = {},
    };
    const TabNoteLayout layout = tabNoteLayout(geometry, sustained);

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

    // The RIBBON's band is still the lane's own geometry — the painter reads it straight from
    // \ref tailSpan — but no rectangle here claims it, because no pointer resolves against it. A
    // point well along the tail belongs to no mark of this note at all.
    const TailSpan span = tailSpan(geometry, layout.center_y);
    CHECK(span.bottom > span.top);
    CHECK_FALSE(layout.head.contains(150.0f, 220.0f));

    // A note drawing no tail — its ink ends at its onset while its ring runs on — lays out exactly
    // like one that does: the case a chugged member of a strum under a hand-shape span is in. The
    // head is what addresses it either way, so nothing about the ring can make it unreachable or
    // make it claim pixels the lane never drew.
    const common::core::NoteViewState chug{
        .start_seconds = 5.0,
        .ring_end_seconds = 7.5,
        .ink_end_seconds = 5.0,
        .string = 1,
        .fret = 3,
        .bend = {},
        .slides = {},
        .keyframes = {},
        .vibrato = {},
    };
    const TabNoteLayout chug_layout = tabNoteLayout(geometry, chug);
    CHECK(chug_layout.head.contains(100.0f, 220.0f));
    CHECK_FALSE(chug_layout.head.contains(150.0f, 220.0f));
}

// THE HELD STOP'S SATELLITE, and the terms it is shown on (THE SATELLITE REVEAL). The column stands
// outboard of the head's own bracket columns at the instant the mark carries — for a note's own
// face that is its onset, so the satellite sits beside its head whether or not a bracket draws
// there — and a REVEAL-ONLY face lays out to nothing until the reveal brings it in.
TEST_CASE("A held stop's satellite lays out where its face is shown", "[ui][tab-layout]")
{
    const TabLaneGeometry geometry = makeReferenceGeometry();
    const auto tap = [](const common::core::StopMarkFace face) {
        common::core::NoteViewState note;
        note.start_seconds = 5.0;
        note.ring_end_seconds = 5.5;
        note.ink_end_seconds = 5.0;
        note.string = 1;
        note.fret = 12;
        note.attack = common::core::NoteAttack::Tap;
        note.stop_mark = common::core::StopMarkViewState{.seconds = 5.0, .fret = 5, .face = face};
        return note;
    };

    // A revealed face lays out outboard of the closing bar's column at the note's own onset
    // (x = 100), centred in the slot — the same columns the paint core draws the digit in.
    const std::optional<TabHeldStopLayout> shown =
        tabHeldStopLayout(geometry, tap(common::core::StopMarkFace::Revealed), true);
    REQUIRE(shown.has_value());
    if (shown.has_value())
    {
        const TabSatelliteSlot slot = geometry.satelliteSlot();
        const auto bar_right =
            static_cast<float>(geometry.bracketColumnsAt(100.0f, 220.5f).bar_right);
        CHECK(shown->box.x == Catch::Approx(bar_right));
        CHECK(shown->box.width == Catch::Approx(static_cast<float>(slot.extent())));
        CHECK(
            shown->center_x == Catch::Approx(bar_right + static_cast<float>(slot.extent()) / 2.0f));
        CHECK(shown->center_y == Catch::Approx(220.5f));
    }

    // A REVEAL-ONLY face is absent until the note's truth is on show, and present exactly then.
    CHECK_FALSE(
        tabHeldStopLayout(geometry, tap(common::core::StopMarkFace::Revealed), false).has_value());
    CHECK(tabHeldStopLayout(geometry, tap(common::core::StopMarkFace::Revealed), true).has_value());

    // THE DEFAULT wears the same reveal-only face, and its ZERO is the case worth naming: a tap
    // holding nothing resolves to the open string, so the column is a question about the FACE and
    // never about the number — a presence test that read the value would drop exactly this
    // satellite.
    common::core::NoteViewState defaulted = tap(common::core::StopMarkFace::Revealed);
    defaulted.stop_mark = common::core::StopMarkViewState{
        .seconds = 5.0, .fret = 0, .face = common::core::StopMarkFace::Revealed
    };
    CHECK_FALSE(tabHeldStopLayout(geometry, defaulted, false).has_value());
    CHECK(tabHeldStopLayout(geometry, defaulted, true).has_value());

    // And the reveal grants nothing to a note that states no held stop: the column is the STOP's.
    common::core::NoteViewState unheld = tap(common::core::StopMarkFace::Revealed);
    unheld.stop_mark.reset();
    CHECK_FALSE(tabHeldStopLayout(geometry, unheld, true).has_value());
}

} // namespace rock_hero::common::ui
