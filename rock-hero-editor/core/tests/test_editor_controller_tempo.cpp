#include "log_capture.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using Catch::Matchers::WithinAbs;
using common::core::GridPosition;
using common::core::SongSection;
using common::core::testing::anchor;
using common::core::testing::beatAt;
using common::core::testing::commonTimeMap;

// The half beat after 2:1, where the sub-beat song's extra anchor stands.
[[nodiscard]] GridPosition subBeatPosition()
{
    return GridPosition{.measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}};
}

// The eight-measure song with a sub-beat anchor at 2:1+1/2 beside the on-beat one at 2:1, both at
// the 120 BPM the map already has: the pair an address floored to its beat cannot tell apart.
[[nodiscard]] common::core::Song makeSubBeatTempoSong()
{
    common::core::Song song = makeEightMeasureSong();
    song.tempo_map = commonTimeMap({
        anchor("1:1", 0),
        anchor("2:1", 2000),
        anchor("2:1+1/2", 2250),
        anchor("9:1", 16000),
    });
    return song;
}

// A controller with a song loaded, the eight-measure song unless a test supplies its own.
struct TempoEditor : SongEditorHarness
{
    explicit TempoEditor(common::core::Song song = makeEightMeasureSong())
    {
        open(std::move(song));
    }

    // The seconds of the anchor standing exactly on a beat, or NaN (which no matcher accepts) when
    // none does.
    [[nodiscard]] double anchorSeconds(int measure, int beat) const
    {
        for (const common::core::BeatAnchor& anchor : tempoMap().anchors())
        {
            if (anchor.position == beatAt(measure, beat))
            {
                return anchor.seconds();
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    // The stored time of the anchor at a position in the map the grid draws: the one a held
    // tempo-chip drag previews, and the session's own map otherwise.
    [[nodiscard]] std::optional<std::int64_t> gridAnchorMilliseconds(
        const GridPosition& position) const
    {
        const std::vector<common::core::BeatAnchor>& anchors = state().grid_map.anchors();
        const auto found =
            std::ranges::find_if(anchors, [&position](const common::core::BeatAnchor& held) {
                return held.position == position;
            });
        return found != anchors.end() ? std::optional{found->milliseconds} : std::nullopt;
    }

    [[nodiscard]] TempoChordTarget chord() const
    {
        return state().tempo_chord_target;
    }

    // The position of the anchor Enter would retype, which is the selected one; empty when the
    // selection is no span-opening anchor.
    [[nodiscard]] std::optional<GridPosition> selectedAnchorPosition() const
    {
        const RestateTarget restate = state().restate_target;
        const auto* const target = std::get_if<SetTempoTarget>(&restate);
        return target != nullptr ? std::optional{target->position} : std::nullopt;
    }

    // Turns the tempo lock off, as a test that moves anchors must: every open turns it on.
    void unlockTempo()
    {
        controller.onTempoLockToggleRequested();
        REQUIRE_FALSE(state().tempo_lock);
    }

    [[nodiscard]] std::vector<common::core::FretHandPosition> placements() const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        return chart != nullptr ? chart->fret_hand_positions
                                : std::vector<common::core::FretHandPosition>{};
    }

    // Clicks the lane chip of the placement at `index` in the chart's list (fretHandChipAt).
    void clickPlacement(const std::size_t index)
    {
        const std::shared_ptr<const common::core::ChartViewState>& tab = state().tab;
        REQUIRE(tab != nullptr);
        if (tab == nullptr)
        {
            return;
        }
        const std::optional<std::size_t> chip =
            fretHandChipAt(*tab, placements().at(index).position);
        REQUIRE(chip.has_value());
        if (chip.has_value())
        {
            controller.onFretHandPositionSelected(*chip);
        }
    }
};

// The marker refusal records in a captured log, in order, each as the text after its category.
[[nodiscard]] std::vector<std::string> markerRefusalLines(const std::string& log)
{
    constexpr std::string_view category = "editor.marker: ";
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < log.size())
    {
        const std::size_t end = std::min(log.find('\n', start), log.size());
        const std::string_view line = std::string_view{log}.substr(start, end - start);
        if (const std::size_t at = line.find(category); at != std::string_view::npos)
        {
            std::string_view text = line.substr(at + category.size());
            if (text.ends_with('\r'))
            {
                text.remove_suffix(1);
            }
            lines.emplace_back(text);
        }
        start = end + 1;
    }
    return lines;
}

// The record every Tempo Lock refusal of a drag on 3:1 leaves.
constexpr std::string_view g_locked_drag_refusal =
    R"(Rejected marker edit label="Move Tempo Anchor" )"
    R"(detail="Tempo Lock is on: dragging the tempo anchor at 3:1 is blocked")";

} // namespace

