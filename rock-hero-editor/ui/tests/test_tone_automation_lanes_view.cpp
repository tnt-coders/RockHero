#include "tone/tone_automation_lanes_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <expected>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <span>
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

    void onToneAutomationLaneRemoveRequested(std::string instance_id, std::string param_id) override
    {
        last_remove_instance_id = std::move(instance_id);
        last_remove_param_id = std::move(param_id);
        remove_count += 1;
    }

    void onToneAutomationPointSelectRequested(
        std::string instance_id, std::string param_id, common::core::GridPosition position) override
    {
        last_select_instance_id = std::move(instance_id);
        last_select_param_id = std::move(param_id);
        last_select_position = position;
        select_count += 1;
    }

    std::string last_add_instance_id;
    std::string last_add_param_id;
    int add_count = 0;
    std::string last_edit_instance_id;
    std::string last_edit_param_id;
    std::vector<common::core::ToneAutomationPoint> last_edit_points;
    int edit_count = 0;
    std::string last_remove_instance_id;
    std::string last_remove_param_id;
    int remove_count = 0;
    std::string last_select_instance_id;
    std::string last_select_param_id;
    common::core::GridPosition last_select_position{};
    int select_count = 0;
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
    resolved_lane.default_norm_value = 0.5F;
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
            .plugin_name = {},
        },
    };
    return state;
}

// Tone-automation port whose queries all resolve to empty; the geometry tests never read it.
struct StubToneAutomation final : public common::audio::IToneAutomation
{
    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomatableParamInfo>, common::audio::ToneAutomationError>
    listAutomatableParameters(const std::string&) const override
    {
        return std::vector<common::audio::AutomatableParamInfo>{};
    }

    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomationCurvePoint>, common::audio::ToneAutomationError>
    readParameterCurve(const std::string&, const std::string&, const std::string&) const override
    {
        return std::vector<common::audio::AutomationCurvePoint>{};
    }

    [[nodiscard]] std::expected<void, common::audio::ToneAutomationError> writeParameterCurve(
        const std::string&, const std::string&, const std::string&,
        std::span<const common::audio::AutomationCurvePoint>) override
    {
        return {};
    }

    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> readParameterNormValue(
        const std::string&, const std::string&, const std::string&) const override
    {
        return 0.0F;
    }

    // Formats the value as a stable "[0.NN]" token so readout tests can assert the exact value the
    // gesture is producing, independent of any real plugin's units.
    [[nodiscard]] std::expected<std::string, common::audio::ToneAutomationError>
    formatParameterValue(
        const std::string&, const std::string&, const std::string&, float norm_value) const override
    {
        return ("[" + juce::String(norm_value, 2) + "]").toStdString();
    }

    // Parses the numeric body of the "[0.NN]" token (or any plain number) back to a norm value.
    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> parseParameterValue(
        const std::string&, const std::string&, const std::string&,
        const std::string& text) const override
    {
        return juce::String{text}.retainCharacters("0123456789.-").getFloatValue();
    }
};

// Left-button modifiers with Alt held: the insert quasimode's click.
const juce::ModifierKeys g_alt_click{
    juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier
};

// Owns the JUCE runtime the component needs for fonts and cursors in headless tests.
struct LanesHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    common::core::TempoMap tempo_map =
        common::core::TempoMap::defaultMap(common::core::TimeDuration{8.0});
    RecordingLanesListener listener;
    StubToneAutomation tone_automation;
    ToneAutomationLanesView view{listener, tempo_map, tone_automation};

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
    const ToneAutomationLanesView empty{
        harness.listener, harness.tempo_map, harness.tone_automation
    };
    CHECK(empty.totalHeight() == 0);
    CHECK_FALSE(empty.wantsPointerAt({10, 10}));
}

TEST_CASE(
    "Lanes view keeps the plus chip hittable with nothing to offer", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    ToneAutomationLanesView view{harness.listener, harness.tempo_map, harness.tone_automation};
    view.setSize(800, 200);

    // A selected tone with no lanes and no listable parameters (empty tone, or listing failure)
    // must still surface the chip so the picker can explain the empty state.
    core::ToneAutomationViewState lane_free;
    lane_free.tone_document_ref = "tones/x/tone.json";
    view.setState(lane_free);

    CHECK(view.totalHeight() == 26);
    CHECK(view.wantsPointerAt({10, 12}));
    CHECK_FALSE(view.wantsPointerAt({400, 12}));
}

