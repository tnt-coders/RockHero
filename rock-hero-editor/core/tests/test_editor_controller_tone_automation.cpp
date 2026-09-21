#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <compare>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/testing/chart_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <rock_hero/editor/core/tone/tone_automation_pointer.h>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_region = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e";
// A second region and tone, so a test can close the first region's window early: a region ends
// where the next begins, and adjacent regions must name different tones.
constexpr const char* g_later_region = "6b2e1d4f-8a3c-4b1d-9e2f-3a4b5c6d7e8f";
constexpr const char* g_later_tone_ref = "tones/1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d/tone.json";
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
        .baseline_norm_value = 0.4F,
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

    // chain_tone_ref is the tone the load reports the plugin chain under. Tests pass an empty ref
    // to reproduce a plugin whose runtime association has no tone yet (inserted before a region was
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
        // Every scenario in this file states its placements in quarter-note grid slots, so pin
        // that grid explicitly instead of riding the editor default.
        controller.onGridNoteValueChangeRequested(common::core::Fraction{1, 4});
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

    // Authors the given points on the test lane through the commit intent, seeding a drag's
    // starting model (also opens the lane so it is published).
    void seedPoints(std::vector<common::core::ToneAutomationPoint> points)
    {
        controller.onToneAutomationLaneAddRequested(g_instance, g_param);
        controller.onToneAutomationPointsEditRequested(g_instance, g_param, std::move(points));
    }
};

// The full content width and visible timeline the pointer-gesture tests map pixels against, plus
// the pressed lane's value band. At 400 px over [0, 4] s a point at t seconds sits at x = 100·t
// (the ÷width forward map the hit-test uses); the placement snap inverts through ÷ (width - 1). The
// band {top 5, height 40} matches a default 56 px lane, so value v draws at y = 5 + (1 - v)·40.
constexpr int g_pointer_content_width = 400;
constexpr float g_pointer_band_top = 5.0F;
constexpr float g_pointer_band_height = 40.0F;

[[nodiscard]] common::core::TimeRange pointerVisibleTimeline()
{
    return common::core::TimeRange{
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
}

// The pixel y a value draws at in the test band, so a press or drag targets a value exactly.
[[nodiscard]] float pointerYForValue(float value)
{
    return g_pointer_band_top + ((1.0F - value) * g_pointer_band_height);
}

// A Down/Drag/Up pointer event over the single test lane (index 0). Drag/Up ride the frozen
// gesture, so they ignore the lane geometry, but one builder keeps every phase uniform.
[[nodiscard]] ToneAutomationPointerEvent pointerEvent(
    float x, float y, ToneAutomationPointerModifiers modifiers = {}, bool is_discrete = false,
    int discrete_value_count = 0, int clicks = 1)
{
    ToneAutomationPointerEvent event;
    event.instance_id = g_instance;
    event.param_id = g_param;
    event.geometry.visible_timeline = pointerVisibleTimeline();
    event.geometry.content_width = g_pointer_content_width;
    event.lane_extents = {ToneAutomationLaneExtent{
        .value_band_top = g_pointer_band_top, .value_band_height = g_pointer_band_height
    }};
    event.lane_index = 0;
    event.lane_is_discrete = is_discrete;
    event.lane_discrete_value_count = discrete_value_count;
    event.x = x;
    event.y = y;
    event.clicks = clicks;
    event.modifiers = modifiers;
    return event;
}

// A Drag-phase event: same as pointerEvent but flagged as having crossed the framework's click→drag
// threshold, so an existing-point grab actually advances (JUCE's mouseWasDraggedSinceMouseDown,
// which the shipped view forwarded).
[[nodiscard]] ToneAutomationPointerEvent dragEvent(
    float x, float y, ToneAutomationPointerModifiers modifiers = {}, bool is_discrete = false,
    int discrete_value_count = 0)
{
    ToneAutomationPointerEvent event =
        pointerEvent(x, y, modifiers, is_discrete, discrete_value_count);
    event.dragged_since_down = true;
    return event;
}

// The automation song with a noteless six-string chart, so the keyboard's focus rows have strings
// above the tone row to walk from.
[[nodiscard]] common::core::Song makeChartedAutomationSong()
{
    common::core::Song song = makeAutomationSong();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    song.arrangements.front().chart = std::move(chart);
    return song;
}

// A tab-lane press on a string at a musical time, over a 400x240 six-lane band spanning the whole
// four-second fixture timeline: 100 px/s and 40 px lanes, so string 1 draws at the bottom (y = 220)
// and each string above it 40 px higher.
[[nodiscard]] ChartPointerEvent chartPressAt(double seconds, int string_number)
{
    return ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            0.0F, 0.0F, 400.0F, 240.0F, pointerVisibleTimeline(), 6, 6),
        .x = static_cast<float>(seconds * 100.0),
        .y = 240.0F - (static_cast<float>(string_number) * 40.0F) + 20.0F,
        .modifiers = {},
        .clicks = 1,
    };
}

// The charted automation song with a second tone change: the opening region keeps the harness tone,
// whose chain owns the test lane, and a later region from later_start sounds a tone with no lanes.
[[nodiscard]] common::core::Song makeTwoToneChartedSong(common::core::GridPosition later_start)
{
    common::core::Song song = makeChartedAutomationSong();
    song.arrangements.front().tones.push_back(
        common::core::Tone{.tone_document_ref = g_later_tone_ref, .name = "Dirty"});
    song.arrangements.front().tone_track.regions.push_back(
        common::core::ToneRegion{
            .id = g_later_region,
            .start = later_start,
            .tone_document_ref = g_later_tone_ref,
        });
    return song;
}

} // namespace

TEST_CASE(
    "EditorController opens an unauthored lane without authoring points", "[core][tone-automation]")
{
    AutomationEditor editor;

    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // Opening a lane authors nothing: the model stays empty and no derived curve is written; the
    // lane shows only its derived anchor until the first point is added.
    CHECK(editor.model().empty());
    CHECK(editor.tone_automation.write_call_count == 0);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(editor.automation().lanes.front().instance_id == g_instance);
    CHECK(editor.automation().lanes.front().name == "Gain");
    CHECK(editor.automation().lanes.front().plugin_name.empty());
    CHECK(editor.automation().lanes.front().resolved);
    CHECK(editor.automation().lanes.front().points.empty());
    CHECK(std::is_eq(editor.automation().lanes.front().anchor_norm_value <=> 0.4F));

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
    // than the lane vanishing the moment it stops being an unauthored lane.
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.3F}});
    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.automation().lanes.front().points.size() == 1);
    CHECK(std::is_eq(editor.automation().lanes.front().points.front().norm_value <=> 0.3F));
}

TEST_CASE(
    "EditorController stores musical automation points and derives seconds",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    const std::vector<common::core::ToneAutomationPoint> points{
        common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
        common::core::ToneAutomationPoint{.position = pointAt(2, 1, 1), .norm_value = 0.8F},
    };

    editor.controller.onToneAutomationPointsEditRequested(g_instance, g_param, points);

    REQUIRE(editor.model().size() == 1);
    CHECK(editor.model().front().points == points);

    // The default 4/4 map runs at 120 BPM (0.5 s per beat): measure 2 beat 1 plus half a beat is
    // global beat 4.5 , i.e. 2.25 seconds.
    const auto written = editor.tone_automation.curves.find(
        FakeToneAutomation::curveKey(g_tone_document_ref, g_instance, g_param));
    REQUIRE(written != editor.tone_automation.curves.end());
    REQUIRE(written->second.size() == 2);
    CHECK(written->second.back().seconds == Catch::Approx(2.25));
    CHECK(std::is_eq(written->second.back().norm_value <=> 0.8F));

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

    editor.controller.onToneAutomationPointsEditRequested(g_instance, g_param, points);
    REQUIRE(editor.automation().lanes.size() == 1);

    editor.controller.onUndoRequested();
    CHECK(editor.model().empty());
    CHECK(editor.automation().lanes.empty());

    editor.controller.onRedoRequested();
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.automation().lanes.size() == 1);
    CHECK(std::is_eq(editor.automation().lanes.front().points.front().norm_value <=> 0.6F));
}

TEST_CASE(
    "EditorController removes a plugin's automation with it and restores it on undo",
    "[core][tone-automation]")
{
    // Seed the load so g_instance is a removable plugin in both the editor chain (its automation
    // resolves through the tone-chain identity) and the fake backend (capture/remove/recreate act
    // on it). This mirrors a loaded song whose tone already hosts a
    // plugin, without the async insert.
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeToneAutomation tone_automation;
    FakeProjectServices project_services;
    common::audio::testing::InMemoryAudioConfigStore store;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig, tone_automation),
        controllerServices(nullEditorSettings(), store, monitor),
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
    controller.onToneAutomationPointsEditRequested(g_instance, g_param, points);
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
    CHECK_THAT(written->second.front().norm_value, Catch::Matchers::WithinULP(0.75F, 0));

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