// The chord's whole decision is published as the verb: an insert at the cursor's slot where no
// anchor stands, and the BPM prompt — prefilled with what the chip prints — where one does, whether
// that anchor is selected or not.
TEST_CASE("The published tempo chord names the verb at the cursor's slot", "[core][tempo]")
{
    TempoEditor editor;

    editor.seek(2.0);
    const TempoChordTarget empty_beat = editor.chord();
    const auto* const insert = std::get_if<InsertTempoAnchorTarget>(&empty_beat);
    REQUIRE(insert != nullptr);
    CHECK(insert->position == beatAt(2, 1));

    editor.seek(0.0);
    const TempoChordTarget unselected = editor.chord();
    const auto* const restate = std::get_if<SetTempoTarget>(&unselected);
    REQUIRE(restate != nullptr);
    CHECK(restate->position == beatAt(1, 1));
    CHECK(restate->bpm_text == "120");
    CHECK(std::holds_alternative<std::monostate>(editor.state().restate_target));

    // Selecting the chip changes nothing about the chord, and gives Enter the same verb.
    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    CHECK(editor.chord() == unselected);
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTempoTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == *restate);
}

// The chord reads the cursor at the placement quantum, as the tone chord does: the grid line with
// snap on, the tick with snap off, so an anchor stands between beats wherever the grid does. The
// insert pins the time the map gives that slot, and the next press restates the new anchor.
TEST_CASE("The tempo chord reads the cursor at the placement quantum", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 8});

    // 2.3 s is 0.6 of a beat into 2:1, whose nearest eighth is the half beat.
    editor.seek(2.3);
    const TempoChordTarget snapped = editor.chord();
    const auto* const on_grid = std::get_if<InsertTempoAnchorTarget>(&snapped);
    REQUIRE(on_grid != nullptr);
    CHECK(on_grid->position == subBeatPosition());

    // With snap off the slot is the tick: 0.6 of a beat is 576 of its 960 ticks.
    turnGridSnapOff(editor.controller);
    editor.seek(2.3);
    const GridPosition tick{.measure = 2, .beat = 1, .offset = common::core::Fraction{3, 5}};
    const TempoChordTarget unsnapped = editor.chord();
    const auto* const on_tick = std::get_if<InsertTempoAnchorTarget>(&unsnapped);
    REQUIRE(on_tick != nullptr);
    CHECK(on_tick->position == tick);

    editor.controller.onTempoAnchorInsertRequested(on_tick->position);
    CHECK(editor.anchorMilliseconds(tick) == std::optional<std::int64_t>{2300});
    const SetTempoTarget expected{.position = tick, .bpm_text = "120"};
    const TempoChordTarget restated = editor.chord();
    const auto* const restate = std::get_if<SetTempoTarget>(&restated);
    REQUIRE(restate != nullptr);
    CHECK(*restate == expected);
}

// Inert exactly where the commit would refuse: on the terminal, which opens no span; while the
// transport plays; and with no song.
TEST_CASE("The tempo chord is nothing where no anchor can stand", "[core][tempo]")
{
    TempoEditor editor;

    editor.seek(16.0);
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));

    editor.transport.current_state.playing = true;
    editor.seek(2.0);
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));

    editor.transport.current_state.playing = false;
    editor.controller.onCloseRequested();
    CHECK(std::holds_alternative<std::monostate>(editor.chord()));
}

// An insert pins the time the map already gives the beat, records one entry and selects what it
// made, so the chord's next press on the same beat opens the prompt rather than inserting again.
TEST_CASE(
    "A tempo insert selects the new anchor and the chord then sets its tempo", "[core][tempo]")
{
    TempoEditor editor;
    editor.seek(4.0);
    REQUIRE(std::holds_alternative<InsertTempoAnchorTarget>(editor.chord()));

    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    REQUIRE(editor.tempoMap().anchors().size() == 3);
    CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.0, 1e-9));
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Add Tempo Anchor");

    const SetTempoTarget expected{.position = beatAt(3, 1), .bpm_text = "120"};
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTempoTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == expected);
    const TempoChordTarget chord = editor.chord();
    const auto* const restate = std::get_if<SetTempoTarget>(&chord);
    REQUIRE(restate != nullptr);
    CHECK(*restate == expected);

    // A second insert on the occupied beat is refused and records nothing.
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    CHECK(editor.tempoMap().anchors().size() == 3);
    CHECK(editor.undoEntryCount() == 1);
}

