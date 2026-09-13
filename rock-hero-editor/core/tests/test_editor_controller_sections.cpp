#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::GridPosition;
using common::core::SongSection;

[[nodiscard]] GridPosition downbeat(int measure)
{
    return GridPosition{.measure = measure, .beat = 1};
}

// A 4/4 map at 120 BPM whose terminal downbeat is measure 5 beat 1, so measures 1-4 can carry a
// section and measure 5 is the closing barline every out-of-song refusal is measured against. The
// audio runs longer than the map on purpose, so the transport can rest at or past the terminal
// downbeat without the seek clamping short of it — which is the only way to aim a verb there.
[[nodiscard]] common::core::Song makeSectionSong(std::vector<SongSection> sections)
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(16.0),
        std::string{g_tone_document_ref});
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
    song.sections = std::move(sections);
    // A chart, so a caret can arm: the marker every section verb lands on IS the armed caret.
    song.arrangements.front().chart = makeTestChart();
    return song;
}

// Owns the fakes and a controller with the supplied song loaded, ready to drive section verbs.
struct LoadedSectionEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    explicit LoadedSectionEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
              },
          }
    {
        // The prepared duration is what the loaded arrangement's timeline ends up being, and the
        // timeline is what a seek clamps to; without it the fake's 4 s default would stop the
        // transport two measures short of the terminal downbeat these tests aim at.
        audio.next_prepared_audio_duration = common::core::TimeDuration{16.0};
        audio.next_set_active_arrangement_result = true;
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    }

    [[nodiscard]] const std::vector<SongSection>& sections() const
    {
        return controller.session().song().sections;
    }

    // Seeks the transport into a measure so the marker rule resolves there. 120 BPM 4/4 puts
    // measure N's downbeat at (N - 1) * 2 seconds.
    void seekToMeasure(int measure)
    {
        controller.onTimelineSeekRequested(
            common::core::TimePosition{static_cast<double>(measure - 1) * 2.0});
    }

    // The measure downbeat a section verb would land on, as the surface reads it: nothing while no
    // caret is armed.
    [[nodiscard]] std::optional<GridPosition> publishedMarkerDownbeat() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->section_marker_downbeat : std::nullopt;
    }

    // Arms the caret at the parked cursor: the first arrow press on a passive marker arms without
    // stepping, so this is the keyboard's own way of placing a marker verb's position.
    void armCaretAtCursor()
    {
        controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }

    // The published section views, which is where the selection outline is read from.
    [[nodiscard]] std::vector<SongSectionViewState> publishedSections() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->sections : std::vector<SongSectionViewState>{};
    }

    // True while the published state says a selection exists on some surface.
    [[nodiscard]] bool selectionPresent() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->selection_present;
    }

    // True while the published state offers an undo, which is how a refusal proves it pushed
    // nothing rather than pushing an entry that happens to restore the same list.
    [[nodiscard]] bool undoAvailable() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr && state->undo_enabled;
    }
};

} // namespace

// The whole point of the one-edit design: add, rename, move and delete all round-trip exactly,
// because each carries both whole lists and undo is an assignment rather than a replayed inverse.
TEST_CASE("EditorController adds a section at the marker's measure", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    editor.controller.onSongSectionInsertRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(3));
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    CHECK(editor.sections().empty());

    editor.controller.onRedoRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");
}

// The chord's two halves differ by what already stands where the press would land, so the measure
// it targets is published rather than re-derived by the surface: the section chord reads it to
// decide between inserting and restating, and a press over an existing section reopens that
// section's prompt on its own name. The marker IS the armed caret: a parked cursor with no caret
// publishes no marker at all, so a marker verb can never land a beat late off a moving transport.
TEST_CASE(
    "The published marker downbeat names the measure a section verb lands on", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(3), .name = "Chorus"}})};

    editor.seekToMeasure(2);
    CHECK_FALSE(editor.publishedMarkerDownbeat().has_value());

    editor.armCaretAtCursor();
    CHECK(editor.publishedMarkerDownbeat() == downbeat(2));
    // Nothing stands there, so the surface would insert.
    CHECK(std::ranges::none_of(editor.publishedSections(), [](const SongSectionViewState& section) {
        return section.position == GridPosition{.measure = 2, .beat = 1};
    }));

    // A mid-measure caret snaps back to the downbeat, the only place a section can start.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{5.0});
    editor.armCaretAtCursor();
    CHECK(editor.publishedMarkerDownbeat() == downbeat(3));
    // And the section already standing there is the one the chord would restate, name and all.
    // The published list is bound once: the accessor returns by value, so searching the call
    // itself would leave the iterator dangling.
    const std::vector<SongSectionViewState> published = editor.publishedSections();
    const auto at_marker = std::ranges::find_if(published, [](const SongSectionViewState& section) {
        return section.position == GridPosition{.measure = 3, .beat = 1};
    });
    REQUIRE(at_marker != published.end());
    CHECK(at_marker->name == "Chorus");
}

// An insert leaves what it made selected, as a typed note does, so the next verb acts on the new
// section; the caret it was typed from demotes in place, and an arrow re-arms it where it stood.
TEST_CASE("Section insert selects the new section and demotes the caret", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};
    editor.seekToMeasure(3);
    editor.armCaretAtCursor();
    REQUIRE(editor.publishedMarkerDownbeat() == downbeat(3));

    editor.controller.onSongSectionInsertRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK_FALSE(editor.publishedMarkerDownbeat().has_value());
    const std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 1);
    CHECK(published.front().selected);

    editor.armCaretAtCursor();
    CHECK(editor.publishedMarkerDownbeat() == downbeat(3));
}

