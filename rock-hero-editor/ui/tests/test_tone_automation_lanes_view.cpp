#include "tone/tone_automation_lanes_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <compare>
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

    void onToneAutomationLaneCaretRequested(
        std::string instance_id, std::string param_id, common::core::TimePosition time) override
    {
        last_lane_caret_instance_id = std::move(instance_id);
        last_lane_caret_param_id = std::move(param_id);
        last_lane_caret_time = time;
        lane_caret_count += 1;
    }

    void onToneAutomationPointerMove(const core::ToneAutomationPointerEvent& event) override
    {
        last_pointer_event = event;
        pointer_move_count += 1;
    }

    void onToneAutomationPointerExit() override
    {
        pointer_exit_count += 1;
    }

    void onToneAutomationPointerDown(const core::ToneAutomationPointerEvent& event) override
    {
        last_pointer_event = event;
        pointer_down_count += 1;
    }

    void onToneAutomationPointerDrag(const core::ToneAutomationPointerEvent& event) override
    {
        last_pointer_event = event;
        pointer_drag_count += 1;
    }

    void onToneAutomationPointerUp(const core::ToneAutomationPointerEvent& event) override
    {
        last_pointer_event = event;
        pointer_up_count += 1;
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
    std::string last_lane_caret_instance_id;
    std::string last_lane_caret_param_id;
    common::core::TimePosition last_lane_caret_time{};
    int lane_caret_count = 0;
    std::optional<core::ToneAutomationPointerEvent> last_pointer_event;
    int pointer_move_count = 0;
    int pointer_exit_count = 0;
    int pointer_down_count = 0;
    int pointer_drag_count = 0;
    int pointer_up_count = 0;
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

    // Empty editable lane area claims the pointer (§9b): a plain click seeks and arms the
    // caret on the lane, and with Alt held it is the insert quasimode's target.
    CHECK(harness.view.wantsPointerAt({100, 30}));
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

TEST_CASE(
    "Lanes view shows the controller's drag preview in the readout", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // The controller owns the drag now and publishes its preview; the view paints the readout from
    // it. A preview on lane 0 at measure 2 beat 1, value 0.5, formats through the stub as [0.50].
    core::ToneAutomationViewState state = makeState();
    state.drag_preview = core::ToneAutomationDragPreviewRef{
        .lane_index = 0,
        .position = {.measure = 2, .beat = 1, .offset = {}},
        .value = 0.5F,
        .is_new_point = false,
        .source_point_index = 1,
    };
    harness.view.setState(state);

    // A drag advance seeds the readout at the cursor from the published preview.
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 200.0f, 25.0f, 200.0f, 15.0f));
    const std::optional<juce::String> readout = harness.view.valueReadoutTextForTest();
    REQUIRE(readout.has_value());
    if (readout.has_value())
    {
        CHECK(readout->contains("[0.50]"));
    }

    // A preview-less push (the drag ended) clears the readout.
    harness.view.setState(makeState());
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

TEST_CASE(
    "Lanes view forwards editing presses to the controller pointer seam",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // A press on empty editable lane area forwards a pointer Down carrying the lane identity, its
    // published index, the raw pixel, the value-band extents, the click count, and the modifiers —
    // the controller re-resolves point-vs-area and arms the caret or the insert (a controller test).
    // The view emits no edit or caret intent itself; those became the controller's.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f));
    REQUIRE(harness.listener.pointer_down_count == 1);
    CHECK(harness.listener.edit_count == 0);
    CHECK(harness.listener.lane_caret_count == 0);
    REQUIRE(harness.listener.last_pointer_event.has_value());
    if (harness.listener.last_pointer_event.has_value())
    {
        const core::ToneAutomationPointerEvent& event = *harness.listener.last_pointer_event;
        CHECK(event.instance_id == "instance-a");
        CHECK(event.param_id == "gain");
        CHECK(event.lane_index == 0);
        CHECK(std::is_eq(event.x <=> 100.0f));
        CHECK(event.clicks == 1);
        CHECK_FALSE(event.modifiers.alt);
        // Both real lanes contribute a value-band extent so the controller can map y to a value.
        CHECK(event.lane_extents.size() == 2);
    }

    // A press on the authored point (x 200, y 15) forwards a Down for the same lane; an Alt press
    // carries the Alt modifier so the controller runs its insert branch instead.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    CHECK(harness.listener.pointer_down_count == 2);
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, g_alt_click));
    REQUIRE(harness.listener.pointer_down_count == 3);
    REQUIRE(harness.listener.last_pointer_event.has_value());
    if (harness.listener.last_pointer_event.has_value())
    {
        CHECK(harness.listener.last_pointer_event->modifiers.alt);
    }

    // A drag advance and a release forward the matching pointer phases.
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 120.0f, 30.0f, 100.0f, 30.0f));
    CHECK(harness.listener.pointer_drag_count == 1);
    harness.view.mouseUp(testing::makeMouseDownEvent(harness.view, 120.0f, 30.0f));
    CHECK(harness.listener.pointer_up_count == 1);
}