// A typed tempo ripples: its anchor stays, every later anchor shifts by the same whole-millisecond
// amount, so every later span keeps its length exactly, and the chip prints what was typed. Text
// that is not a number changes nothing and records nothing.
TEST_CASE("A typed tempo ripples every later anchor", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    editor.controller.onTempoAnchorInsertRequested(beatAt(7, 1));
    REQUIRE(editor.undoEntryCount() == 3);

    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), " 137 ");
    CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.0, 1e-9));
    // Eight beats at 137 BPM is 3.504 s on the millisecond grid, 0.496 s short of the 4 s at 120.
    const double shift = editor.anchorSeconds(5, 1) - 8.0;
    CHECK_THAT(shift, WithinAbs(-0.496, 1e-9));
    CHECK_THAT(editor.anchorSeconds(7, 1) - 12.0, WithinAbs(shift, 1e-9));
    CHECK_THAT(editor.anchorSeconds(7, 1) - editor.anchorSeconds(5, 1), WithinAbs(4.0, 1e-9));
    CHECK(editor.undoEntryCount() == 4);
    CHECK(editor.undoTopLabel() == "Set Tempo to 137");
    const RestateTarget enter = editor.state().restate_target;
    const auto* const entered = std::get_if<SetTempoTarget>(&enter);
    REQUIRE(entered != nullptr);
    CHECK(entered->bpm_text == "137");

    const common::core::TempoMap rippled = editor.tempoMap();
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "fast");
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "137x");
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "   ");
    CHECK(editor.tempoMap() == rippled);
    CHECK(editor.undoEntryCount() == 4);
}

// The first anchor is refused, every map keeps it; a delete merges the two spans with both outer
// anchors pinned, so undo restores the map exactly.
TEST_CASE("Deleting a tempo anchor round-trips through undo", "[core][tempo]")
{
    TempoEditor editor;
    const common::core::TempoMap original = editor.tempoMap();

    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.tempoMap() == original);
    CHECK(editor.undoEntryCount() == 0);

    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.tempoMap() == original);
    CHECK(editor.undoTopLabel() == "Delete Tempo Anchor");

    editor.controller.onUndoRequested();
    CHECK(editor.tempoMap() == inserted);
    editor.controller.onRedoRequested();
    CHECK(editor.tempoMap() == original);
}

// The view's Delete guard reads the published selection_present flag, so a selected tempo chip
// must count there or the key never reaches the delete.
TEST_CASE("A selected tempo chip publishes a selection the Delete key reaches", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    CHECK(editor.state().selection_present);

    editor.controller.onTempoAnchorSelected(beatAt(3, 1));
    REQUIRE(editor.state().selection_present);
    editor.controller.onSelectionDeleteRequested();
    CHECK_FALSE(editor.anchorMilliseconds(beatAt(3, 1)).has_value());
}

// One press is one on-screen pixel at the zoom the view reported, and a step onto a neighbour is
// refused rather than clamped. The cursor rides the anchor.
TEST_CASE("A tempo anchor moves one pixel at the reported zoom", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(100.0);

    editor.pressAlt(ChartStepDirection::Right);
    CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.010, 1e-9));
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(4.010, 1e-9));
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");

    // At one pixel per second a step is a whole second, which would carry 3:1 past 3:2 at 4.51 s.
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 2));
    editor.controller.onTempoAnchorSelected(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(1.0);
    const common::core::TempoMap before = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.tempoMap() == before);
    CHECK(editor.undoEntryCount() == entries);
}

