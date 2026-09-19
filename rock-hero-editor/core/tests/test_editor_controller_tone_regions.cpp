#include <algorithm>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <utility>
#include <variant>
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

// The two-region song carrying a bare six-string chart: Tab reads the displayed tablature to know
// the row stack exists at all, so the tone row's own step needs one even with no notes on it.
[[nodiscard]] common::core::Song makeTwoRegionChartedSong()
{
    common::core::Song song = makeTwoRegionSong();
    common::core::Chart chart;
    chart.tuning.strings = common::core::testing::standardTuning();
    song.arrangements.front().chart = std::move(chart);
    return song;
}

// Owns the fakes and a controller with the supplied song loaded, ready to drive tone edits.
struct LoadedToneEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeToneTimeline tone_timeline;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    explicit LoadedToneEditor(common::core::Song song)
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig, tone_timeline),
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

    // What the tone chord would do right now, as the surface reads it: the verb the core publishes.
    [[nodiscard]] ToneChordTarget publishedToneChordTarget() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        return state != nullptr ? state->tone_chord_target : ToneChordTarget{};
    }

    // The id of the region drawn selected, or empty while none is.
    [[nodiscard]] std::string selectedRegionId() const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        if (state == nullptr)
        {
            return {};
        }
        // Not named "regions": a local of that name would shadow this fixture's own accessor, which
        // GCC's -Wshadow rejects and MSVC says nothing about.
        const std::vector<ToneRegionViewState>& published = state->tone_track.regions;
        const auto selected = std::ranges::find_if(
            published, [](const ToneRegionViewState& region) { return region.selected; });
        return selected != published.end() ? selected->id : std::string{};
    }

    // Turns snapping off, which only the warning's own answer can do.
    void turnSnapOff()
    {
        controller.onGridSnapToggleRequested();
        controller.onGridSnapWarningDecision(GridSnapWarningDecision::TurnSnappingOff);
    }

    // The engine's own end-of-playback report, which FakeTransport leaves to the test: every stop
    // reaches the controller as this one notification, whether it was Pause, Stop, the transport
    // running off the end of the content, or a device failure releasing the playback context.
    void reportPlaybackEnded()
    {
        transport.setStateAndNotify(common::audio::TransportState{.playing = false});
    }

    // Whether a region view state is drawn active, by region id.
    [[nodiscard]] bool regionDrawnActive(const std::string& region_id) const
    {
        const EditorViewState* const state = stateOrNull(view.last_state);
        if (state == nullptr)
        {
            return false;
        }
        const auto drawn = std::ranges::find_if(
            state->tone_track.regions,
            [&region_id](const ToneRegionViewState& region) { return region.id == region_id; });
        return drawn != state->tone_track.regions.end() && drawn->active;
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
//
// The crossing frame makes the ORDINARY rig call, the same one a caret move makes. It changes no
// sound — the audio thread switched the branch gains from the baked schedule before the frame ran,
// and the rig declines to write a gain the schedule owns — but it is what rebinds the signal-chain
// panel and the branch every chain verb writes onto the tone actually being heard.
TEST_CASE(
    "EditorController follows the region a playback frame crossed into",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // Give the second tone its own chain AND its own authored level, which is what makes the
    // rebinding visible: the panel and the fader must end up naming the tone the playhead crossed
    // INTO. The level is the tone's own, carried on its branch, so the crossing reveals it rather
    // than moving it.
    editor.live_rig.tone_results[g_second_tone_ref] = common::audio::LiveRigLoadResult{
        .plugins =
            {
                common::audio::PluginChainEntry{
                    .instance_id = "dirty-instance",
                    .plugin_id = "dirty-plugin",
                    .name = "Dirty Amp",
                    .manufacturer = "Example Audio",
                    .format_name = "VST3",
                    .category = {},
                    .chain_index = 0,
                    .display_type_override = {},
                },
            },
        .output_gain = common::audio::Gain{-7.5},
    };

    // Park the transport inside the first region through the seek entry: that is what records the
    // region the next frame compares against, and it leaves the rig hosting Clean.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    editor.transport.current_state.playing = true;
    const EditorViewState* const parked_state = stateOrNull(editor.view.last_state);
    REQUIRE(parked_state != nullptr);
    REQUIRE(parked_state->signal_chain.plugins.size() == 1);
    CHECK(parked_state->signal_chain.plugins[0].name == "Loaded Amp");

    // A frame that moved the playhead WITHIN the first region crosses no boundary, so it decides
    // nothing at all.
    editor.transport.current_position = common::core::TimePosition{1.5};
    const int rig_calls_inside = editor.live_rig.set_audible_tone_call_count;
    const int pushes_inside = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_inside);
    CHECK(editor.view.set_state_call_count == pushes_inside);

    // Past the boundary at 2 s the crossing frame flips the drawn active flags, switches the rig
    // onto the crossed-into tone and pushes, leaving no formal selection for Delete to find. The
    // panel follows that switch onto the tone the audio thread is already playing.
    editor.transport.current_position = common::core::TimePosition{2.5};
    const int rig_calls_before_crossing = editor.live_rig.set_audible_tone_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_before_crossing + 1);
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    CHECK_FALSE(editor.regionDrawnActive(g_region_a));
    CHECK(editor.regionDrawnActive(g_region_b));
    const EditorViewState* const crossed_state = stateOrNull(editor.view.last_state);
    REQUIRE(crossed_state != nullptr);
    REQUIRE(crossed_state->signal_chain.plugins.size() == 1);
    CHECK(crossed_state->signal_chain.plugins[0].name == "Dirty Amp");
    CHECK_THAT(crossed_state->signal_chain.output_gain.db, Catch::Matchers::WithinULP(-7.5, 0));

    // The next frame finds the same region under the playhead, so the debounce holds: no second
    // push for a crossing that already happened.
    const int pushes_after = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.view.set_state_call_count == pushes_after);

    // The handback at the end of playback re-derives against the same region, so it lands on the
    // tone the crossing already reached rather than moving the rig a second time — and the fader
    // keeps the level it took at the crossing, which is the crossed-into tone's own.
    editor.reportPlaybackEnded();
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    const EditorViewState* const stopped_state = stateOrNull(editor.view.last_state);
    REQUIRE(stopped_state != nullptr);
    CHECK_THAT(stopped_state->signal_chain.output_gain.db, Catch::Matchers::WithinULP(-7.5, 0));
}

