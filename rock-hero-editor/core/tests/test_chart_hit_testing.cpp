#include "chart/chart_hit_testing.h"
#include "chart/chart_selection.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
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
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 5.0,
            .end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 6.0,
            .end_seconds = 6.0,
            .string = 2,
            .fret = 7,
            .bend = {},
            .slides = {},
            .vibrato = {},
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

// The projected note at one index, as a hit target — the kind every note assertion below means.
[[nodiscard]] ChartHitTarget noteTarget(const std::size_t index)
{
    return ChartHitTarget{.kind = ChartSelectableKind::Note, .index = index};
}

// One note slot as a selection key: the selection unit spans both authored arrays now.
[[nodiscard]] ChartSelectionKey noteKey(const ChartSlotKey& slot)
{
    return ChartSelectionKey{.kind = ChartSelectableKind::Note, .slot = slot};
}

// The same slot read as the other kind — the pair below is what proves the kind is part of the
// identity rather than a label the answer carries.
[[nodiscard]] ChartSelectionKey markerKey(const ChartSlotKey& slot)
{
    return ChartSelectionKey{.kind = ChartSelectableKind::HoldMarker, .slot = slot};
}

[[nodiscard]] ChartSlotKey slotAt(const int measure, const int string)
{
    return ChartSlotKey{
        .position = {.measure = measure, .beat = 1, .offset = {}}, .string = string
    };
}

} // namespace

// Heads win over tails, and overlapping candidates resolve to the nearest onset.
TEST_CASE("Chart hit testing resolves heads tails and overlaps", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    // The first note's head at (40, 220).
    CHECK(chartHitTarget(tab, geometry, 40.0f, 220.0f) == noteTarget(0));

    // The second note's head at (100, 220) sits inside the first note's sustain span: the head
    // still wins over the tail.
    CHECK(chartHitTarget(tab, geometry, 100.0f, 220.0f) == noteTarget(1));

    // Between the two onsets both tails overlap; the nearer onset (note 1 at x = 100) wins at
    // x = 130, note 0 keeps a point right after its own onset at x = 60.
    CHECK(chartHitTarget(tab, geometry, 130.0f, 220.0f) == noteTarget(1));
    CHECK(chartHitTarget(tab, geometry, 60.0f, 220.0f) == noteTarget(0));

    // Empty lane space and other lanes resolve to nothing.
    CHECK_FALSE(chartHitTarget(tab, geometry, 300.0f, 220.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 40.0f, 100.0f).has_value());

    // The string-2 head at (120, 180) resolves on its own lane.
    CHECK(chartHitTarget(tab, geometry, 120.0f, 180.0f) == noteTarget(2));
}

// Hit testing keeps resolving at zoom extremes: a crowded narrow lane and a sparse wide one.
TEST_CASE("Chart hit testing survives zoom extremes", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();

    // At 80px for 20 seconds (4 px/s) heads overlap heavily; the nearest onset still wins and
    // out-of-band points still miss.
    const common::ui::TabLaneGeometry narrow = makeGeometry(80.0f);
    CHECK(chartHitTarget(tab, narrow, 8.0f, 220.0f) == noteTarget(0));
    CHECK_FALSE(chartHitTarget(tab, narrow, 79.0f, 220.0f).has_value());

    // At 8000px for 20 seconds (400 px/s) the same probes stay exact.
    const common::ui::TabLaneGeometry wide = makeGeometry(8000.0f);
    CHECK(chartHitTarget(tab, wide, 800.0f, 220.0f) == noteTarget(0));
    CHECK(chartHitTarget(tab, wide, 2000.0f, 220.0f) == noteTarget(1));
    CHECK_FALSE(chartHitTarget(tab, wide, 700.0f, 220.0f).has_value());
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
            .vibrato = {},
        },
    };
    // The board's hold for that chug, to 8s (x = 160). Nothing below may spend it.
    tab.display_hold_ends = {8.0};
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    // The bare head is the whole affordance: every point out along the string hits nothing, right
    // through where the hold reaches.
    CHECK(chartHitTarget(tab, geometry, 40.0f, 220.0f) == noteTarget(0));
    CHECK_FALSE(chartHitTarget(tab, geometry, 130.0f, 220.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 155.0f, 220.0f).has_value());

    // Give the same note a presented tail to 8s and the drawn ribbon is clickable along its whole
    // length, stopping where the ink does.
    tab.notes[0].end_seconds = 8.0;
    CHECK(chartHitTarget(tab, geometry, 130.0f, 220.0f) == noteTarget(0));
    CHECK(chartHitTarget(tab, geometry, 155.0f, 220.0f) == noteTarget(0));
    CHECK_FALSE(chartHitTarget(tab, geometry, 200.0f, 220.0f).has_value());
}

