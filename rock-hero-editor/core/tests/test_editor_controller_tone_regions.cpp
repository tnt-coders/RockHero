#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// A second canonical tone the create/delete tests reuse, distinct from the harness default tone.
constexpr const char* g_second_tone_ref = "tones/1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d/tone.json";
// A third canonical tone the retone tests repoint onto, distinct from both region tones.
constexpr const char* g_third_tone_ref = "tones/2b3c4d5e-6f7a-4b8c-9d0e-1f2a3b4c5d6e/tone.json";
constexpr const char* g_unknown_tone_ref = "tones/4d5e6f7a-8b9c-4d0e-8f1a-2b3c4d5e6f7a/tone.json";
constexpr const char* g_region_a = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";
constexpr const char* g_region_b = "6b2e1d4f-8a3c-4b1d-9e2f-3a4b5c6d7e8f";
constexpr const char* g_region_new = "7c3f2e5a-9b4d-4c2e-af3a-4b5c6d7e8f90";
constexpr const char* g_minted_ref = "tones/3a4b5c6d-7e8f-4a1b-8c2d-9e0f1a2b3c4d/tone.json";

[[nodiscard]] common::core::GridPosition gridAt(int measure, int beat)
{
    return common::core::GridPosition{.measure = measure, .beat = beat};
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
            .tone_document_ref = g_tone_document_ref,
        },
        common::core::ToneRegion{
            .id = g_region_b,
            .start = gridAt(2, 1),
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
    "EditorController resolves cursor-follow regions with sub-beat boundaries",
    "[core][editor-controller]")
{
    // The boundary sits at measure 2 beat 1 + half a beat (2.25 s at 120 BPM 4/4): cursor-follow
    // must honor the offset exactly as the drawn tone row does; dropping it flips to the next
    // region a quarter second early.
    common::core::Song song = makeToneSongBase();
    const common::core::GridPosition off_beat_boundary{
        .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}
    };
    song.arrangements.front().tone_track.regions = {
        common::core::ToneRegion{
            .id = g_region_a,
            .start = gridAt(1, 1),
            .tone_document_ref = g_tone_document_ref,
        },
        common::core::ToneRegion{
            .id = g_region_b,
            .start = off_beat_boundary,
            .tone_document_ref = g_second_tone_ref,
        },
    };
    LoadedToneEditor editor{std::move(song)};

    // Just before the off-beat boundary the first region is still the active one.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.1});
    const EditorViewState* state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[0].active);
    CHECK_FALSE(state->tone_track.regions[1].active);

    // Just past it the second region takes over.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.3});
    CHECK_FALSE(state->tone_track.regions[0].active);
    CHECK(state->tone_track.regions[1].active);
}

// The tone row supplies only the render cadence; the crossing decision and its debounce live here.
// A frame that crosses nothing must be free — this runs sixty times a second — so the two no-change
// cases assert the absence of a rig call and of a view push, not merely the right end state.
TEST_CASE(
    "EditorController activates the region a playback frame crossed into",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // Park the transport inside the first region through the seek entry: that is what records the
    // region the next frame compares against, and it leaves the rig hosting Clean.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    editor.transport.current_state.playing = true;

    // A frame that moved the playhead WITHIN the first region crosses no boundary, so it decides
    // nothing at all.
    editor.transport.current_position = common::core::TimePosition{1.5};
    const int rig_calls_inside = editor.live_rig.set_audible_tone_call_count;
    const int pushes_inside = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_inside);
    CHECK(editor.view.set_state_call_count == pushes_inside);

    // Past the boundary at 2 s the crossing frame switches the audible tone and flips the drawn
    // active flags, with no formal selection left behind for Delete to find.
    editor.transport.current_position = common::core::TimePosition{2.5};
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].active);
    CHECK(state->tone_track.regions[1].active);

    // The next frame finds the same region under the playhead, so the debounce holds: no second rig
    // call and no second push for a crossing that already happened.
    const int rig_calls_after = editor.live_rig.set_audible_tone_call_count;
    const int pushes_after = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_after);
    CHECK(editor.view.set_state_call_count == pushes_after);
}

