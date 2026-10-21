#include "log_capture.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

    // Presses the tempo chord and returns the prompt it asked the view for; nothing where it asked
    // none, because it inserted at once, was refused or addressed nothing.
    [[nodiscard]] std::optional<MarkerPrompt> pressTempoChord()
    {
        return pressForPrompt(&EditorController::onTempoChordRequested);
    }

    // The position of the anchor Enter would retype, which is the selected one; empty when the
    // selection is no span-opening anchor.
    [[nodiscard]] std::optional<GridPosition> selectedAnchorPosition()
    {
        const std::optional<MarkerPrompt> restate =
            pressForPrompt(&EditorController::onSelectionRestateRequested);
        const auto* const target = promptAs<SetTempoTarget>(restate);
        return target != nullptr ? std::optional{target->position} : std::nullopt;
    }

    // Turns the tempo lock off, as a test that edits the map must: every open turns it on.
    void unlockTempo()
    {
        controller.onTempoLockToggleRequested();
        REQUIRE_FALSE(state().tempo_lock);
    }

    // Turns the tempo lock back on, over anchors a test inserted with it off.
    void lockTempo()
    {
        controller.onTempoLockToggleRequested();
        REQUIRE(state().tempo_lock);
    }

    // Inserts anchors with the lock off and turns it back on, the map a lock test refuses edits
    // to.
    void insertLocked(const std::initializer_list<GridPosition> positions)
    {
        unlockTempo();
        for (const GridPosition& position : positions)
        {
            controller.onTempoAnchorInsertRequested(position);
        }
        lockTempo();
    }

    // The chart's authored hand positions, empty without a chart.
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

// The locked-action refusal records in a captured log, in order, each from its record's name on.
[[nodiscard]] std::vector<std::string> lockRefusalLines(const std::string& log)
{
    constexpr std::string_view record = "Refused locked action ";
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < log.size())
    {
        const std::size_t end = std::min(log.find('\n', start), log.size());
        const std::string_view line = std::string_view{log}.substr(start, end - start);
        if (const std::size_t at = line.find(record); at != std::string_view::npos)
        {
            std::string_view text = line.substr(at);
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

} // namespace

// The chord's whole decision is made at the press: an insert at the cursor's slot where no anchor
// stands, landing at once since it has nothing to prompt for, and the BPM prompt — prefilled with
// what the chip prints — where one does, whether that anchor is selected or not.
TEST_CASE("The tempo chord acts on the verb at the cursor's slot", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();

    editor.seek(0.0);
    const std::optional<MarkerPrompt> unselected = editor.pressTempoChord();
    const auto* const restate = promptAs<SetTempoTarget>(unselected);
    REQUIRE(restate != nullptr);
    CHECK(restate->position == beatAt(1, 1));
    CHECK(restate->bpm_text == "120");
    CHECK_FALSE(editor.pressForPrompt(&EditorController::onSelectionRestateRequested).has_value());

    // Selecting the chip changes nothing about the chord, and gives Enter the same verb.
    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    CHECK(editor.pressTempoChord() == unselected);
    const std::optional<MarkerPrompt> enter =
        editor.pressForPrompt(&EditorController::onSelectionRestateRequested);
    const auto* const entered = promptAs<SetTempoTarget>(enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == *restate);

    editor.seek(2.0);
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.anchorMilliseconds(beatAt(2, 1)) == std::optional<std::int64_t>{2000});
}

// The chord reads the cursor at the placement quantum, as the tone chord does: the grid line with
// snap on, the tick with snap off, so an anchor stands between beats wherever the grid does. The
// insert pins the time the map gives that slot, and the next press restates the new anchor.
TEST_CASE("The tempo chord reads the cursor at the placement quantum", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 8});

    // 2.3 s is 0.6 of a beat into 2:1, whose nearest eighth is the half beat.
    editor.seek(2.3);
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.anchorMilliseconds(subBeatPosition()) == std::optional<std::int64_t>{2250});
    editor.controller.onUndoRequested();

    // With snap off the slot is the tick: 0.6 of a beat is 576 of its 960 ticks.
    turnGridSnapOff(editor.controller);
    editor.seek(2.3);
    const GridPosition tick{.measure = 2, .beat = 1, .offset = common::core::Fraction{3, 5}};
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.anchorMilliseconds(tick) == std::optional<std::int64_t>{2300});
    const SetTempoTarget expected{.position = tick, .bpm_text = "120"};
    const std::optional<MarkerPrompt> restated = editor.pressTempoChord();
    const auto* const restate = promptAs<SetTempoTarget>(restated);
    REQUIRE(restate != nullptr);
    CHECK(*restate == expected);
}