TEST_CASE(
    "EditorController owns the automation point selection editor-wide", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.8F},
        });

    // The fixture formally selected the tone region; the flag publishes on the tone track.
    REQUIRE(editor.view.last_state.has_value());
    if (!editor.view.last_state.has_value())
    {
        throw std::logic_error("editor pushed no view state");
    }
    REQUIRE(editor.view.last_state->tone_track.regions.size() == 1);
    CHECK(editor.view.last_state->tone_track.regions.front().selected);

    // Selecting a point makes it THE selection: the published point reference resolves and the
    // region's selected flag drops (one selection editor-wide — two cannot coexist).
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(2, 1));
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->lane_index == 0);
    CHECK(selected->point_index == 1);
    CHECK_FALSE(editor.view.last_state->tone_track.regions.front().selected);
    // A selected point is what Alt+Up/Down act on.
    CHECK(editor.view.last_state->selection_start_seconds == std::optional{2.0});

    // The caret arms at the clicked point's slot, so keyboard verbs continue from the object just
    // touched.
    const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(editor.automation());
    REQUIRE(caret != nullptr);
    CHECK(caret->lane_index == 0);
    CHECK(caret->position == pointAt(2, 1));

    // A seek is cursor motion: the cursor-coupled selection clears, exactly like the shipped
    // tone-region rule.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{1.0});
    CHECK_FALSE(editor.automation().selected_point.has_value());
}

TEST_CASE(
    "EditorController arms the lane caret on a lane click and Inserts on the curve",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // The lane click seeks to the nearest grid slot and arms the caret there: 1.1 s snaps to
    // the 1.0 s quarter-note line (measure 1 beat 3 at 120 BPM 4/4).
    editor.controller.onToneAutomationLaneCaretRequested(
        g_instance, g_param, common::core::TimePosition{1.1});
    const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(editor.automation());
    REQUIRE(caret != nullptr);
    CHECK(caret->lane_index == 0);
    CHECK(caret->position == pointAt(1, 3));
    CHECK(editor.transport.position().seconds == Catch::Approx(1.0));

    // Insert is the neutral create: on an unauthored lane the point lands on the anchor's flat
    // line (the tone state's value, 0.4 in the fixture) and becomes the selection.
    editor.controller.onLanePointInsertRequested();
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK(editor.model().front().points.front().position == pointAt(1, 3));
    CHECK(std::is_eq(editor.model().front().points.front().norm_value <=> 0.4F));
    REQUIRE(editor.automation().selected_point.has_value());

    // A second Insert at the now-occupied slot is a no-op: Insert never mutates existing
    // objects.
    editor.controller.onLanePointInsertRequested();
    CHECK(editor.model().front().points.size() == 1);

    // Arming onto the occupied slot re-derives the selection from the point under the caret.
    editor.controller.onToneAutomationLaneCaretRequested(
        g_instance, g_param, common::core::TimePosition{1.0});
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->point_index == 0);
}

TEST_CASE(
    "EditorController deletes the selected automation point through the one Delete dispatch",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.8F},
        });
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(2, 1));

    // The unified Delete intent dispatches on the selection's kind and removes exactly the
    // selected point as one undoable points edit.
    editor.controller.onSelectionDeleteRequested();
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK(editor.model().front().points.front().position == pointAt(1, 1));
    CHECK_FALSE(editor.automation().selected_point.has_value());

    // The durable selection stays put through the delete, so undoing the removal lights the
    // restored point back up instead of leaving it unselected.
    editor.controller.onUndoRequested();
    REQUIRE(editor.model().front().points.size() == 2);
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->point_index == 1);

    // A stale selection (the point already gone) deletes nothing.
    editor.controller.onSelectionDeleteRequested();
    editor.controller.onSelectionDeleteRequested();
    REQUIRE(editor.model().size() == 1);
    CHECK(editor.model().front().points.size() == 1);
}

TEST_CASE(
    "EditorController moves the selected automation point through the one move dispatch",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.8F},
        });
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(2, 1));

    // Alt+Up steps the value by 0.01 as one committed edit; the selection stays on the point.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK(std::abs(editor.model().front().points.back().norm_value - 0.81F) < 0.0001F);

    // Alt+Right moves to the adjacent grid line; the selection re-points to the new identity.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.model().front().points.back().position == pointAt(2, 2));
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->point_index == 1);

    // Arm the lane caret onto the moved point (2.5 s = measure 2 beat 2): further nudges carry
    // the caret along — it stays on its object through the move.
    editor.controller.onToneAutomationLaneCaretRequested(
        g_instance, g_param, common::core::TimePosition{2.5});
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.model().front().points.back().position == pointAt(2, 3));
    const ToneAutomationLaneCaretRef* const moved_caret = laneCaretOrNull(editor.automation());
    REQUIRE(moved_caret != nullptr);
    CHECK(moved_caret->position == pointAt(2, 3));

    // With snap off, Alt+Left steps back one tick (1/3840 whole note, 1/960 beat in x/4): the
    // point (and its caret) leave the grid exactly. Snap goes back on for the grid steps below.
    turnGridSnapOff(editor.controller);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    editor.controller.onGridSnapToggleRequested();
    const common::core::GridPosition tick_slot{
        .measure = 2, .beat = 2, .offset = common::core::Fraction{959, 960}
    };
    CHECK(editor.model().front().points.back().position == tick_slot);
    const ToneAutomationLaneCaretRef* const tick_caret = laneCaretOrNull(editor.automation());
    REQUIRE(tick_caret != nullptr);
    CHECK(tick_caret->position == tick_slot);

    // A grid step from the off-grid slot lands on the ADJACENT grid line — never jumping past
    // it (the shared adjacent-line primitive, matching the caret's own stepping rule); the
    // step that would land on the region's end boundary (4.0 s) is refused by the window
    // clamp.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.model().front().points.back().position == pointAt(2, 3));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.model().front().points.back().position == pointAt(2, 4));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.model().front().points.back().position == pointAt(2, 4));

    // At the map edge the step collapses: refused, nothing changes.
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(1, 1));
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    CHECK(editor.model().front().points.front().position == pointAt(1, 1));
}

TEST_CASE(
    "EditorController creates on the curve when the move intent lands on an empty lane slot",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(3, 1), .norm_value = 0.8F},
        });

    // Arm the caret at the empty midpoint slot (2.0 s, measure 2 beat 1), where the drawn
    // curve reads 0.5; arming an empty slot leaves nothing selected.
    editor.controller.onToneAutomationLaneCaretRequested(
        g_instance, g_param, common::core::TimePosition{2.0});
    CHECK_FALSE(editor.automation().selected_point.has_value());

    // Alt+Up creates ON the curve with the step baked in — one points edit, one undo entry —
    // and the new point becomes the selection.
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    REQUIRE(editor.model().front().points.size() == 3);
    CHECK(editor.model().front().points[1].position == pointAt(2, 1));
    CHECK(std::abs(editor.model().front().points[1].norm_value - 0.51F) < 0.0001F);
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->point_index == 1);

    // One undo removes the whole create-then-nudge (the step was baked into the creation).
    editor.controller.onUndoRequested();
    CHECK(editor.model().front().points.size() == 2);
}

TEST_CASE(
    "EditorController refuses lane point creation outside the active region window",
    "[core][tone-automation]")
{
    // Stepping runs on the marker's shared row axis, which needs a (noteless) chart.
    common::core::Song song = makeAutomationSong();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    song.arrangements.front().chart = std::move(chart);
    AutomationEditor editor{std::move(song)};
    // The lane is shown, as it is whenever a charter has a caret on it: a caret stepping on a lane
    // that is not visible falls back onto its string, like every landing that keeps the row.
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // Arm at 3.5 s (measure 2 beat 4) and step onto the region's end boundary (4.0 s): the
    // caret may rest there — stepping is navigation — but creation refuses outside the window,
    // exactly as moves and drags do, so no immovable point can ever be planted.
    editor.controller.onToneAutomationLaneCaretRequested(
        g_instance, g_param, common::core::TimePosition{3.5});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onLanePointInsertRequested();
    CHECK(editor.model().empty());
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Up);
    CHECK(editor.model().empty());

    // One step back inside the window, the same verbs create as always.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    editor.controller.onLanePointInsertRequested();
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK(editor.model().front().points.front().position == pointAt(2, 4));
}

TEST_CASE(
    "EditorController steps the Esc ladder on lane carets and point selections",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.8F},
        });
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(2, 1));
    REQUIRE(editor.automation().lane_caret.has_value());
    REQUIRE(editor.automation().selected_point.has_value());

    // The first Esc dissolves the lane caret in place (the marker rung is row-agnostic),
    // keeping the selection.
    editor.controller.onChartEscapePressed();
    CHECK_FALSE(editor.automation().lane_caret.has_value());
    CHECK(editor.automation().selected_point.has_value());

    // The second Esc clears THE selection — the last rung is kind-agnostic, like Delete.
    editor.controller.onChartEscapePressed();
    CHECK_FALSE(editor.automation().selected_point.has_value());
}

