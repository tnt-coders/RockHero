#include "chart/chart_hit_testing.h"
#include "chart/chart_selection.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/testing/view_state_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/editor/core/chart/chart_reveal.h>
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
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 10.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 5.0,
            .ring_end_seconds = 8.0,
            .ink_end_seconds = 8.0,
            .string = 1,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 6.0,
            .ring_end_seconds = 6.5,
            .ink_end_seconds = 6.0,
            .string = 2,
            .fret = 7,
            .bend = {},
            .slides = {},
            .keyframes = {},
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

// One of a note's keyframes as a hit target: two indices, because a keyframe belongs to a note
// rather than to a flat array — which is the whole reason the target is a sum.
[[nodiscard]] ChartHitTarget keyframeTarget(
    const std::size_t note_index, const std::size_t keyframe_index)
{
    return ChartKeyframeHit{.note_index = note_index, .keyframe_index = keyframe_index};
}

// The chip printing one note's onset bend, a face of that note.
[[nodiscard]] ChartHitTarget noteBendChipTarget(const std::size_t index)
{
    return ChartBendChipHit{.owner = ChartNoteHit{.index = index}};
}

// The chip printing one keyframe's bend, a face of that keyframe.
[[nodiscard]] ChartHitTarget keyframeBendChipTarget(
    const std::size_t note_index, const std::size_t keyframe_index)
{
    return ChartBendChipHit{
        .owner = ChartKeyframeHit{.note_index = note_index, .keyframe_index = keyframe_index}
    };
}

// One keyframe as a selection key: the note's slot plus the offset along its ring.
[[nodiscard]] ChartSelectionKey keyframeKey(
    const ChartSlotKey& slot, const common::core::Fraction offset)
{
    return ChartKeyframeKey{.note = slot, .offset = offset};
}

// The reveal while the lane reveal is held: every note's whole truth is on show, every note in
// front.
[[nodiscard]] common::ui::TabPresence revealEverything()
{
    return [](std::size_t) { return common::ui::TabNotePresence{.reveal = 1.0f, .recede = 0.0f}; };
}

// The reveal a selection makes: one note's whole truth on show, and every other note cropped.
[[nodiscard]] common::ui::TabPresence revealOnly(const std::size_t revealed_index)
{
    return [revealed_index](const std::size_t index) {
        return common::ui::TabNotePresence{
            .reveal = index == revealed_index ? 1.0f : 0.0f,
            .recede = 0.0f,
        };
    };
}

[[nodiscard]] ChartSlotKey slotAt(const int measure, const int string)
{
    return ChartSlotKey{
        .position = {.measure = measure, .beat = 1, .offset = {}}, .string = string
    };
}

// A glide on string 3 — a lane the fixture above leaves empty, so nothing else can answer a
// probe. The onset sits at 2s (x = 40), the junction it arrives at at 6s (x = 120), and the ring
// ends at 10s (x = 200) where a second keyframe sits exactly at the end — inside the ink, which
// runs the whole ring, so it draws its head like any other.
[[nodiscard]] common::core::ChartViewState makeGlideTabState()
{
    common::core::ChartViewState state;
    state.open_strings = common::core::testing::standardTuning();
    state.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 10.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides =
                {common::core::SlideStopViewState{.seconds = 6.0, .fret = 9},
                 common::core::SlideStopViewState{.seconds = 10.0, .fret = 12}},
            // Each at the offset the chart stores it at, which is what a selection keys it by.
            .keyframes =
                {common::core::KeyframeViewState{
                     .seconds = 6.0,
                     .offset = common::core::Fraction{2},
                     .mark = common::core::KeyframeStopMark{.stop = 0},
                     .bend_point = std::nullopt,
                 },
                 common::core::KeyframeViewState{
                     .seconds = 10.0,
                     .offset = common::core::Fraction{4},
                     .mark = common::core::KeyframeStopMark{.stop = 1},
                     .bend_point = std::nullopt,
                 }},
            .vibrato = {},
        },
    };
    return state;
}

} // namespace