// A sub-beat anchor is addressed by its whole position: nudged, retyped and deleted as ITSELF,
// never as the on-beat anchor of the beat it falls in.
TEST_CASE("A sub-beat tempo anchor is edited as itself, never its beat-mate", "[core][tempo]")
{
    TempoEditor editor{makeSubBeatTempoSong()};
    editor.unlockTempo();
    const GridPosition on_beat = beatAt(2, 1);
    const GridPosition sub_beat = subBeatPosition();

    editor.controller.onTempoAnchorSelected(sub_beat);
    editor.controller.onTimelineZoomChanged(100.0);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2260});
    CHECK(editor.anchorMilliseconds(on_beat) == std::optional<std::int64_t>{2000});
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");

    // 27.5 quarters at 60 BPM from 2.26 s put the terminal at 29.76 s; the anchor before stays.
    editor.controller.onTempoSpanBpmRequested(sub_beat, "60");
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2260});
    CHECK(editor.anchorMilliseconds(on_beat) == std::optional<std::int64_t>{2000});
    CHECK(editor.anchorMilliseconds(beatAt(9, 1)) == std::optional<std::int64_t>{29760});
    CHECK(editor.undoTopLabel() == "Set Tempo to 60");

    editor.controller.onSelectionDeleteRequested();
    CHECK_FALSE(editor.anchorMilliseconds(sub_beat).has_value());
    CHECK(editor.anchorMilliseconds(on_beat) == std::optional<std::int64_t>{2000});
    CHECK(editor.undoTopLabel() == "Delete Tempo Anchor");
}

// Undo and redo focus the anchor they change by its whole position, so a sub-beat anchor comes back
// selected as itself rather than as the on-beat anchor of its beat.
TEST_CASE("Undo and redo re-select an edited sub-beat anchor", "[core][tempo]")
{
    TempoEditor editor{makeSubBeatTempoSong()};
    editor.unlockTempo();
    const GridPosition sub_beat = subBeatPosition();
    editor.controller.onTempoAnchorSelected(sub_beat);
    editor.controller.onTimelineZoomChanged(100.0);
    editor.pressAlt(ChartStepDirection::Right);
    REQUIRE(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2260});

    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onUndoRequested();
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2250});
    CHECK(editor.selectedAnchorPosition() == std::optional{sub_beat});

    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onRedoRequested();
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2260});
    CHECK(editor.selectedAnchorPosition() == std::optional{sub_beat});
}

// The engine's tempo-derived caches follow the map through every transition: the commit, the
// undo and the redo each re-mirror the host tempo, and a transition that leaves the map alone
// does not.
TEST_CASE("Tempo edits and their undo re-mirror the host tempo", "[core][tempo]")
{
    TempoEditor editor;
    const common::core::TempoMap original = editor.tempoMap();
    const int loaded_mirrors = editor.audio.mirror_tempo_map_call_count;
    REQUIRE(loaded_mirrors >= 1);

    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "60");
    const common::core::TempoMap rippled = editor.tempoMap();
    REQUIRE_FALSE(rippled == original);
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 1);
    REQUIRE(editor.audio.last_mirrored_tempo_map.has_value());
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == rippled);
    }

    editor.controller.onUndoRequested();
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 2);
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == original);
    }

    editor.controller.onRedoRequested();
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 3);
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == rippled);
    }

    // A section edit and its undo leave the map alone, so nothing is re-mirrored.
    editor.controller.onSongSectionInsertRequested(beatAt(4, 1), "Bridge");
    editor.controller.onUndoRequested();
    CHECK(editor.audio.mirror_tempo_map_call_count == loaded_mirrors + 3);
}

// The map places every note in time, so a tempo edit with no chart edit still moves the published
// notes: the projection caches key on the revision the map's replacement advances.
TEST_CASE("A tempo edit moves the projected notes", "[core][tempo]")
{
    TempoEditor editor;
    const std::shared_ptr<const common::core::ChartViewState> before = editor.state().tab;
    REQUIRE(before != nullptr);
    REQUIRE_FALSE(before->notes.empty());
    CHECK_THAT(before->notes.front().start_seconds, WithinAbs(2.0, 1e-9));

    // Half the tempo puts 2:1 at 4 s.
    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "60");
    const std::shared_ptr<const common::core::ChartViewState> after = editor.state().tab;
    REQUIRE(after != nullptr);
    REQUIRE_FALSE(after->notes.empty());
    CHECK_THAT(after->notes.front().start_seconds, WithinAbs(4.0, 1e-9));
}

