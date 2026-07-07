#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A second canonical tone the create/delete tests reuse, distinct from the harness default tone.
constexpr const char* g_second_tone_ref = "tones/1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d/tone.json";
constexpr const char* g_region_a = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";
constexpr const char* g_region_b = "6b2e1d4f-8a3c-4b1d-9e2f-3a4b5c6d7e8f";
constexpr const char* g_region_new = "7c3f2e5a-9b4d-4c2e-af3a-4b5c6d7e8f90";

[[nodiscard]] common::core::ToneGridPosition gridAt(int measure, int beat)
{
    return common::core::ToneGridPosition{.measure = measure, .beat = beat};
}

// Pins a 4/4 map at 120 BPM whose terminal downbeat is measure 3 beat 1, so authored regions across
// measures 1-3 satisfy the same structural rules persistence enforces.
[[nodiscard]] common::core::Song makeToneSongBase()
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(4.0),
        std::string{g_tone_document_ref});
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    common::core::Arrangement& arrangement = song.arrangements.front();
    arrangement.tones = {
        common::core::Tone{.tone_document_ref = g_tone_document_ref, .name = "Clean"},
        common::core::Tone{.tone_document_ref = g_second_tone_ref, .name = "Dirty"},
    };
    return song;
}

// One whole-song region referencing the Clean tone; the Dirty tone stays a spare to reuse.
[[nodiscard]] common::core::Song makeSingleRegionSong()
{
    common::core::Song song = makeToneSongBase();
    song.arrangements.front().tone_track.regions = {
        common::core::ToneRegion{
            .id = g_region_a,
            .start = gridAt(1, 1),
            .end = gridAt(3, 1),
            .tone_document_ref = g_tone_document_ref,
        },
    };
    return song;
}

// Two adjacent regions: Clean over [1.1, 2.1) and Dirty over [2.1, 3.1).
[[nodiscard]] common::core::Song makeTwoRegionSong()
{
    common::core::Song song = makeToneSongBase();
    song.arrangements.front().tone_track.regions = {
        common::core::ToneRegion{
            .id = g_region_a,
            .start = gridAt(1, 1),
            .end = gridAt(2, 1),
            .tone_document_ref = g_tone_document_ref,
        },
        common::core::ToneRegion{
            .id = g_region_b,
            .start = gridAt(2, 1),
            .end = gridAt(3, 1),
            .tone_document_ref = g_second_tone_ref,
        },
    };
    return song;
}

// Owns the fakes and a controller with the supplied song loaded, ready to drive tone edits.
struct LoadedToneEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    explicit LoadedToneEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
              },
          }
    {
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    }

    [[nodiscard]] const common::core::Arrangement& arrangement() const
    {
        return *controller.session().currentArrangement();
    }

    [[nodiscard]] const std::vector<common::core::ToneRegion>& regions() const
    {
        return arrangement().tone_track.regions;
    }
};

} // namespace

TEST_CASE(
    "EditorController renames a catalog tone and its regions relabel", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    editor.controller.onToneRenameRequested(g_second_tone_ref, "Rhythm");
    CHECK(common::core::toneNameFor(editor.arrangement(), g_second_tone_ref) == "Rhythm");

    editor.controller.onUndoRequested();
    CHECK(common::core::toneNameFor(editor.arrangement(), g_second_tone_ref) == "Dirty");

    editor.controller.onRedoRequested();
    CHECK(common::core::toneNameFor(editor.arrangement(), g_second_tone_ref) == "Rhythm");
}

TEST_CASE(
    "EditorController deletes a tone region into the previous region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    editor.controller.onToneRegionDeleteRequested(g_region_b);
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
    CHECK(editor.regions().front().end == gridAt(3, 1));

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].id == g_region_a);
    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].id == g_region_b);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);

    editor.controller.onRedoRequested();
    CHECK(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
}

TEST_CASE(
    "EditorController deletes the first tone region into the next region",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    editor.controller.onToneRegionDeleteRequested(g_region_a);
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_b);
    CHECK(editor.regions().front().start == gridAt(1, 1));

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].id == g_region_a);
    CHECK(editor.regions()[0].start == gridAt(1, 1));
    CHECK(editor.regions()[1].id == g_region_b);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

TEST_CASE(
    "EditorController leaves the only tone region in place on delete", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);
    const std::string only_id = editor.regions().front().id;

    editor.controller.onToneRegionDeleteRequested(only_id);

    // The only-region delete is the reset case (clear the chain, rename to "default"), which needs
    // the audio boundary and is not wired here, so the region is left covering the song.
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == only_id);
}

TEST_CASE("EditorController creates a tone-change region by splitting", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);

    editor.controller.onToneRegionCreateRequested(gridAt(2, 1), g_region_new, g_second_tone_ref);
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].id == g_region_a);
    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].id == g_region_new);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].end == gridAt(3, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
    CHECK(editor.regions().front().end == gridAt(3, 1));

    editor.controller.onRedoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].id == g_region_new);
}

TEST_CASE(
    "EditorController moves a shared tone boundary across both neighbors",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    editor.controller.onToneBoundaryMoveRequested(g_region_b, gridAt(2, 3));
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].end == gridAt(2, 3));   // earlier region extends to the new boundary
    CHECK(editor.regions()[1].start == gridAt(2, 3)); // later region starts there too, no gap

    editor.controller.onUndoRequested();
    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));

    editor.controller.onRedoRequested();
    CHECK(editor.regions()[0].end == gridAt(2, 3));
    CHECK(editor.regions()[1].start == gridAt(2, 3));
}

TEST_CASE(
    "EditorController ignores a boundary move on the first region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // The first region has no earlier neighbor; its start is the pinned song boundary.
    editor.controller.onToneBoundaryMoveRequested(g_region_a, gridAt(1, 3));

    CHECK(editor.regions()[0].start == gridAt(1, 1));
    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

TEST_CASE(
    "EditorController rejects a boundary move that empties a region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // Moving the boundary onto the later region's end would leave it empty; rules reject it.
    editor.controller.onToneBoundaryMoveRequested(g_region_b, gridAt(3, 1));

    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].end == gridAt(3, 1));
}

} // namespace rock_hero::editor::core