// HEADS ARE TARGETS; TAILS ARE TESTIMONY. Overlapping heads resolve to the nearest onset, and a
// point on a ribbon resolves to nothing at all.
TEST_CASE("Chart hit testing resolves heads and overlaps, never tails", "[core][chart]")
{
    const common::core::ChartViewState tab = makeTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    // The first note's head at (40, 220).
    CHECK(chartHitTarget(tab, geometry, 40.0f, 220.0f) == noteTarget(0));

    // The second note's head at (100, 220) sits inside the first note's sustain span: the head is
    // still the target there, because a head is the only thing that ever is.
    CHECK(chartHitTarget(tab, geometry, 100.0f, 220.0f) == noteTarget(1));

    // Between the two onsets both ribbons run, and neither is a target: the click belongs to the
    // slot under the pointer, not to a note whose onset is somewhere else. A point right after an
    // onset is the same answer — it is past the head's own box.
    CHECK_FALSE(chartHitTarget(tab, geometry, 130.0f, 220.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 60.0f, 220.0f).has_value());

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

// A RING CHANGES NOTHING about what a note is addressed by. The chug with a span-held board hold
// and the same note with a four-second drawn ribbon offer exactly the same target: the head. That
// is the whole point of a tail not being a target: the affordance does not move with a length
// nobody clicked for.
TEST_CASE("Chart hit testing offers the head whatever the ring does", "[core][chart]")
{
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 2.5,
            .ink_end_seconds = 2.0,
            .string = 1,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
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

    // Give the same note a drawn tail to 8s and nothing changes: the ribbon is testimony, so
    // every point along it still belongs to the slot under the pointer.
    tab.notes[0].ring_end_seconds = 8.0;
    tab.notes[0].ink_end_seconds = 8.0;
    CHECK(chartHitTarget(tab, geometry, 40.0f, 220.0f) == noteTarget(0));
    CHECK_FALSE(chartHitTarget(tab, geometry, 130.0f, 220.0f).has_value());
    CHECK_FALSE(chartHitTarget(tab, geometry, 155.0f, 220.0f).has_value());
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
            .keyframes = {},
        },
        common::core::ChartNote{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .string = 2,
            .fret = 5,
            .sustain = g_fixture_sustain,
            .bend = 0.0,
            .keyframes = {},
        },
        common::core::ChartNote{
            .position = {.measure = 3, .beat = 1, .offset = {}},
            .string = 1,
            .fret = 7,
            .sustain = g_fixture_sustain,
            .bend = 0.0,
            .keyframes = {},
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

// The selection unit is the kind's own key and not the slot alone: a note's keyframe shares the
// slot space with its note, so the identity has to survive a selectable that is not slot-unique. A
// slot-only key would silently make one of these two objects unselectable.
TEST_CASE("Chart selection keys separate the kinds sharing one slot", "[core][chart]")
{
    const ChartSlotKey slot = slotAt(2, 1);
    const ChartSelectionKey note = noteKey(slot);
    const ChartSelectionKey keyframe =
        ChartKeyframeKey{.note = slot, .offset = common::core::Fraction{1, 2}};

    ChartSelection selection;
    selection.add(note);
    selection.add(keyframe);
    CHECK(selection.contains(note));
    CHECK(selection.contains(keyframe));
    CHECK(selection.notes() == std::vector<ChartSlotKey>{slot});
    CHECK(selection.keyframes().size() == 1);
    // Notes first, each kind in its own order: the verb window compares whole selections across
    // the kinds, so the flattened order is part of what it proves.
    CHECK(selection.keys() == (std::vector<ChartSelectionKey>{note, keyframe}));

    // Toggling one kind off leaves the other standing, which a slot-only identity could not do:
    // a keyframe SHARES its note's slot rather than excluding it.
    selection.toggle(note);
    CHECK_FALSE(selection.contains(note));
    CHECK(selection.contains(keyframe));
    CHECK_FALSE(selection.empty());

    // clear() is kind-agnostic: one selection editor-wide, emptied in one call.
    selection.clear();
    CHECK(selection.empty());
}

// The double click's unit is the onset GROUP, and the group is the HAND's at that instant: every
// member of the strum joins with no rule of its own — one equal_range over one stream. Only that
// instant joins; a note one onset later belongs to its own group.
TEST_CASE("Chart onset group keys collect every member of the instant", "[core][chart]")
{
    const std::vector<common::core::ChartNote> notes{
        makeTestNote({.measure = 2, .beat = 1}, 1, 3),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5),
        makeTestNote({.measure = 2, .beat = 1}, 3, 5),
        makeTestNote({.measure = 3, .beat = 1}, 1, 7),
        makeTestNote({.measure = 3, .beat = 1}, 4, 9),
    };

    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
    CHECK(
        chartOnsetGroupKeys(tempo_map, notes, noteKey(slotAt(2, 1))) ==
        (std::vector<ChartSelectionKey>{
            noteKey(slotAt(2, 1)), noteKey(slotAt(2, 2)), noteKey(slotAt(2, 3))
        }));

    // An onset holding one note is still a group, so double-clicking a lone head selects it.
    const std::vector<common::core::ChartNote> lone{notes.back()};
    CHECK(
        chartOnsetGroupKeys(tempo_map, lone, noteKey(slotAt(3, 4))) ==
        std::vector<ChartSelectionKey>{noteKey(slotAt(3, 4))});

    // An onset nothing sits on collects nothing.
    CHECK(chartOnsetGroupKeys(tempo_map, notes, noteKey(slotAt(9, 1))).empty());

    // A keyframe's group is every keyframe at its INSTANT, across the notes that carry one: the two
    // measure-2 strings glide together and arrive together three beats in, while string 2's
    // earlier point is not a member. String 1's ring stops short of its own measure-3 restrike, as
    // the chart's laws require, and that onset's own group stays its notes — the restrike and the
    // string-4 note — with no arrival swept in.
    std::vector<common::core::ChartNote> gliding = notes;
    gliding[0].sustain = common::core::Fraction{4};
    gliding[0].keyframes = {common::core::Keyframe{.offset = common::core::Fraction{3}, .fret = 7}};
    gliding[1].sustain = common::core::Fraction{6};
    gliding[1].keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 6},
        common::core::Keyframe{.offset = common::core::Fraction{3}, .fret = 9},
    };
    const ChartSelectionKey junction = keyframeKey(slotAt(2, 1), common::core::Fraction{3});
    CHECK(
        chartOnsetGroupKeys(tempo_map, gliding, junction) ==
        (std::vector<ChartSelectionKey>{
            junction, keyframeKey(slotAt(2, 2), common::core::Fraction{3})
        }));
    CHECK(
        chartOnsetGroupKeys(tempo_map, gliding, noteKey(slotAt(3, 1))) ==
        (std::vector<ChartSelectionKey>{noteKey(slotAt(3, 1)), noteKey(slotAt(3, 4))}));
}