TEST_CASE("EditorController steps the lane caret onto off-grid points", "[core][tone-automation]")
{
    // Lane caret stepping runs on the marker's shared row axis, which needs a (noteless) chart
    // so the tab projection exists.
    common::core::Song song = makeAutomationSong();
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    song.arrangements.front().chart = std::move(chart);
    AutomationEditor editor{std::move(song)};

    editor.controller.onToneAutomationPointsEditRequested(
        g_instance,
        g_param,
        {
            common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.2F},
            common::core::ToneAutomationPoint{.position = pointAt(2, 2), .norm_value = 0.8F},
        });

    // Selecting the (2,2) point arms the caret on it; a tick step with snap off slides both off
    // the grid, and snap goes back on so the plain arrows below step the grid again.
    editor.controller.onToneAutomationPointSelectRequested(g_instance, g_param, pointAt(2, 2));
    turnGridSnapOff(editor.controller);
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Left);
    editor.controller.onGridSnapToggleRequested();
    const common::core::GridPosition tick_slot{
        .measure = 2, .beat = 1, .offset = common::core::Fraction{959, 960}
    };
    const ToneAutomationLaneCaretRef* const tick_caret = laneCaretOrNull(editor.automation());
    REQUIRE(tick_caret != nullptr);
    CHECK(tick_caret->position == tick_slot);

    // Plain Left steps to the (2,1) grid line — an empty slot, so the selection clears.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    const ToneAutomationLaneCaretRef* const left_caret = laneCaretOrNull(editor.automation());
    REQUIRE(left_caret != nullptr);
    CHECK(left_caret->position == pointAt(2, 1));
    CHECK_FALSE(editor.automation().selected_point.has_value());

    // Plain Right stops ON the off-grid point before the (2,2) line, selecting it (the union
    // stop set on lane rows).
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ToneAutomationLaneCaretRef* const right_caret = laneCaretOrNull(editor.automation());
    REQUIRE(right_caret != nullptr);
    CHECK(right_caret->position == tick_slot);
    const ToneAutomationSelectedPointRef* const selected = selectedPointOrNull(editor.automation());
    REQUIRE(selected != nullptr);
    CHECK(selected->point_index == 1);
}

TEST_CASE(
    "EditorController snaps the insert ghost through the placement seam, not the raw pixel time",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    // Geometry chosen so the two x->seconds conversions straddle a snap boundary. Over [0, 4] s
    // with a 401 px content width, the pixel x 225.3 maps to 2.253 s through the placement seam
    // (timelinePositionForX divides by width - 1 and clamps) but to 2.247 s through Phase 1's
    // secondsForX (which divides by width). The quarter-note grid boundary sits at 2.25 s, so the
    // two paths snap to DIFFERENT slots — this is exactly the <=1px discrepancy Phase 2 erases.
    const common::core::TimeRange visible{
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
    constexpr int content_width = 401;
    constexpr float boundary_x = 225.3F;

    editor.controller.onToneAutomationPointerMove(
        ToneAutomationPointerEvent{
            .instance_id = g_instance,
            .param_id = g_param,
            .geometry =
                ToneAutomationLaneGeometry{
                    .visible_timeline = visible, .content_width = content_width
                },
            .lane_extents = {},
            .x = boundary_x,
            .y = 0.0F,
            .modifiers = ToneAutomationPointerModifiers{.alt = true},
        });

    const common::core::TempoMap& tempo_map = editor.controller.session().song().tempo_map;

    // The slot an Alt+click at this pixel would place: the raw pixel inverted through the exact
    // placement seam (timelinePositionForX then the tempo grid) — 2.5 s, measure 2 beat 2.
    const std::optional<common::core::TimePosition> placement_time =
        timelinePositionForX(boundary_x, visible, content_width);
    REQUIRE(placement_time.has_value());
    if (!placement_time.has_value())
    {
        throw std::logic_error("placement seam mapped no time");
    }
    const common::core::GridPosition placement_slot =
        nearestTempoGridPosition(tempo_map, common::core::Fraction{1, 4}, *placement_time);
    CHECK(placement_slot == gridAt(2, 2));

    // Phase 1's divide-by-width secondsForX would have snapped this same pixel to the EARLIER slot
    // (2.0 s, measure 2 beat 1): the two paths genuinely disagree here.
    const double phase1_seconds = visible.start.seconds + (static_cast<double>(boundary_x) /
                                                           static_cast<double>(content_width)) *
                                                              visible.duration().seconds;
    const common::core::GridPosition phase1_slot = nearestTempoGridPosition(
        tempo_map, common::core::Fraction{1, 4}, common::core::TimePosition{phase1_seconds});
    CHECK(phase1_slot == gridAt(2, 1));
    CHECK(phase1_slot != placement_slot);

    // The published ghost lands on the placement slot, not Phase 1's: the gap is closed. Its
    // seconds match the placement path's secondsAtNote exactly (2.5 s), never the 2.0 s Phase 1
    // would have produced.
    REQUIRE(editor.automation().insert_ghost.has_value());
    if (editor.automation().insert_ghost.has_value())
    {
        CHECK(editor.automation().insert_ghost->lane_index == 0);
        CHECK(
            editor.automation().insert_ghost->seconds ==
            Catch::Approx(tempo_map.secondsAtNote(
                placement_slot.measure, placement_slot.beat, placement_slot.offset)));
        CHECK(editor.automation().insert_ghost->seconds == Catch::Approx(2.5));
        CHECK(editor.automation().insert_ghost->seconds != Catch::Approx(2.0));
    }
}

TEST_CASE(
    "EditorController arms the lane caret through the placement seam, not the raw pixel time",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    // The same boundary geometry the ghost-snap test uses, so the caret's snap is proven against
    // the identical disagreement: over [0, 4] s at 401 px, the pixel x 225.3 inverts to 2.253 s
    // through the placement seam (÷ (width - 1)) but to 2.247 s through the shipped view's ÷width
    // secondsForX. The 1/4 grid boundary sits at 2.25 s, so the two paths
    // arm the caret on DIFFERENT slots.
    const common::core::TimeRange visible{
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
    constexpr int content_width = 401;
    constexpr float boundary_x = 225.3F;

    // A plain (no Alt) primary press on the empty unauthored lane arms the caret — the row-axis
    // empty click. The lane extent lets the press resolve to the lane; y is irrelevant at this
    // pixel (no point to grab, and it is nowhere near the lane's anchor), so it sits mid-band.
    ToneAutomationPointerEvent down;
    down.instance_id = g_instance;
    down.param_id = g_param;
    down.geometry.visible_timeline = visible;
    down.geometry.content_width = content_width;
    down.lane_extents = {ToneAutomationLaneExtent{
        .value_band_top = g_pointer_band_top, .value_band_height = g_pointer_band_height
    }};
    down.lane_index = 0;
    down.x = boundary_x;
    down.y = pointerYForValue(0.5F);
    down.clicks = 1;
    down.modifiers = ToneAutomationPointerModifiers{.alt = false};
    editor.controller.onToneAutomationPointerDown(down);

    const common::core::TempoMap& tempo_map = editor.controller.session().song().tempo_map;

    // The slot an Alt+click / the insert ghost lands on at this pixel — the placement seam then the
    // tempo grid — 2.5 s, measure 2 beat 2.
    const std::optional<common::core::TimePosition> placement_time =
        timelinePositionForX(boundary_x, visible, content_width);
    REQUIRE(placement_time.has_value());
    if (!placement_time.has_value())
    {
        throw std::logic_error("placement seam mapped no time");
    }
    const common::core::GridPosition placement_slot =
        nearestTempoGridPosition(tempo_map, common::core::Fraction{1, 4}, *placement_time);
    CHECK(placement_slot == gridAt(2, 2));

    // The shipped ÷width secondsForX would have armed the caret one slot EARLIER (2.0 s, measure 2
    // beat 1): the two paths genuinely disagree at this pixel.
    const double phase1_seconds = visible.start.seconds + (static_cast<double>(boundary_x) /
                                                           static_cast<double>(content_width)) *
                                                              visible.duration().seconds;
    const common::core::GridPosition phase1_slot = nearestTempoGridPosition(
        tempo_map, common::core::Fraction{1, 4}, common::core::TimePosition{phase1_seconds});
    CHECK(phase1_slot == gridAt(2, 1));
    CHECK(phase1_slot != placement_slot);

    // The armed caret lands on the placement slot, not the shipped ÷width slot: the caret now sits
    // exactly where an Alt+click / ghost would at this pixel, and the transport follows it there.
    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->lane_index == 0);
        CHECK(editor.automation().lane_caret->position == placement_slot);
        CHECK(editor.automation().lane_caret->position != phase1_slot);
    }
    CHECK(editor.transport.position().seconds == Catch::Approx(2.5));
    CHECK(editor.transport.position().seconds != Catch::Approx(2.0));
}

