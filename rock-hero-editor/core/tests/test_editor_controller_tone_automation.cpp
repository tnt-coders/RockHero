#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
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

constexpr const char* g_region = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";
constexpr const char* g_instance = "plugin-instance-1";
constexpr const char* g_param = "gain";

[[nodiscard]] common::core::ToneGridPosition gridAt(int measure, int beat)
{
    return common::core::ToneGridPosition{.measure = measure, .beat = beat};
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
    };
}

// Loads the automation song with the region selected, so the selected tone drives the projection,
// and a tone-automation fake that exposes one parameter.
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

    AutomationEditor()
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
        project_services.next_song = makeAutomationSong();
        controller.attachView(view);
        controller.onOpenRequested(std::filesystem::path{"song.rhp"});
        controller.onToneRegionSelected(g_region);
    }

    [[nodiscard]] const ToneAutomationViewState& automation() const
    {
        REQUIRE(view.last_state.has_value());
        return view.last_state->tone_automation;
    }
};

} // namespace

TEST_CASE(
    "EditorController writes tone automation points and projects a lane", "[core][tone-automation]")
{
    AutomationEditor editor;
    const std::vector<common::audio::AutomationCurvePoint> points{
        common::audio::AutomationCurvePoint{.seconds = 0.0, .norm_value = 0.2F},
        common::audio::AutomationCurvePoint{.seconds = 2.0, .norm_value = 0.8F},
    };

    editor.controller.onSetToneAutomationPoints(g_tone_document_ref, g_instance, g_param, points);

    CHECK(editor.tone_automation.write_call_count == 1);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().param_id == g_param);
    CHECK(editor.automation().lanes.front().name == "Gain");
    REQUIRE(editor.automation().lanes.front().points.size() == 2);
    CHECK(editor.automation().lanes.front().points.back().norm_value == 0.8F);
}

TEST_CASE("EditorController undoes and redoes a tone automation edit", "[core][tone-automation]")
{
    AutomationEditor editor;
    const std::vector<common::audio::AutomationCurvePoint> points{
        common::audio::AutomationCurvePoint{.seconds = 1.0, .norm_value = 0.6F},
    };

    editor.controller.onSetToneAutomationPoints(g_tone_document_ref, g_instance, g_param, points);
    REQUIRE(editor.automation().lanes.size() == 1);

    editor.controller.onUndoRequested();
    CHECK(editor.automation().lanes.empty());

    editor.controller.onRedoRequested();
    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.automation().lanes.front().points.size() == 1);
    CHECK(editor.automation().lanes.front().points.front().norm_value == 0.6F);
}

} // namespace rock_hero::editor::core
