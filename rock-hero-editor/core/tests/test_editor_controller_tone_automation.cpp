#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_region = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";
constexpr const char* g_instance = "plugin-instance-1";
constexpr const char* g_plugin_id = "3f8a2b1c-4d5e-4f60-8a9b-0c1d2e3f4a5b";
constexpr const char* g_param = "gain";

[[nodiscard]] common::core::GridPosition gridAt(int measure, int beat)
{
    return common::core::GridPosition{.measure = measure, .beat = beat};
}

[[nodiscard]] common::core::GridPosition pointAt(int measure, int beat, int numerator = 0)
{
    return common::core::GridPosition{
        .measure = measure,
        .beat = beat,
        .offset = numerator == 0 ? common::core::Fraction{} : common::core::Fraction{numerator, 2},
    };
}

// One whole-song region referencing the harness default tone, over a 4/4 map with terminal 3.1.
[[nodiscard]] common::core::Song makeAutomationSong()
{
    common::core::Song song = makeSong(
        std::filesystem::path{"song.wav"},
        loadedTimelineRange(4.0),
        std::string{g_tone_document_ref});
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    common::core::Arrangement& arrangement = song.arrangements.front();
    arrangement.tones = {
        common::core::Tone{.tone_document_ref = g_tone_document_ref, .name = "Clean"},
    };
    arrangement.tone_track.regions = {
        common::core::ToneRegion{
            .id = g_region,
            .start = gridAt(1, 1),
            .end = gridAt(3, 1),
            .tone_document_ref = g_tone_document_ref,
        },
    };
    return song;
}

[[nodiscard]] common::audio::AutomatableParamInfo makeParam()
{
    return common::audio::AutomatableParamInfo{
        .instance_id = g_instance,
        .param_id = g_param,
        .name = "Gain",
        .group = {},
        .is_discrete = false,
        .labels = {},
        .default_norm_value = 0.5F,
        .current_norm_value = 0.4F,
        .plugin_name = {},
    };
}

// Loads the automation song with the region selected and a live-rig fake whose load result reports
// the tone chain identity, so the controller's plugin association is populated the way a real load
// populates it.
struct AutomationEditor
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeToneAutomation tone_automation;
    FakeProjectServices project_services;
    EditorController controller;
    FakeEditorView view;

    // chain_tone_ref is the tone the load reports the plugin chain under. Tests pass an empty ref to
    // reproduce a plugin whose runtime association has no tone yet (inserted before a region was
    // selected), so the lane-add path must recover the association from the selection.
    explicit AutomationEditor(
        common::core::Song song = makeAutomationSong(),
        std::string chain_tone_ref = std::string{g_tone_document_ref})
        : controller{
              audioPorts(transport, audio, plugin_host, live_rig, tone_automation),
              defaultControllerServices(),
              noopExitFunction(),
              EditorController::ProjectOperations{
                  .open_function = project_services.openFunction(),
              },
          }
    {
        tone_automation.parameters.push_back(makeParam());
        live_rig.next_load_result.tone_chains = {
            common::audio::LoadedToneChainIdentities{
                .tone_document_ref = std::move(chain_tone_ref),
                .plugins = {common::audio::LoadedTonePluginIdentity{
                    .instance_id = g_instance,
                    .stable_id = g_plugin_id,
                }},
            },
        };
        project_services.next_song = std::move(song);
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
        controller.onToneRegionSelected(g_region);
    }

    [[nodiscard]] const ToneAutomationViewState& automation() const
    {
        REQUIRE(view.last_state.has_value());
        // clang-tidy does not treat Catch2 REQUIRE as an optional guard, so assert engagement
        // explicitly before dereferencing.
        if (!view.last_state.has_value())
        {
            throw std::logic_error("editor pushed no view state");
        }
        return view.last_state->tone_automation;
    }

    [[nodiscard]] const std::vector<common::core::ToneParameterAutomation>& model() const
    {
        return controller.session().currentArrangement()->tone_automation;
    }
};

} // namespace

TEST_CASE(
    "EditorController opens a tracking lane without authoring points", "[core][tone-automation]")
{
    AutomationEditor editor;

    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // Opening a lane authors nothing: the model stays empty and no derived curve is written; the
    // lane tracks the parameter's live value until the first point is added.
    CHECK(editor.model().empty());
    CHECK(editor.tone_automation.write_call_count == 0);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().instance_id == g_instance);
    CHECK(editor.automation().lanes.front().name == "Gain");
    CHECK(editor.automation().lanes.front().plugin_name.empty());
    CHECK(editor.automation().lanes.front().resolved);
    CHECK(editor.automation().lanes.front().points.empty());
    CHECK(editor.automation().lanes.front().live_norm_value == 0.4F);

    // Closing the unauthored lane removes it from the view again.
    editor.controller.onToneAutomationLaneRemoveRequested(g_instance, g_param);
    CHECK(editor.automation().lanes.empty());
}