TEST_CASE(
    "Lanes view announces height changes for lane-free tone selection",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    ToneAutomationLanesView view{harness.listener, harness.tempo_map, harness.tone_automation};
    int heights_changed_count = 0;
    view.setHeightsChangedCallback([&heights_changed_count] { heights_changed_count += 1; });

    // Selecting a tone with no lanes yet still grows the view from nothing to the "+" lane; the
    // viewport only re-lays rows out when this callback fires, so silence here hides the picker.
    core::ToneAutomationViewState lane_free;
    lane_free.tone_document_ref = "tones/x/tone.json";
    view.setState(lane_free);
    CHECK(heights_changed_count == 1);
    CHECK(view.totalHeight() > 0);

    // Same lane count both sides, so only the height comparison reports the deselection too.
    view.setState(core::ToneAutomationViewState{});
    CHECK(heights_changed_count == 2);
    CHECK(view.totalHeight() == 0);
}

TEST_CASE("Lanes view claims editable zones and rejects inert ones", "[ui][tone-automation-lanes]")
{
    const LanesHarness harness;

    // Empty editable lane area passes through to the seek overlay: a plain click never inserts
    // (the insert quasimode needs Alt, which wantsPointerAt reads from the live modifier state).
    CHECK_FALSE(harness.view.wantsPointerAt({100, 30}));
    // Same lane but outside the editable window (x=500 = 5 s): seek stays with the overlay.
    CHECK_FALSE(harness.view.wantsPointerAt({500, 30}));
    // The lane name chip is the lane handle and always claims the pointer — on the unresolved
    // second lane too, so its lane menu stays reachable.
    CHECK(harness.view.wantsPointerAt({10, 10}));
    CHECK(harness.view.wantsPointerAt({10, 56 + 10}));
    // Away from its chip, the unresolved second lane is inert.
    CHECK_FALSE(harness.view.wantsPointerAt({100, 56 + 30}));
    // The "+" chip at the pinned left edge of the trailing lane claims the pointer.
    CHECK(harness.view.wantsPointerAt({10, (2 * 56) + 12}));
    // The rest of the "+" strip does not.
    CHECK_FALSE(harness.view.wantsPointerAt({400, (2 * 56) + 12}));
}

TEST_CASE(
    "Lanes view shows a position and value readout when hovering a point",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    // Second authored point: seconds 2.0 -> x 200 of 800; norm 0.75 -> y 15 in the 56 px lane.
    harness.view.mouseMove(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));

    const std::optional<juce::String> readout = harness.view.valueReadoutTextForTest();
    REQUIRE(readout.has_value());
    if (readout.has_value())
    {
        // The stub formats the hovered point's own value; the position token rides alongside it.
        CHECK(readout->contains("[0.75]"));
        CHECK(readout->contains("2"));
    }

    // Moving off every point clears the readout.
    harness.view.mouseMove(testing::makeMouseDownEvent(harness.view, 600.0f, 15.0f));
    CHECK_FALSE(harness.view.valueReadoutTextForTest().has_value());
}

TEST_CASE("Lanes view tracks the dragged value in the readout", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    // Grab the second point (x 200, y 15) and drag it to y 25, which maps to the 0.5 value line.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 200.0f, 25.0f, 200.0f, 15.0f));

    const std::optional<juce::String> readout = harness.view.valueReadoutTextForTest();
    REQUIRE(readout.has_value());
    if (readout.has_value())
    {
        CHECK(readout->contains("[0.50]"));
    }

    // Releasing ends the gesture and clears the readout.
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 200.0f, 25.0f));
    CHECK_FALSE(harness.view.valueReadoutTextForTest().has_value());
}

TEST_CASE("Lanes view double-click never edits a point directly", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    // Double-click opens the typed value editor (skipped headless: the view is not on screen);
    // the old immediate reset-to-default lives in the point's context menu now. Either way a
    // double-click alone must not emit an edit.
    harness.view.mouseDoubleClick(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    CHECK(harness.listener.edit_count == 0);
}

TEST_CASE("Lanes view requires Alt to insert on empty lane area", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // A plain press on empty editable area starts nothing: without Alt the zone is not even a
    // hit, so the release commits no edit (in production the seek overlay owns that click).
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f));
    CHECK(harness.listener.edit_count == 0);

    // With Alt held the same press-release places a point.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, g_alt_click));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, g_alt_click));
    REQUIRE(harness.listener.edit_count == 1);
    CHECK(harness.listener.last_edit_points.size() == 3);
}