// A held run of Alt+arrow presses on one selected marker is ONE undo entry for every marker kind,
// and one Ctrl+Z returns it to where the run began.
TEST_CASE("A held marker move is one undo entry for every marker kind", "[core][tempo]")
{
    TempoEditor editor;

    SECTION("a section")
    {
        const std::vector<SongSection> original = editor.sections();
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAlt(ChartStepDirection::Right, 3);
        REQUIRE(editor.sections().size() == 2);
        CHECK(editor.sections().front().position == beatAt(5, 1));
        CHECK(editor.undoEntryCount() == 1);
        CHECK(editor.undoTopLabel() == "Move Section");
        editor.controller.onUndoRequested();
        CHECK(editor.sections() == original);
    }
    SECTION("a fret-hand position")
    {
        const std::vector<common::core::FretHandPosition> original = editor.placements();
        editor.clickPlacement(1);
        editor.pressAlt(ChartStepDirection::Right, 3);
        CHECK_FALSE(editor.placements() == original);
        CHECK(editor.undoEntryCount() == 1);
        editor.controller.onUndoRequested();
        CHECK(editor.placements() == original);
    }
    SECTION("a tempo anchor")
    {
        editor.unlockTempo();
        editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
        const common::core::TempoMap inserted = editor.tempoMap();
        editor.controller.onTimelineZoomChanged(100.0);
        editor.pressAlt(ChartStepDirection::Right, 3);
        CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.030, 1e-9));
        CHECK(editor.undoEntryCount() == 2);
        editor.controller.onUndoRequested();
        CHECK(editor.tempoMap() == inserted);
    }
}

// The run lives exactly as long as its proof: a selection change and a save each start a new
// entry, and a run that walks back to where it began leaves none.
TEST_CASE("A marker move run ends at every other commit point", "[core][tempo]")
{
    TempoEditor editor;
    const std::vector<SongSection> original = editor.sections();

    SECTION("a selection change")
    {
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAlt(ChartStepDirection::Right);
        editor.controller.onSongSectionSelected(beatAt(6, 1));
        editor.pressAlt(ChartStepDirection::Right);
        CHECK(editor.undoEntryCount() == 2);
    }
    SECTION("a walk back to the start")
    {
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAlt(ChartStepDirection::Right);
        REQUIRE(editor.undoEntryCount() == 1);
        editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
        CHECK(editor.sections() == original);
        CHECK(editor.undoEntryCount() == 0);
    }
    SECTION("a save")
    {
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAlt(ChartStepDirection::Right);
        editor.controller.onSaveRequested();
        REQUIRE(editor.project_services.save_call_count == 1);
        editor.pressAlt(ChartStepDirection::Right);
        CHECK(editor.undoEntryCount() == 2);
        CHECK(editor.sections().front().position == beatAt(4, 1));
    }
}

// The run is ended by a selection change itself, not judged by comparing selections, so stepping
// away from the moved section and back onto it starts a new entry even though the selection reads
// the same again.
TEST_CASE("A marker move run ends when the selection leaves and returns", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onSongSectionSelected(beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right);
    REQUIRE(editor.sections().front().position == beatAt(3, 1));

    editor.controller.onRowObjectStepRequested(true, false);
    editor.controller.onRowObjectStepRequested(false, false);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.sections().front().position == beatAt(4, 1));
    CHECK(editor.undoEntryCount() == 2);
}

// Any other edit is a commit point: a typed tempo between two presses on the same anchor splits the
// presses into entries of their own.
TEST_CASE("A typed tempo between two anchor moves ends the run", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(100.0);
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right);
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "137");
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.undoEntryCount() == entries + 3);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
}

// An undo ends the run: the press after it starts a new entry from where the undo left the marker,
// rather than widening the entry the undo walked away from.
TEST_CASE("A press after an undo mid-run records a new entry", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onSongSectionSelected(beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right, 2);
    REQUIRE(editor.sections().front().position == beatAt(4, 1));

    editor.controller.onUndoRequested();
    REQUIRE(editor.sections().front().position == beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.sections().front().position == beatAt(3, 1));
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Move Section");

    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().position == beatAt(2, 1));
}

// A run walked back to its start lands the starting map again, and the engine follows it there.
TEST_CASE("A walked-back anchor run re-mirrors the starting map", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    editor.controller.onTimelineZoomChanged(100.0);

    editor.pressAlt(ChartStepDirection::Right);
    REQUIRE_FALSE(editor.tempoMap() == inserted);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.tempoMap() == inserted);
    REQUIRE(editor.audio.last_mirrored_tempo_map.has_value());
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == inserted);
    }
}

