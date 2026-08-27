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
    return ChartNoteHit{.index = index};
}

// One note slot as a selection key.
[[nodiscard]] ChartSelectionKey noteKey(const ChartSlotKey& slot)
{
    return ChartNoteKey{.slot = slot};
}

// One silently-held stop in a projection: no head, no tail, and a bracket instant naming where its
// face draws — the span's start, which is not in general the slot it was authored at.
[[nodiscard]] common::core::NoteViewState heldView(
    const double bracket_seconds, const int string, const double onset_seconds)
{
    common::core::NoteViewState note;
    note.start_seconds = onset_seconds;
    note.end_seconds = onset_seconds;
    note.string = string;
    note.attack = common::core::NoteAttack::None;
    note.bracket_seconds = bracket_seconds;
    return note;
}

// One of a note's waypoints as a hit target: two indices, because a waypoint belongs to a note
// rather than to a flat array — which is the whole reason the target is a sum.
[[nodiscard]] ChartHitTarget waypointTarget(
    const std::size_t note_index, const std::size_t waypoint_index)
{
    return ChartWaypointHit{.note_index = note_index, .waypoint_index = waypoint_index};
}

// One waypoint as a selection key: the note's slot plus the offset along its ring.
[[nodiscard]] ChartSelectionKey waypointKey(
    const ChartSlotKey& slot, const common::core::Fraction offset)
{
    return ChartWaypointKey{.note = slot, .offset = offset};
}

[[nodiscard]] ChartSlotKey slotAt(const int measure, const int string)
{
    return ChartSlotKey{
        .position = {.measure = measure, .beat = 1, .offset = {}}, .string = string
    };
}

// A glide on string 3 — a lane the fixture above leaves empty, so nothing else can answer a
// probe. The onset sits at 2s (x = 40), the junction it arrives at at 6s (x = 120), and the ring
// ends at 10s (x = 200) where a second waypoint sits exactly at the end and therefore draws no
// head at all.
[[nodiscard]] common::core::ChartViewState makeGlideTabState()
{
    common::core::ChartViewState state;
    state.string_count = 6;
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .end_seconds = 10.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides =
                {common::core::SlideViewState{
                     .seconds = 6.0, .fret = 9, .offset = common::core::Fraction{2}
                 },
                 common::core::SlideViewState{
                     .seconds = 10.0, .fret = 12, .offset = common::core::Fraction{4}
                 }},
            .vibrato = {},
        },
    };
    return state;
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