// The press asks for no prompt the commit would refuse: on the terminal, which opens no span, it
// is refused, logged and said on the lane — the Tempo Lock answering first while it is on, since
// nothing changes while it holds; while the transport plays and with no song it says nothing.
TEST_CASE("The tempo chord asks nothing where no anchor can stand", "[core][tempo]")
{
    const testing::LogCapture capture;
    TempoEditor editor;
    const common::core::TempoMap before = editor.tempoMap();
    const std::string sentence = "nothing can start at or past the end of the song";

    editor.seek(16.0);
    const std::size_t flashes = editor.view.refusal_flashes.size();
    CHECK_FALSE(editor.pressTempoChord().has_value());
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().sentence == g_tempo_lock_sentence);
    CHECK(editor.view.refusal_flashes.back().locks == std::vector{LockControl::Tempo});

    editor.unlockTempo();
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.tempoMap() == before);
    const std::string logged = capture.text();
    CHECK(
        logged.find(lockedActionRecord("TempoChord", g_tempo_lock_sentence)) != std::string::npos);
    CHECK(
        logged.find(R"(Rejected marker edit label="Add Tempo Anchor" detail=")" + sentence) !=
        std::string::npos);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 2);
    CHECK(editor.view.refusal_flashes.back().sentence == sentence);

    editor.transport.current_state.playing = true;
    editor.seek(2.0);
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.view.refusal_flashes.size() == flashes + 2);

    editor.transport.current_state.playing = false;
    editor.controller.onCloseRequested();
    CHECK_FALSE(editor.pressTempoChord().has_value());
    CHECK(editor.view.refusal_flashes.size() == flashes + 2);
}

// An insert pins the time the map already gives the beat, records one entry and selects what it
// made, so the chord's next press on the same beat opens the prompt rather than inserting again.
TEST_CASE(
    "A tempo insert selects the new anchor and the chord then sets its tempo", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.seek(4.0);

    CHECK_FALSE(editor.pressTempoChord().has_value());
    REQUIRE(editor.tempoMap().anchors().size() == 3);
    CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.0, 1e-9));
    CHECK(editor.undoEntryCount() == 1);
    CHECK(editor.undoTopLabel() == "Add Tempo Anchor");

    const SetTempoTarget expected{.position = beatAt(3, 1), .bpm_text = "120"};
    const std::optional<MarkerPrompt> enter =
        editor.pressForPrompt(&EditorController::onSelectionRestateRequested);
    const auto* const entered = promptAs<SetTempoTarget>(enter);
    REQUIRE(entered != nullptr);
    CHECK(*entered == expected);
    const std::optional<MarkerPrompt> chord = editor.pressTempoChord();
    const auto* const restate = promptAs<SetTempoTarget>(chord);
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
    editor.unlockTempo();
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
    const std::optional<MarkerPrompt> enter =
        editor.pressForPrompt(&EditorController::onSelectionRestateRequested);
    const auto* const entered = promptAs<SetTempoTarget>(enter);
    REQUIRE(entered != nullptr);
    CHECK(entered->bpm_text == "137");

    const common::core::TempoMap rippled = editor.tempoMap();
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "fast");
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "137x");
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "   ");
    CHECK(editor.tempoMap() == rippled);
    CHECK(editor.undoEntryCount() == 4);
}