// A tempo-chip drag is one gesture: its events write nothing — the song, the history, the engine
// and the transport stay as the press found them while the grid previews the held time and the
// cursor rides the chip — and the release lands the whole drag as ONE entry, seeking the cursor
// onto the anchor where it was drawn, that one Ctrl+Z takes back.
TEST_CASE("A tempo-chip drag writes nothing until its release lands one entry", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    const int mirrors = editor.audio.mirror_tempo_map_call_count;
    const int seeks = editor.transport.seek_call_count;
    const std::optional<KeyboardPositionViewState> keyboard = editor.state().keyboard_position;

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4300);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.audio.mirror_tempo_map_call_count == mirrors);
    CHECK(editor.transport.seek_call_count == seeks);
    // The cursor stays put, so the view's keep-the-measure-in-view scroll has nothing to follow.
    CHECK(editor.state().keyboard_position == keyboard);
    CHECK(editor.state().tempo_map == inserted);
    CHECK(editor.gridAnchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    // With the Note Lock off, content moves with the beat: it draws under the preview too.
    CHECK(editor.state().content_map == editor.state().grid_map);
    const std::optional<double> drag_cursor = editor.state().drag_cursor_seconds;
    REQUIRE(drag_cursor.has_value());
    if (drag_cursor.has_value())
    {
        CHECK_THAT(*drag_cursor, WithinAbs(4.2, 1e-9));
    }

    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    CHECK(editor.audio.mirror_tempo_map_call_count == mirrors + 1);
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(4.2, 1e-9));
    CHECK_FALSE(editor.state().drag_cursor_seconds.has_value());
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
    CHECK(editor.selectedAnchorPosition() == std::optional{beatAt(3, 1)});
    CHECK(editor.state().grid_map == editor.tempoMap());
    CHECK(editor.state().content_map == editor.tempoMap());

    editor.controller.onUndoRequested();
    CHECK(editor.tempoMap() == inserted);
}

// The release lands exactly what one Alt+arrow press to the same time lands, with the Note Lock off
// and on: the same song, the same history, and the same projections on both surfaces. The press
// steps one pixel, which at 5 px/s is the drag's 200 ms. Both open one song, so both carry its ids.
TEST_CASE("A tempo-chip drag lands what one anchor nudge lands", "[core][tempo]")
{
    const common::core::Song song = makeEightMeasureSong();
    TempoEditor dragged{song};
    TempoEditor nudged{song};
    bool note_lock = false;
    SECTION("the Note Lock off")
    {
        note_lock = false;
    }
    SECTION("the Note Lock on")
    {
        note_lock = true;
    }
    for (TempoEditor* const editor : {&dragged, &nudged})
    {
        editor->unlockTempo();
        if (note_lock)
        {
            editor->controller.onNoteLockToggleRequested();
        }
        editor->controller.onTempoAnchorInsertRequested(beatAt(3, 1));
        REQUIRE(editor->state().note_lock == note_lock);
    }

    dragged.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    dragged.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4300);
    dragged.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    dragged.controller.onTempoAnchorDragReleased();
    nudged.controller.onTimelineZoomChanged(5.0);
    nudged.pressAlt(ChartStepDirection::Right);

    REQUIRE(nudged.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    // Each open mints its tone regions fresh ids, so the timelines compare with the ids aligned.
    const common::core::SongTimeline dragged_timeline = dragged.timeline();
    common::core::SongTimeline nudged_timeline = nudged.timeline();
    REQUIRE(nudged_timeline.arrangements.size() == dragged_timeline.arrangements.size());
    for (std::size_t arrangement = 0; arrangement < nudged_timeline.arrangements.size();
         ++arrangement)
    {
        const std::vector<common::core::ToneRegion>& dragged_regions =
            dragged_timeline.arrangements[arrangement].tone_track.regions;
        std::vector<common::core::ToneRegion>& nudged_regions =
            nudged_timeline.arrangements[arrangement].tone_track.regions;
        REQUIRE(nudged_regions.size() == dragged_regions.size());
        for (std::size_t region = 0; region < nudged_regions.size(); ++region)
        {
            nudged_regions[region].id = dragged_regions[region].id;
        }
    }
    CHECK(dragged_timeline == nudged_timeline);
    CHECK(dragged.state().undo_history.labels == nudged.state().undo_history.labels);
    const std::shared_ptr<const common::core::ChartViewState> dragged_tab = dragged.state().tab;
    const std::shared_ptr<const common::core::ChartViewState> nudged_tab = nudged.state().tab;
    REQUIRE(dragged_tab != nullptr);
    REQUIRE(nudged_tab != nullptr);
    CHECK(*dragged_tab == *nudged_tab);
    const std::shared_ptr<const common::core::HighwayViewState> dragged_highway =
        dragged.state().highway;
    const std::shared_ptr<const common::core::HighwayViewState> nudged_highway =
        nudged.state().highway;
    REQUIRE(dragged_highway != nullptr);
    REQUIRE(nudged_highway != nullptr);
    CHECK(*dragged_highway == *nudged_highway);
}