// While the transport plays, the PLAYHEAD'S tone is what plays. A selection outranks the cursor in
// activeToneRegionId, so a click elsewhere would otherwise keep a foreign tone audible until the
// next crossing; the frame after it has to take the playhead's side. A selection on the playhead's
// own region is the exception that proves the rule: it already names the right tone, so the frame
// has nothing to correct and the click survives until the crossing actually arrives.
TEST_CASE(
    "EditorController yields a playing frame to the playhead over a foreign selection",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    editor.transport.current_state.playing = true;

    // Clicking the LATER region while the playhead sits in the earlier one makes Dirty audible.
    editor.controller.onToneRegionSelected(g_region_b);
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);

    // The next frame corrects it: the selection clears, Clean is audible again, and the earlier
    // region is the drawn active one.
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[0].active);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK_FALSE(state->tone_track.regions[1].selected);

    // The correction happens ONCE, not once per frame: with nothing left to correct the following
    // frame is free again. (The correcting frame itself re-derives the audible tone twice, because
    // clearCursorCoupledSelection routes through setSelection, which re-derives on its own before
    // activateToneAtCursor's explicit sync — a pre-existing property of every selection-clearing
    // cursor entry, not of the frame path, so it is not pinned here.)
    const int rig_calls_corrected = editor.live_rig.set_audible_tone_call_count;
    const int pushes_corrected = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_corrected);
    CHECK(editor.view.set_state_call_count == pushes_corrected);

    // Selecting the region the playhead is already in names the tone that should be playing, so a
    // frame inside it decides nothing and the selection stands.
    editor.controller.onToneRegionSelected(g_region_a);
    const int rig_calls_own = editor.live_rig.set_audible_tone_call_count;
    const int pushes_own = editor.view.set_state_call_count;
    editor.transport.current_position = common::core::TimePosition{1.5};
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_own);
    CHECK(editor.view.set_state_call_count == pushes_own);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[0].selected);

    // The crossing still clears it: transport motion out of the selected region is exactly what the
    // cursor-coupled lifecycle means, so Delete can never fire from playback alone.
    editor.transport.current_position = common::core::TimePosition{2.5};
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK(state->tone_track.regions[1].active);
}

// The regression a datum keyed on the last TRANSPORT move would carry: a standing playhead can
// change which region holds it without the transport moving at all, because the MODEL moved under
// it. An insert BEHIND the playhead is exactly that — and it selects what it made, so a frame that
// mistook the new region for a crossing would wipe the charter's selection 16 ms later. Keying on
// the region the rig is audibly on gets both directions right, because the insert's own commit is
// what recorded it.
TEST_CASE(
    "EditorController leaves an insert behind the playhead alone and corrects one ahead of it",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);

    // Playing at measure 1 beat 4 (1.5 s at 120 BPM 4/4), inside the sole region.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{1.5});
    editor.transport.current_state.playing = true;
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);

    // Insert a tone change at beat 3 (1.0 s), BEHIND the playhead: the new region now holds the
    // playhead, is selected, and its tone is audible.
    editor.controller.onToneRegionCreateRequested(gridAt(1, 3), g_region_new, g_second_tone_ref);
    REQUIRE(editor.regions().size() == 2);
    REQUIRE(editor.regions()[1].id == g_region_new);
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);

    // The next frame has nothing to correct — the audible region IS the one under the playhead — so
    // the selection the insert made survives and the frame costs nothing.
    const int rig_calls_behind = editor.live_rig.set_audible_tone_call_count;
    const int pushes_behind = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_behind);
    CHECK(editor.view.set_state_call_count == pushes_behind);
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[1].selected);
    CHECK(state->tone_track.regions[1].active);

    // The mirror case: an insert AHEAD of the playhead selects a region the playhead is not in, so
    // the next frame hands the tone back to the cursor and clears that selection.
    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 1);
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    editor.controller.onToneRegionCreateRequested(gridAt(1, 3), g_region_new, g_second_tone_ref);
    REQUIRE(editor.regions().size() == 2);
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);

    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[0].active);
    CHECK_FALSE(state->tone_track.regions[1].selected);
}

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
    "EditorController repoints a tone region at another catalog tone", "[core][editor-controller]")
{
    // A third catalog tone is what keeps this a plain repoint: onto the neighbor's own tone the
    // two regions would merge, which the merge case below covers instead.
    common::core::Song song = makeTwoRegionSong();
    song.arrangements.front().tones.push_back(
        common::core::Tone{.tone_document_ref = g_third_tone_ref, .name = "Solo"});
    LoadedToneEditor editor{std::move(song)};
    REQUIRE(editor.regions().size() == 2);

    editor.controller.onToneRegionToneRequested(g_region_b, g_third_tone_ref);
    CHECK(editor.regions()[1].tone_document_ref == g_third_tone_ref);

    // The drawn row follows the model, so the region relabels to the tone it now references.
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[1].tone_document_ref == g_third_tone_ref);
    CHECK(state->tone_track.regions[1].name == "Solo");
    CHECK(state->undo_label == "Change Tone of Region to Solo");

    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);

    editor.controller.onRedoRequested();
    CHECK(editor.regions()[1].tone_document_ref == g_third_tone_ref);
}