TEST_CASE(
    "Lanes view lands a new point on the curve and pulls its value by delta",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // Alt-press at x 100 (1.0 s, halfway between the authored 0.25 and 0.75 points) with the
    // pointer well below the curve (y 40 maps to 0.125): the new point lands ON the drawn curve
    // (the 0.5 interpolation), not at the pointer's y, so placement is sonically silent
    // (2026-07-18 amendment).
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 40.0f, g_alt_click));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 100.0f, 40.0f, g_alt_click));
    REQUIRE(harness.listener.edit_count == 1);
    REQUIRE(harness.listener.last_edit_points.size() == 3);
    CHECK_THAT(
        harness.listener.last_edit_points[1].norm_value, Catch::Matchers::WithinULP(0.5F, 0));

    // The drag phase pulls by the pointer's DELTA from the press: rising 10 px (a quarter of the
    // 40 px value band) lifts the on-curve landing from 0.5 to 0.75 — the value never jumps to
    // the raw pointer y (which at y 30 would read 0.375).
    harness.view.setState(makeState());
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 40.0f, g_alt_click));
    harness.view.mouseDrag(
        testing::makeMouseDragEvent(harness.view, 100.0f, 30.0f, 100.0f, 40.0f, g_alt_click));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, g_alt_click));
    REQUIRE(harness.listener.edit_count == 2);
    REQUIRE(harness.listener.last_edit_points.size() == 3);
    CHECK_THAT(
        harness.listener.last_edit_points[1].norm_value, Catch::Matchers::WithinULP(0.75F, 0));
}

TEST_CASE("Lanes view cancels an in-flight drag on request", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // Drag the second point somewhere else, then cancel (the editor routes Esc here): the
    // release afterwards must not commit anything.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 200.0f, 30.0f, 200.0f, 15.0f));
    CHECK(harness.view.cancelActiveGesture());
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 200.0f, 30.0f));
    CHECK(harness.listener.edit_count == 0);

    // With no gesture active the cancel reports unhandled so Esc can serve other owners.
    CHECK_FALSE(harness.view.cancelActiveGesture());
}

TEST_CASE("Lanes view Shift-locks a point drag to its dominant axis", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // Drag the second point (x 200, y 15, value 0.75) far right and slightly down while holding
    // Shift: the horizontal axis dominates, so the value must hold at 0.75 while the position
    // snaps along the grid.
    const juce::ModifierKeys shift_drag{
        juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier
    };
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    harness.view.mouseDrag(
        testing::makeMouseDragEvent(harness.view, 260.0f, 40.0f, 200.0f, 15.0f, shift_drag));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 260.0f, 40.0f, shift_drag));

    REQUIRE(harness.listener.edit_count == 1);
    REQUIRE(harness.listener.last_edit_points.size() == 2);
    CHECK_THAT(
        harness.listener.last_edit_points.back().norm_value, Catch::Matchers::WithinULP(0.75F, 0));
    // 260 px = 2.6 s snaps to the 2.5 s quarter-note line: measure 2, beat 2 at 120 BPM 4/4.
    CHECK(
        harness.listener.last_edit_points.back().position ==
        common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
}