TEST_CASE(
    "EditorController arms the lane caret on the placement quantum, snap on or off",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    // Routing the caret arm through the one placement snap (laneSnapPositionForX) means it reads
    // the session's placement quantum exactly as lane placement and the insert ghost do — one
    // lattice, no per-gesture modifier. This pixel inverts to an off-grid 2.13 s, so the grid and
    // tick answers disagree and the test can tell which one the caret took.
    const common::core::TimeRange visible{
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
    constexpr int content_width = 401;
    constexpr float off_grid_x = 213.0F;

    const common::core::TempoMap& tempo_map = editor.controller.session().song().tempo_map;
    const std::optional<common::core::TimePosition> placement_time =
        timelinePositionForX(off_grid_x, visible, content_width);
    REQUIRE(placement_time.has_value());
    if (!placement_time.has_value())
    {
        throw std::logic_error("placement seam mapped no time");
    }

    // The nearest 1/4 line is measure 2 beat 1 (2.0 s); the tick lattice keeps the off-grid slot.
    // Both are derived through the same helper the controller's snap uses, so the expectation
    // tracks the placement seam rather than a hand-computed fraction.
    const common::core::GridPosition coarse_slot =
        nearestTempoGridPosition(tempo_map, common::core::Fraction{1, 4}, *placement_time);
    const common::core::GridPosition tick_slot = nearestTempoGridPosition(
        tempo_map, common::core::g_tick_quantum_note_value, *placement_time);
    CHECK(coarse_slot == gridAt(2, 1));
    CHECK(tick_slot != coarse_slot);

    const auto down = [&] {
        ToneAutomationPointerEvent event;
        event.instance_id = g_instance;
        event.param_id = g_param;
        event.geometry.visible_timeline = visible;
        event.geometry.content_width = content_width;
        event.lane_extents = {ToneAutomationLaneExtent{
            .value_band_top = g_pointer_band_top, .value_band_height = g_pointer_band_height
        }};
        event.lane_index = 0;
        event.x = off_grid_x;
        event.y = pointerYForValue(0.5F);
        event.clicks = 1;
        event.modifiers = ToneAutomationPointerModifiers{.alt = false};
        return event;
    };

    // Snap on (the session default): the same pixel arms on the coarse grid line.
    editor.controller.onToneAutomationPointerDown(down());
    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->position == coarse_slot);
    }

    // Snap off: the caret arms on the tick lattice instead — the mode is what selects the answer.
    turnGridSnapOff(editor.controller);
    editor.controller.onToneAutomationPointerDown(down());
    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->position == tick_slot);
        CHECK(editor.automation().lane_caret->position != coarse_slot);
    }
}

TEST_CASE(
    "EditorController clears the insert ghost on a non-Alt move and on pointer exit",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    const common::core::TimeRange visible{
        .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
    };
    const auto move_at = [&](float x, bool alt) {
        return ToneAutomationPointerEvent{
            .instance_id = g_instance,
            .param_id = g_param,
            .geometry =
                ToneAutomationLaneGeometry{.visible_timeline = visible, .content_width = 400},
            .lane_extents = {},
            .x = x,
            .y = 0.0F,
            .modifiers = ToneAutomationPointerModifiers{.alt = alt},
        };
    };

    // An Alt hover over an insertable slot publishes the ghost (x 200 of 400 -> 2.0 s, measure 2).
    editor.controller.onToneAutomationPointerMove(move_at(200.0F, true));
    CHECK(editor.automation().insert_ghost.has_value());

    // A move without Alt clears it: the ghost previews the Alt-create gesture only, so a plain
    // caret-arming hover must show none.
    editor.controller.onToneAutomationPointerMove(move_at(200.0F, false));
    CHECK_FALSE(editor.automation().insert_ghost.has_value());

    // Re-arm, then the pointer leaving the lanes clears it too.
    editor.controller.onToneAutomationPointerMove(move_at(200.0F, true));
    REQUIRE(editor.automation().insert_ghost.has_value());
    editor.controller.onToneAutomationPointerExit();
    CHECK_FALSE(editor.automation().insert_ghost.has_value());
}

TEST_CASE(
    "EditorController lands a mouse-inserted point on the curve and pulls it by delta",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    REQUIRE(editor.automation().lanes.size() == 1);

    const ToneAutomationPointerModifiers alt{.alt = true, .shift = false};

    // Alt-press at x 100 (1.0 s, measure 1 beat 3 — halfway between the authored points) well below
    // the curve (y for 0.125): the new point lands ON the drawn curve (the 0.5 interpolation), not
    // at the pointer's y, so a release without a pull is sonically silent.
    //
    // This press-and-release never crosses the drag threshold and still authors: that is the Alt
    // law, and it is the control the anchor's click rule must not break — the anchor answers a
    // drag, Alt on empty area answers the click itself.
    editor.controller.onToneAutomationPointerDown(
        pointerEvent(100.0F, pointerYForValue(0.125F), alt));
    // The preview shows on the press (that gesture has its edit in hand at once), on the curve.
    REQUIRE(editor.automation().drag_preview.has_value());
    if (editor.automation().drag_preview.has_value())
    {
        CHECK(editor.automation().drag_preview->is_new_point);
        CHECK(editor.automation().drag_preview->position == gridAt(1, 3));
        CHECK_THAT(editor.automation().drag_preview->value, Catch::Matchers::WithinULP(0.5F, 0));
    }
    editor.controller.onToneAutomationPointerUp(
        pointerEvent(100.0F, pointerYForValue(0.125F), alt));
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.model().front().points.size() == 3);
    CHECK(editor.model().front().points[1].position == gridAt(1, 3));
    CHECK_THAT(editor.model().front().points[1].norm_value, Catch::Matchers::WithinULP(0.5F, 0));
    // The committed preview is gone and the landed point is the selection.
    CHECK_FALSE(editor.automation().drag_preview.has_value());

    // Re-seed, then pull the on-curve landing up by delta: rising 10 px (a quarter of the 40 px
    // band) from press y 40 to y 30 lifts the 0.5 landing to 0.75 — never jumping to the raw
    // pointer y (which at y 30 reads 0.375).
    AutomationEditor pulled;
    pulled.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    pulled.controller.onToneAutomationPointerDown(pointerEvent(100.0F, 40.0F, alt));
    pulled.controller.onToneAutomationPointerDrag(dragEvent(100.0F, 30.0F, alt));
    pulled.controller.onToneAutomationPointerUp(pointerEvent(100.0F, 30.0F, alt));
    REQUIRE(pulled.model().front().points.size() == 3);
    CHECK(pulled.model().front().points[1].position == gridAt(1, 3));
    CHECK_THAT(pulled.model().front().points[1].norm_value, Catch::Matchers::WithinULP(0.75F, 0));
}

TEST_CASE(
    "EditorController authors a real point when the lane anchor is dragged",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.model().empty());

    // The anchor sits at the lane's start (x 0) at the parameter's tone-state value (0.4). It is
    // read-only, so a press on it does not move it — it arms a NEW point at the lane start, on the
    // curve (which at the start IS the anchor's value), with no Alt needed. Like a point grab, the
    // press stays a click until the pointer crosses the drag threshold, so it publishes nothing
    // yet: the anchor's mark simply stays as it is.
    editor.controller.onToneAutomationPointerDown(pointerEvent(0.0F, pointerYForValue(0.4F)));
    CHECK_FALSE(editor.automation().drag_preview.has_value());

    // Dragging up 10 px of the 40 px band crosses the threshold and lifts the landing by a
    // quarter — from the anchor's own 0.4, which is what makes the committed 0.65 proof that the
    // new point was born on the anchor's value rather than at the pointer.
    editor.controller.onToneAutomationPointerDrag(dragEvent(0.0F, pointerYForValue(0.4F) - 10.0F));
    REQUIRE(editor.automation().drag_preview.has_value());
    if (editor.automation().drag_preview.has_value())
    {
        CHECK(editor.automation().drag_preview->is_new_point);
        CHECK(editor.automation().drag_preview->position == gridAt(1, 1));
        CHECK_THAT(editor.automation().drag_preview->value, Catch::Matchers::WithinULP(0.65F, 1));
    }

    // The release commits one real authored point at the lane start; the derived anchor itself
    // never moved.
    editor.controller.onToneAutomationPointerUp(pointerEvent(0.0F, pointerYForValue(0.4F) - 10.0F));
    REQUIRE(editor.model().size() == 1);
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK(editor.model().front().points.front().position == gridAt(1, 1));
    CHECK_THAT(
        editor.model().front().points.front().norm_value, Catch::Matchers::WithinULP(0.65F, 1));
}

TEST_CASE(
    "EditorController authors nothing when the lane anchor is clicked", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // Press and release on the anchor without ever crossing the drag threshold. The anchor is a
    // handle, so it answers a DRAG, not a click: a bare click authors nothing at all. A point
    // that only restates the tone state's own value is still an edit the user did not ask for,
    // and the lane already says that value without it.
    editor.controller.onToneAutomationPointerDown(pointerEvent(0.0F, pointerYForValue(0.4F)));
    editor.controller.onToneAutomationPointerUp(pointerEvent(0.0F, pointerYForValue(0.4F)));
    CHECK(editor.model().empty());
    CHECK(editor.tone_automation.write_call_count == 0);

    // The click falls through to what a plain lane-area click does at that pixel (§9b): x 0 snaps
    // to the lane start, and the caret arms there on this lane.
    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->lane_index == 0);
        CHECK(editor.automation().lane_caret->position == gridAt(1, 1));
    }
}

TEST_CASE(
    "EditorController leaves the anchor alone for a press away from it", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);

    // Same column as the anchor but well away from its value: this is empty lane area, so the
    // plain click arms the caret instead of authoring — the anchor's grab is a handle, not a
    // whole-column claim.
    editor.controller.onToneAutomationPointerDown(pointerEvent(0.0F, pointerYForValue(0.95F)));
    CHECK_FALSE(editor.automation().drag_preview.has_value());
    editor.controller.onToneAutomationPointerUp(pointerEvent(0.0F, pointerYForValue(0.95F)));
    CHECK(editor.model().empty());
}

