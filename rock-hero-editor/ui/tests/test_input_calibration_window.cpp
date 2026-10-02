#include "input_calibration/input_calibration_window.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>

namespace rock_hero::editor::ui
{

namespace
{

using core::testing::RecordingEditorController;
using testing::findRequiredDescendant;

[[nodiscard]] core::InputCalibrationPrompt calibrationPrompt()
{
    return core::InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = -6.0,
    };
}

} // namespace

// The window opens on the interface and the gain; measuring waits behind its header, which opens
// the pickup and the start button under it and fits the window to them, then closes them again.
// A running measurement locks the section and its header, since the controller holds the section
// open until it ends.
TEST_CASE("InputCalibrationWindow keeps measuring behind its header", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, prompt, nullptr};

    auto& disclosure =
        findRequiredDescendant<juce::Button>(window, "input_calibration_measure_disclosure");
    const auto& pickups =
        findRequiredDescendant<juce::ComboBox>(window, "input_calibration_pickup");
    const auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    auto& pickup_help = findRequiredDescendant<juce::DrawableButton>(
        window, "input_calibration_pickup_help_button");
    const auto& pickup_label =
        findRequiredDescendant<juce::Label>(window, "input_calibration_pickup_label");
    const auto& apply =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_manual_apply_button");
    const auto& slider =
        findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    const auto& later =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_cancel_button");
    const auto& interface_label =
        findRequiredDescendant<juce::Label>(window, "input_calibration_interface_label");
    const juce::Component* const content = window.getContentComponent();
    REQUIRE(content != nullptr);
    // The bottom margin matches the side margin in either state.
    const int margin = interface_label.getX();
    const int closed_height = content->getHeight();
    CHECK(later.getBottom() + margin == closed_height);

    CHECK(disclosure.getButtonText() == "Device not listed? Calibrate by playing");
    CHECK(disclosure.isEnabled());
    CHECK_FALSE(pickups.isVisible());
    CHECK_FALSE(calibrate.isVisible());
    CHECK(apply.isEnabled());
    CHECK(slider.isEnabled());

    REQUIRE(disclosure.onClick);
    disclosure.onClick();
    CHECK(disclosure.getToggleState());
    CHECK(pickups.isVisible());
    CHECK(pickup_help.isVisible());
    CHECK(pickup_help.getTooltip() == "Open the pickup types table");
    CHECK(pickups.getRight() <= pickup_help.getX());
    CHECK(pickup_label.getText() == "Pickup type");
    CHECK(calibrate.isVisible());
    CHECK(calibrate.isEnabled());
    CHECK(calibrate.getButtonText() == "Start Calibration");
    CHECK(disclosure.getBounds().getBottom() <= pickups.getY());
    CHECK(pickups.getBottom() <= calibrate.getY());
    CHECK(content->getHeight() > closed_height);
    CHECK(later.getBottom() + margin == content->getHeight());

    disclosure.onClick();
    CHECK_FALSE(pickups.isVisible());
    CHECK_FALSE(calibrate.isVisible());
    CHECK(content->getHeight() == closed_height);

    disclosure.onClick();
    REQUIRE(calibrate.onClick);
    calibrate.onClick();
    CHECK_FALSE(disclosure.isEnabled());
    CHECK_FALSE(pickups.isEnabled());
    CHECK_FALSE(calibrate.isEnabled());
}

} // namespace rock_hero::editor::ui