// The boundary crossing is the frame handler's ONLY input, because a playing transport admits no
// other: marker selection and every marker edit are paused-only, so a click on a region while the
// song plays reaches nothing and the frame after it still finds no selection to reconcile.
TEST_CASE(
    "EditorController refuses a tone region select while playing", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    editor.transport.current_state.playing = true;

    // Clicking the LATER region leaves the playhead's own tone audible: the select is refused, so
    // nothing ever outranks the cursor.
    editor.controller.onToneRegionSelected(g_region_b);
    CHECK(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK_FALSE(state->tone_track.regions[1].selected);

    // With no selection in play the next frame has nothing to correct, so it costs nothing.
    const int rig_calls = editor.live_rig.set_audible_tone_call_count;
    const int pushes = editor.view.set_state_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls);
    CHECK(editor.view.set_state_call_count == pushes);
}

// The MODEL can still move under a standing playhead, because undo and redo stay live while the
// transport plays (the tone designer edits mid-play and must stay undoable). It is the one way a
// baked schedule can go stale, so the transition rebuilds it — and the next frame, keyed on the
// AUDIBLE region rather than on the last transport move, carries the restored region into the
// display. Restored from the insert-behind-the-playhead case that the forward gate retired — undo
// is now the only verb that can reach this state.
TEST_CASE(
    "EditorController rebakes and redraws an undone tone-region delete under a standing playhead",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    // Delete the later region while paused, leaving Clean covering the whole song, then park the
    // playhead at 2.5 s — inside where the deleted region stood — and start playing.
    editor.controller.onToneRegionDeleteRequested(g_region_b);
    REQUIRE(editor.regions().size() == 1);
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});
    REQUIRE(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);

    // Play through the handler, so a schedule genuinely exists; the fake transport does not move
    // its own state, so the test reports the playing transport the engine would have.
    editor.controller.onPlayPausePressed();
    editor.transport.current_state.playing = true;
    const int bakes_before_undo = editor.tone_timeline.prepare_call_count;

    // Undo restores the later region under the standing playhead. The transport never moved.
    editor.controller.onUndoRequested();
    REQUIRE(editor.regions().size() == 2);

    // The model moved under a live schedule, so the schedule is rebuilt from it — otherwise the
    // rest of the song would play the tone track as it stood before the undo.
    CHECK(editor.tone_timeline.prepare_call_count == bakes_before_undo + 1);
    REQUIRE(editor.tone_timeline.last_regions.size() == 2);
    CHECK(editor.tone_timeline.last_regions[1].tone_document_ref == g_second_tone_ref);

    // The frame that follows carries the restored region into the display and rebinds the rig onto
    // its tone. The audio already switched itself from the rebaked schedule, so that call moves no
    // branch gain — it moves the panel and the branch the chain verbs write.
    const int rig_calls_before_frame = editor.live_rig.set_audible_tone_call_count;
    editor.controller.onPlaybackFrameAdvanced();
    CHECK(editor.live_rig.set_audible_tone_call_count == rig_calls_before_frame + 1);
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);
    CHECK_FALSE(editor.regionDrawnActive(g_region_a));
    CHECK(editor.regionDrawnActive(g_region_b));
}