TEST_CASE(
    "EditorController adds a lane for a plugin with no tone association yet",
    "[core][tone-automation]")
{
    // The plugin's runtime association carries an empty tone ref (inserted before a region was
    // selected). The picker still lists the parameter under the selected tone, so adding a lane
    // must recover the tone from the selection instead of silently dropping it.
    AutomationEditor editor{makeAutomationSong(), std::string{}};

    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().instance_id == g_instance);
    CHECK(editor.automation().lanes.front().resolved);

    // The recovered association also lets a subsequently authored point survive projection, rather
    // than the lane vanishing the moment it stops being an unauthored tracking lane.
    editor.controller.onSetToneAutomationPoints(
        g_instance,
        g_param,
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.3F}});
    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.automation().lanes.front().points.size() == 1);
    CHECK(editor.automation().lanes.front().points.front().norm_value == 0.3F);
}

TEST_CASE(
    "EditorController stores musical automation points and derives seconds",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    const std::vector<common::core::ToneAutomationPoint> points{
        common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
        common::core::ToneAutomationPoint{
            .position = pointAt(2, 1, 1), .norm_value = 0.8F, .curve_shape = 0.0F
        },
    };

    editor.controller.onSetToneAutomationPoints(g_instance, g_param, points);

    REQUIRE(editor.model().size() == 1);
    CHECK(editor.model().front().points == points);

    // The default 4/4 map runs at 120 BPM (0.5 s per beat): measure 2 beat 1 plus half a beat is
    // global beat 4.5 , i.e. 2.25 seconds.
    const auto written = editor.tone_automation.curves.find(
        FakeToneAutomation::curveKey(g_tone_document_ref, g_instance, g_param));
    REQUIRE(written != editor.tone_automation.curves.end());
    REQUIRE(written->second.size() == 2);
    CHECK(written->second.back().seconds == Catch::Approx(2.25));
    CHECK(written->second.back().norm_value == 0.8F);

    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.automation().lanes.front().points.size() == 2);
    CHECK(editor.automation().lanes.front().points.back().seconds == Catch::Approx(2.25));
    CHECK(editor.automation().lanes.front().points.back().position == pointAt(2, 1, 1));
}

TEST_CASE("EditorController undoes and redoes a tone automation edit", "[core][tone-automation]")
{
    AutomationEditor editor;
    const std::vector<common::core::ToneAutomationPoint> points{
        common::core::ToneAutomationPoint{.position = pointAt(1, 2), .norm_value = 0.6F},
    };

    editor.controller.onSetToneAutomationPoints(g_instance, g_param, points);
    REQUIRE(editor.automation().lanes.size() == 1);

    editor.controller.onUndoRequested();
    CHECK(editor.model().empty());
    CHECK(editor.automation().lanes.empty());

    editor.controller.onRedoRequested();
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().points.front().norm_value == 0.6F);
}

TEST_CASE(
    "EditorController removes a plugin's automation with it and restores it on undo",
    "[core][tone-automation]")
{
    // Seed the load so g_instance is a removable plugin in both the editor chain (its automation
    // resolves through the tone-chain identity) and the fake backend (capture/remove/recreate act on
    // it). This mirrors a loaded song whose tone already hosts a plugin, without the async insert.
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeToneAutomation tone_automation;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig, tone_automation),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        },
    };
    FakeEditorView view;

    const common::audio::PluginChainEntry entry{
        .instance_id = g_instance,
        .plugin_id = g_plugin_id,
        .name = "Amp",
        .manufacturer = {},
        .format_name = "VST3",
        .category = {},
        .chain_index = 0,
        .block_index = 0,
        .display_type_override = {},
    };
    plugin_host.chain = {entry};
    tone_automation.parameters.push_back(makeParam());
    live_rig.next_load_result.plugins = {entry};
    live_rig.next_load_result.tone_chains = {
        common::audio::LoadedToneChainIdentities{
            .tone_document_ref = std::string{g_tone_document_ref},
            .plugins = {common::audio::LoadedTonePluginIdentity{
                .instance_id = g_instance,
                .stable_id = g_plugin_id,
            }},
        },
    };
    project_services.next_song = makeAutomationSong();

    // Calibrate the input so plugin edits (which require live-input audition) are available.
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    audio.next_prepared_audio_duration = loadedTimelineRange(4.0).duration();
    audio.next_set_active_arrangement_result = true;
    controller.attachView(view);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationManuallySet(0.0).has_value());
    controller.onInputCalibrationDismissed();
    controller.onToneRegionSelected(g_region);

    REQUIRE(view.last_state.has_value());
    if (!view.last_state.has_value())
    {
        throw std::logic_error("editor pushed no view state");
    }
    REQUIRE(view.last_state->signal_chain.plugins.size() == 1);

    const auto& model = controller.session().currentArrangement()->tone_automation;
    const std::vector<common::core::ToneAutomationPoint> points{
        common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.3F},
        common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.7F},
    };
    controller.onSetToneAutomationPoints(g_instance, g_param, points);
    REQUIRE(model.size() == 1);
    REQUIRE(model.front().points == points);

    // Removing the plugin takes its automation out of the model with it, instead of stranding the
    // entry as an unresolvable lane.
    controller.onRemovePluginRequested(g_instance);
    REQUIRE(view.last_state->signal_chain.plugins.empty());
    CHECK(model.empty());

    // Undo recreates the plugin (behind the loading fence) and restores its automation verbatim.
    controller.onUndoRequested();
    while (view.hasBusyOverlayPaintCallback())
    {
        view.runNextBusyOverlayPaintCallback();
    }
    REQUIRE(model.size() == 1);
    CHECK(model.front().points == points);

    // Redo removes the plugin and its automation again.
    controller.onRedoRequested();
    CHECK(model.empty());
}