TEST_CASE(
    "EditorController Shift-locks a mouse point drag to its dominant axis",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});

    // Grab the second point (x 200, value 0.75 -> y 15) and Shift-drag it far right and down. The
    // horizontal axis dominates (60 px vs 25 px), so Shift locks the value at 0.75 while the
    // position snaps: 260 px = 2.6 s snaps to the 2.5 s quarter-note line, measure 2 beat 2.
    const ToneAutomationPointerModifiers shift{.alt = false, .shift = true};
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(260.0F, 40.0F, shift));
    editor.controller.onToneAutomationPointerUp(pointerEvent(260.0F, 40.0F, shift));

    REQUIRE(editor.model().front().points.size() == 2);
    CHECK(editor.model().front().points.back().position == gridAt(2, 2));
    CHECK_THAT(
        editor.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.75F, 0));

    // A dominantly-vertical Shift drag instead locks the position: grab the same point and drag it
    // down (5 px right, 20 px down) so the value moves while the
    // position holds at measure 2 beat 1.
    AutomationEditor vertical;
    vertical.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    vertical.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    vertical.controller.onToneAutomationPointerDrag(
        dragEvent(205.0F, pointerYForValue(0.5F), shift));
    vertical.controller.onToneAutomationPointerUp(
        pointerEvent(205.0F, pointerYForValue(0.5F), shift));
    REQUIRE(vertical.model().front().points.size() == 2);
    CHECK(vertical.model().front().points.back().position == gridAt(2, 1));
    CHECK_THAT(
        vertical.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.5F, 0));
}

TEST_CASE(
    "EditorController neighbor-clamps a mouse point drag short of its neighbor",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});

    // Grab the second point and drag it left to measure 1 beat 2 (valid), then keep dragging left
    // onto the first point's own slot (measure 1 beat 1). The neighbor clamp refuses the crossing,
    // so the preview stays at the last legal slot and the commit keeps
    // the points strictly ascending.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(50.0F, pointerYForValue(0.75F)));
    REQUIRE(editor.automation().drag_preview.has_value());
    if (editor.automation().drag_preview.has_value())
    {
        CHECK(editor.automation().drag_preview->position == gridAt(1, 2));
    }
    editor.controller.onToneAutomationPointerDrag(dragEvent(5.0F, pointerYForValue(0.75F)));
    // Still measure 1 beat 2 — the drag could not cross onto the neighbor at measure 1 beat 1.
    REQUIRE(editor.automation().drag_preview.has_value());
    if (editor.automation().drag_preview.has_value())
    {
        CHECK(editor.automation().drag_preview->position == gridAt(1, 2));
    }
    editor.controller.onToneAutomationPointerUp(pointerEvent(5.0F, pointerYForValue(0.75F)));
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK(editor.model().front().points.front().position == gridAt(1, 1));
    CHECK(editor.model().front().points.back().position == gridAt(1, 2));
}

TEST_CASE(
    "EditorController clamps a mouse point drag inside the editable window",
    "[core][tone-automation]")
{
    // A second tone change at measure 2 beat 1 (2.0 s) ends the first region there, windowing edits
    // to [0, 2] s even though the visible timeline runs to 4 s: a drag toward the far right cannot
    // leave the window.
    common::core::Song song = makeAutomationSong();
    song.arrangements.front().tones.push_back(
        common::core::Tone{.tone_document_ref = g_later_tone_ref, .name = "Dirty"});
    song.arrangements.front().tone_track.regions.push_back(
        common::core::ToneRegion{
            .id = g_later_region,
            .start = gridAt(2, 1),
            .tone_document_ref = g_later_tone_ref,
        });
    AutomationEditor editor{std::move(song)};
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.5F}});

    // Grab the sole point at measure 1 beat 1 and drag it to x 390 (3.9 s, well past the window).
    // Without the clamp the snap would land at measure 3 beat 1 (4.0 s); the window clamp keeps it
    // at the last in-window grid line, measure 2 beat 1.
    editor.controller.onToneAutomationPointerDown(pointerEvent(0.0F, pointerYForValue(0.5F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(390.0F, pointerYForValue(0.5F)));
    editor.controller.onToneAutomationPointerUp(pointerEvent(390.0F, pointerYForValue(0.5F)));
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK(editor.model().front().points.front().position == gridAt(2, 1));
}

TEST_CASE(
    "EditorController snaps a discrete mouse point drag to the nearest state",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.0F}});

    // A two-state toggle: only 0 and 1 are valid. Grab the point at value 0 (y 45) and drag up past
    // the midpoint (y 15 -> raw 0.75) — it snaps to the "on" state.
    editor.controller.onToneAutomationPointerDown(
        pointerEvent(200.0F, pointerYForValue(0.0F), {}, /*is_discrete=*/true, 2));
    editor.controller.onToneAutomationPointerDrag(
        dragEvent(200.0F, pointerYForValue(0.75F), {}, true, 2));
    editor.controller.onToneAutomationPointerUp(
        pointerEvent(200.0F, pointerYForValue(0.75F), {}, true, 2));
    REQUIRE(editor.model().front().points.size() == 1);
    CHECK_THAT(
        editor.model().front().points.front().norm_value, Catch::Matchers::WithinULP(1.0F, 0));

    // A smaller drag that stays below the midpoint (raw 0.3) snaps back to "off".
    AutomationEditor low;
    low.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.0F}});
    low.controller.onToneAutomationPointerDown(
        pointerEvent(200.0F, pointerYForValue(0.0F), {}, true, 2));
    low.controller.onToneAutomationPointerDrag(
        dragEvent(200.0F, pointerYForValue(0.3F), {}, true, 2));
    low.controller.onToneAutomationPointerUp(
        pointerEvent(200.0F, pointerYForValue(0.3F), {}, true, 2));
    REQUIRE(low.model().front().points.size() == 1);
    CHECK_THAT(low.model().front().points.front().norm_value, Catch::Matchers::WithinULP(0.0F, 0));
}

TEST_CASE(
    "EditorController selects a clicked point without moving or committing",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    const auto writes_before = editor.tone_automation.write_call_count;

    // A press on the second point that never crosses the drag threshold is a click: it selects the
    // point (arming the caret on it, paused) and commits nothing — no preview, no model change.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    CHECK_FALSE(editor.automation().drag_preview.has_value());
    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, pointerYForValue(0.75F)));

    CHECK(editor.tone_automation.write_call_count == writes_before);
    REQUIRE(editor.model().front().points.size() == 2);
    // The click armed the caret on the pressed point (measure 2 beat 1), which re-derives it as the
    // selection.
    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->position == gridAt(2, 1));
    }
    REQUIRE(editor.automation().selected_point.has_value());
    if (editor.automation().selected_point.has_value())
    {
        CHECK(editor.automation().selected_point->point_index == 1);
    }
}

TEST_CASE(
    "EditorController refuses a mouse insert onto an occupied slot", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    const auto writes_before = editor.tone_automation.write_call_count;

    // Alt-press on empty lane area whose x snaps onto the occupied measure 2 beat 1 slot (x 200,
    // but y 40 is far from the point handle at y 15, so it is an area press, not a grab). Placement
    // shares the keyboard Insert's occupied-slot refusal, so no
    // gesture arms and no duplicate lands.
    const ToneAutomationPointerModifiers alt{.alt = true, .shift = false};
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, 40.0F, alt));
    CHECK_FALSE(editor.automation().drag_preview.has_value());
    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, 40.0F, alt));

    CHECK(editor.tone_automation.write_call_count == writes_before);
    REQUIRE(editor.model().front().points.size() == 2);
}

TEST_CASE(
    "EditorController hides the insert ghost over an occupied slot", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});

    const ToneAutomationPointerModifiers alt{.alt = true, .shift = false};

    // Over the occupied measure 2 beat 1 slot (x 200) the ring is hidden: an insert there would
    // no-op, so the affordance must not advertise it (§7, and the
    // placement refusal makes it honest).
    editor.controller.onToneAutomationPointerMove(pointerEvent(200.0F, 40.0F, alt));
    CHECK_FALSE(editor.automation().insert_ghost.has_value());

    // Over the empty measure 1 beat 3 slot (x 100) the ring shows — the insert there would land.
    editor.controller.onToneAutomationPointerMove(pointerEvent(100.0F, 40.0F, alt));
    CHECK(editor.automation().insert_ghost.has_value());
}

TEST_CASE(
    "EditorController keeps a mouse point drag across a mid-drag state rebuild",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});

    // Grab the second point and drag it toward the 0.5 line.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(200.0F, pointerYForValue(0.5F)));
    REQUIRE(editor.automation().drag_preview.has_value());

    // A fresh state rebuild lands mid-drag (the engine pushes state in bursts). Because the
    // controller owns the drag, the rebuild republishes the preview instead of resetting it: the
    // gesture survives and the release still commits its edit.
    editor.controller.onTimelineZoomChanged(120.0);
    REQUIRE(editor.automation().drag_preview.has_value());
    if (editor.automation().drag_preview.has_value())
    {
        CHECK_THAT(editor.automation().drag_preview->value, Catch::Matchers::WithinULP(0.5F, 0));
    }

    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, pointerYForValue(0.5F)));
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK_THAT(
        editor.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.5F, 0));
}

