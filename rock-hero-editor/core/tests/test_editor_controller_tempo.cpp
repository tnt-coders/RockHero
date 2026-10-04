#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
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
using common::core::testing::commonTimeMap;

[[nodiscard]] GridPosition beatAt(int measure, int beat)
{
    return GridPosition{.measure = measure, .beat = beat, .offset = {}};
}

// A 4/4 map at 120 BPM whose terminal is 9:1 at 16 s, so measure N's downbeat sits at (N - 1) * 2
// seconds. The audio runs to 20 s, past the terminal, so the transport can rest ON the terminal
// beat without a seek clamping short of it. Two sections and two hand placements give every marker
// kind something to move, and the shared chart puts a note at 2:1 for the projection to place.
[[nodiscard]] common::core::Song makeTempoSong()
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(20.0),
        std::string{g_tone_document_ref});
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{16.0});
    song.sections = {
        SongSection{.position = beatAt(2, 1), .name = "Verse"},
        SongSection{.position = beatAt(6, 1), .name = "Chorus"},
    };
    common::core::Chart chart = makeTestChart();
    chart.fret_hand_positions = {
        common::core::FretHandPosition{.position = beatAt(1, 1), .fret = 5, .end_fret = {}},
        common::core::FretHandPosition{.position = beatAt(3, 1), .fret = 7, .end_fret = {}},
    };
    song.arrangements.front().chart = std::move(chart);
    return song;
}

// The half beat after 2:1, where the sub-beat song's extra anchor stands.
[[nodiscard]] GridPosition subBeatPosition()
{
    return GridPosition{.measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}};
}

// The tempo song with a sub-beat anchor at 2:1+1/2 beside the on-beat one at 2:1, both at the 120
// BPM the map already has: the pair an address floored to its beat cannot tell apart.
[[nodiscard]] common::core::Song makeSubBeatTempoSong()
{
    common::core::Song song = makeTempoSong();
    song.tempo_map = commonTimeMap({
        anchor("1:1", 0),
        anchor("2:1", 2000),
        anchor("2:1+1/2", 2250),
        anchor("9:1", 16000),
    });
    return song;
}

// Owns the fakes and a controller with the tempo song loaded and its rig ready.
struct TempoEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    TempoEditor()
        : TempoEditor{makeTempoSong()}
    {}

    explicit TempoEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
                  .save_function = project_services.saveFunction(),
              },
          }
    {
        audio.next_prepared_audio_duration = common::core::TimeDuration{20.0};
        audio.next_set_active_arrangement_result = true;
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    }

    [[nodiscard]] const EditorViewState& state() const
    {
        REQUIRE(view.last_state.has_value());
        if (!view.last_state.has_value())
        {
            throw std::logic_error("editor pushed no view state");
        }
        return *view.last_state;
    }

    [[nodiscard]] const common::core::TempoMap& tempoMap() const
    {
        return controller.session().song().tempo_map;
    }

    // The seconds of the anchor standing exactly on a beat, or NaN (which no matcher accepts) when
    // none does.
    [[nodiscard]] double anchorSeconds(int measure, int beat) const
    {
        for (const common::core::BeatAnchor& anchor : tempoMap().anchors())
        {
            if (anchor.position ==
                common::core::GridPosition{.measure = measure, .beat = beat, .offset = {}})
            {
                return anchor.seconds();
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    // The stored time of the anchor standing exactly at a position, or nothing when none does.
    [[nodiscard]] std::optional<std::int64_t> anchorMilliseconds(const GridPosition& position) const
    {
        const std::vector<common::core::BeatAnchor>& anchors = tempoMap().anchors();
        const auto found =
            std::ranges::find(anchors, position, &common::core::BeatAnchor::position);
        if (found == anchors.end())
        {
            return std::nullopt;
        }
        return found->milliseconds;
    }

    void seek(double seconds)
    {
        controller.onTimelineSeekRequested(common::core::TimePosition{seconds});
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

    // Turns snapping off, which only the warning's own answer can do.
    void turnSnapOff()
    {
        controller.onGridSnapToggleRequested();
        controller.onGridSnapWarningDecision(GridSnapWarningDecision::TurnSnappingOff);
    }

    [[nodiscard]] std::size_t undoEntryCount() const
    {
        return state().undo_history.labels.size();
    }

    // The label Ctrl+Z would undo next, or empty when nothing is undoable.
    [[nodiscard]] std::string undoTopLabel() const
    {
        const UndoHistoryState& history = state().undo_history;
        return history.position == 0 ? std::string{} : history.labels.at(history.position - 1);
    }

    [[nodiscard]] const std::vector<SongSection>& sections() const
    {
        return controller.session().song().sections;
    }

    [[nodiscard]] std::vector<common::core::FretHandPosition> placements() const
    {
        const common::core::Chart* const chart = chartOrNull(controller);
        return chart != nullptr ? chart->fret_hand_positions
                                : std::vector<common::core::FretHandPosition>{};
    }

    void pressAltRight(int presses = 1)
    {
        for (int press = 0; press < presses; ++press)
        {
            controller.onSelectionMoveRequested(ChartStepDirection::Right);
        }
    }

    // Arms the caret at the parked cursor: the first arrow press on a passive marker arms in place.
    void armCaretAtCursor()
    {
        controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }
};

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
    editor.turnSnapOff();
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
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(100.0);

    editor.pressAltRight();
    CHECK_THAT(editor.anchorSeconds(3, 1), WithinAbs(4.010, 1e-9));
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(4.010, 1e-9));
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");

    // At one pixel per second a step is a whole second, which would carry 3:1 past 3:2 at 4.51 s.
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 2));
    editor.controller.onTempoAnchorSelected(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(1.0);
    const common::core::TempoMap before = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();
    editor.pressAltRight();
    CHECK(editor.tempoMap() == before);
    CHECK(editor.undoEntryCount() == entries);
}