// THE PROTOCOL, start to finish: Play bakes the tone track's schedule and only then asks the
// transport to start, because from the moment a schedule exists the audio thread owns the branch
// gains — the play boundary's own resync is what applies the curve before the first block.
TEST_CASE("EditorController bakes the tone schedule before playing", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.tone_timeline.prepare_call_count == 0);

    editor.controller.onPlayPausePressed();

    CHECK(editor.tone_timeline.prepare_call_count == 1);
    CHECK(editor.transport.play_call_count == 1);

    // The ordering the protocol turns on, read off the shared call-sequence counter.
    CHECK(editor.tone_timeline.last_prepare_sequence < editor.transport.last_play_sequence);

    // The schedule is the tone track resolved to seconds: measure 1 and measure 2 at 120 BPM 4/4,
    // with the first span owning the lead-in and the last running to the end of the content.
    REQUIRE(editor.tone_timeline.last_regions.size() == 2);
    CHECK(editor.tone_timeline.last_regions[0].tone_document_ref == g_tone_document_ref);
    CHECK_THAT(
        editor.tone_timeline.last_regions[0].time_range.start.seconds,
        Catch::Matchers::WithinAbs(0.0, 1.0e-9));
    CHECK_THAT(
        editor.tone_timeline.last_regions[0].time_range.end.seconds,
        Catch::Matchers::WithinAbs(2.0, 1.0e-9));
    CHECK(editor.tone_timeline.last_regions[1].tone_document_ref == g_second_tone_ref);
    CHECK_THAT(
        editor.tone_timeline.last_regions[1].time_range.start.seconds,
        Catch::Matchers::WithinAbs(2.0, 1.0e-9));
    CHECK_THAT(
        editor.tone_timeline.last_regions[1].time_range.end.seconds,
        Catch::Matchers::WithinAbs(editor.arrangement().audio_duration.seconds, 1.0e-9));
}

// The other end of the protocol, reached through the ONE seam that sees every end of playback: the
// transport's own state report. Pause, Stop, the transport running off the end of the content and
// a device failure all arrive here identically, so clearing the schedule here covers the three no
// editor handler ever runs for.
TEST_CASE(
    "EditorController clears the tone schedule when playback ends", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onPlayPausePressed();
    editor.transport.current_state.playing = true;
    REQUIRE(editor.tone_timeline.prepare_call_count == 1);
    REQUIRE_FALSE(editor.tone_timeline.last_regions.empty());

    // Park the playhead in the later region before the stop, so the handback has somewhere to land
    // that the pre-play direct write did not already point at.
    editor.transport.current_position = common::core::TimePosition{2.5};
    editor.reportPlaybackEnded();

    // The clear is a prepare with an empty schedule; the direct write then owns the gains again and
    // re-asserts the region the playhead is standing in.
    CHECK(editor.tone_timeline.prepare_call_count == 2);
    CHECK(editor.tone_timeline.last_regions.empty());
    CHECK(editor.live_rig.last_audible_tone_ref == g_second_tone_ref);

    // And with no schedule, the paused caret owns the tone again: seeking back writes the rig.
    const int rig_calls_before_seek = editor.live_rig.set_audible_tone_call_count;
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.5});
    CHECK(editor.live_rig.set_audible_tone_call_count > rig_calls_before_seek);
    CHECK(editor.live_rig.last_audible_tone_ref == g_tone_document_ref);
}