TEST_CASE(
    "EditorController commits a mouse point drag as one undoable edit", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});

    // Drag the second point down to the 0.5 line and release: one commit.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(200.0F, pointerYForValue(0.5F)));
    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, pointerYForValue(0.5F)));
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK_THAT(
        editor.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.5F, 0));

    // A single undo restores the pre-drag value in one step (the drag is one undo entry).
    editor.controller.onUndoRequested();
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK_THAT(
        editor.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.75F, 0));
}

TEST_CASE(
    "EditorController cancels a mouse point drag on Escape without committing",
    "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    const auto writes_before = editor.tone_automation.write_call_count;

    // Drag the second point somewhere else, then Escape: the preview drops and the release after it
    // commits nothing, so the point keeps its authored value.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, pointerYForValue(0.75F)));
    editor.controller.onToneAutomationPointerDrag(dragEvent(200.0F, pointerYForValue(0.5F)));
    REQUIRE(editor.automation().drag_preview.has_value());
    editor.controller.onChartEscapePressed();
    CHECK_FALSE(editor.automation().drag_preview.has_value());
    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, pointerYForValue(0.5F)));

    CHECK(editor.tone_automation.write_call_count == writes_before);
    REQUIRE(editor.model().front().points.size() == 2);
    CHECK_THAT(
        editor.model().front().points.back().norm_value, Catch::Matchers::WithinULP(0.75F, 0));
}

TEST_CASE(
    "EditorController arms the lane caret on a plain empty-area press", "[core][tone-automation]")
{
    AutomationEditor editor;
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    // A plain (no-Alt) press on empty lane area seeks and arms the lane caret at the snapped slot —
    // no drag, no ghost. x 200 -> 2.0 s -> measure 2 beat 1.
    editor.controller.onToneAutomationPointerDown(pointerEvent(200.0F, 30.0F));
    CHECK_FALSE(editor.automation().drag_preview.has_value());
    editor.controller.onToneAutomationPointerUp(pointerEvent(200.0F, 30.0F));

    REQUIRE(editor.automation().lane_caret.has_value());
    if (editor.automation().lane_caret.has_value())
    {
        CHECK(editor.automation().lane_caret->lane_index == 0);
        CHECK(editor.automation().lane_caret->position == gridAt(2, 1));
    }
}

// The focus rows below the strings (docs/plans/completed/keyboard-focus-rows.md): Down from
// string 1 selects the tone region holding the cursor, then arms the first lane, then selects the
// "+" row, and Up retraces them. The caret is armed only on the string and the lane; the tone and
// "+" rows are selections. Ctrl's reach jumps a whole group at a time, landing on its nearest row.
TEST_CASE("EditorController walks the focus rows below the strings", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    const auto step = [&editor](ChartStepDirection direction, bool reach) {
        editor.controller.onChartCaretStepRequested(direction, reach);
    };
    // The fixture leaves the region selected; a horizontal press from it arms in place on the
    // remembered string.
    step(ChartStepDirection::Right, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 1);
    const auto on_string = [&state](int string) {
        const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
        return caret != nullptr && caret->string == string &&
               !state->tone_automation.lane_caret.has_value();
    };
    const auto on_tone_row = [&state] {
        return caretOrNull(state->chart_edit) == nullptr &&
               !state->tone_automation.lane_caret.has_value() &&
               state->tone_track.regions.front().selected;
    };
    const auto on_lane = [&state] {
        const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(state->tone_automation);
        return caret != nullptr && caret->lane_index == 0 &&
               caretOrNull(state->chart_edit) == nullptr;
    };
    const auto on_add_row = [&state] {
        return state->tone_automation.add_lane_row_selected &&
               !state->tone_automation.lane_caret.has_value() &&
               caretOrNull(state->chart_edit) == nullptr;
    };
    CHECK(on_string(1));
    CHECK_FALSE(state->tone_track.regions.front().selected);

    step(ChartStepDirection::Down, false);
    CHECK(on_tone_row());
    step(ChartStepDirection::Down, false);
    CHECK(on_lane());
    CHECK_FALSE(state->tone_track.regions.front().selected);
    step(ChartStepDirection::Down, false);
    CHECK(on_add_row());
    // The "+" row is the bottom of the stack: a further Down is inert.
    step(ChartStepDirection::Down, false);
    CHECK(on_add_row());

    step(ChartStepDirection::Up, false);
    CHECK(on_lane());
    CHECK_FALSE(state->tone_automation.add_lane_row_selected);
    step(ChartStepDirection::Up, false);
    CHECK(on_tone_row());
    step(ChartStepDirection::Up, false);
    CHECK(on_string(1));

    // Reach: string -> tone row -> lanes -> "+" row, and back up group by group.
    step(ChartStepDirection::Down, true);
    CHECK(on_tone_row());
    step(ChartStepDirection::Down, true);
    CHECK(on_lane());
    step(ChartStepDirection::Down, true);
    CHECK(on_add_row());
    step(ChartStepDirection::Up, true);
    CHECK(on_lane());
    step(ChartStepDirection::Up, true);
    CHECK(on_tone_row());
    step(ChartStepDirection::Up, true);
    CHECK(on_string(1));

    // Reach skips the strings still below: from string 3 it lands straight on the tone row.
    step(ChartStepDirection::Up, false);
    step(ChartStepDirection::Up, false);
    CHECK(on_string(3));
    step(ChartStepDirection::Down, true);
    CHECK(on_tone_row());
}

// The "+" row is reached by selection like a marker row, but it names no document object: Enter
// opens the parameter picker, and Ctrl+R has nothing to rename. Both verbs are published by the
// core, so the view dispatches on no kind of its own.
TEST_CASE("EditorController publishes the add-lane row's restate verb", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    // String, tone row, lane, "+" row: the walk's own route down the stack.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    for (int press = 0; press < 3; ++press)
    {
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    }

    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_automation.add_lane_row_selected);
    CHECK(std::holds_alternative<OpenAutomationPickerTarget>(state->restate_target));
    CHECK(std::holds_alternative<std::monostate>(state->rename_target));
}

// A horizontal press or a jump from a marker row returns to the row the caret was reached from, a
// lane included, because the passive marker remembers the lane as well as the string. A lane that
// has since left the visible set falls back onto the remembered string — string 3 here, so the
// fallback cannot pass by landing on the default string.
TEST_CASE(
    "EditorController returns an arrow from a marker row to the remembered lane",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, true);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(laneCaretOrNull(editor.automation()) != nullptr);

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    REQUIRE(laneCaretOrNull(editor.automation()) == nullptr);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ToneAutomationLaneCaretRef* const returned = laneCaretOrNull(editor.automation());
    REQUIRE(returned != nullptr);
    CHECK(returned->lane_index == 0);

    // A jump from the tone row keeps the remembered lane too.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    editor.controller.onChartCaretJumpRequested(ChartCaretJump::ChartStart);
    const ToneAutomationLaneCaretRef* const jumped = laneCaretOrNull(editor.automation());
    REQUIRE(jumped != nullptr);
    CHECK(jumped->position == gridAt(1, 1));

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(editor.automation().add_lane_row_selected);
    editor.controller.onToneAutomationLaneRemoveRequested(g_instance, g_param);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    CHECK(laneCaretOrNull(state->tone_automation) == nullptr);
    const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 3);
}

// A region selected with the pointer while the cursor stands in another tone does not decide which
// lanes an arrow can return to: the arming replaces that selection, so the lanes that count are the
// ones under the cursor. A remembered lane the cursor's tone lacks falls back onto its string, and
// the rig follows the cursor's tone rather than staying on the selected region's.
TEST_CASE(
    "EditorController returns an arrow past a pointer-selected region to the cursor's rows",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(laneCaretOrNull(editor.automation()) != nullptr);

    // Park the cursor in the later tone, then select the opening region with the pointer, which
    // shows its lanes again while the cursor stays put.
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{3.0});
    editor.controller.onToneRegionSelected(g_region);
    REQUIRE(editor.automation().lanes.size() == 1);

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    CHECK(laneCaretOrNull(state->tone_automation) == nullptr);
    const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 1);
    CHECK(caret->seconds == Catch::Approx(3.0));
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
}

// A selected region is the active tone, so selecting any other marker hands the active tone back to
// the cursor, and the rig follows: here a section chip replaces a region selected away from the
// cursor, and the rig returns to the cursor's tone.
TEST_CASE(
    "EditorController hands the rig back to the cursor's tone on a marker selection",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    REQUIRE(editor.transport.position().seconds < 2.0);
    editor.controller.onToneRegionSelected(g_later_region);
    REQUIRE(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});

    editor.controller.onSongSectionSelected(gridAt(1, 1));
    CHECK(
        editor.live_rig.last_audible_tone_ref ==
        std::optional<std::string>{std::string{g_tone_document_ref}});
}