TEST_CASE(
    "EditorController makes a repointed active region audible immediately",
    "[core][editor-controller]")
{
    common::core::Song song = makeTwoRegionSong();
    song.arrangements.front().tones.push_back(
        common::core::Tone{.tone_document_ref = g_third_tone_ref, .name = "Solo"});
    LoadedToneEditor editor{std::move(song)};

    // Park the cursor inside the later region so it is the active one: the rig plays the active
    // region's tone, and Dirty is what it should be hosting before the repoint.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);

    editor.controller.onToneRegionToneRequested(g_region_b, g_third_tone_ref);
    CHECK(editor.live_rig.last_audible_tone_ref == g_third_tone_ref);
}

// A boundary IS a tone change: a region retoned onto its neighbour's tone merges with it, because a
// boundary with no change across it is no boundary. The selection follows the region that survives,
// so Enter and Delete keep acting on what the charter just made; the tone that lost its last
// reference leaves the catalog; and one undo brings the region, its id and the catalog entry back.
TEST_CASE(
    "EditorController merges a tone region into its neighbor on retone",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);
    editor.controller.onToneRegionSelected(g_region_b);

    editor.controller.onToneRegionToneRequested(g_region_b, g_tone_document_ref);

    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
    CHECK(editor.regions().front().start == gridAt(1, 1));
    CHECK(editor.regions().front().tone_document_ref == g_tone_document_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_second_tone_ref).empty());
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 1);
    CHECK(state->tone_track.regions[0].selected);
    CHECK(state->undo_label == "Change Tone of Region to Clean");

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].id == g_region_b);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_second_tone_ref) == "Dirty");

    editor.controller.onRedoRequested();
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
}

// Deleting the region between two spans of one tone leaves ONE region, not two adjacent ones
// pointing at the same tone: the previous region runs on and the next, now changing nothing,
// merges into it.
TEST_CASE(
    "EditorController merges the neighbors a deleted tone region separated",
    "[core][editor-controller]")
{
    common::core::Song song = makeToneSongBase();
    song.arrangements.front().tone_track.regions = {
        common::core::ToneRegion{
            .id = g_region_a, .start = gridAt(1, 1), .tone_document_ref = g_tone_document_ref
        },
        common::core::ToneRegion{
            .id = g_region_b, .start = gridAt(2, 1), .tone_document_ref = g_second_tone_ref
        },
        common::core::ToneRegion{
            .id = g_region_new, .start = gridAt(2, 3), .tone_document_ref = g_tone_document_ref
        },
    };
    LoadedToneEditor editor{std::move(song)};
    REQUIRE(editor.regions().size() == 3);

    editor.controller.onToneRegionDeleteRequested(g_region_b);
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);
    CHECK(editor.regions().front().tone_document_ref == g_tone_document_ref);

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 3);
    CHECK(editor.regions()[1].id == g_region_b);
    CHECK(editor.regions()[2].id == g_region_new);
    CHECK(editor.regions()[2].start == gridAt(2, 3));
}

TEST_CASE(
    "EditorController records nothing for a retone to the current tone and refuses an unknown one",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    // Repointing a region at the tone it already references changes nothing, so nothing is pushed.
    editor.controller.onToneRegionToneRequested(g_region_b, g_second_tone_ref);
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->undo_label.has_value());

    // A ref that names no catalog tone would leave the region pointing at nothing.
    editor.controller.onToneRegionToneRequested(g_region_b, g_unknown_tone_ref);
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);
    const EditorViewState* const state_after = stateOrNull(editor.view.last_state);
    REQUIRE(state_after != nullptr);
    CHECK_FALSE(state_after->undo_label.has_value());
}