// A junction's head is drawn ON the tail, and it is the LAST mark a pointer can reach — it loses
// to an onset head, which is the primary affordance. The drawn extent is the clickable one in both
// directions: the ribbon between heads is not hit-testable, and every head the lane draws is —
// the arrival at the tail's tip included wherever the ink reaches it, and nothing past the ink
// unless the note is revealed.
TEST_CASE("Chart hit testing resolves linked keyframe heads", "[core][chart]")
{
    const common::core::ChartViewState tab = makeGlideTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    CHECK(chartHitTarget(tab, geometry, 120.0f, 140.0f) == keyframeTarget(0, 0));
    // A pixel between the heads is bare ribbon, and ribbon is testimony: nothing resolves there.
    CHECK_FALSE(chartHitTarget(tab, geometry, 80.0f, 140.0f).has_value());
    // The onset head wins its own pixels: heads resolve before junctions.
    CHECK(chartHitTarget(tab, geometry, 40.0f, 140.0f) == noteTarget(0));
    // At the ring's end the glide's arrival draws its continuation head — the ink runs the whole
    // ring here — so a press on it resolves to it.
    CHECK(chartHitTarget(tab, geometry, 195.0f, 140.0f) == keyframeTarget(0, 1));
    // Another string's lane answers nothing at the same instant.
    CHECK_FALSE(chartHitTarget(tab, geometry, 120.0f, 180.0f).has_value());

    // Crop the ink at 8s and the arrival at 10s stands in the ENDING ZONE: stored, undrawn, and
    // therefore unreachable — until the reveal draws the ring to its end, when it answers again.
    // The junction inside the ink answers either way.
    common::core::ChartViewState cropped = tab;
    cropped.notes[0].ink_end_seconds = 8.0;
    CHECK_FALSE(chartHitTarget(cropped, geometry, 195.0f, 140.0f).has_value());
    CHECK(
        chartHitTarget(cropped, geometry, 195.0f, 140.0f, revealEverything()) ==
        keyframeTarget(0, 1));
    CHECK(chartHitTarget(cropped, geometry, 120.0f, 140.0f) == keyframeTarget(0, 0));
}

// The marquee reaches exactly what the click reaches, so a box drawn over a junction selects that
// junction — and the collection order is notes, then keyframes.
TEST_CASE("Chart hit testing collects keyframe heads inside a marquee box", "[core][chart]")
{
    const common::core::ChartViewState tab = makeGlideTabState();
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const std::vector<ChartHitTarget> junction =
        chartTargetsInBox(tab, geometry, 110.0f, 120.0f, 130.0f, 160.0f);
    CHECK(junction == std::vector<ChartHitTarget>{keyframeTarget(0, 0)});

    // A box over the whole gesture takes the onset head, the junction and the arrival at the
    // ring's end, which draws a head like any other keyframe the tail reaches.
    const std::vector<ChartHitTarget> whole =
        chartTargetsInBox(tab, geometry, 20.0f, 120.0f, 220.0f, 160.0f);
    CHECK(
        whole ==
        (std::vector<ChartHitTarget>{noteTarget(0), keyframeTarget(0, 0), keyframeTarget(0, 1)}));

    // With the ink cropped at 8s the arrival is undrawn at its instant (x = 200), so a box around
    // that instant leaves it out, and boxes it again only while the reveal draws it there. The leg
    // toward it wears its destination chip at the crop (x = 160) instead, which names it, so the
    // box over the whole gesture still takes it.
    common::core::ChartViewState cropped = tab;
    cropped.notes[0].ink_end_seconds = 8.0;
    CHECK(chartTargetsInBox(cropped, geometry, 180.0f, 120.0f, 220.0f, 160.0f).empty());
    CHECK(
        chartTargetsInBox(cropped, geometry, 180.0f, 120.0f, 220.0f, 160.0f, revealEverything()) ==
        (std::vector<ChartHitTarget>{keyframeTarget(0, 1)}));
    CHECK(chartTargetsInBox(cropped, geometry, 20.0f, 120.0f, 220.0f, 160.0f) == whole);
}