// Text that is not a number is refused at the prompt, and lights the chip it was typed on.
TEST_CASE("A typed tempo that is not a number flashes its anchor", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const std::size_t flashes_before = editor.view.refusal_flashes.size();

    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "fast");

    REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 1);
    CHECK(editor.view.refusal_flashes.back().ruler_chips == std::vector{tempoChip(beatAt(3, 1))});
}

// The first anchor is refused, every map keeps it, and its chip flashes: the delete lands no
// anchor, but the one it addressed still stands. A delete merges the two spans with both outer
// anchors pinned, so undo restores the map exactly.
TEST_CASE("Deleting a tempo anchor round-trips through undo", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    const common::core::TempoMap original = editor.tempoMap();

    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onSelectionDeleteRequested();
    CHECK(editor.tempoMap() == original);
    CHECK(editor.undoEntryCount() == 0);
    CHECK(flashedRulerChips(editor.view) == std::vector{tempoChip(beatAt(1, 1))});

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

// A refused tempo edit lights its subject with what stood in its way: a step onto the next anchor's
// time lights both anchors of the pair the ordering rule names, and an insert at a position an
// anchor already holds lights that anchor, the place the insert addressed.
TEST_CASE("A refused tempo edit flashes the anchors in its way", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 2));
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    REQUIRE(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4000});
    REQUIRE(editor.anchorMilliseconds(beatAt(3, 2)) == std::optional<std::int64_t>{4500});
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    // At two pixels per second a step is half a second: 3:1 would land on 3:2's 4.5 s.
    editor.controller.onTimelineZoomChanged(2.0);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    REQUIRE_FALSE(editor.view.refusal_flashes.empty());
    CHECK(
        editor.view.refusal_flashes.back().ruler_chips ==
        std::vector{tempoChip(beatAt(3, 1)), tempoChip(beatAt(3, 2))});

    const std::size_t flashes_before = editor.view.refusal_flashes.size();
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 2));
    CHECK(editor.tempoMap() == inserted);
    REQUIRE(editor.view.refusal_flashes.size() == flashes_before + 1);
    CHECK(editor.view.refusal_flashes.back().ruler_chips == std::vector{tempoChip(beatAt(3, 2))});
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
    editor.unlockTempo();
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
    editor.unlockTempo();
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
    // A quarter grid, so a section's placement-line step is one beat.
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});

    SECTION("a section")
    {
        const std::vector<SongSection> original = editor.sections();
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAlt(ChartStepDirection::Right, 3);
        REQUIRE(editor.sections().size() == 2);
        CHECK(editor.sections().front().position == beatAt(2, 4));
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
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
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
        CHECK(editor.sections().front().position == beatAt(2, 3));
    }
}

// The run is ended by a selection change itself, not judged by comparing selections, so stepping
// away from the moved section and back onto it starts a new entry even though the selection reads
// the same again.
TEST_CASE("A marker move run ends when the selection leaves and returns", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onSongSectionSelected(beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right);
    REQUIRE(editor.sections().front().position == beatAt(2, 2));

    editor.controller.onRowObjectStepRequested(true, false);
    editor.controller.onRowObjectStepRequested(false, false);
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.sections().front().position == beatAt(2, 3));
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
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onSongSectionSelected(beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right, 2);
    REQUIRE(editor.sections().front().position == beatAt(2, 3));

    editor.controller.onUndoRequested();
    REQUIRE(editor.sections().front().position == beatAt(2, 1));
    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.sections().front().position == beatAt(2, 2));
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
// 3:3 (global beat 10.5), which half the tempo puts at 10.5 s. The cursor rides its music in one
// seek and one publish, and that is no keyboard move: the window stays where the charter left it.
TEST_CASE("A typed tempo keeps the cursor on its musical place", "[core][tempo]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.seek(5.25);

    SECTION("the passive cursor")
    {}
    SECTION("the armed caret")
    {
        editor.armCaretAtCursor();
    }

    // The section chord asks for nothing but the cursor's slot, so it reads where the cursor
    // stands.
    const std::optional<MarkerPrompt> before =
        editor.pressForPrompt(&EditorController::onSongSectionChordRequested);
    REQUIRE(promptAs<InsertSectionTarget>(before) != nullptr);
    const int seeks = editor.transport.seek_call_count;
    const int publishes = editor.view.set_state_call_count;
    const std::uint64_t moves = editor.state().keyboard_move_count;
    editor.controller.onTempoSpanBpmRequested(beatAt(1, 1), "60");
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(10.5, 1e-9));
    CHECK(editor.pressForPrompt(&EditorController::onSongSectionChordRequested) == before);
    CHECK(editor.transport.seek_call_count == seeks + 1);
    CHECK(editor.view.set_state_call_count == publishes + 1);
    CHECK(editor.state().keyboard_move_count == moves);
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
// flashes instead, with the lock's own button: the lock is what stood in the way.
TEST_CASE("The tempo lock refuses an anchor nudge", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.insertLocked({beatAt(3, 1)});
    editor.controller.onTimelineZoomChanged(100.0);
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAlt(ChartStepDirection::Right);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    REQUIRE(editor.view.refusal_flashes.size() == 1);
    CHECK(editor.view.refusal_flashes.back().ruler_chips == std::vector{tempoChip(beatAt(3, 1))});
    CHECK(editor.view.refusal_flashes.back().locks == std::vector{LockControl::Tempo});
}

