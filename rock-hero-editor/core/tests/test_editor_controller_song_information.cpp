#include "controller/editor_action_availability.h"
#include "log_capture.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

using common::core::SongMetadata;

// The information every scenario opens on: stored trimmed, as every producer stores it.
[[nodiscard]] SongMetadata storedInformation()
{
    return SongMetadata{.title = "Stored", .artist = "Band", .album = "Record", .year = 1987};
}

// A song carrying the stored information; song information needs no chart.
[[nodiscard]] common::core::Song makeInformationSong()
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(16.0),
        std::string{g_tone_document_ref});
    song.metadata = storedInformation();
    return song;
}

// A controller with the information song loaded, ready to take dialog submissions.
struct LoadedInformationEditor : SongEditorHarness
{
    LoadedInformationEditor()
    {
        open(makeInformationSong());
    }

    // The song's information as the session holds it.
    [[nodiscard]] const SongMetadata& information() const
    {
        return controller.session().song().metadata;
    }

    // The number of entries the published history holds.
    [[nodiscard]] std::size_t entryCount() const
    {
        return state().undo_history.labels.size();
    }

    // Whether the history cursor sits on the state the file holds: the project's "no unsaved
    // edits" answer.
    [[nodiscard]] bool atCleanState() const
    {
        const EditorViewState& published = state();
        return published.undo_history.clean_position.has_value() &&
               *published.undo_history.clean_position == published.undo_history.position;
    }
};

} // namespace

// A submission lands trimmed as one entry named for the dialog, and the published state carries
// the information the next open of the dialog seeds from.
TEST_CASE("Song information lands trimmed as one undo entry", "[core][song-information]")
{
    LoadedInformationEditor editor;

    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = "  Title ", .artist = "Artist", .album = "", .year = 1999});

    const SongMetadata expected{.title = "Title", .artist = "Artist", .album = "", .year = 1999};
    CHECK(editor.information() == expected);
    CHECK(editor.entryCount() == 1);
    CHECK(editor.state().undo_label == std::optional<std::string>{"Edit Song Information"});
    CHECK(editor.state().song_metadata == expected);
    CHECK(editor.state().song_information_enabled);
}

// The entry holds the whole information on both sides, so undo restores exactly what was stored
// and redo exactly what was submitted.
TEST_CASE("Song information undo and redo restore each whole side", "[core][song-information]")
{
    LoadedInformationEditor editor;
    const SongMetadata submitted{.title = "New", .artist = "", .album = "Album", .year = 0};

    editor.controller.onSongInformationSubmitted(submitted);
    REQUIRE(editor.entryCount() == 1);

    editor.controller.onUndoRequested();
    CHECK(editor.information() == storedInformation());
    CHECK(editor.state().song_metadata == storedInformation());

    editor.controller.onRedoRequested();
    CHECK(editor.information() == submitted);
    CHECK(editor.state().song_metadata == submitted);
}

// OK on an untouched dialog records nothing, and neither does one whose only difference is the
// whitespace the trim takes away again.
TEST_CASE("An unchanged song information submission records nothing", "[core][song-information]")
{
    LoadedInformationEditor editor;

    editor.controller.onSongInformationSubmitted(storedInformation());
    CHECK(editor.entryCount() == 0);

    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = " Stored", .artist = "Band\t", .album = "  Record  ", .year = 1987});
    CHECK(editor.entryCount() == 0);
    CHECK(editor.information() == storedInformation());
    CHECK(editor.atCleanState());
}

// The one rule: a year is positive or 0 for unknown. A negative one is refused whole, so nothing
// else in the submission lands either, and the log names the reason under the verb's label.
TEST_CASE("A negative song year is refused and records nothing", "[core][song-information]")
{
    const testing::LogCapture capture;
    LoadedInformationEditor editor;

    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = "Other", .artist = "Band", .album = "Record", .year = -1});

    CHECK(editor.information() == storedInformation());
    CHECK(editor.entryCount() == 0);
    CHECK(
        capture.text().find(
            R"(Rejected marker edit label="Edit Song Information" detail="a year is a positive)") !=
        std::string::npos);
}

// Song information is song-level and timeless: a project is its one condition, no lock governs it,
// and the input-calibration prompt holds it back like every other edit of the project.
TEST_CASE("Song information needs a project and nothing else", "[core][song-information]")
{
    ActionConditions conditions;
    CHECK(
        whyUnavailable(EditorAction::Id::SetSongInformation, conditions) ==
        std::optional{ActionUnavailableReason::NoProject});

    conditions.has_project = true;
    conditions.transport_playing = true;
    CHECK(isActionAvailable(EditorAction::Id::SetSongInformation, conditions));
    CHECK(governingLock(EditorAction::Id::SetSongInformation) == ActionLock::None);

    conditions.input_calibration_prompt_visible = true;
    CHECK(
        whyUnavailable(EditorAction::Id::SetSongInformation, conditions) ==
        std::optional{ActionUnavailableReason::InputCalibrationPrompt});

    SongEditorHarness editor;
    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = "Nowhere", .artist = "", .album = "", .year = 0});
    CHECK(editor.controller.session().song().metadata == SongMetadata{});
    CHECK_FALSE(editor.state().song_information_enabled);
}

// The edit is an ordinary history entry: it leaves the project with unsaved changes, and undoing
// it returns the project to the state the file holds.
TEST_CASE("Song information edits mark the project unsaved", "[core][song-information]")
{
    LoadedInformationEditor editor;
    REQUIRE(editor.atCleanState());

    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = "Changed", .artist = "Band", .album = "Record", .year = 1987});
    CHECK_FALSE(editor.atCleanState());

    editor.controller.onUndoRequested();
    CHECK(editor.atCleanState());
}

// The suggested project name is read off the song's information, so an edit renames it at once.
TEST_CASE("Song information names the suggested project file", "[core][song-information]")
{
    LoadedInformationEditor editor;
    CHECK(editor.state().suggested_project_file.filename() == "Band - Stored.rhp");

    editor.controller.onSongInformationSubmitted(
        SongMetadata{.title = "Renamed", .artist = "Other Band", .album = "Record", .year = 1987});

    CHECK(editor.state().suggested_project_file.filename() == "Other Band - Renamed.rhp");
}

} // namespace rock_hero::editor::core