TEST_CASE(
    "EditorController deletes a tone region into the previous region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    editor.controller.onToneRegionDeleteRequested(g_region_b);
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[0].id == g_region_a);
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
    CHECK(editor.regions()[1].id == g_region_new);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);

    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == g_region_a);

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
    CHECK(editor.regions()[0].start == gridAt(1, 1)); // the earlier region still opens the song
    CHECK(editor.regions()[1].start == gridAt(2, 3)); // and now runs on to the moved boundary

    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].start == gridAt(2, 1));

    editor.controller.onRedoRequested();
    CHECK(editor.regions()[1].start == gridAt(2, 3));
}

TEST_CASE(
    "EditorController moves a shared tone boundary to a sub-beat position",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    // Tone regions address the same measure/beat/sub-beat grid as chart notes, so a boundary can
    // land on a grid line finer than a whole beat, and the sub-beat offset must survive the move.
    const common::core::GridPosition sub_beat{
        .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}
    };
    editor.controller.onToneBoundaryMoveRequested(g_region_b, sub_beat);
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].start == sub_beat);

    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

TEST_CASE(
    "EditorController ignores a boundary move on the first region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // The first region has no earlier neighbor; its start is the pinned song boundary.
    editor.controller.onToneBoundaryMoveRequested(g_region_a, gridAt(1, 3));

    CHECK(editor.regions()[0].start == gridAt(1, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

TEST_CASE(
    "EditorController rejects a boundary move that empties a region", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // Measure 3 beat 1 is the terminal anchor: opening the later region there would leave it with
    // no length at all, so the rules reject the move and both starts stand.
    editor.controller.onToneBoundaryMoveRequested(g_region_b, gridAt(3, 1));

    CHECK(editor.regions()[0].start == gridAt(1, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

// Alt+arrows on a selected tone region move the tone change it opens — its START — one grid line,
// and bring the cursor to the new start so the edit is in view; the region stays selected.
TEST_CASE(
    "EditorController moves the selected tone region's start a grid line",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    // Mid-region, away from the start the move addresses (120 BPM 4/4: measure 2 opens at 2 s).
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.75});
    editor.controller.onToneRegionSelected(g_region_b);

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].start == gridAt(2, 2));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.5, 1e-9));
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[1].selected);

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));

    // Each landed press is its own undo entry.
    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].start == gridAt(2, 2));
    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].start == gridAt(2, 1));
}

// A refused move changes nothing, the cursor included: the first region's start is the song's, a
// start cannot reach the song's closing barline, and Up/Down address no second row.
TEST_CASE(
    "EditorController refuses a tone region start move without moving the cursor",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{1.25});
    editor.controller.onToneRegionSelected(g_region_a);

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.regions()[0].start == gridAt(1, 1));
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(1.25, 1e-9));

    // Measure 3 beat 1 closes the song, so the later region's start stops one line short of it.
    editor.controller.onToneRegionSelected(g_region_b);
    for (int press = 0; press < 3; ++press)
    {
        editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    }
    CHECK(editor.regions()[1].start == gridAt(2, 4));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(3.5, 1e-9));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.regions()[1].start == gridAt(2, 4));

    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.75});
    editor.controller.onToneRegionSelected(g_region_b);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Down);
    CHECK(editor.regions()[1].start == gridAt(2, 4));
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(3.75, 1e-9));
}

// A pointer can select a region while the song plays; the move still lands, but the playhead is
// not the move's to seek.
TEST_CASE(
    "EditorController moves a tone region start during playback without seeking",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onToneRegionSelected(g_region_b);
    editor.transport.current_state.playing = true;
    const auto seeks_before = editor.transport.seek_call_count;

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);

    CHECK(editor.regions()[1].start == gridAt(2, 2));
    CHECK(editor.transport.seek_call_count == seeks_before);
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
    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    // The catalog gained the new tone. The empty tone takes the incremental fast path: one
    // branch add on the live rig, no full reload, and no capture (nothing on disk is replaced).
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.live_rig.mint_call_count == 1);
    CHECK(editor.live_rig.add_branch_call_count == 1);
    CHECK(editor.live_rig.last_added_branch_ref == g_minted_ref);
    CHECK(editor.live_rig.load_call_count == loads_before);
    CHECK(editor.live_rig.capture_call_count == 0);

    // Undo removes both the region and the catalog tone (pure model).
    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 1);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref).empty());

    // Redo recreates both without re-minting.
    editor.controller.onRedoRequested();
    REQUIRE(editor.regions().size() == 2);
    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.live_rig.mint_call_count == 1);
}