// Tab steps a lane caret from point to point with the grid ignored; a lane has no keyframes, so
// Ctrl+Tab steps it the same way. Past the last point the press is inert.
TEST_CASE("EditorController steps a lane caret to the adjacent point", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.seedPoints(
        {common::core::ToneAutomationPoint{.position = pointAt(1, 1), .norm_value = 0.25F},
         common::core::ToneAutomationPoint{.position = pointAt(1, 3), .norm_value = 0.5F},
         common::core::ToneAutomationPoint{.position = pointAt(2, 1), .norm_value = 0.75F}});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(laneCaretOrNull(editor.automation()) != nullptr);

    const auto lane_position = [&editor]() -> std::optional<common::core::GridPosition> {
        const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(editor.automation());
        return caret != nullptr ? std::optional{caret->position} : std::nullopt;
    };
    CHECK(lane_position() == std::optional{gridAt(1, 1)});
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(lane_position() == std::optional{gridAt(1, 3)});
    editor.controller.onRowObjectStepRequested(true, true);
    CHECK(lane_position() == std::optional{gridAt(2, 1)});
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(lane_position() == std::optional{gridAt(2, 1)});
    editor.controller.onRowObjectStepRequested(false, false);
    CHECK(lane_position() == std::optional{gridAt(1, 3)});
}

// With several lanes, reach lands on the NEAREST row of the group it enters: Ctrl+Down from the
// tone row arms the first lane, Ctrl+Up from the "+" row arms the last, and a plain step walks lane
// by lane.
TEST_CASE("EditorController reaches the nearest lane of the lane group", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    common::audio::AutomatableParamInfo presence = makeParam();
    presence.param_id = "presence";
    presence.name = "Presence";
    editor.tone_automation.parameters.push_back(std::move(presence));
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    editor.controller.onToneAutomationLaneAddRequested(g_instance, "presence");
    REQUIRE(editor.automation().lanes.size() == 2);

    const auto lane_index = [&editor]() -> std::optional<std::size_t> {
        const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(editor.automation());
        return caret != nullptr ? std::optional<std::size_t>{caret->lane_index} : std::nullopt;
    };
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(lane_index() == std::optional<std::size_t>{0});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(lane_index() == std::optional<std::size_t>{1});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(editor.automation().add_lane_row_selected);

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, true);
    CHECK(lane_index() == std::optional<std::size_t>{1});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, true);
    CHECK_FALSE(lane_index().has_value());
    CHECK(editor.automation().lanes.size() == 2);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, true);
    CHECK(lane_index() == std::optional<std::size_t>{0});
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, true);
    CHECK(editor.automation().add_lane_row_selected);
}

// The "+" row names no document object: nothing is published as a deletable selection, Delete and
// Alt+arrows do nothing to it, Esc releases it, and a cursor move releases it like every other
// selection that follows the cursor.
TEST_CASE("EditorController keeps the plus row inert and cursor-coupled", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    const auto walk_to_plus_row = [&editor] {
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    };
    walk_to_plus_row();
    REQUIRE(editor.automation().add_lane_row_selected);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->selection_present);

    editor.controller.onSelectionDeleteRequested();
    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);
    CHECK(editor.automation().add_lane_row_selected);
    CHECK(editor.model().empty());
    CHECK(state->tone_track.regions.size() == 1);

    // The "+" row holds no objects, so Tab has nowhere to step.
    editor.controller.onRowObjectStepRequested(true, false);
    CHECK(editor.automation().add_lane_row_selected);

    editor.controller.onChartEscapePressed();
    CHECK_FALSE(editor.automation().add_lane_row_selected);

    walk_to_plus_row();
    REQUIRE(editor.automation().add_lane_row_selected);
    editor.controller.onTimelineSeekRequested(common::core::TimePosition{1.0});
    CHECK_FALSE(editor.automation().add_lane_row_selected);
}

// Enter on the "+" row opens the parameter picker; choosing a parameter there opens its lane AND
// arms the caret on it, so the keyboard continues on the lane it just made. A lane opened while
// focus stands anywhere else moves nothing.
TEST_CASE(
    "EditorController arms the caret on a lane opened from the plus row", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    CHECK(laneCaretOrNull(editor.automation()) == nullptr);
    editor.controller.onToneAutomationLaneRemoveRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.empty());

    // With no lane, Down from the tone row lands straight on the "+" row.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(editor.automation().add_lane_row_selected);

    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);
    const ToneAutomationLaneCaretRef* const caret = laneCaretOrNull(editor.automation());
    REQUIRE(caret != nullptr);
    CHECK(caret->lane_index == 0);
    CHECK_FALSE(editor.automation().add_lane_row_selected);
}

// Ctrl+Shift+T and Ctrl+Shift+A land on the tone row and the "+" row through the walk's own
// landing, so they reach them from a string caret and from a lane caret alike: the caret
// dissolves in place and the row's holder at the cursor is selected.
TEST_CASE("EditorController jumps onto the tone and plus rows", "[core][tone-automation]")
{
    AutomationEditor editor{makeChartedAutomationSong()};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);

    const auto region_selected = [&editor] {
        const EditorViewState* const state = stateOrNull(editor.view.last_state);
        REQUIRE(state != nullptr);
        if (state == nullptr)
        {
            throw std::logic_error("editor pushed no view state");
        }
        REQUIRE(state->tone_track.regions.size() == 1);
        return state->tone_track.regions.front().selected;
    };
    const auto caret_string = [&editor]() -> std::optional<int> {
        const EditorViewState* const state = stateOrNull(editor.view.last_state);
        REQUIRE(state != nullptr);
        if (state == nullptr)
        {
            throw std::logic_error("editor pushed no view state");
        }
        const ChartCaretViewState* const armed = caretOrNull(state->chart_edit);
        return armed != nullptr ? std::optional{armed->string} : std::nullopt;
    };

    // From a string caret.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    REQUIRE(caret_string() == std::optional{1});
    editor.controller.onFocusRowJumpRequested(FocusRowJump::Tone);
    CHECK(region_selected());
    CHECK_FALSE(caret_string().has_value());

    editor.controller.onFocusRowJumpRequested(FocusRowJump::AddAutomationLane);
    CHECK(editor.automation().add_lane_row_selected);
    CHECK_FALSE(region_selected());

    // From a lane caret: arm on a string, walk down past the tone row onto the lane, then jump.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    REQUIRE(laneCaretOrNull(editor.automation()) != nullptr);

    editor.controller.onFocusRowJumpRequested(FocusRowJump::Tone);
    CHECK(region_selected());
    CHECK(laneCaretOrNull(editor.automation()) == nullptr);

    editor.controller.onFocusRowJumpRequested(FocusRowJump::AddAutomationLane);
    CHECK(editor.automation().add_lane_row_selected);
}

// The column rule runs before the stack is listed, on the jump exactly as on the walk: a region
// selected far from the cursor brings the cursor inside itself first, so the "+" row the jump lands
// on belongs to THAT region's tone — its lanes are the ones shown and the rig follows. Without it
// the landing would show the cursor's tone's lanes under another tone's region.
TEST_CASE(
    "EditorController jumps to the plus row of a pointer-selected region",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.transport.position().seconds < 2.0);

    editor.controller.onToneRegionSelected(g_later_region);
    editor.controller.onFocusRowJumpRequested(FocusRowJump::AddAutomationLane);
    CHECK(editor.transport.position().seconds == Catch::Approx(2.0));
    CHECK(editor.automation().add_lane_row_selected);
    CHECK(editor.automation().lanes.empty());
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
}

// The "+" row's silence case is structurally unreachable, and this pins why: the load's tone
// baseline mints a catalog tone and materializes a whole-song region for every arrangement, and the
// first region owns the lead-in, so a loaded chart always has an active tone and the "+" row is
// always listed. The jump therefore lands even on a song authored with no regions at all. The
// silence rule itself — stack membership — is pinned where it IS reachable, on the section row
// (test_editor_controller_marker_rows.cpp).
TEST_CASE("EditorController always has a plus row to jump onto", "[core][tone-automation]")
{
    common::core::Song song = makeChartedAutomationSong();
    song.arrangements.front().tone_track.regions.clear();
    AutomationEditor editor{std::move(song)};

    const EditorViewState* const loaded = stateOrNull(editor.view.last_state);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->tone_track.regions.size() == 1);

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onFocusRowJumpRequested(FocusRowJump::AddAutomationLane);
    CHECK(editor.automation().add_lane_row_selected);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    if (state != nullptr)
    {
        CHECK(caretOrNull(state->chart_edit) == nullptr);
    }
}