// THE REVEAL IS PER NOTE, and the crop's chip is a face of the keyframe it names. A keyframe past
// its note's ink end is reachable at its true instant while THAT note is revealed — the selection
// reveals the note it names — and not while some other note is. And the destination chip the cut
// leg wears at the crop reaches the keyframe it heads for, since a chip is a face of what owns it:
// selecting it reveals the note, which then draws the keyframe at its instant.
TEST_CASE("Chart hit testing reveals a cropped keyframe per note", "[core][chart]")
{
    common::core::ChartViewState cropped = makeGlideTabState();
    // Ink cropped at 8s (x = 160): the arrival at 10s (x = 200) stands in the ending zone, and the
    // leg toward it, rising 9 -> 12, wears its destination chip at the crop.
    cropped.notes[0].ink_end_seconds = 8.0;
    // A second note on string 4, after the glide in projection order, for the reveal to name
    // instead.
    cropped.notes.push_back(
        common::core::NoteViewState{
            .start_seconds = 12.0,
            .ring_end_seconds = 14.0,
            .ink_end_seconds = 14.0,
            .string = 4,
            .fret = 3,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        });
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    CHECK(chartHitTarget(cropped, geometry, 195.0f, 140.0f, revealOnly(0)) == keyframeTarget(0, 1));
    CHECK_FALSE(chartHitTarget(cropped, geometry, 195.0f, 140.0f, revealOnly(1)).has_value());
    CHECK(
        chartTargetsInBox(cropped, geometry, 180.0f, 120.0f, 220.0f, 160.0f, revealOnly(0)) ==
        (std::vector<ChartHitTarget>{keyframeTarget(0, 1)}));
    CHECK(chartTargetsInBox(cropped, geometry, 180.0f, 120.0f, 220.0f, 160.0f, revealOnly(1))
              .empty());

    // The chip at the crop, laid out where the lane draws it for the unrevealed note: a press on
    // it, or a box around it, reaches the keyframe it names.
    const common::core::NoteViewState& glide = cropped.notes[0];
    REQUIRE(glide.keyframes.size() == 2);
    const common::ui::TabKeyframeLayout chip = common::ui::tabKeyframeLayout(
        geometry, glide, glide.keyframes[1], glide.ink_end_seconds, true);
    REQUIRE(chip.shape == common::ui::TabKeyframeShape::Chip);
    REQUIRE(chip.mark_drawn);
    CHECK(
        chartHitTarget(cropped, geometry, chip.box.x + chip.box.width / 2.0f, chip.center_y) ==
        keyframeTarget(0, 1));
    CHECK(
        chartTargetsInBox(
            cropped,
            geometry,
            chip.box.x,
            chip.box.y,
            chip.box.x + chip.box.width,
            chip.box.y + chip.box.height) == (std::vector<ChartHitTarget>{keyframeTarget(0, 1)}));
}

// A point stating only a bend is a target like any other keyframe: its mark is the dot the curve
// wears at the amount it states, off the string line, and a click on the dot or a box around it
// reaches it.
TEST_CASE("Chart hit testing reaches a bend-only point at its dot", "[core][chart]")
{
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 10.0,
            .string = 3,
            .fret = 5,
            .bend =
                {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
                 common::core::BendPointViewState{.seconds = 6.0, .semitones = 2.0}},
            .slides = {},
            .keyframes = {common::core::KeyframeViewState{
                .seconds = 6.0,
                .offset = common::core::Fraction{2},
                .mark = common::core::KeyframeCurveMark{},
                .bend_point = std::size_t{1},
            }},
            .vibrato = {},
        },
    };
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const common::core::NoteViewState& note = tab.notes.front();
    const common::ui::TabKeyframeLayout dot = common::ui::tabKeyframeLayout(
        geometry, note, note.keyframes.front(), note.ink_end_seconds, true);
    REQUIRE(dot.shape == common::ui::TabKeyframeShape::Dot);
    CHECK(dot.center_y < geometry.laneY(3));

    CHECK(chartHitTarget(tab, geometry, dot.center_x, dot.center_y) == keyframeTarget(0, 0));
    CHECK(
        chartTargetsInBox(
            tab,
            geometry,
            dot.box.x,
            dot.box.y,
            dot.box.x + dot.box.width,
            dot.box.y + dot.box.height) == (std::vector<ChartHitTarget>{keyframeTarget(0, 0)}));
}