TEST_CASE(
    "EditorController frees a tone's name when its last region is deleted",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    editor.live_rig.next_mint_ref = g_minted_ref;
    editor.controller.onToneCreateNewRequested(gridAt(2, 1), "Solo");
    REQUIRE(editor.regions().size() == 2);
    const std::string solo_region_id = editor.regions()[1].id;

    // Deleting the tone's only region prunes it from the catalog too: a phantom entry would keep
    // owning the name while nothing could reach the tone.
    editor.controller.onToneRegionDeleteRequested(solo_region_id);
    REQUIRE(editor.regions().size() == 1);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref).empty());

    // Undo restores the region together with its catalog entry; redo prunes both again.
    editor.controller.onUndoRequested();
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    editor.controller.onRedoRequested();
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref).empty());

    // The freed name is reusable: creating another "Solo" succeeds instead of reporting a
    // duplicate.
    editor.controller.onToneCreateNewRequested(gridAt(2, 1), "Solo");
    REQUIRE(editor.regions().size() == 2);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.view.shown_errors.empty());
}

TEST_CASE("EditorController resets the sole tone region on delete", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeSingleRegionSong()};
    REQUIRE(editor.regions().size() == 1);
    const std::string only_id = editor.regions().front().id;
    editor.live_rig.next_mint_ref = g_minted_ref;
    // Force the full-reload fallback: this test exercises the reload path, whose completion
    // reports only the fresh tone's branch — dropping the previous tone from the rig, exactly as
    // the real engine would.
    editor.live_rig.next_add_branch_error = common::audio::LiveRigError{
        common::audio::LiveRigErrorCode::InvalidRequest, "forced fallback"
    };
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

// A retone can point at a tone that does not exist yet, and then minting it and repointing the
// region are ONE gesture and so one undo entry. The tone it replaces loses its last reference here,
// so it leaves the catalog by the same rule the delete verb applies: a phantom entry would keep
// owning its name while nothing could reach it, since the picker offers only referenced tones.
TEST_CASE(
    "EditorController mints a tone for a region and prunes the one it replaced",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);
    editor.live_rig.next_mint_ref = g_minted_ref;

    const auto catalog_has = [&editor](const std::string& ref) {
        for (const common::core::Tone& tone : editor.arrangement().tones)
        {
            if (tone.tone_document_ref == ref)
            {
                return true;
            }
        }
        return false;
    };
    REQUIRE(catalog_has(g_second_tone_ref));

    editor.controller.onToneRegionNewToneRequested(g_region_b, "Solo");

    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK(editor.live_rig.mint_call_count == 1);
    CHECK_FALSE(catalog_has(g_second_tone_ref));

    // One entry: the mint and the repoint come back together, catalog included.
    editor.controller.onUndoRequested();
    CHECK(editor.regions()[1].tone_document_ref == g_second_tone_ref);
    CHECK(catalog_has(g_second_tone_ref));
    CHECK_FALSE(catalog_has(g_minted_ref));

    editor.controller.onRedoRequested();
    CHECK(editor.regions()[1].tone_document_ref == g_minted_ref);
    CHECK(common::core::toneNameFor(editor.arrangement(), g_minted_ref) == "Solo");
    CHECK_FALSE(catalog_has(g_second_tone_ref));
}

// Delete leaves NOTHING selected. The absorbing neighbour used to inherit the selection so the
// signal-chain panel stayed bound to a region, but the panel follows the ACTIVE tone (the cursor's)
// while "selected" is only the Delete target — inheriting it armed Delete at a region the charter
// never pointed at.
TEST_CASE(
    "EditorController leaves nothing selected after deleting a tone region",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    editor.controller.onToneRegionSelected(g_region_b);
    const EditorViewState* const armed = stateOrNull(editor.view.last_state);
    REQUIRE(armed != nullptr);
    REQUIRE(armed->tone_track.regions.size() == 2);
    REQUIRE(armed->tone_track.regions[1].selected);

    editor.controller.onToneRegionDeleteRequested(g_region_b);

    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 1);
    CHECK_FALSE(state->tone_track.regions[0].selected);
}

} // namespace rock_hero::editor::core