// A drag is refused from its first event: nothing lands, nothing previews, no entry opens, and the
// chip the press selected — not the one selected before it — is the one that flashes, beside the
// lock's button.
TEST_CASE("The tempo lock refuses a tempo-chip drag", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.insertLocked({beatAt(5, 1), beatAt(3, 1)});
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    const std::size_t flashes = editor.view.refusal_flashes.size();

    // The press on 5:1's chip selects it, as the ruler's press does, before any drag event.
    editor.controller.onTempoAnchorSelected(beatAt(5, 1));
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8300);
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8500);
    CHECK(editor.gridAnchorMilliseconds(beatAt(5, 1)) == std::optional<std::int64_t>{8000});
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    // One flash per event, each the dragged chip and the lock, saying the lock's logged sentence.
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 3);
    for (std::size_t index = flashes; index < editor.view.refusal_flashes.size(); ++index)
    {
        const RefusalFlash& flash = editor.view.refusal_flashes[index];
        CHECK(flash.ruler_chips == std::vector{tempoChip(beatAt(5, 1))});
        CHECK(flash.locks == std::vector{LockControl::Tempo});
        CHECK(flash.notes.empty());
        CHECK(flash.tone_regions.empty());
        CHECK(flash.sentence == g_tempo_lock_sentence);
    }
}

// A lock engaged mid-drag, by a bound key, refuses the release: the time the drag held is not
// committed, the drag still ends so nothing previews the move that never landed, and the chip
// flashes with the lock.
TEST_CASE("The tempo lock refuses a drag's release", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.unlockTempo();
    editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    editor.controller.onTempoAnchorSelected(beatAt(5, 1));
    editor.controller.onTempoAnchorMoveRequested(beatAt(5, 1), 8300);
    REQUIRE(editor.gridAnchorMilliseconds(beatAt(5, 1)) == std::optional<std::int64_t>{8300});

    editor.lockTempo();
    const std::size_t flashes = editor.view.refusal_flashes.size();
    editor.controller.onTempoAnchorDragReleased();
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.gridAnchorMilliseconds(beatAt(5, 1)) == std::optional<std::int64_t>{8000});
    CHECK(editor.undoEntryCount() == entries);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().ruler_chips == std::vector{tempoChip(beatAt(5, 1))});
    CHECK(editor.view.refusal_flashes.back().locks == std::vector{LockControl::Tempo});
}