// The DESTINATION chip a cut bend leg wears at the crop is a face of the point it heads for: a bend
// landing on the next head of its own string stands past the ink, and its amount prints at the
// crop, where a click reaches it. A leg that keeps the amount wears no chip there, so nothing
// answers.
TEST_CASE("Chart hit testing reaches a cropped bend point by its chip at the crop", "[core][chart]")
{
    // A bend reaching a whole step at 10.0s (x = 200), exactly where the next head of the string
    // is struck; the ink stops at 8.0s (x = 160).
    const auto landing_bend = [](std::vector<common::core::BendPointViewState> bend) {
        common::core::ChartViewState tab;
        tab.open_strings = common::core::testing::standardTuning();
        const std::size_t last_point = bend.size() - 1;
        tab.notes = {
            common::core::NoteViewState{
                .start_seconds = 2.0,
                .ring_end_seconds = 10.0,
                .ink_end_seconds = 8.0,
                .string = 3,
                .fret = 5,
                .bend = std::move(bend),
                .slides = {},
                .keyframes = {common::core::KeyframeViewState{
                    .seconds = 10.0,
                    .offset = common::core::Fraction{4},
                    .mark = common::core::KeyframeCurveMark{},
                    .bend_point = last_point,
                }},
                .vibrato = {},
                .end_head = std::size_t{1},
            },
            common::core::NoteViewState{
                .start_seconds = 10.0,
                .ring_end_seconds = 12.0,
                .ink_end_seconds = 12.0,
                .string = 3,
                .fret = 5,
                .bend = {},
                .slides = {},
                .keyframes = {},
                .vibrato = {},
            },
        };
        return tab;
    };
    const common::ui::TabLaneGeometry geometry = makeGeometry();

    const common::core::ChartViewState rising = landing_bend(
        {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
         common::core::BendPointViewState{.seconds = 10.0, .semitones = 2.0}});
    const common::core::NoteViewState& note = rising.notes.front();
    const std::optional<common::ui::TabLayoutRect> chip =
        common::ui::tabKeyframeLayout(
            geometry, note, note.keyframes.front(), note.ink_end_seconds, true)
            .bend_chip;
    REQUIRE(chip.has_value());
    if (!chip.has_value())
    {
        return;
    }
    // Centred on the crop, where the cut leg stops.
    const float chip_x = chip->x + chip->width / 2.0f;
    const float chip_y = chip->y + chip->height / 2.0f;
    CHECK_THAT(chip_x, Catch::Matchers::WithinAbs(geometry.x(8.0), 1e-3));
    CHECK(chartHitTarget(rising, geometry, chip_x, chip_y) == keyframeBendChipTarget(0, 0));

    // Held level into the landing: the cut leg says nothing new, so it wears no chip to click.
    const common::core::ChartViewState held = landing_bend(
        {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
         common::core::BendPointViewState{.seconds = 5.0, .semitones = 2.0},
         common::core::BendPointViewState{.seconds = 10.0, .semitones = 2.0}});
    const common::core::NoteViewState& held_note = held.notes.front();
    CHECK_FALSE(
        common::ui::tabKeyframeLayout(
            geometry, held_note, held_note.keyframes.front(), held_note.ink_end_seconds, true)
            .bend_chip.has_value());
}

// Chips standing close together OVERLAP, and the lane paints the later on top, so a press where
// they overlap names the chip the charter sees: among faces the one painted on top answers.
TEST_CASE("Chart hit testing answers a chip stack with the chip painted on top", "[core][chart]")
{
    // Two points just before a ring's end on the next head of string 3 at 10.0s (x = 200): a whole
    // step at 9.9s, released to a half at 10.0s; the ink stops at 9.0s (x = 180).
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 9.0,
            .string = 3,
            .fret = 5,
            .bend =
                {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
                 common::core::BendPointViewState{.seconds = 9.9, .semitones = 2.0},
                 common::core::BendPointViewState{.seconds = 10.0, .semitones = 1.0}},
            .slides = {},
            .keyframes =
                {common::core::KeyframeViewState{
                     .seconds = 9.9,
                     .offset = common::core::Fraction{79, 10},
                     .mark = common::core::KeyframeCurveMark{},
                     .bend_point = std::size_t{1},
                 },
                 common::core::KeyframeViewState{
                     .seconds = 10.0,
                     .offset = common::core::Fraction{8},
                     .mark = common::core::KeyframeCurveMark{},
                     .bend_point = std::size_t{2},
                 }},
            .vibrato = {},
            .end_head = std::size_t{1},
        },
        common::core::NoteViewState{
            .start_seconds = 10.0,
            .ring_end_seconds = 12.0,
            .ink_end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const common::core::NoteViewState& note = tab.notes.front();
    const std::optional<common::ui::TabLayoutRect> top =
        common::ui::tabKeyframeLayout(
            geometry, note, note.keyframes.back(), note.ring_end_seconds, true)
            .bend_chip;
    const std::optional<common::ui::TabLayoutRect> under =
        common::ui::tabKeyframeLayout(
            geometry, note, note.keyframes.front(), note.ring_end_seconds, true)
            .bend_chip;
    REQUIRE(top.has_value());
    REQUIRE(under.has_value());
    if (!top.has_value() || !under.has_value())
    {
        return;
    }
    // A press where the two overlap left of the next head, which answers before any chip over it.
    const float press_x = std::max(top->x, under->x) + 1.0f;
    const float press_y = std::max(top->y, under->y) + 1.0f;
    REQUIRE(top->contains(press_x, press_y));
    REQUIRE(under->contains(press_x, press_y));
    CHECK(
        chartHitTarget(tab, geometry, press_x, press_y, revealEverything()) ==
        keyframeBendChipTarget(0, 1));
}