// A refused event changes nothing it held — the preview stays at the last legal time — and the
// drag goes on: a later legal event is the one the release lands.
TEST_CASE("A refused tempo-chip drag event holds the last legal time", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 5000);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 8000);
    CHECK(editor.gridAnchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{5000});
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 9000);
    CHECK(editor.gridAnchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{5000});
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 6000);
    CHECK(editor.gridAnchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{6000});
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{6000});
    CHECK(editor.anchorMilliseconds(beatAt(5, 1)) == std::optional<std::int64_t>{8000});
    CHECK(editor.undoEntryCount() == entries + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.tempoMap() == inserted);

    // The first anchor may not go below zero.
    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onTempoAnchorMoveRequested(beatAt(1, 1), -1);
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(beatAt(1, 1)) == std::optional<std::int64_t>{0});
}

// A drag released where it began leaves no Ctrl+Z that changes nothing, and resyncs nothing.
TEST_CASE("A tempo-chip drag released at its origin commits nothing", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    const int mirrors = editor.audio.mirror_tempo_map_call_count;

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 3900);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4000);
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.undoTopLabel() == "Add Tempo Anchor");
    CHECK(editor.audio.mirror_tempo_map_call_count == mirrors);
    CHECK(editor.state().grid_map == inserted);
}

// A sub-beat anchor drags as itself, never as the on-beat anchor of its beat, which bounds it.
TEST_CASE("A sub-beat tempo anchor drags as itself", "[core][tempo]")
{
    TempoEditor editor{makeSubBeatTempoSong()};
    editor.unlockTempo();
    const GridPosition sub_beat = subBeatPosition();
    editor.controller.onTempoAnchorSelected(sub_beat);

    editor.controller.onTempoAnchorMoveRequested(sub_beat, 2300);
    CHECK(editor.gridAnchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2300});
    editor.controller.onTempoAnchorMoveRequested(sub_beat, 2000);
    CHECK(editor.gridAnchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2300});
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2300});
    CHECK(editor.anchorMilliseconds(beatAt(2, 1)) == std::optional<std::int64_t>{2000});
}

// The drag moves only the SELECTED anchor, the one its press selected: once the anchor stops being
// the selection (Esc released it), the rest of the drag moves nothing, and while the transport
// plays nothing moves at all, as for every marker edit.
TEST_CASE("A tempo-chip drag moves only the selected anchor while paused", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    SECTION("a released selection")
    {
        editor.controller.onChartEscapePressed();
        REQUIRE_FALSE(editor.selectedAnchorPosition().has_value());
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    }
    SECTION("another anchor selected")
    {
        editor.controller.onTempoAnchorSelected(beatAt(1, 1));
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    }
    SECTION("a playing transport")
    {
        editor.transport.current_state.playing = true;
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    }
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
}

// The grid is truth: a typed tempo moves the cursor in time with the map, so it keeps its musical
// place whether it is the passive cursor or the armed caret. 5.25 s at 120 BPM is half a beat into
// 3:3 (global beat 10.5), which half the tempo puts at 10.5 s.
TEST_CASE("A typed tempo keeps the cursor on its musical place", "[core][tempo]")
{
    TempoEditor editor;
    editor.seek(5.25);

    SECTION("the passive cursor")
    {}
    SECTION("the armed caret")
    {
        editor.armCaretAtCursor();
    }

    const TempoChordTarget before = editor.chord();
    REQUIRE(std::holds_alternative<InsertTempoAnchorTarget>(before));
    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "60");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(10.5, 1e-9));
    CHECK(editor.chord() == before);
}

