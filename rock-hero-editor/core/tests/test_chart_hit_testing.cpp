#include "chart/chart_hit_testing.h"
#include "chart/chart_selection.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Two overlapping sustains on string 1 (bottom lane) and a short note on string 2 whose head
// sits on top of the first sustain's tail span.
[[nodiscard]] common::core::ChartViewState makeTabState()
{
    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 10.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 5.0,
            .end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
        },
        common::core::NoteViewState{
            .start_seconds = 6.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 7,
            .bend = {},
            .slides = {},
        },
    };
    return state;
}

// 20-second window across 400x240: 20 px/s, 40px lanes; string 1 centers at y = 220, string 2
// at y = 180.
[[nodiscard]] common::ui::TabLaneGeometry makeGeometry(float width = 400.0f)
{
    return common::ui::makeTabLaneGeometry(
        0.0f,
        0.0f,
        width,
        240.0f,
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{20.0},
        },
        6,
        6);
}

} // namespace

// Heads win over tails, and overlapping candidates resolve to the nearest onset.
TEST_CASE("Chart hit testing resolves heads tails and overlaps", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    // The first note's head at (40, 220).
    CHECK(chartNoteHitIndex(tab, geometry, 40.0f, 220.0f) == std::size_t{0});

    // The second note's head at (100, 220) sits inside the first note's sustain span: the head
    // still wins over the tail.
    CHECK(chartNoteHitIndex(tab, geometry, 100.0f, 220.0f) == std::size_t{1});

    // Between the two onsets both tails overlap; the nearer onset (note 1 at x = 100) wins at
    // x = 130, note 0 keeps a point right after its own onset at x = 60.
    CHECK(chartNoteHitIndex(tab, geometry, 130.0f, 220.0f) == std::size_t{1});
    CHECK(chartNoteHitIndex(tab, geometry, 60.0f, 220.0f) == std::size_t{0});

    // Empty lane space and other lanes resolve to nothing.
    CHECK_FALSE(chartNoteHitIndex(tab, geometry, 300.0f, 220.0f).has_value());
    CHECK_FALSE(chartNoteHitIndex(tab, geometry, 40.0f, 100.0f).has_value());

    // The string-2 head at (120, 180) resolves on its own lane.
    CHECK(chartNoteHitIndex(tab, geometry, 120.0f, 180.0f) == std::size_t{2});
}

// Hit testing keeps resolving at zoom extremes: a crowded narrow lane and a sparse wide one.
TEST_CASE("Chart hit testing survives zoom extremes", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();

    // At 80px for 20 seconds (4 px/s) heads overlap heavily; the nearest onset still wins and
    // out-of-band points still miss.
    const common::ui::TabLaneGeometry narrow = makeGeometry(80.0f);
    CHECK(chartNoteHitIndex(tab, narrow, 8.0f, 220.0f) == std::size_t{0});
    CHECK_FALSE(chartNoteHitIndex(tab, narrow, 79.0f, 220.0f).has_value());

    // At 8000px for 20 seconds (400 px/s) the same probes stay exact.
    const common::ui::TabLaneGeometry wide = makeGeometry(8000.0f);
    CHECK(chartNoteHitIndex(tab, wide, 800.0f, 220.0f) == std::size_t{0});
    CHECK(chartNoteHitIndex(tab, wide, 2000.0f, 220.0f) == std::size_t{1});
    CHECK_FALSE(chartNoteHitIndex(tab, wide, 700.0f, 220.0f).has_value());
}

// Hit testing consumes the notes' PRESENTED tails, which is exactly the ink the paint core lays
// down: every drawn ribbon is clickable and nothing undrawn is. A chugged member of a strum a
// hand-shape span holds presents no tail — the 3D board pins its head for the posture instead —
// so this lane offers nothing along the string to click, however long that hold runs.
TEST_CASE("Chart hit testing follows the presented tails", "[core][chart]")
{
    common::core::ChartViewState tab;
    tab.string_count = 6;
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 2.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
        },
    };
    // The board's hold for that chug, to 8s (x = 160). Nothing below may spend it.
    tab.display_hold_ends = {8.0};
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    // The bare head is the whole affordance: every point out along the string hits nothing, right
    // through where the hold reaches.
    CHECK(chartNoteHitIndex(tab, geometry, 40.0f, 220.0f) == std::size_t{0});
    CHECK_FALSE(chartNoteHitIndex(tab, geometry, 130.0f, 220.0f).has_value());
    CHECK_FALSE(chartNoteHitIndex(tab, geometry, 155.0f, 220.0f).has_value());

    // Give the same note a presented tail to 8s and the drawn ribbon is clickable along its whole
    // length, stopping where the ink does.
    tab.notes[0].end_seconds = 8.0;
    CHECK(chartNoteHitIndex(tab, geometry, 130.0f, 220.0f) == std::size_t{0});
    CHECK(chartNoteHitIndex(tab, geometry, 155.0f, 220.0f) == std::size_t{0});
    CHECK_FALSE(chartNoteHitIndex(tab, geometry, 200.0f, 220.0f).has_value());
}

// The marquee query returns notes whose heads intersect the box, in ascending order.
TEST_CASE("Chart hit testing collects notes inside a marquee box", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const std::vector<std::size_t> both_strings =
        chartNoteIndicesInBox(tab, geometry, 20.0f, 160.0f, 130.0f, 240.0f);
    CHECK(both_strings == (std::vector<std::size_t>{0, 1, 2}));

    const std::vector<std::size_t> bottom_lane =
        chartNoteIndicesInBox(tab, geometry, 20.0f, 200.0f, 60.0f, 240.0f);
    CHECK(bottom_lane == std::vector<std::size_t>{0});

    const std::vector<std::size_t> empty =
        chartNoteIndicesInBox(tab, geometry, 300.0f, 0.0f, 380.0f, 240.0f);
    CHECK(empty.empty());
}

// Selection keys resolve back to projection indices through the sorted chart note stream, and
// keys whose notes vanished drop out instead of mismapping.
TEST_CASE("Chart selection resolves keys to projection indices", "[core][chart]")
{
    const std::vector<common::core::ChartNote> notes{
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 3,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_sustain,
            .bend = {},
            .slides = {},
        },
    };

    ChartSelection selection;
    selection.add(ChartNoteKey{.position = {.measure = 3, .beat = 1, .offset = {}}, .string = 1});
    selection.add(ChartNoteKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2});
    CHECK(selectedNoteIndices(notes, selection) == (std::vector<std::size_t>{1, 2}));

    // Toggling removes; toggling again restores.
    selection.toggle(
        ChartNoteKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2});
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{2});

    // A key with no matching note resolves to nothing.
    selection.add(ChartNoteKey{.position = {.measure = 9, .beat = 1, .offset = {}}, .string = 1});
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{2});

    // replaceWith collapses to one; clear empties.
    selection.replaceWith(
        ChartNoteKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1});
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{0});
    selection.clear();
    CHECK(selection.empty());
}

} // namespace rock_hero::editor::core