// The lock freezes the whole map, so the verbs that make, retype, delete or scale an anchor are
// refused as the moves are, first: each leaves the map and the history as they stood and flashes
// the selection — the anchor the insert left selected — beside the lock's button. With the lock
// off every one of them lands, as the tests above pin.
TEST_CASE("The tempo lock refuses every tempo verb", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.insertLocked({beatAt(3, 1)});
    REQUIRE(editor.state().selected_tempo_anchor == std::optional{beatAt(3, 1)});
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    const std::size_t flashes = editor.view.refusal_flashes.size();

    SECTION("an insert")
    {
        editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    }
    SECTION("a typed tempo")
    {
        editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "60");
    }
    SECTION("a delete")
    {
        editor.controller.onSelectionDeleteRequested();
    }
    SECTION("Enter, which opens no prompt")
    {
        CHECK_FALSE(
            editor.pressForPrompt(&EditorController::onSelectionRestateRequested).has_value());
    }
    SECTION("Scale Tempo, the notes following the beat")
    {
        editor.controller.onScaleTempoRequested("2");
    }
    SECTION("Scale Tempo, the notes keeping their time")
    {
        editor.controller.onNoteLockToggleRequested();
        editor.controller.onScaleTempoRequested("2");
    }

    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().ruler_chips == std::vector{tempoChip(beatAt(3, 1))});
    CHECK(editor.view.refusal_flashes.back().locks == std::vector{LockControl::Tempo});
}

// A locked chord answers with the lock before anything else, wherever the cursor stands: on a free
// slot it inserts nothing, on an anchor it opens no prompt, and past the terminal the lock — not
// the song's end — is the refusal.
TEST_CASE("A locked tempo chord opens no prompt and flashes the lock", "[core][tempo][tempo-lock]")
{
    TempoEditor editor;
    editor.insertLocked({beatAt(3, 1)});
    double seconds = 0.0;
    SECTION("a free slot")
    {
        seconds = 6.0;
    }
    SECTION("an anchor")
    {
        seconds = 4.0;
    }
    SECTION("past the terminal")
    {
        seconds = 16.0;
    }
    editor.seek(seconds);
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t flashes = editor.view.refusal_flashes.size();

    CHECK_FALSE(editor.pressForPrompt(&EditorController::onTempoChordRequested).has_value());
    CHECK(editor.tempoMap() == inserted);
    REQUIRE(editor.view.refusal_flashes.size() == flashes + 1);
    CHECK(editor.view.refusal_flashes.back().locks == std::vector{LockControl::Tempo});
    CHECK(editor.view.refusal_flashes.back().sentence == g_tempo_lock_sentence);
}

// Each refused action leaves the locked-action record under its own name, the lock's one sentence
// beside it.
TEST_CASE("The tempo lock logs each refused verb", "[core][tempo][tempo-lock]")
{
    const testing::LogCapture capture;
    TempoEditor editor;
    editor.insertLocked({beatAt(3, 1)});
    editor.pressAlt(ChartStepDirection::Right);
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "60");

    const std::string contents = capture.text();
    CHECK(
        contents.find(lockedActionRecord("MoveSelection", g_tempo_lock_sentence)) !=
        std::string::npos);
    CHECK(
        contents.find(lockedActionRecord("SetTempoSpanBpm", g_tempo_lock_sentence)) !=
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
        editor.insertLocked({beatAt(3, 1)});
        for (std::int64_t milliseconds = 4100; milliseconds < 4500; milliseconds += 100)
        {
            editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), milliseconds);
        }
        editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "60");
    }

    const std::string dragged = lockedActionRecord("MoveTempoAnchor", g_tempo_lock_sentence);
    CHECK(
        lockRefusalLines(capture.text()) ==
        std::vector<std::string>{
            dragged,
            dragged + " repeats=3",
            lockedActionRecord("SetTempoSpanBpm", g_tempo_lock_sentence),
        });
}

// A run still open when the controller goes writes its count then, so no count is lost on exit.
TEST_CASE("A refusal run's count is written when the controller goes", "[core][tempo][tempo-lock]")
{
    const testing::LogCapture capture;
    {
        TempoEditor editor;
        editor.insertLocked({beatAt(3, 1)});
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
        editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    }

    const std::string dragged = lockedActionRecord("MoveTempoAnchor", g_tempo_lock_sentence);
    CHECK(
        lockRefusalLines(capture.text()) ==
        std::vector<std::string>{dragged, dragged + " repeats=1"});
}

} // namespace rock_hero::editor::core