TEST_CASE(
    "EditorController rebuilds derived curves from persisted automation at load",
    "[core][tone-automation]")
{
    common::core::Song song = makeAutomationSong();
    song.arrangements.front().tone_automation = {
        common::core::ToneParameterAutomation{
            .plugin_id = g_plugin_id,
            .param_id = g_param,
            .points = {
                common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}
            },
        },
    };
    const AutomationEditor editor{std::move(song)};

    // The load-completion rebuild wrote the derived curve from the persisted musical truth.
    const auto written = editor.tone_automation.curves.find(
        FakeToneAutomation::curveKey(g_tone_document_ref, g_instance, g_param));
    REQUIRE(written != editor.tone_automation.curves.end());
    REQUIRE(written->second.size() == 1);
    CHECK(written->second.front().seconds == Catch::Approx(2.0));
    CHECK(written->second.front().norm_value == 0.75F);

    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().resolved);
}

TEST_CASE(
    "EditorController keeps automation lanes bound across rig reloads", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().resolved);

    // A rig reload recreates every plugin instance: the same durable stable id comes back under
    // a brand-new instance id. Force the create onto the full-reload fallback (the fast path
    // adds a branch without recreating instances) to exercise exactly that reload.
    editor.live_rig.next_add_branch_error = common::audio::LiveRigError{
        common::audio::LiveRigErrorCode::InvalidRequest, "forced fallback"
    };
    constexpr const char* reloaded_instance = "plugin-instance-2";
    editor.tone_automation.parameters.clear();
    editor.tone_automation.parameters.push_back(makeParam());
    editor.tone_automation.parameters.front().instance_id = reloaded_instance;
    editor.live_rig.next_load_result.tone_chains = {
        common::audio::LoadedToneChainIdentities{
            .tone_document_ref = std::string{g_tone_document_ref},
            .plugins = {common::audio::LoadedTonePluginIdentity{
                .instance_id = reloaded_instance,
                .stable_id = g_plugin_id,
            }},
        },
    };
    editor.controller.onToneCreateNewRequested(
        common::core::GridPosition{.measure = 2, .beat = 1}, "Solo");

    // Back on the original tone, the open lane must resolve through the durable plugin id to the
    // reloaded instance instead of dangling on the dead one.
    editor.controller.onToneRegionSelected(g_region);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().resolved);
    CHECK(editor.automation().lanes.front().instance_id == reloaded_instance);
    CHECK(editor.automation().lanes.front().name == "Gain");
}

TEST_CASE(
    "EditorController mirrors the song tempo map at rig-load completion", "[core][tone-automation]")
{
    const AutomationEditor editor;

    // The one-way host-tempo mirror runs beside the derived-curve rebuild so hosted plugins see
    // the loaded song's tempo map instead of the backend default.
    CHECK(editor.audio.mirror_tempo_map_call_count == 1);
    REQUIRE(editor.audio.last_mirrored_tempo_map.has_value());
    if (editor.audio.last_mirrored_tempo_map.has_value())
    {
        CHECK(
            *editor.audio.last_mirrored_tempo_map == editor.controller.session().song().tempo_map);
    }
}

} // namespace rock_hero::editor::core