// A sub-beat anchor is addressed by its whole position: nudged, retyped and deleted as ITSELF,
// never as the on-beat anchor of the beat it falls in.
TEST_CASE("A sub-beat tempo anchor is edited as itself, never its beat-mate", "[core][tempo]")
{
    TempoEditor editor{makeSubBeatTempoSong()};
    const GridPosition on_beat = beatAt(2, 1);
    const GridPosition sub_beat = subBeatPosition();

    editor.controller.onTempoAnchorSelected(sub_beat);
    editor.controller.onTimelineZoomChanged(100.0);
    editor.pressAltRight();
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
    const GridPosition sub_beat = subBeatPosition();
    editor.controller.onTempoAnchorSelected(sub_beat);
    editor.controller.onTimelineZoomChanged(100.0);
    editor.pressAltRight();
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
        editor.pressAltRight(3);
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
        editor.controller.onFretHandPositionSelected(1);
        editor.pressAltRight(3);
        CHECK_FALSE(editor.placements() == original);
        CHECK(editor.undoEntryCount() == 1);
        editor.controller.onUndoRequested();
        CHECK(editor.placements() == original);
    }
    SECTION("a tempo anchor")
    {
        editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
        const common::core::TempoMap inserted = editor.tempoMap();
        editor.controller.onTimelineZoomChanged(100.0);
        editor.pressAltRight(3);
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
        editor.pressAltRight();
        editor.controller.onSongSectionSelected(beatAt(6, 1));
        editor.pressAltRight();
        CHECK(editor.undoEntryCount() == 2);
    }
    SECTION("a walk back to the start")
    {
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAltRight();
        REQUIRE(editor.undoEntryCount() == 1);
        editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
        CHECK(editor.sections() == original);
        CHECK(editor.undoEntryCount() == 0);
    }
    SECTION("a save")
    {
        editor.controller.onSongSectionSelected(beatAt(2, 1));
        editor.pressAltRight();
        editor.controller.onSaveRequested();
        REQUIRE(editor.project_services.save_call_count == 1);
        editor.pressAltRight();
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
    editor.pressAltRight();
    REQUIRE(editor.sections().front().position == beatAt(3, 1));

    editor.controller.onRowObjectStepRequested(true, false);
    editor.controller.onRowObjectStepRequested(false, false);
    editor.pressAltRight();
    CHECK(editor.sections().front().position == beatAt(4, 1));
    CHECK(editor.undoEntryCount() == 2);
}

// Any other edit is a commit point: a typed tempo between two presses on the same anchor splits the
// presses into entries of their own.
TEST_CASE("A typed tempo between two anchor moves ends the run", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    editor.controller.onTimelineZoomChanged(100.0);
    const std::size_t entries = editor.undoEntryCount();

    editor.pressAltRight();
    editor.controller.onTempoSpanBpmRequested(beatAt(3, 1), "137");
    editor.pressAltRight();
    CHECK(editor.undoEntryCount() == entries + 3);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
}