// A HEAD STEPPED BACK ANSWERS AFTER THE RING IN FRONT OF IT. While a ring ending on the next head
// of its string is in focus, that head is drawn faint beneath it and the point at the ring's end
// draws its dot in truth over it: a press on the dot reaches the point, and the head is still
// reached where the ring draws nothing.
TEST_CASE("Chart hit testing answers a focused ring before the head it steps back", "[core][chart]")
{
    // A ring on string 3 from 2.0s released to a half step at its end, 10.0s (x = 200), where the
    // next head of the string is struck; the ink stops at 9.0s.
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 9.0,
            .string = 3,
            .fret = 5,
            .bend =
                {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
                 common::core::BendPointViewState{.seconds = 10.0, .semitones = 1.0}},
            .slides = {},
            .keyframes = {common::core::KeyframeViewState{
                .seconds = 10.0,
                .offset = common::core::Fraction{8},
                .mark = common::core::KeyframeCurveMark{},
                .bend_point = std::size_t{1},
            }},
            .vibrato = {},
            .end_head = std::size_t{1},
        },
        common::core::NoteViewState{
            .start_seconds = 10.0,
            .ring_end_seconds = 12.0,
            .ink_end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const common::core::NoteViewState& ring = tab.notes.front();

    // The point selected: the ring is in focus and the head it ends on steps back.
    ChartEditViewState edit;
    edit.selected_keyframes = {ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}};
    const common::ui::TabPresence focused = [answers = chartPresence(tab.notes, false, edit)](
                                                const std::size_t index) { return answers[index]; };

    const common::ui::TabKeyframeLayout dot = common::ui::tabKeyframeLayout(
        geometry, ring, ring.keyframes.front(), ring.ring_end_seconds, false);
    const common::ui::TabLayoutRect head =
        common::ui::tabNoteLayout(geometry, tab.notes.back()).head;
    REQUIRE(dot.mark_drawn);
    REQUIRE(head.contains(dot.center_x, dot.center_y));
    CHECK(
        chartHitTarget(tab, geometry, dot.center_x, dot.center_y, focused) == keyframeTarget(0, 0));
    // In front, the head takes the same press.
    CHECK(
        chartHitTarget(tab, geometry, dot.center_x, dot.center_y, revealEverything()) ==
        noteTarget(1));
    // Stepped back, the head still answers where the ring draws nothing over it.
    const float below_x = head.x + head.width / 2.0f;
    const float below_y = head.y + head.height - 1.0f;
    CHECK(chartHitTarget(tab, geometry, below_x, below_y, focused) == noteTarget(1));
}