// A silently-held stop is reached through the posture BRACKET that states it, at exactly the
// extent that bracket is drawn at, and it wins over a head it overlaps even though the paint core
// draws it under one. The marquee collects it too, so a box over a chord takes the held member
// with the rest of the shape.
TEST_CASE("Chart hit testing resolves held stops at their brackets", "[core][chart]")
{
    common::core::ChartViewState tab = makeTabState();
    // On string 5 at 6s (x = 120, y = 60.5), a lane the fixture leaves empty so nothing else can
    // answer the probes.
    tab.notes.push_back(heldView(6.0, 5, 6.0));
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const ChartHitTarget held{ChartNoteHit{.index = 3}};
    CHECK(chartHitTarget(tab, geometry, 120.0f, 60.0f) == held);
    // The box is the BRACKET's, not a head's, and the two probes say so in both axes: the bracket
    // stands wider than the head it wraps (x = 134 is outside the head's 26 px box and inside the
    // bracket) and stops short of it vertically (y = 71 is inside the head and outside the
    // bracket). The drawn extent IS the clickable one.
    CHECK(chartHitTarget(tab, geometry, 134.0f, 60.0f) == held);
    CHECK_FALSE(chartHitTarget(tab, geometry, 120.0f, 71.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 145.0f, 60.0f).has_value());

    // A hold that resolved into no span has no bracket, so nothing reaches it — the projection
    // publishes no instant for it and the layout answers with nothing at all. Its own onset is
    // not a fallback: a silent hold draws no head there either.
    tab.notes.back() = heldView(6.0, 5, 6.0);
    tab.notes.back().bracket_seconds.reset();
    CHECK_FALSE(chartHitTarget(tab, geometry, 120.0f, 60.0f).has_value());
    // Same for the marquee, boxed over string 5's own lane so no note can answer either.
    CHECK(chartTargetsInBox(tab, geometry, 0.0f, 40.0f, 400.0f, 80.0f).empty());

    // A bracket never wraps a head of its OWN string — a held stop's string is silent at the span
    // start by construction — but it can overlap one a little later on that string. The paint core
    // draws the bracket UNDER that head, and the hold takes the overlap anyway: the bracket is its
    // only affordance, while the head keeps every column the bracket does not reach. Bracket at 4s
    // spans x 63..97 on string 1; the 5s head spans 86..114.
    tab.notes.back() = heldView(4.0, 1, 4.0);
    CHECK(chartHitTarget(tab, geometry, 90.0f, 220.0f) == held);
    CHECK(chartHitTarget(tab, geometry, 110.0f, 220.0f) == noteTarget(1));

    // The marquee collects heads and brackets together, heads first.
    tab.notes.back() = heldView(2.0, 3, 2.0);
    const std::vector<ChartHitTarget> boxed =
        chartTargetsInBox(tab, geometry, 20.0f, 120.0f, 130.0f, 240.0f);
    CHECK(
        boxed == (std::vector<ChartHitTarget>{noteTarget(0), noteTarget(1), noteTarget(2), held}));
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
    const ChartSelectionKey waypoint =
        ChartWaypointKey{.note = slot, .offset = common::core::Fraction{1, 2}};

    ChartSelection selection;
    selection.add(note);
    selection.add(waypoint);
    CHECK(selection.contains(note));
    CHECK(selection.contains(waypoint));
    CHECK(selection.notes() == std::vector<ChartSlotKey>{slot});
    CHECK(selection.waypoints().size() == 1);
    // Notes first, each kind in its own order: the verb window compares whole selections across
    // the kinds, so the flattened order is part of what it proves.
    CHECK(selection.keys() == (std::vector<ChartSelectionKey>{note, waypoint}));

    // Toggling one kind off leaves the other standing, which a slot-only identity could not do:
    // a waypoint SHARES its note's slot rather than excluding it.
    selection.toggle(note);
    CHECK_FALSE(selection.contains(note));
    CHECK(selection.contains(waypoint));
    CHECK_FALSE(selection.empty());

    // clear() is kind-agnostic: one selection editor-wide, emptied in one call.
    selection.clear();
    CHECK(selection.empty());
}

// A silently-held stop resolves through the SAME merge a sounding note does, because it is one:
// the note stream is the one slot-keyed array there is, and a key naming a note that is gone drops
// out instead of mismapping onto its neighbour.
TEST_CASE("Chart selection resolves held stops like any other note", "[core][chart]")
{
    common::core::ChartNote held = makeTestNote({.measure = 3, .beat = 1}, 4, 9);
    held.attack = common::core::NoteAttack::None;
    held.sustain = common::core::Fraction{};
    const std::vector<common::core::ChartNote> notes{
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        held,
    };

    ChartSelection selection;
    selection.add(noteKey(slotAt(2, 2)));
    selection.add(noteKey(slotAt(3, 4)));
    CHECK(selectedNoteIndices(notes, selection) == (std::vector<std::size_t>{1, 2}));

    // A key naming a slot nothing occupies is skipped rather than mismapped.
    selection.clear();
    selection.add(noteKey(slotAt(9, 4)));
    CHECK(selectedNoteIndices(notes, selection).empty());
}

