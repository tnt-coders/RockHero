#include "tone/tone_automation_lanes_view.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Records automation intents emitted by the lanes view.
struct RecordingLanesListener final : public ToneAutomationLanesView::Listener
{
    void onToneAutomationLaneAddRequested(std::string instance_id, std::string param_id) override
    {
        last_add_instance_id = std::move(instance_id);
        last_add_param_id = std::move(param_id);
        add_count += 1;
    }

    void onToneAutomationPointsEditRequested(
        std::string instance_id, std::string param_id,
        std::vector<common::core::ToneAutomationPoint> points) override
    {
        last_edit_instance_id = std::move(instance_id);
        last_edit_param_id = std::move(param_id);
        last_edit_points = std::move(points);
        edit_count += 1;
    }

    std::string last_add_instance_id;
    std::string last_add_param_id;
    int add_count = 0;
    std::string last_edit_instance_id;
    std::string last_edit_param_id;
    std::vector<common::core::ToneAutomationPoint> last_edit_points;
    int edit_count = 0;
};

[[nodiscard]] core::ToneAutomationViewState makeState()
{
    core::ToneAutomationViewState state;
    state.tone_document_ref = "tones/x/tone.json";
    core::ToneAutomationLaneViewState resolved_lane;
    resolved_lane.instance_id = "instance-a";
    resolved_lane.param_id = "gain";
    resolved_lane.name = "Gain";
    resolved_lane.resolved = true;
    resolved_lane.points = {
        core::ToneAutomationPointViewState{
            .position = {.measure = 1, .beat = 1, .offset = {}},
            .seconds = 0.0,
            .norm_value = 0.25F,
            .curve_shape = 0.0F,
        },
        core::ToneAutomationPointViewState{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .seconds = 2.0,
            .norm_value = 0.75F,
            .curve_shape = 0.0F,
        },
    };
    core::ToneAutomationLaneViewState unresolved_lane;
    unresolved_lane.instance_id = "instance-b";
    unresolved_lane.param_id = "mix";
    unresolved_lane.name = "Mix";
    unresolved_lane.resolved = false;
    state.lanes = {std::move(resolved_lane), std::move(unresolved_lane)};
    state.available_parameters = {
        core::ToneAutomationParamChoice{
            .instance_id = "instance-a",
            .param_id = "tone",
            .name = "Tone",
            .group = {},
        },
    };
    return state;
}

// Owns the JUCE runtime the component needs for fonts and cursors in headless tests.
struct LanesHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
    RecordingLanesListener listener;
    ToneAutomationLanesView view{listener, tempo_map};

    LanesHarness()
    {
        view.setSize(800, 200);
        view.setVisibleTimeline(
            common::core::TimeRange{
                .start = common::core::TimePosition{0.0},
                .end = common::core::TimePosition{8.0},
            });
        view.setEditableWindow(
            common::core::TimeRange{
                .start = common::core::TimePosition{0.0},
                .end = common::core::TimePosition{4.0},
            });
        view.setState(makeState());
    }
};

} // namespace

TEST_CASE("Lanes view reports lane heights plus the plus lane", "[ui][tone-automation-lanes]")
{
    const LanesHarness harness;
    // Two default-height lanes (56 px) plus the 26 px "+" lane.
    CHECK(harness.view.totalHeight() == (2 * 56) + 26);
}

TEST_CASE("Lanes view has zero height with no selected tone", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    const ToneAutomationLanesView empty{harness.listener, harness.tempo_map};
    CHECK(empty.totalHeight() == 0);
    CHECK_FALSE(empty.wantsPointerAt({10, 10}));
}

TEST_CASE("Lanes view claims editable zones and rejects inert ones", "[ui][tone-automation-lanes]")
{
    const LanesHarness harness;

    // Inside the first (resolved) lane and inside the 0..4 s window (x=100 of 800 = 1 s).
    CHECK(harness.view.wantsPointerAt({100, 20}));
    // Same lane but outside the editable window (x=500 = 5 s): seek stays with the overlay.
    CHECK_FALSE(harness.view.wantsPointerAt({500, 20}));
    // The unresolved second lane is inert everywhere.
    CHECK_FALSE(harness.view.wantsPointerAt({100, 56 + 20}));
    // The "+" chip at the pinned left edge of the trailing lane claims the pointer.
    CHECK(harness.view.wantsPointerAt({10, (2 * 56) + 12}));
    // The rest of the "+" strip does not.
    CHECK_FALSE(harness.view.wantsPointerAt({400, (2 * 56) + 12}));
}

TEST_CASE("Lanes view paints headlessly", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    const juce::Image image{juce::Image::ARGB, 800, 200, true, juce::SoftwareImageType{}};
    juce::Graphics graphics{image};
    harness.view.paint(graphics);
    SUCCEED("paint completed without assertions");
}

} // namespace rock_hero::editor::ui
