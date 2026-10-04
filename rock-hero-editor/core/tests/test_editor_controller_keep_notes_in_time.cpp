#include "timeline/tempo_map_edits.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::testing::anchor;
using common::core::testing::beatAt;
using testing::checkTimesHeld;

// The eight-measure song (notes at 2:1 and 3:1, hand positions at 1:1 and 3:1) under a map whose
// tempo changes at 3:1 and 5:1: two measures in 3 s (375 ms a beat), two in 5 s (625 ms a beat),
// then four at 120 BPM to 9:1 at 16 s. Opened with Keep Notes in Time on, the lock off, and the
// anchor at 3:1 selected.
struct KeepNotesEditor : SongEditorHarness
{
    KeepNotesEditor()
    {
        common::core::Song song = makeEightMeasureSong();
        song.tempo_map = common::core::testing::commonTimeMap({
            anchor("1:1", 0),
            anchor("3:1", 3000),
            anchor("5:1", 8000),
            anchor("9:1", 16000),
        });
        open(std::move(song));
        controller.onTempoLockToggleRequested();
        controller.onKeepNotesInTimeToggleRequested();
        REQUIRE_FALSE(state().tempo_lock);
        REQUIRE(state().keep_notes_in_time);
        controller.onTempoAnchorSelected(beatAt(3, 1));
        REQUIRE(state().selected_tempo_anchor == std::optional{beatAt(3, 1)});
    }
};

} // namespace

// The switch is session state: off at every open, whatever the session before it left, and
// flipping it records nothing to undo.
TEST_CASE("Keep Notes in Time is off at every open", "[core][keep-notes-in-time]")
{
    SongEditorHarness editor;
    editor.open(makeEightMeasureSong());
    CHECK_FALSE(editor.state().keep_notes_in_time);

    editor.controller.onKeepNotesInTimeToggleRequested();
    CHECK(editor.state().keep_notes_in_time);
    CHECK(editor.undoEntryCount() == 0);

    editor.open(makeEightMeasureSong());
    REQUIRE(editor.project_services.open_call_count == 2);
    CHECK_FALSE(editor.state().keep_notes_in_time);
}

// With the switch on, every tempo verb that changes a time leaves every object where it sounds,
// within half a tick of the new grid, as ONE entry that undo takes back whole. A held Alt+arrow
// run is one entry too, re-addressed from where the run began.
TEST_CASE(
    "Keep Notes in Time holds every time through each tempo verb", "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    std::string label;
    SECTION("a typed tempo")
    {
        editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
        CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4800});
        label = "Set Tempo to 100";
    }
    SECTION("a delete")
    {
        editor.controller.onSelectionDeleteRequested();
        CHECK_FALSE(editor.anchorMilliseconds(beatAt(3, 1)).has_value());
        label = "Delete Tempo Anchor";
    }
    SECTION("a held nudge run")
    {
        editor.pressAlt(ChartStepDirection::Right, 7);
        CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{3070});
        label = "Move Tempo Anchor";
    }

    const SongTimeline after = editor.timeline();
    REQUIRE_FALSE(after.arrangements == before.arrangements);
    checkTimesHeld(before, after, 0.0);
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == label);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == before);
    editor.controller.onRedoRequested();
    CHECK(editor.timeline() == after);
}

// A nudge run that walks back to where it began rounds nothing, because every press re-addresses
// from the run's origin, so it lands the song exactly as it was and leaves no entry.
TEST_CASE(
    "A walked-back nudge run under Keep Notes in Time leaves no entry",
    "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right, 3);
    editor.pressAlt(ChartStepDirection::Left, 3);
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
}

// With the switch off the grid is truth (plan 41): the same typed tempo keeps every address, so
// the notes move with their beats.
TEST_CASE(
    "With Keep Notes in Time off a tempo edit keeps every address", "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    editor.controller.onKeepNotesInTimeToggleRequested();
    REQUIRE_FALSE(editor.state().keep_notes_in_time);
    const SongTimeline before = editor.timeline();

    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
    const SongTimeline after = editor.timeline();
    CHECK(after.arrangements == before.arrangements);
    CHECK(after.sections == before.sections);
    CHECK_FALSE(after.tempo_map == before.tempo_map);
}

// The cursor is carried as one more slice through the content map, so it keeps its time. At 2 s it
// stands on 2:2+1/3; at 100 BPM the same instant is 1:4+1/3, a tick-lattice place, so it holds
// exactly.
TEST_CASE("Keep Notes in Time keeps the cursor's time", "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    editor.seek(2.0);

    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "100");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(2.0, 1e-9));
}

// The lock refuses moves first, whatever the switch says: a nudge and a drag both leave the song
// and the history alone, and the drag holds and previews nothing.
TEST_CASE("The tempo lock refuses moves under Keep Notes in Time", "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    editor.controller.onTempoLockToggleRequested();
    REQUIRE(editor.state().tempo_lock);
    editor.controller.onTimelineZoomChanged(100.0);
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK_FALSE(editor.state().tempo_drag_preview.has_value());
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.view.tempo_anchor_refusal_flashes.size() == 2);
}

// During a drag the song is not written: the session keeps its map, which is what content keeps
// drawing from, and the grid's preview map is published beside it. The release lands the whole
// drag as one entry from the map the drag started on, every time held, and the preview ends.
TEST_CASE(
    "A Keep-Notes-in-Time drag previews the grid and commits on release",
    "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4300);
    // Onto the neighbour at 5:1: refused, so the last legal time stays held.
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 9000);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK(editor.timeline() == before);
    CHECK(editor.state().tempo_map == before.tempo_map);
    CHECK(editor.undoEntryCount() == entries);
    const std::optional<TempoMap>& preview = editor.state().tempo_drag_preview;
    REQUIRE(preview.has_value());
    if (preview.has_value())
    {
        const TempoMapEditResult expected = moveTempoAnchor(before.tempo_map, beatAt(3, 1), 4200);
        REQUIRE(expected.has_value());
        CHECK(*preview == *expected);
    }

    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    CHECK_FALSE(editor.state().tempo_drag_preview.has_value());
    const SongTimeline after = editor.timeline();
    checkTimesHeld(before, after, 0.0);
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
    CHECK(editor.state().selected_tempo_anchor == std::optional{beatAt(3, 1)});

    // A second release has nothing left to commit.
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.undoEntryCount() == entries + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.timeline() == before);
}

// A drag released where it began commits nothing, and one whose anchor stopped being the selection
// (Esc released it) drops its preview at once and commits nothing at the release.
TEST_CASE(
    "A Keep-Notes-in-Time drag that goes nowhere commits nothing", "[core][keep-notes-in-time]")
{
    KeepNotesEditor editor;
    const SongTimeline before = editor.timeline();
    const std::size_t entries = editor.undoEntryCount();

    SECTION("released at its origin")
    {
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 3000);
    }
    SECTION("released after Esc")
    {
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        REQUIRE(editor.state().tempo_drag_preview.has_value());
        editor.controller.onChartEscapePressed();
        CHECK_FALSE(editor.state().tempo_drag_preview.has_value());
    }

    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.timeline() == before);
    CHECK(editor.undoEntryCount() == entries);
    CHECK_FALSE(editor.state().tempo_drag_preview.has_value());
}

} // namespace rock_hero::editor::core