// A section starts on a downbeat and nowhere else, so a marker resting mid-measure snaps back to
// that measure's downbeat instead of authoring a position the format would accept but the board
// could not promote a bar for.
TEST_CASE("Section insert snaps the marker to its measure downbeat", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Half a measure into measure 2: the verb snaps the position it was handed back to measure 2's
    // downbeat.
    editor.controller.onSongSectionInsertRequested(GridPosition{.measure = 2, .beat = 3}, "Verse");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(2));
}

// Every section verb refuses rather than clamping or overwriting: an empty name, a downbeat
// another section already holds, and a measure past the closing barline each leave the list alone.
TEST_CASE("Section insert refuses an empty name and a duplicate", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionInsertRequested(downbeat(2), "   ");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");

    editor.controller.onSongSectionInsertRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");

    // Nothing was committed, so there is no entry to undo.
    CHECK_FALSE(editor.undoAvailable());
}

TEST_CASE("Section insert refuses a measure past the song end", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong({})};

    // Measure 5 beat 1 is the terminal anchor: a section there would name a passage of no length.
    editor.controller.onSongSectionInsertRequested(downbeat(5), "Outro");
    CHECK(editor.sections().empty());
}

TEST_CASE("EditorController renames a section and round-trips it", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionRenameRequested(downbeat(2), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().name == "Verse");

    editor.controller.onRedoRequested();
    CHECK(editor.sections().front().name == "Chorus");
}

// The package reader forbids an empty name, so the verb must refuse one rather than author a
// section that would fail to save.
TEST_CASE("Section rename refuses an empty name", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionRenameRequested(downbeat(2), "  ");
    CHECK(editor.sections().front().name == "Verse");
    CHECK_FALSE(editor.undoAvailable());

    // A position no section starts at names nothing to rename.
    editor.controller.onSongSectionRenameRequested(downbeat(3), "Chorus");
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Verse");
}

// Alt+arrows move the selected section one MEASURE, because a measure is the section's step.
TEST_CASE("EditorController moves the selected section a measure", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().position == downbeat(3));

    // The position is the section's identity, so the selection moved with it and a second press
    // keeps going instead of losing its operand.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections().front().position == downbeat(2));

    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().position == downbeat(3));
    editor.controller.onUndoRequested();
    CHECK(editor.sections().front().position == downbeat(2));
}

TEST_CASE("Section move refuses an occupied downbeat and the song end", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    // Measure 3 already holds a section: refused, not swapped and not merged.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].position == downbeat(2));
    CHECK(editor.sections()[1].position == downbeat(3));

    // Measure 4 is the last that can carry a section; measure 5 is the closing barline.
    editor.controller.onSongSectionSelected(downbeat(3));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.sections()[1].position == downbeat(4));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.sections()[1].position == downbeat(4));

    // Measure 1 is the first: a step earlier from there leaves the grid.
    editor.controller.onSongSectionSelected(downbeat(2));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections()[0].position == downbeat(1));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.sections()[0].position == downbeat(1));
}

TEST_CASE("EditorController deletes the selected section", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};
    editor.controller.onSongSectionSelected(downbeat(2));

    editor.controller.onSelectionDeleteRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");

    editor.controller.onUndoRequested();
    REQUIRE(editor.sections().size() == 2);
    CHECK(editor.sections()[0].name == "Verse");
    CHECK(editor.sections()[1].name == "Chorus");

    editor.controller.onRedoRequested();
    REQUIRE(editor.sections().size() == 1);
    CHECK(editor.sections().front().name == "Chorus");
}

// The selection is an alternative of the one editor-wide selection, so it publishes through the
// same view state every other kind does, and it is what Delete finds.
TEST_CASE("A section selection round-trips through the view state", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"},
         SongSection{.position = downbeat(3), .name = "Chorus"}})};

    editor.controller.onSongSectionSelected(downbeat(3));
    std::vector<SongSectionViewState> published = editor.publishedSections();
    REQUIRE(published.size() == 2);
    CHECK_FALSE(published[0].selected);
    CHECK(published[1].selected);
    CHECK(editor.selectionPresent());

    // Deselecting releases the alternative and the outline with it.
    editor.controller.onSongSectionSelected(std::nullopt);
    published = editor.publishedSections();
    REQUIRE(published.size() == 2);
    CHECK_FALSE(published[0].selected);
    CHECK_FALSE(published[1].selected);
    CHECK_FALSE(editor.selectionPresent());
}

// A section chip is a timeline marker like a tone region, so it shares that lifecycle: moving the
// cursor releases it. Selecting a chip seeks nothing, which is what lets the pair coexist.
TEST_CASE("A section selection clears when the cursor moves", "[core][sections]")
{
    LoadedSectionEditor editor{makeSectionSong(
        {SongSection{.position = downbeat(2), .name = "Verse"}})};

    editor.controller.onSongSectionSelected(downbeat(2));
    REQUIRE(editor.selectionPresent());

    editor.seekToMeasure(4);
    CHECK_FALSE(editor.selectionPresent());
}

} // namespace rock_hero::editor::core