// The double click's unit is the onset GROUP, and the group is the HAND's at that instant: a
// silently-held stop authored on the same onset is a member of the shape the strum takes, so it
// joins the notes with no rule of its own — one equal_range over one stream. Only that instant
// joins; a hold one onset later belongs to its own group.
TEST_CASE("Chart onset group keys collect every member of the instant", "[core][chart]")
{
    common::core::ChartNote near_hold = makeTestNote({.measure = 2, .beat = 1}, 3, 5);
    near_hold.attack = common::core::NoteAttack::None;
    near_hold.sustain = common::core::Fraction{};
    common::core::ChartNote far_hold = makeTestNote({.measure = 3, .beat = 1}, 4, 9);
    far_hold.attack = common::core::NoteAttack::None;
    far_hold.sustain = common::core::Fraction{};
    const std::vector<common::core::ChartNote> notes{
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        near_hold,
        makeTestNote({.measure = 3, .beat = 1}, 1, 7),
        far_hold,
    };

    CHECK(
        chartOnsetGroupKeys(notes, {.measure = 2, .beat = 1, .offset = {}}) ==
        (std::vector<ChartSelectionKey>{
            noteKey(slotAt(2, 1)), noteKey(slotAt(2, 2)), noteKey(slotAt(2, 3))
        }));

    // An onset holding only a hold is still a group, so double-clicking a lone bracket selects it.
    const std::vector<common::core::ChartNote> hold_only{far_hold};
    CHECK(
        chartOnsetGroupKeys(hold_only, {.measure = 3, .beat = 1, .offset = {}}) ==
        std::vector<ChartSelectionKey>{noteKey(slotAt(3, 4))});

    // An onset nothing sits on collects nothing.
    CHECK(chartOnsetGroupKeys(notes, {.measure = 9, .beat = 1, .offset = {}}).empty());
}

// A junction's head is drawn ON the tail, so it has to win over it or no waypoint would ever be
// clickable — and it loses to an onset head, which is the primary affordance. The drawn extent is
// the clickable one in both directions: a waypoint the lane draws no head for is not hit-testable
// at all.
TEST_CASE("Chart hit testing resolves linked waypoint heads", "[core][chart]")
{
    const common::core::ChartViewState tab = makeGlideTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    CHECK(chartHitTarget(tab, geometry, 120.0f, 140.0f) == waypointTarget(0, 0));
    // A pixel between the heads falls through to the tail, which is the note itself.
    CHECK(chartHitTarget(tab, geometry, 80.0f, 140.0f) == noteTarget(0));
    // The onset head wins its own pixels: heads resolve before junctions.
    CHECK(chartHitTarget(tab, geometry, 40.0f, 140.0f) == noteTarget(0));
    // At the ring's end the glide's arrival draws no head — a re-picked landing draws its own —
    // so a press inside the box that head WOULD have occupied resolves to the tail instead.
    CHECK(chartHitTarget(tab, geometry, 195.0f, 140.0f) == noteTarget(0));
    // Another string's lane answers nothing at the same instant.
    CHECK_FALSE(chartHitTarget(tab, geometry, 120.0f, 180.0f).has_value());
}

// The marquee reaches exactly what the click reaches, so a box drawn over a junction selects that
// junction — and the collection order is notes, then markers, then waypoints.
TEST_CASE("Chart hit testing collects waypoint heads inside a marquee box", "[core][chart]")
{
    const common::core::ChartViewState tab = makeGlideTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const std::vector<ChartHitTarget> junction =
        chartTargetsInBox(tab, geometry, 110.0f, 120.0f, 130.0f, 160.0f);
    CHECK(junction == std::vector<ChartHitTarget>{waypointTarget(0, 0)});

    // A box over the whole gesture takes the onset head and the junction — and NOT the arrival at
    // the ring's end, which draws no head to catch.
    const std::vector<ChartHitTarget> whole =
        chartTargetsInBox(tab, geometry, 20.0f, 120.0f, 220.0f, 160.0f);
    CHECK(whole == (std::vector<ChartHitTarget>{noteTarget(0), waypointTarget(0, 0)}));
}