// A MIXED overlap answers by paint layer, not by distance: every bend chip paints above every
// slide chip, so a nearest-centre rule would hand the covered slide-out a press on the bend chip
// over it.
TEST_CASE("Chart hit testing answers a mixed chip stack with the bend chip on top", "[core][chart]")
{
    // A whole-step point at 9.9s and a slide-out at the ring's end, 10.0s (x = 200), where the next
    // head of string 3 is struck; the ink stops at 9.0s (x = 180).
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::testing::withStops(
            common::core::NoteViewState{
                .start_seconds = 2.0,
                .ring_end_seconds = 10.0,
                .ink_end_seconds = 9.0,
                .string = 3,
                .fret = 5,
                .bend =
                    {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
                     common::core::BendPointViewState{.seconds = 9.9, .semitones = 2.0}},
                .slides = {},
                .keyframes = {},
                .vibrato = {},
                .end_head = std::size_t{1},
            },
            {common::core::SlideStopViewState{.seconds = 10.0, .fret = 9, .slide_out = true}}),
        common::core::NoteViewState{
            .start_seconds = 10.0,
            .ring_end_seconds = 12.0,
            .ink_end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    // The bend point's keyframe, before the slide-out's in time and in the list.
    std::vector<common::core::KeyframeViewState>& keyframes = tab.notes.front().keyframes;
    keyframes.insert(
        keyframes.begin(),
        common::core::KeyframeViewState{
            .seconds = 9.9,
            .offset = common::core::Fraction{79, 10},
            .mark = common::core::KeyframeCurveMark{},
            .bend_point = std::size_t{1},
        });
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    const common::core::NoteViewState& note = tab.notes.front();
    const std::optional<common::ui::TabLayoutRect> bend_chip =
        common::ui::tabKeyframeLayout(
            geometry, note, keyframes.front(), note.ring_end_seconds, true)
            .bend_chip;
    const common::ui::TabKeyframeLayout slide_out = common::ui::tabKeyframeLayout(
        geometry, note, keyframes.back(), note.ring_end_seconds, true);
    REQUIRE(slide_out.shape == common::ui::TabKeyframeShape::Chip);
    REQUIRE(bend_chip.has_value());
    if (!bend_chip.has_value())
    {
        return;
    }
    // A press where both boxes overlap, at the slide chip's own centre.
    const common::ui::TabLayoutRect& slide_chip = slide_out.box;
    const float press_x = slide_chip.x + slide_chip.width / 2.0f;
    const float press_y = std::max(bend_chip->y, slide_chip.y) + 1.0f;
    REQUIRE(bend_chip->contains(press_x, press_y));
    REQUIRE(slide_chip.contains(press_x, press_y));
    CHECK(
        chartHitTarget(tab, geometry, press_x, press_y, revealEverything()) ==
        keyframeBendChipTarget(0, 0));
}

// A CHIP IS A FACE OF WHAT OWNS IT. The chip printing a keyframe's bend reaches that keyframe, and
// the chip printing the onset's own bend above the head reaches the note, by a click and by a box
// drawn around the chip alone. A head still answers before any chip over it: a chip's box is as
// wide as the widest amount it can print, so a chip answering first would hand its empty margin a
// press meant for the head beside it.
TEST_CASE("Chart hit testing reaches a point and a note by their bend chips", "[core][chart]")
{
    // A whole step reached at 6.0s (x = 120) on a note struck at 2.0s (x = 40), and a small bend
    // point at 9.5s (x = 190) just before the next head of the same string at 10.0s (x = 200).
    common::core::ChartViewState tab;
    tab.open_strings = common::core::testing::standardTuning();
    tab.notes = {
        common::core::NoteViewState{
            .start_seconds = 2.0,
            .ring_end_seconds = 10.0,
            .ink_end_seconds = 10.0,
            .string = 3,
            .fret = 5,
            .bend =
                {common::core::BendPointViewState{.seconds = 2.0, .semitones = 0.0},
                 common::core::BendPointViewState{.seconds = 6.0, .semitones = 2.0},
                 common::core::BendPointViewState{.seconds = 9.5, .semitones = 0.5}},
            .slides = {},
            .keyframes =
                {common::core::KeyframeViewState{
                     .seconds = 6.0,
                     .offset = common::core::Fraction{2},
                     .mark = common::core::KeyframeCurveMark{},
                     .bend_point = std::size_t{1},
                 },
                 common::core::KeyframeViewState{
                     .seconds = 9.5,
                     .offset = common::core::Fraction{15, 4},
                     .mark = common::core::KeyframeCurveMark{},
                     .bend_point = std::size_t{2},
                 }},
            .vibrato = {},
        },
        common::core::NoteViewState{
            .start_seconds = 10.0,
            .ring_end_seconds = 12.0,
            .ink_end_seconds = 12.0,
            .string = 3,
            .fret = 5,
            .bend = {},
            .slides = {},
            .keyframes = {},
            .vibrato = {},
        },
    };
    const common::ui::TabLaneGeometry geometry = makeGeometry();
    REQUIRE(geometry.draw_text);
    const common::core::NoteViewState& note = tab.notes.front();
    const auto in_box = [&tab, &geometry](const common::ui::TabLayoutRect& box) {
        return chartTargetsInBox(
            tab, geometry, box.x, box.y, box.x + box.width, box.y + box.height);
    };

    const std::optional<common::ui::TabLayoutRect> point_chip =
        common::ui::tabKeyframeLayout(
            geometry, note, note.keyframes.front(), note.ink_end_seconds, true)
            .bend_chip;
    REQUIRE(point_chip.has_value());
    if (point_chip.has_value())
    {
        CHECK(
            chartHitTarget(
                tab,
                geometry,
                point_chip->x + point_chip->width / 2.0f,
                point_chip->y + point_chip->height / 2.0f) == keyframeBendChipTarget(0, 0));
        CHECK(in_box(*point_chip) == (std::vector<ChartHitTarget>{keyframeTarget(0, 0)}));
    }

    const std::optional<common::ui::TabLayoutRect> onset_chip =
        common::ui::tabNoteLayout(geometry, note).bend_chip;
    REQUIRE(onset_chip.has_value());
    if (onset_chip.has_value())
    {
        CHECK(
            chartHitTarget(
                tab,
                geometry,
                onset_chip->x + onset_chip->width / 2.0f,
                onset_chip->y + onset_chip->height / 2.0f) == noteBendChipTarget(0));
        CHECK(in_box(*onset_chip) == (std::vector<ChartHitTarget>{noteTarget(0)}));
    }

    // The small bend's chip box reaches over the next head's centre; the head answers there.
    const std::optional<common::ui::TabLayoutRect> near_chip =
        common::ui::tabKeyframeLayout(
            geometry, note, note.keyframes.back(), note.ink_end_seconds, true)
            .bend_chip;
    REQUIRE(near_chip.has_value());
    if (near_chip.has_value())
    {
        REQUIRE(near_chip->contains(200.0f, geometry.laneY(3)));
    }
    CHECK(chartHitTarget(tab, geometry, 200.0f, geometry.laneY(3)) == noteTarget(1));
}

// A keyframe's identity is (note slot, OFFSET), which is what makes the selection key a sum
// rather than a slot plus a kind: a note carries many keyframes, so the slot alone cannot name
// one. The offset and not an index — removing an earlier keyframe shifts every later index and
// moves no offset, so an index-keyed selection would silently point at a different keyframe after
// any edit that dropped one.
TEST_CASE("Chart selection keys a keyframe by its offset", "[core][chart]")
{
    common::core::ChartNote glide =
        makeTestNote({.measure = 2, .beat = 1}, 3, 5, common::core::Fraction{4});
    glide.keyframes = {
        common::core::Keyframe{.offset = common::core::Fraction{2}, .fret = 9},
        common::core::Keyframe{.offset = common::core::Fraction{4}, .fret = 12},
    };
    const std::vector<common::core::ChartNote> notes{glide};
    const common::core::ChartViewState tab = makeGlideTabState();
    const ChartSlotKey slot = slotAt(2, 3);

    ChartSelection selection;
    selection.add(keyframeKey(slot, common::core::Fraction{4}));
    CHECK(
        selectedKeyframeIndices(notes, tab.notes, selection) ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 1}}));

    // The same key against a drawn list the earlier keyframe has left still names the SAME
    // keyframe, now at index 0. An index-keyed selection would have named the wrong one, or
    // nothing at all.
    common::core::ChartViewState trimmed = tab;
    trimmed.notes[0].keyframes.erase(trimmed.notes[0].keyframes.begin());
    CHECK(
        selectedKeyframeIndices(notes, trimmed.notes, selection) ==
        (std::vector<ChartKeyframeRef>{ChartKeyframeRef{.note_index = 0, .keyframe_index = 0}}));

    // A key naming an offset no keyframe sits on resolves to nothing, exactly as a note key whose
    // note was deleted does.
    selection.clear();
    selection.add(keyframeKey(slot, common::core::Fraction{3}));
    CHECK(selectedKeyframeIndices(notes, tab.notes, selection).empty());

    // And so does a key whose NOTE is gone.
    selection.clear();
    selection.add(keyframeKey(slotAt(9, 3), common::core::Fraction{2}));
    CHECK(selectedKeyframeIndices(notes, tab.notes, selection).empty());
}