TEST_CASE(
    "Lanes view forwards lane-area hovers as pointer Move and Exit intents",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // A hover over empty editable lane area forwards a pointer Move carrying the hovered lane's
    // identity, the raw lane-local pixel x, and the geometry the controller needs to snap. The
    // Phase 2 seam forwards raw pixels + geometry rather than Phase 1's view-computed time, so the
    // ghost and an Alt+click resolve through one snap path.
    const juce::ModifierKeys alt_hover{juce::ModifierKeys::altModifier};
    harness.view.mouseMove(testing::makeMouseDownEvent(harness.view, 100.0f, 30.0f, alt_hover));
    REQUIRE(harness.listener.pointer_move_count == 1);
    CHECK(harness.listener.pointer_exit_count == 0);
    REQUIRE(harness.listener.last_pointer_event.has_value());
    if (harness.listener.last_pointer_event.has_value())
    {
        const core::ToneAutomationPointerEvent& event = *harness.listener.last_pointer_event;
        CHECK(event.instance_id == "instance-a");
        CHECK(event.param_id == "gain");
        // The raw pixel, not a snapped time: exact by construction, is_eq keeps -Wfloat-equal clean.
        CHECK(std::is_eq(event.x <=> 100.0f));
        CHECK(event.geometry.content_width == 800);
        CHECK(std::is_eq(event.geometry.visible_timeline.start.seconds <=> 0.0));
        CHECK(std::is_eq(event.geometry.visible_timeline.end.seconds <=> 8.0));
        CHECK(event.modifiers.alt);
        CHECK_FALSE(event.modifiers.ctrl);
    }

    // Moving onto an authored point (x 200, y 15) is not empty lane area, so the hover ends: the
    // view forwards Exit, no further Move.
    harness.view.mouseMove(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    CHECK(harness.listener.pointer_move_count == 1);
    CHECK(harness.listener.pointer_exit_count == 1);

    // The pointer leaving the component clears the hover too.
    harness.view.mouseExit(testing::makeMouseDownEvent(harness.view, 200.0f, 15.0f));
    CHECK(harness.listener.pointer_exit_count == 2);
}

TEST_CASE(
    "Lanes view masks the paused column at a published lane caret", "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // No caret published: no mask span.
    CHECK_FALSE(harness.view.caretMaskYRange().has_value());

    // A published caret square (drawn on the curve) reports its mask span for the cut-out.
    core::ToneAutomationViewState caret_state = makeState();
    caret_state.lane_caret = core::ToneAutomationLaneCaretRef{
        .lane_index = 0,
        .seconds = 1.0,
        .position = {.measure = 1, .beat = 3, .offset = {}},
    };
    harness.view.setState(caret_state);
    CHECK(harness.view.caretMaskYRange().has_value());
}

TEST_CASE(
    "Lanes view cancels a lane resize but leaves point drags to the controller",
    "[ui][tone-automation-lanes]")
{
    LanesHarness harness;

    // The lane-height resize is the one gesture the view still owns (the point move/insert drag and
    // its whole state machine moved to the controller and are exercised there). Grab the first
    // lane's resize band (the bottom 6 px of the 56 px lane) and drag it taller.
    harness.view.mouseDown(testing::makeMouseDownEvent(harness.view, 100.0f, 53.0f));
    // A resize is view-owned, so it forwards no pointer event to the controller.
    CHECK(harness.listener.pointer_down_count == 0);
    harness.view.mouseDrag(testing::makeMouseDragEvent(harness.view, 100.0f, 83.0f, 100.0f, 53.0f));
    CHECK(harness.view.totalHeight() > (2 * 56) + 26);

    // Esc (routed here by the editor) cancels the resize and restores the starting height.
    CHECK(harness.view.cancelActiveGesture());
    CHECK(harness.view.totalHeight() == (2 * 56) + 26);

    // With no view-owned gesture active the cancel reports unhandled, so the editor's Esc ladder
    // falls through to the controller (which cancels its own point drag through the drag preview).
    CHECK_FALSE(harness.view.cancelActiveGesture());
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