TEST_CASE("Lanes view nudges the selected point with commits", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // Nothing selected: nudges report unhandled so arrow keys can fall through.
    CHECK_FALSE(
        harness.view.nudgeSelectedPoint(ToneAutomationLanesView::NudgeDirection::Up, false));

    // Selection is controller-owned: the view acts on whatever the published state selects.
    // Publish the second point (value 0.75) as the editor-wide selection.
    core::ToneAutomationViewState selected_state = makeState();
    selected_state.selected_point =
        core::ToneAutomationSelectedPointRef{.lane_index = 0, .point_index = 1};
    harness.view.setState(selected_state);

    // A value nudge steps 0.01 upward as one committed edit.
    REQUIRE(harness.view.nudgeSelectedPoint(ToneAutomationLanesView::NudgeDirection::Up, false));
    REQUIRE(harness.listener.edit_count == 1);
    REQUIRE(harness.listener.last_edit_points.size() == 2);
    CHECK(std::abs(harness.listener.last_edit_points.back().norm_value - 0.76F) < 0.0001F);

    // A time nudge moves to the adjacent grid line (quarter-note grid: measure 2 beat 2). The
    // commit's synchronous state push is not simulated here, so re-push the selected state to
    // keep the model and the selection in step before nudging again.
    harness.view.setState(selected_state);
    REQUIRE(harness.view.nudgeSelectedPoint(ToneAutomationLanesView::NudgeDirection::Later, false));
    REQUIRE(harness.listener.edit_count == 2);
    CHECK(
        harness.listener.last_edit_points.back().position ==
        common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
    // The committed nudge re-announces the selection at the point's new identity.
    CHECK(harness.listener.select_count >= 1);
    CHECK(
        harness.listener.last_select_position ==
        common::core::GridPosition{.measure = 2, .beat = 2, .offset = {}});
}

TEST_CASE(
    "Lanes view snaps discrete point drags to the nearest step", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    ToneAutomationLanesView view{harness.listener, harness.tempo_map, harness.tone_automation};
    view.setSize(800, 200);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{8.0}
        });
    view.setEditableWindow(
        common::core::TimeRange{
            .start = common::core::TimePosition{0.0}, .end = common::core::TimePosition{4.0}
        });

    core::ToneAutomationViewState state;
    state.tone_document_ref = "tones/x/tone.json";
    core::ToneAutomationLaneViewState lane;
    lane.instance_id = "instance-a";
    lane.param_id = "wah";
    lane.name = "Wah Mode";
    lane.resolved = true;
    lane.is_discrete = true;
    lane.discrete_value_count = 2; // a two-state toggle: only 0 and 1 are valid.
    lane.points = {
        core::ToneAutomationPointViewState{
            .position = {.measure = 2, .beat = 1, .offset = {}},
            .seconds = 2.0,
            .norm_value = 0.0F,
            .curve_shape = 0.0F,
        },
    };
    state.lanes = {std::move(lane)};
    view.setState(state);

    // The point at value 0 renders at y 45 (x 200 for seconds 2.0). Dragging up past the midpoint
    // snaps to the "on" state; a small drag that stays below the midpoint snaps back to "off".
    view.mouseDown(testing::makeMouseDownEvent(view, 200.0f, 45.0f));
    view.mouseDrag(testing::makeMouseDragEvent(view, 200.0f, 15.0f, 200.0f, 45.0f));
    view.mouseUp(testing::makeMouseDownEvent(view, 200.0f, 15.0f));
    REQUIRE(harness.listener.edit_count == 1);
    REQUIRE(harness.listener.last_edit_points.size() == 1);
    CHECK_THAT(
        harness.listener.last_edit_points.front().norm_value, Catch::Matchers::WithinULP(1.0F, 0));

    view.mouseDown(testing::makeMouseDownEvent(view, 200.0f, 45.0f));
    view.mouseDrag(testing::makeMouseDragEvent(view, 200.0f, 33.0f, 200.0f, 45.0f));
    view.mouseUp(testing::makeMouseDownEvent(view, 200.0f, 33.0f));
    REQUIRE(harness.listener.edit_count == 2);
    REQUIRE(harness.listener.last_edit_points.size() == 1);
    CHECK_THAT(
        harness.listener.last_edit_points.front().norm_value, Catch::Matchers::WithinULP(0.0F, 0));
}

TEST_CASE(
    "Lanes view keeps an in-flight point drag across a state push", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    // Grab the second authored point (x 200, y 15) and begin dragging it toward the 0.5 line.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));

    // The engine pushes fresh state mid-drag (its notifications fire in bursts). This must not
    // cancel the gesture: the drag keeps editing the model it started with, so the release still
    // commits. Dropping the gesture here is exactly what made a dragged point snap back.
    harness.view.setState(makeState());

    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 200.0f, 25.0f, 200.0f, 15.0f));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 200.0f, 25.0f));

    REQUIRE(harness.listener.edit_count == 1);
    REQUIRE(harness.listener.last_edit_points.size() == 2);
    // The dragged point committed at its new value rather than reverting to the authored 0.75.
    CHECK_THAT(
        harness.listener.last_edit_points.back().norm_value, Catch::Matchers::WithinULP(0.5F, 0));
}

TEST_CASE(
    "Lanes view commits a new point clicked through a state push", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;
    // Alt-press in the empty editable area of the resolved lane (x 100 = 1 s, inside the 0..4 s
    // window) to create a new-point preview.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, g_alt_click));

    // A state push arrives before the release, as it does when a click spans one of the engine's
    // notification bursts. Without deferral the preview point would be dropped and never commit --
    // the "point appears then disappears" symptom.
    harness.view.setState(makeState());

    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f));

    REQUIRE(harness.listener.edit_count == 1);
    // The new point joins the lane's two authored points.
    CHECK(harness.listener.last_edit_points.size() == 3);
}

TEST_CASE(
    "Lanes view announces a clicked point as the selection intent", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // A plain click (no drag) on the second authored point (x 200, y 15) emits the selection
    // intent without any edit -- selection is distinct from a move, and the selection itself is
    // controller-owned (deletion is the controller's Delete dispatch, not a view API).
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    CHECK(harness.listener.edit_count == 0);
    REQUIRE(harness.listener.select_count == 1);
    CHECK(harness.listener.last_select_instance_id == "instance-a");
    CHECK(harness.listener.last_select_param_id == "gain");
    CHECK(
        harness.listener.last_select_position ==
        common::core::GridPosition{.measure = 2, .beat = 1, .offset = {}});
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