// A region edit while paused bakes nothing: there is no schedule while paused, so the model is the
// only thing that has to change, and the next Play derives the schedule from it.
TEST_CASE(
    "EditorController bakes no schedule for a paused region edit", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.regions().size() == 2);

    editor.controller.onToneRegionDeleteRequested(g_region_b);
    REQUIRE(editor.regions().size() == 1);
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});

    CHECK(editor.tone_timeline.prepare_call_count == 0);

    // The next Play is what carries the edit into playback, as the one region the model now holds.
    editor.controller.onPlayPausePressed();
    CHECK(editor.tone_timeline.prepare_call_count == 1);
    CHECK(editor.tone_timeline.last_regions.size() == 1);
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

// A marker move is paused-only: pressed while the song plays it changes nothing at all — no start
// moves, no seek fires, and nothing reaches the undo history to be taken back.
TEST_CASE(
    "EditorController refuses a tone region start move while playing", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
    editor.controller.onToneRegionSelected(g_region_b);
    editor.transport.current_state.playing = true;
    const auto seeks_before = editor.transport.seek_call_count;
    const EditorViewState* const before = stateOrNull(editor.view.last_state);
    REQUIRE(before != nullptr);
    const std::size_t entries_before = before->undo_history.labels.size();

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);

    CHECK(editor.regions()[1].start == gridAt(2, 1));
    CHECK(editor.transport.seek_call_count == seeks_before);
    const EditorViewState* const after = stateOrNull(editor.view.last_state);
    REQUIRE(after != nullptr);
    CHECK(after->undo_history.labels.size() == entries_before);
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
    // Selected first, so the deselect below is a change rather than an already-empty selection.
    editor.controller.onToneRegionSelected(only_id);
    REQUIRE(editor.selectedRegionId() == only_id);

    editor.controller.onToneRegionDeleteRequested(only_id);

    // Coverage is preserved: the region stays but is repointed to a fresh empty "Default" tone.
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.regions().front().id == only_id);
    // A delete leaves NOTHING selected, and this one survives its own delete — so the reset
    // deselects explicitly rather than riding the retone's own rule 4 selection.
    CHECK(editor.selectedRegionId().empty());
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

// The chord's whole decision is published as the VERB: the cursor standing exactly on a region's
// start restates that change — a region's start IS the tone change that opens it, the first
// region's included, since restating that repoints the opening tone and a split there would have no
// width — and the core hands over the tone it sounds now, which the picker leaves out.
TEST_CASE(
    "The published tone chord retones the region starting at the cursor",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});

    editor.controller.onTimelineSeekRequested(common::core::TimePosition{0.0});
    const ToneChordTarget at_first = editor.publishedToneChordTarget();
    const auto* const first_retone = std::get_if<RetoneRegionTarget>(&at_first);
    REQUIRE(first_retone != nullptr);
    CHECK(first_retone->region_id == g_region_a);
    CHECK(first_retone->tone_document_ref == g_tone_document_ref);

    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.0});
    const ToneChordTarget at_second = editor.publishedToneChordTarget();
    const auto* const second_retone = std::get_if<RetoneRegionTarget>(&at_second);
    REQUIRE(second_retone != nullptr);
    CHECK(second_retone->region_id == g_region_b);
    CHECK(second_retone->tone_document_ref == g_second_tone_ref);
}

// A cursor strictly inside a region splits it, at the PLACEMENT quantum — the slot an arrow press
// would arm at — rather than at the tick the section chord reads: a tone change may stand anywhere
// on the grid, so the snap the charter set is the one that decides where it lands.
TEST_CASE(
    "The published tone chord splits the region the cursor stands inside",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});

    // 2.6 s is inside the later region; the nearest quarter line is measure 2 beat 2 at 2.5 s.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.6});
    const ToneChordTarget snapped = editor.publishedToneChordTarget();
    const auto* const snapped_split = std::get_if<SplitToneRegionTarget>(&snapped);
    REQUIRE(snapped_split != nullptr);
    CHECK(snapped_split->position == gridAt(2, 2));
    CHECK(snapped_split->containing_tone_document_ref == g_second_tone_ref);

    // With snap off the placement quantum IS the tick, so the change lands on the exact sub-beat.
    editor.turnSnapOff();
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.25});
    const ToneChordTarget unsnapped = editor.publishedToneChordTarget();
    const auto* const unsnapped_split = std::get_if<SplitToneRegionTarget>(&unsnapped);
    REQUIRE(unsnapped_split != nullptr);
    CHECK(
        unsnapped_split->position ==
        common::core::GridPosition{
            .measure = 2, .beat = 1, .offset = common::core::Fraction{1, 2}
        });
    CHECK(unsnapped_split->containing_tone_document_ref == g_second_tone_ref);
}