// The tempo lock is session state: on at every open, whatever the session before it left, and
// flipping it records nothing to undo.
TEST_CASE("The tempo lock is on at every open", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    CHECK(editor.state().tempo_lock);

    editor.controller.onTempoLockToggleRequested();
    CHECK_FALSE(editor.state().tempo_lock);
    CHECK(editor.undoEntryCount() == 0);

    editor.open(makeEightMeasureSong());
    REQUIRE(editor.project_services.open_call_count == 2);
    CHECK(editor.state().tempo_lock);
}

// While the lock is on, Alt+arrow on a selected anchor moves nothing and opens no entry; the chip
// flashes instead.
TEST_CASE("The tempo lock refuses an anchor nudge", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(100.0);
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.view.tempo_anchor_refusal_flashes == std::vector{beatAt(3, 1)});
}

// A drag is refused from its first event: nothing lands, no entry opens, and the chip the press
// selected — not the one selected before it — is the one that flashes.
TEST_CASE("The tempo lock refuses a tempo-chip drag", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    // The press on 5:1's chip selects it, as the ruler's press does, before any drag event.
    editor.controller.onTempoAnchorSelected(beatAt(5, 1));
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8300);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.view.tempo_anchor_refusal_flashes == std::vector{beatAt(5, 1), beatAt(5, 1)});
}

// The lock guards anchor TIMES only: inserting, typing a tempo and deleting all work while it is
// on, and none of them flashes.
TEST_CASE(
    "The tempo lock leaves insert, typed tempo and delete available", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    REQUIRE(editor.state().tempo_lock);

    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4000});

    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "60");
    CHECK(editor.undoTopLabel() == "Set Tempo to 60");

    editor.controller.onSelectionDeleteRequested();
    CHECK_FALSE(editor.anchorMilliseconds(beatAt(3, 1)).has_value());
    CHECK(editor.undoTopLabel() == "Delete Tempo Anchor");
    CHECK(editor.view.tempo_anchor_refusal_flashes.empty());
}

// Each refused gesture leaves the marker refusal record under the move's own label, naming the
// gesture and the anchor.
TEST_CASE("The tempo lock logs each refused gesture", "[core][tempo][tempo-lock]")
{
    const testing::LogCapture capture;
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.pressAlt(ChartStepDirection::Right);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);

    const std::string contents = capture.text();
    CHECK(
        contents.find(
            R"(Rejected marker edit label="Move Tempo Anchor" )"
            R"(detail="Tempo Lock is on: nudging the tempo anchor at 3:1 is blocked")") !=
        std::string::npos);
    CHECK(
        contents.find(
            R"(Rejected marker edit label="Move Tempo Anchor" )"
            R"(detail="Tempo Lock is on: dragging the tempo anchor at 3:1 is blocked")") !=
        std::string::npos);
}

// A refused drag repeats one record per event. The run's first record is written at once and
// unchanged; the repeats are only counted, and the run's count line goes out before the different
// record that ends it. That record, a lone refusal, gains no count line when the controller goes.
TEST_CASE("Identical consecutive refusals log one line and its count", "[core][tempo][tempo-lock]")
{
    const testing::LogCapture capture;
    {
        TempoEditor editor;
        editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
        for (std::int64_t milliseconds = 4100; milliseconds < 4500; milliseconds += 100)
        {
            editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), milliseconds);
        }
        editor.pressAlt(ChartStepDirection::Right);
    }

    const std::string dragged{g_locked_drag_refusal};
    CHECK(
        markerRefusalLines(capture.text()) ==
        std::vector<std::string>{
            dragged,
            dragged + " repeats=3",
            R"(Rejected marker edit label="Move Tempo Anchor" )"
            R"(detail="Tempo Lock is on: nudging the tempo anchor at 3:1 is blocked")",
        });
}

// A run still open when the controller goes writes its count then, so no count is lost on exit.
TEST_CASE("A refusal run's count is written when the controller goes", "[core][tempo][tempo-lock]")
{
    const testing::LogCapture capture;
    {
        TempoEditor editor;
        editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    }

    const std::string dragged{g_locked_drag_refusal};
    CHECK(
        markerRefusalLines(capture.text()) ==
        std::vector<std::string>{dragged, dragged + " repeats=1"});
}

} // namespace rock_hero::editor::core
