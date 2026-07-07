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
constexpr const char* g_minted_ref = "tones/3a4b5c6d-7e8f-4a1b-8c2d-9e0f1a2b3c4d/tone.json";

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
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    explicit LoadedToneEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig),
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

TEST_CASE(
    "EditorController creates a new tone by minting and splitting", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);
    editor.live_rig.next_mint_ref = g_minted_ref;
    const int loads_before = editor.live_rig.load_call_count;

    editor.controller.onToneCreateNewRequested(gridAt(2, 1), "Solo");

    // The region is split and the later half references the freshly minted tone.
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].end == gridAt(2, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    // The catalog gained the new tone, and the rig was minted once and reloaded to add its branch.
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.live_rig.mint_call_count == 1);
    CHECK(editor.live_rig.load_call_count == loads_before + 1);
    // The reload replaces branches from disk, so unsaved branch drift is captured first.
    CHECK(editor.live_rig.capture_call_count == 1);

    // Undo removes both the region and the catalog tone (pure model).
    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().end == gridAt(3, 1));
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref).empty());

    // Redo recreates both without re-minting.
    editor.controller.onRedoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.live_rig.mint_call_count == 1);
}

TEST_CASE("EditorController resets the sole tone region on delete", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);
    const std::string only_id = editor.regions().front().id;
    editor.live_rig.next_mint_ref = g_minted_ref;
    // The reset's reload reports only the fresh tone's branch, dropping the previous tone from
    // the rig, exactly as the real engine would.
    editor.live_rig.next_load_result.tone_chains = {
        common::audio::LoadedToneChainIdentities{
            .tone_document_ref = g_minted_ref,
            .plugins = {},
        },
    };
    const int loads_before = editor.live_rig.load_call_count;

    editor.controller.onToneRegionDeleteRequested(only_id);

    // Coverage is preserved: the region stays but is repointed to a fresh empty "Default" tone.
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == only_id);
    CHECK(editor.regions().front().tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Default");
    CHECK(editor.live_rig.mint_call_count == 1);
    CHECK(editor.live_rig.load_call_count == loads_before + 1);

    // Undo restores the region's previous tone and name. The rig no longer hosts that tone's
    // branch (the reset reload dropped it), so the restore must also reload the rig — otherwise
    // the lanes come back without their plugins.
    editor.controller.onUndoRequested();
    CHECK(editor.regions().front().tone_document_ref == g_tone_document_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_tone_document_ref) == "Clean");
    CHECK(editor.live_rig.load_call_count == loads_before + 2);

    // Redo re-applies the reset.
    editor.controller.onRedoRequested();
    CHECK(editor.regions().front().tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Default");
}

} // namespace rock_hero::editor::core