// The marquee query returns notes whose heads intersect the box, in ascending order.
TEST_CASE("Chart hit testing collects notes inside a marquee box", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const std::vector<ChartHitTarget> both_strings =
        chartTargetsInBox(tab, geometry, 20.0f, 160.0f, 130.0f, 240.0f);
    CHECK(
        both_strings == (std::vector<ChartHitTarget>{noteTarget(0), noteTarget(1), noteTarget(2)}));

    const std::vector<ChartHitTarget> bottom_lane =
        chartTargetsInBox(tab, geometry, 20.0f, 200.0f, 60.0f, 240.0f);
    CHECK(bottom_lane == std::vector<ChartHitTarget>{noteTarget(0)});

    const std::vector<ChartHitTarget> empty =
        chartTargetsInBox(tab, geometry, 300.0f, 0.0f, 380.0f, 240.0f);
    CHECK(empty.empty());
}

// A hold marker's own mark is hit-testable at exactly the extent it is drawn at, and it wins over
// the notation under it because the editor draws it on top. The marquee collects it too, so a box
// over a chord takes the silently-held member with the rest of the shape.
TEST_CASE("Chart hit testing resolves hold marker marks", "[core][chart]")
{
    common::core::ChartViewState tab = makeTabState();
    // On string 5 at 6s (x = 120, y = 60), a lane the fixture leaves empty so nothing else can
    // answer the probes.
    tab.hold_markers = {common::core::HoldMarkerViewState{.seconds = 6.0, .string = 5}};
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const ChartHitTarget marker{.kind = ChartSelectableKind::HoldMarker, .index = 0};
    CHECK(chartHitTarget(tab, geometry, 120.0f, 60.0f) == marker);
    // The mark is smaller than a head, so a point a head-width away misses it entirely — the
    // drawn extent IS the clickable one.
    CHECK_FALSE(chartHitTarget(tab, geometry, 120.0f, 80.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 145.0f, 60.0f).has_value());

    // A mark sitting ON a note's head takes the click: topmost drawn wins.
    tab.hold_markers = {common::core::HoldMarkerViewState{.seconds = 2.0, .string = 1}};
    CHECK(chartHitTarget(tab, geometry, 40.0f, 220.0f) == marker);

    // The marquee collects notes and marks together, notes first.
    tab.hold_markers = {common::core::HoldMarkerViewState{.seconds = 2.0, .string = 3}};
    const std::vector<ChartHitTarget> boxed =
        chartTargetsInBox(tab, geometry, 20.0f, 120.0f, 130.0f, 240.0f);
    CHECK(
        boxed ==
        (std::vector<ChartHitTarget>{noteTarget(0), noteTarget(1), noteTarget(2), marker}));
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
            .bend = 0.0,
            .waypoints = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .bend = 0.0,
            .waypoints = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_sustain,
            .bend = 0.0,
            .waypoints = {},
        },
    };

    ChartSelection selection;
    selection.add(
        noteKey(ChartSlotKey{.position = {.measure = 3, .beat = 1, .offset = {}}, .string = 1}));
    selection.add(
        noteKey(ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2}));
    CHECK(selectedNoteIndices(notes, selection) == (std::vector<std::size_t>{1, 2}));

    // Toggling removes; toggling again restores.
    selection.toggle(
        noteKey(ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 2}));
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{2});

    // A key with no matching note resolves to nothing.
    selection.add(
        noteKey(ChartSlotKey{.position = {.measure = 9, .beat = 1, .offset = {}}, .string = 1}));
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{2});

    // replaceWith collapses to one; clear empties.
    selection.replaceWith(
        noteKey(ChartSlotKey{.position = {.measure = 2, .beat = 1, .offset = {}}, .string = 1}));
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{0});
    selection.clear();
    CHECK(selection.empty());
}