// A region selected with the pointer seeks nothing, so the cursor may stand in another region.
// Stepping off the selected region brings the cursor inside it first, so the rows below are the
// ones that region owns: here the later region's tone has no lanes, so Down reaches its "+" row,
// and the rig follows the cursor into the later tone.
TEST_CASE(
    "EditorController steps off a pointer-selected region from inside it",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    editor.controller.onToneAutomationLaneAddRequested(g_instance, g_param);
    REQUIRE(editor.automation().lanes.size() == 1);
    REQUIRE(editor.transport.position().seconds < 2.0);

    editor.controller.onToneRegionSelected(g_later_region);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Down, false);
    CHECK(editor.transport.position().seconds == Catch::Approx(2.0));
    CHECK(editor.automation().lanes.empty());
    // The cursor moved onto the "+" row, so the keyboard position moved with it; the row names no
    // object a verb could act on.
    const EditorViewState* const moved = stateOrNull(editor.view.last_state);
    REQUIRE(moved != nullptr);
    const std::optional<KeyboardPositionViewState>& position = moved->keyboard_position;
    REQUIRE(position.has_value());
    if (position.has_value())
    {
        CHECK(position->seconds == Catch::Approx(2.0));
    }
    CHECK_FALSE(moved->selection_start_seconds.has_value());
    CHECK(editor.automation().add_lane_row_selected);
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});

    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK(state->tone_track.regions[1].selected);
}

// A caret that walks into another tone's region takes the rig with it: the audible tone follows
// where the keyboard stands, and arming never moves the playhead. Esc then dissolves the caret onto
// the position it already reached, so the tone it switched to stays.
TEST_CASE(
    "EditorController points the rig at the tone a caret walks into", "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};

    // Arm at the cursor in the first region, then jump a measure into the later one: the caret
    // moves, the transport does not, and the rig follows the caret.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, true);
    REQUIRE(editor.transport.position().seconds < 2.0);
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});

    editor.controller.onChartEscapePressed();
    CHECK(editor.transport.position().seconds == Catch::Approx(2.0));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    CHECK(caretOrNull(state->chart_edit) == nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].selected);
    CHECK_FALSE(state->tone_track.regions[1].selected);
}

// The arrow walk across a region boundary is the plain case of the rule above: the tone the rig
// plays and the tone row's highlight both move onto the region the caret entered, while the
// playhead stays exactly where it was.
TEST_CASE(
    "EditorController follows the caret across a tone boundary with the arrows",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    const double parked_seconds = editor.transport.position().seconds;
    REQUIRE(parked_seconds < 2.0);

    // Five presses from the passive marker: the first arms in place on the first beat of measure 1
    // without stepping, and the four quarter-note steps after it reach the first beat of measure 2,
    // which is where the later region begins.
    for (int press = 0; press < 5; ++press)
    {
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }

    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].active);
    CHECK(state->tone_track.regions[1].active);

    // And back: the walk is symmetric, so the rig returns to the opening tone with the caret.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Left, false);
    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_tone_document_ref});
    const EditorViewState* const back = stateOrNull(editor.view.last_state);
    REQUIRE(back != nullptr);
    REQUIRE(back->tone_track.regions.size() == 2);
    CHECK(back->tone_track.regions[0].active);
    CHECK_FALSE(back->tone_track.regions[1].active);
}

// The big jumps carry the tone exactly as a step does — the rule is the caret's position, not how
// it got there.
TEST_CASE(
    "EditorController follows the caret across a tone boundary on a chart-end jump",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    const double parked_seconds = editor.transport.position().seconds;
    REQUIRE(parked_seconds < 2.0);

    editor.controller.onChartCaretJumpRequested(ChartCaretJump::ChartEnd);
    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});

    editor.controller.onChartCaretJumpRequested(ChartCaretJump::ChartStart);
    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_tone_document_ref});
}

// A step that stays inside the region the caret already stood in names the same tone again. The
// derivation still runs and still hands the rig an answer — it is idempotent by design rather than
// gated — so what the step must not do is CHANGE the tone.
TEST_CASE(
    "EditorController leaves the audible tone alone stepping within a region",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    REQUIRE(
        editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_tone_document_ref});

    // The first press arms in place and the two steps after it stay inside measure 1, which the
    // opening region owns whole.
    for (int press = 0; press < 3; ++press)
    {
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }

    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_tone_document_ref});
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK(state->tone_track.regions[0].active);
    CHECK_FALSE(state->tone_track.regions[1].active);
}

// A pointer arm is the same arming funnel, so a click in the tab lane inside another region moves
// the audible tone the way an arrow step does — and still seeks nothing.
TEST_CASE(
    "EditorController follows a pointer-armed caret into another tone's region",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    const double parked_seconds = editor.transport.position().seconds;
    REQUIRE(parked_seconds < 2.0);

    // 2.5 s is the second beat of measure 2, a quarter note inside the later region.
    editor.controller.onChartPointerDown(chartPressAt(2.5, 1));
    editor.controller.onChartPointerUp(chartPressAt(2.5, 1));
    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].active);
    CHECK(state->tone_track.regions[1].active);
}

// A caret that rides a NUDGED note across the boundary carries the rig too: the caret is the
// keyboard position however it got there, so the one writer re-derives the tone for a caret moved
// by an edit exactly as for one moved by an arrow. The nudge authors, and still seeks nothing.
TEST_CASE(
    "EditorController follows a caret riding a nudged note into another tone's region",
    "[core][tone-automation]")
{
    common::core::Song song = makeTwoToneChartedSong(gridAt(2, 1));
    std::optional<common::core::Chart>& charted = song.arrangements.front().chart;
    REQUIRE(charted.has_value());
    if (charted.has_value())
    {
        // One quarter note short of the boundary, so a single Alt+Right step lands the note on the
        // later region's first beat.
        charted->notes.push_back(makeTestNote(gridAt(1, 4), 1, 5));
    }
    AutomationEditor editor{std::move(song)};
    const double parked_seconds = editor.transport.position().seconds;
    REQUIRE(parked_seconds < 2.0);

    // Four presses from the passive marker: the first arms in place on the first beat of measure 1
    // and the three steps after it reach the note, which the arming selects.
    for (int press = 0; press < 4; ++press)
    {
        editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    }
    REQUIRE(
        editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_tone_document_ref});

    editor.controller.onSelectionMoveRequested(ChartStepDirection::Right);

    const common::core::Arrangement* const arrangement =
        editor.controller.session().currentArrangement();
    REQUIRE(arrangement != nullptr);
    REQUIRE(arrangement->chart.has_value());
    if (arrangement->chart.has_value())
    {
        REQUIRE(arrangement->chart->notes.size() == 1);
        CHECK(arrangement->chart->notes.front().position == gridAt(2, 1));
    }
    CHECK(editor.transport.position().seconds == Catch::Approx(parked_seconds));
    CHECK(editor.live_rig.last_audible_tone_ref == std::optional<std::string>{g_later_tone_ref});
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->tone_track.regions.size() == 2);
    CHECK_FALSE(state->tone_track.regions[0].active);
    CHECK(state->tone_track.regions[1].active);
}

// Stepping off a pointer-selected region moves the cursor to that region's exact start, and the
// arming that follows keeps that exact position even between grid lines: a region starting half a
// beat into measure 2 arms string 1 at 2.25 s, not on the quarter grid's 2.0 s or 2.5 s.
TEST_CASE(
    "EditorController arms at a region's exact start after stepping off it",
    "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(pointAt(2, 1, 1))};
    REQUIRE(editor.transport.position().seconds < 2.0);

    editor.controller.onToneRegionSelected(g_later_region);
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Up, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    const ChartCaretViewState* const caret = caretOrNull(state->chart_edit);
    REQUIRE(caret != nullptr);
    CHECK(caret->string == 1);
    CHECK(caret->seconds == Catch::Approx(2.25));
}

// An insert leaves what it made selected, as a typed note does — a split onto an existing tone and
// a split that mints one alike — so Enter, Delete and Alt+arrows act on the new region next. The
// caret it was typed from demotes in place, and an arrow re-arms it where it stood. The fixture's
// opening region is selected, so the first arrow press is what arms the caret.
TEST_CASE("EditorController selects the region a tone insert made", "[core][tone-automation]")
{
    AutomationEditor editor{makeTwoToneChartedSong(gridAt(2, 1))};
    editor.live_rig.next_mint_ref = "tones/3c4d5e6f-7a8b-4c9d-8e0f-1a2b3c4d5e6f/tone.json";
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const EditorViewState* const state = stateOrNull(editor.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(caretOrNull(state->chart_edit) != nullptr);

    // A split of the later region onto the opening region's tone.
    editor.controller.onToneRegionCreateRequested(
        gridAt(2, 3), "7c8d9e0f-1a2b-4c3d-9e4f-5a6b7c8d9e0f", std::string{g_tone_document_ref});
    REQUIRE(state->tone_track.regions.size() == 3);
    CHECK(caretOrNull(state->chart_edit) == nullptr);
    CHECK(state->tone_track.regions[2].selected);

    // A split that mints its tone, from a caret re-armed where the first one stood.
    editor.controller.onChartCaretStepRequested(ChartStepDirection::Right, false);
    const ChartCaretViewState* const rearmed = caretOrNull(state->chart_edit);
    REQUIRE(rearmed != nullptr);
    CHECK(rearmed->seconds == Catch::Approx(0.0));
    editor.controller.onToneCreateNewRequested(gridAt(1, 3), "Crunch");
    REQUIRE(state->tone_track.regions.size() == 4);
    CHECK(caretOrNull(state->chart_edit) == nullptr);
    CHECK(state->tone_track.regions[1].selected);
    CHECK_FALSE(state->tone_track.regions[3].selected);
}

} // namespace rock_hero::editor::core