// Keyframes are the second alternative of one selection, not a second selection: the
// kind-agnostic mutations reach them, they publish after the slot-keyed notes, and a keyframe
// shares its note's slot without excluding the note — the pairing a slot-plus-kind key could not
// hold.
TEST_CASE("Chart selection carries keyframes beside the notes", "[core][chart]")
{
    const ChartSlotKey slot = slotAt(2, 1);
    const ChartSelectionKey note = noteKey(slot);
    const ChartSelectionKey keyframe = keyframeKey(slot, common::core::Fraction{2});
    const ChartSelectionKey later = keyframeKey(slot, common::core::Fraction{4});

    ChartSelection selection;
    selection.add(later);
    selection.add(keyframe);
    selection.add(note);
    CHECK(selection.contains(keyframe));
    CHECK(selection.contains(later));
    // Notes first, then keyframes, each kind in its own order — the flattened order the verb
    // window compares whole selections in.
    CHECK(selection.keys() == (std::vector<ChartSelectionKey>{note, keyframe, later}));
    CHECK(
        selection.keyframes() ==
        (std::vector<ChartKeyframeKey>{
            ChartKeyframeKey{.note = slot, .offset = common::core::Fraction{2}},
            ChartKeyframeKey{.note = slot, .offset = common::core::Fraction{4}}
        }));

    // Toggling a keyframe off leaves the note on the same slot standing, which is the pairing the
    // sum exists for.
    selection.toggle(keyframe);
    CHECK_FALSE(selection.contains(keyframe));
    CHECK(selection.contains(note));
    CHECK_FALSE(selection.empty());

    // A keyframe sits on the slot its offset reaches along its note's ring, on the note's own
    // string — two beats past a measure-2 downbeat is beat 3 under the 4/4 default map — so a
    // caret arms for it exactly as it arms for a note.
    const common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
    CHECK(
        chartCaretSlotFor(tempo_map, keyframe) ==
        ChartSlotKey{.position = {.measure = 2, .beat = 3, .offset = {}}, .string = 1});
    CHECK(chartCaretSlotFor(tempo_map, note) == slot);

    selection.clear();
    CHECK(selection.empty());
}

} // namespace rock_hero::editor::core