// The selection unit is (kind, slot) and not the slot alone. Chart validation keeps a note and a
// marker off the same slot, so this pairing never reaches the selection from a valid chart — it is
// asserted anyway because the selection answers the question BEFORE any chart is consulted, and
// because the identity has to survive a selectable that is not slot-unique at all (a note's own
// waypoint, sharing the slot space with its note). A slot-only key would silently make one of
// these two objects unselectable.
TEST_CASE("Chart selection keys separate the kinds sharing one slot", "[core][chart]")
{
    const ChartSlotKey slot = slotAt(2, 1);
    const ChartSelectionKey note = noteKey(slot);
    const ChartSelectionKey marker = markerKey(slot);

    ChartSelection selection;
    selection.add(note);
    selection.add(marker);
    CHECK(selection.contains(note));
    CHECK(selection.contains(marker));
    CHECK(selection.notes() == std::vector<ChartSlotKey>{slot});
    CHECK(selection.holdMarkers() == std::vector<ChartSlotKey>{slot});
    // Notes first, each kind in slot order: the verb window compares whole selections across the
    // kinds, so the flattened order is part of what it proves.
    CHECK(selection.keys() == (std::vector<ChartSelectionKey>{note, marker}));

    // Toggling one kind off leaves the other standing, which a slot-only identity could not do.
    selection.toggle(note);
    CHECK_FALSE(selection.contains(note));
    CHECK(selection.contains(marker));
    CHECK_FALSE(selection.empty());

    // clear() is kind-agnostic: one selection editor-wide, emptied in one call.
    selection.clear();
    CHECK(selection.empty());
}

// Each kind's keys resolve against ITS OWN array through the same merge, so one mixed selection
// hands every verb a per-array operand rather than a filtered mixed one — and a key naming a
// record that is gone drops out instead of mismapping onto its neighbour.
TEST_CASE("Chart selection resolves hold marker keys to their own array", "[core][chart]")
{
    const std::vector<common::core::ChartNote> notes{
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
    };
    const std::vector<common::core::ChartHoldMarker> markers{
        common::core::ChartHoldMarker{
            .position = {.measure = 2, .beat = 1, .offset = {}}, .string = 3, .fret = std::nullopt
        },
        common::core::ChartHoldMarker{
            .position = {.measure = 3, .beat = 1, .offset = {}}, .string = 4, .fret = 9
        },
    };

    ChartSelection selection;
    selection.add(noteKey(slotAt(2, 2)));
    selection.add(markerKey(slotAt(3, 4)));
    CHECK(selectedNoteIndices(notes, selection) == std::vector<std::size_t>{1});
    CHECK(selectedHoldMarkerIndices(markers, selection) == std::vector<std::size_t>{1});

    // The note's own slot resolves to nothing in the marker array and vice versa: neither
    // resolution can reach across the kinds, which is what keeps the arrays' indices honest.
    selection.clear();
    selection.add(markerKey(slotAt(2, 2)));
    selection.add(noteKey(slotAt(3, 4)));
    CHECK(selectedNoteIndices(notes, selection).empty());
    CHECK(selectedHoldMarkerIndices(markers, selection).empty());

    // A marker key naming a marker that is gone is skipped, exactly like a stale note key.
    selection.clear();
    selection.add(markerKey(slotAt(2, 3)));
    selection.add(markerKey(slotAt(9, 4)));
    CHECK(selectedHoldMarkerIndices(markers, selection) == std::vector<std::size_t>{0});
}

// The double click's unit is the onset GROUP, and the group is the HAND's at that instant: a
// silently-held stop authored on the same onset is a member of the shape the strum takes, so it
// joins the notes rather than being left out of the verbs the group then feeds. Only that instant
// joins — a marker one onset later belongs to its own group.
TEST_CASE("Chart onset group keys collect both authored arrays", "[core][chart]")
{
    const std::vector<common::core::ChartNote> notes{
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        makeTestNote({.measure = 3, .beat = 1}, 1, 7),
    };
    const std::vector<common::core::ChartHoldMarker> markers{
        common::core::ChartHoldMarker{
            .position = {.measure = 2, .beat = 1, .offset = {}}, .string = 3, .fret = std::nullopt
        },
        common::core::ChartHoldMarker{
            .position = {.measure = 3, .beat = 1, .offset = {}}, .string = 4, .fret = 9
        },
    };

    CHECK(
        chartOnsetGroupKeys(notes, markers, {.measure = 2, .beat = 1, .offset = {}}) ==
        (std::vector<ChartSelectionKey>{
            noteKey(slotAt(2, 1)), noteKey(slotAt(2, 2)), markerKey(slotAt(2, 3))
        }));

    // An onset holding only a marker is still a group, so double-clicking a lone mark selects it.
    const std::vector<common::core::ChartNote> no_notes;
    CHECK(
        chartOnsetGroupKeys(no_notes, markers, {.measure = 3, .beat = 1, .offset = {}}) ==
        std::vector<ChartSelectionKey>{markerKey(slotAt(3, 4))});

    // An onset nothing sits on collects nothing.
    CHECK(chartOnsetGroupKeys(notes, markers, {.measure = 9, .beat = 1, .offset = {}}).empty());
}

} // namespace rock_hero::editor::core
