#include "input_calibration/input_calibration_window.h"

#include <catch2/catch_test_macros.hpp>
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
        .input_gain_db = -6.0,
    };
}

} // namespace

// The window shows the full strum-to-calibrate controls, enabled by the controller's state.
TEST_CASE("InputCalibrationWindow shows the calibrate action", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr};

    const auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");

    const auto& apply =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_manual_apply_button");
    const auto& slider =
        findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");

    CHECK(calibrate.isVisible());
    CHECK(calibrate.isEnabled());
    CHECK(apply.isEnabled());
    CHECK(slider.isEnabled());
}

} // namespace rock_hero::editor::ui