// The verb is nothing exactly where the commit would refuse it, so the press is inert rather than
// opening a picker that could author nothing: the terminal position holds no region to split, and
// the whole marker plane is paused-only.
TEST_CASE(
    "The published tone chord is nothing at the song end and while playing",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};

    // Measure 3 beat 1 closes the song: a region opened there would have no length.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{4.0});
    CHECK(std::holds_alternative<std::monostate>(editor.publishedToneChordTarget()));

    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});
    REQUIRE(std::holds_alternative<SplitToneRegionTarget>(editor.publishedToneChordTarget()));
    editor.transport.current_state.playing = true;
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});
    CHECK(std::holds_alternative<std::monostate>(editor.publishedToneChordTarget()));
}

// A restate SELECTS its target (grammar rule 4), from whichever input asked for it: the chord
// authors at the cursor and selects nothing first, so the retone itself is what leaves the region
// outlined for Enter, Ctrl+R and Delete.
TEST_CASE("EditorController selects the region a retone produced", "[core][editor-controller]")
{
    common::core::Song song = makeTwoRegionSong();
    song.arrangements.front().tones.push_back(
        common::core::Tone{.tone_document_ref = g_third_tone_ref, .name = "Solo"});
    LoadedToneEditor editor{std::move(song)};
    REQUIRE(editor.selectedRegionId().empty());

    editor.controller.onToneRegionToneRequested(g_region_b, g_third_tone_ref);
    CHECK(editor.selectedRegionId() == g_region_b);
}

// And when the retone merges the region away, the SURVIVOR is what stays selected — the region now
// holding the start the charter pointed at, whatever its id.
TEST_CASE("EditorController selects the survivor of a merging retone", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    REQUIRE(editor.selectedRegionId().empty());

    editor.controller.onToneRegionToneRequested(g_region_b, g_tone_document_ref);
    REQUIRE(editor.regions().size() == 1);
    CHECK(editor.selectedRegionId() == g_region_a);
}

// Tab on the tone row rides the same generic marker branch the ruler rows do, so it reads the
// CURSOR: from inside a region Shift+Tab lands on that region's own start first, then on the
// previous one, and Tab comes back.
TEST_CASE("EditorController steps the tone row from the cursor", "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionChartedSong()};
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{2.5});
    editor.controller.onToneRegionSelected(g_region_b);
    REQUIRE(editor.selectedRegionId() == g_region_b);

    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedRegionId() == g_region_b);
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));

    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedRegionId() == g_region_a);
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(0.0, 1e-9));

    // The first start is the end of the row: a further step back is inert.
    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(editor.selectedRegionId() == g_region_a);

    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.selectedRegionId() == g_region_b);
    CHECK_THAT(editor.transport.current_position.seconds, Catch::Matchers::WithinAbs(2.0, 1e-9));
}

// Enter and Ctrl+R are the SELECTION's verbs, published as the verb the selected kind has:
// restating a region repoints it, renaming one names the TONE it sounds — the catalog document
// every region on that tone shares, which is what the tone row's own double-click renames.
TEST_CASE(
    "EditorController publishes a selected region's restate and rename verbs",
    "[core][editor-controller]")
{
    LoadedToneEditor editor{makeTwoRegionSong()};
    editor.controller.onToneRegionSelected(g_region_b);

    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    const auto* const retone = std::get_if<RetoneRegionTarget>(&state->restate_target);
    REQUIRE(retone != nullptr);
    CHECK(retone->region_id == g_region_b);
    CHECK(retone->tone_document_ref == g_second_tone_ref);

    const auto* const rename = std::get_if<RenameToneTarget>(&state->rename_target);
    REQUIRE(rename != nullptr);
    CHECK(rename->tone_document_ref == g_second_tone_ref);
    CHECK(rename->name == "Dirty");
}

} // namespace rock_hero::editor::core