// An undo ends the run: the press after it starts a new entry from where the undo left the marker,
// rather than widening the entry the undo walked away from.
TEST_CASE("A press after an undo mid-run records a new entry", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onSongSectionSelected(beatAt(2, 1));
    editor.pressAltRight(2);
    REQUIRE(editor.sections().front().position == beatAt(4, 1));

    editor.controller.onUndoRequested();
    REQUIRE(editor.sections().front().position == beatAt(2, 1));
    editor.pressAltRight();
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
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    editor.controller.onTimelineZoomChanged(100.0);

    editor.pressAltRight();
    REQUIRE_FALSE(editor.tempoMap() == inserted);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.tempoMap() == inserted);
    REQUIRE(editor.audio.last_mirrored_tempo_map.has_value());
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(*editor.audio.last_mirrored_tempo_map == inserted);
    }
}

// A tempo-chip drag is a held run: every event moves the model live, the whole drag is ONE entry
// however far it wanders, and one Ctrl+Z puts the anchor back where the press found it.
TEST_CASE("A tempo-chip drag is one undo entry", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4100});
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4300);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4200);
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{4200});
    CHECK_THAT(editor.transport.position().seconds, WithinAbs(4.2, 1e-9));
    CHECK(editor.undoEntryCount() == entries + 1);
    CHECK(editor.undoTopLabel() == "Move Tempo Anchor");
    CHECK(editor.selectedAnchorPosition() == std::optional{beatAt(3, 1)});

    editor.controller.onUndoRequested();
    CHECK(editor.tempoMap() == inserted);
}

// A refused event changes nothing — the anchor stays at its last legal time — and the drag goes on:
// a later legal event still lands in the same entry.
TEST_CASE("A refused tempo-chip drag event holds the last legal time", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(5, 1));
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 5000);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 8000);
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{5000});
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 9000);
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{5000});
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 6000);
    CHECK(editor.anchorMilliseconds(beatAt(3, 1)) == std::optional<std::int64_t>{6000});
    CHECK(editor.anchorMilliseconds(beatAt(5, 1)) == std::optional<std::int64_t>{8000});
    CHECK(editor.undoEntryCount() == entries + 1);

    editor.controller.onUndoRequested();
    CHECK(editor.tempoMap() == inserted);

    // The first anchor may not go below zero.
    editor.controller.onTempoAnchorSelected(beatAt(1, 1));
    editor.controller.onTempoAnchorMoveRequested(beatAt(1, 1), -1);
    CHECK(editor.anchorMilliseconds(beatAt(1, 1)) == std::optional<std::int64_t>{0});
}

// A drag that ends where it began leaves no Ctrl+Z that changes nothing.
TEST_CASE("A tempo-chip drag back to its origin leaves no entry", "[core][tempo]")
{
    TempoEditor editor;
    editor.controller.onTempoAnchorInsertRequested(beatAt(3, 1));
    const common::core::TempoMap inserted = editor.tempoMap();
    const std::size_t entries = editor.undoEntryCount();

    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4100);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 3900);
    editor.controller.onTempoAnchorMoveRequested(beatAt(3, 1), 4000);
    CHECK(editor.tempoMap() == inserted);
    CHECK(editor.undoEntryCount() == entries);
    CHECK(editor.undoTopLabel() == "Add Tempo Anchor");
}

// A sub-beat anchor drags as itself, never as the on-beat anchor of its beat, which bounds it.
TEST_CASE("A sub-beat tempo anchor drags as itself", "[core][tempo]")
{
    TempoEditor editor{makeSubBeatTempoSong()};
    const GridPosition sub_beat = subBeatPosition();
    editor.controller.onTempoAnchorSelected(sub_beat);

    editor.controller.onTempoAnchorMoveRequested(sub_beat, 2300);
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2300});
    editor.controller.onTempoAnchorMoveRequested(sub_beat, 2000);
    CHECK(editor.anchorMilliseconds(sub_beat) == std::optional<std::int64_t>{2300});
    CHECK(editor.anchorMilliseconds(beatAt(2, 1)) == std::optional<std::int64_t>{2000});
}

// The drag moves only the SELECTED anchor, the one its press selected: once the anchor stops being
// the selection (Esc released it), the rest of the drag moves nothing, and while the transport
// plays nothing moves at all, as for every marker edit.
TEST_CASE("A tempo-chip drag moves only the selected anchor while paused", "[core][tempo]")
{
    TempoEditor editor;
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

} // namespace rock_hero::editor::core