// A waypoint's identity is (note slot, OFFSET), which is what makes the selection key a sum
// rather than a slot plus a kind: a note carries many waypoints, so the slot alone cannot name
// one. The offset and not an index — removing an earlier waypoint shifts every later index and
// moves no offset, so an index-keyed selection would silently point at a different waypoint after
// any edit that dropped one.
TEST_CASE("Chart selection keys a waypoint by its offset", "[core][chart]")
{
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{4});
    glide.waypoints = {
        common::core::Waypoint{.offset = common::core::Fraction{2}, .fret = 9},
        common::core::Waypoint{.offset = common::core::Fraction{4}, .fret = 12},
    };
    const std::vector<common::core::ChartNote> notes{glide};
    const common::core::ChartViewState tab = makeGlideTabState();
    const ChartSlotKey slot = slotAt(2, 3);

    ChartSelection selection;
    selection.add(waypointKey(slot, common::core::Fraction{4}));
    CHECK(
        selectedWaypointIndices(notes, tab.notes, selection) ==
        (std::vector<ChartWaypointRef>{ChartWaypointRef{.note_index = 0, .waypoint_index = 1}}));

    // The same key against a drawn list the earlier waypoint has left still names the SAME
    // waypoint, now at index 0. An index-keyed selection would have named the wrong one, or
    // nothing at all.
    common::core::ChartViewState trimmed = tab;
    trimmed.notes[0].slides.erase(trimmed.notes[0].slides.begin());
    CHECK(
        selectedWaypointIndices(notes, trimmed.notes, selection) ==
        (std::vector<ChartWaypointRef>{ChartWaypointRef{.note_index = 0, .waypoint_index = 0}}));

    // A key naming an offset no waypoint sits on resolves to nothing, exactly as a note key whose
    // note was deleted does — which is also what carries the dissolve law's linger.
    selection.clear();
    selection.add(waypointKey(slot, common::core::Fraction{3}));
    CHECK(selectedWaypointIndices(notes, tab.notes, selection).empty());

    // And so does a key whose NOTE is gone.
    selection.clear();
    selection.add(waypointKey(slotAt(9, 3), common::core::Fraction{2}));
    CHECK(selectedWaypointIndices(notes, tab.notes, selection).empty());
}

// Waypoints are the second alternative of one selection, not a second selection: the
// kind-agnostic mutations reach them, they publish after the slot-keyed notes, and a waypoint
// shares its note's slot without excluding the note — the pairing a slot-plus-kind key could not
// hold.
TEST_CASE("Chart selection carries waypoints beside the notes", "[core][chart]")
{
    const ChartSlotKey slot = slotAt(2, 1);
    const ChartSelectionKey note = noteKey(slot);
    const ChartSelectionKey waypoint = waypointKey(slot, common::core::Fraction{2});
    const ChartSelectionKey later = waypointKey(slot, common::core::Fraction{4});

    ChartSelection selection;
    selection.add(later);
    selection.add(waypoint);
    selection.add(note);
    CHECK(selection.contains(waypoint));
    CHECK(selection.contains(later));
    // Notes first, then waypoints, each kind in its own order — the flattened order the verb
    // window compares whole selections in.
    CHECK(selection.keys() == (std::vector<ChartSelectionKey>{note, waypoint, later}));
    CHECK(
        selection.waypoints() ==
        (std::vector<ChartWaypointKey>{
            ChartWaypointKey{.note = slot, .offset = common::core::Fraction{2}},
            ChartWaypointKey{.note = slot, .offset = common::core::Fraction{4}}
        }));

    // Toggling a waypoint off leaves the note on the same slot standing, which is the pairing the
    // sum exists for.
    selection.toggle(waypoint);
    CHECK_FALSE(selection.contains(waypoint));
    CHECK(selection.contains(note));
    CHECK_FALSE(selection.empty());

    // A waypoint occupies no slot, so nothing arms a caret for it: selecting one demotes the
    // marker to a cursor in place rather than putting it on the note the waypoint rides.
    CHECK_FALSE(chartCaretSlotFor(waypoint).has_value());
    CHECK(chartCaretSlotFor(note) == slot);

    selection.clear();
    CHECK(selection.empty());
}

} // namespace rock_hero::editor::core
