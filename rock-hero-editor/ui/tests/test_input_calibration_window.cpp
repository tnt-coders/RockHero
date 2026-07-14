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
        .message = "Calibrate your input",
        .input_gain_db = -6.0,
    };
}

} // namespace

// Sourcing a calibrated game config shows the game value read-only: the Calibrate and Apply buttons
// and gain slider stay visible but disabled (grayed out) with an explanatory tooltip, so a
// two-hands-on-guitar player is never asked to interact.
TEST_CASE(
    "InputCalibrationWindow disables the measure action in the read-only game reflection",
    "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, true};

    auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    auto& apply =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_manual_apply_button");
    auto& slider = findRequiredDescendant<juce::Slider>(window, "input_calibration_manual_gain");
    const auto& dismiss =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_cancel_button");

    // The controls stay visible but disabled (grayed out) rather than hidden.
    CHECK(calibrate.isVisible());
    CHECK_FALSE(calibrate.isEnabled());
    CHECK(apply.isVisible());
    CHECK_FALSE(apply.isEnabled());
    CHECK(slider.isVisible());
    CHECK_FALSE(slider.isEnabled());
    // Each disabled control carries the derived-from-game tooltip explaining why.
    CHECK(calibrate.getTooltip() == "Derived from game settings");
    CHECK(apply.getTooltip() == "Derived from game settings");
    CHECK(slider.getTooltip() == "Derived from game settings");
    CHECK(dismiss.isVisible());
}

// The editable flow keeps the full strum-to-calibrate controls and carries no game-source tooltip.
TEST_CASE(
    "InputCalibrationWindow keeps the editable flow when sourcing the editor's own audio",
    "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, false};

    auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");

    CHECK(calibrate.isVisible());
    CHECK(calibrate.getTooltip().isEmpty());
}

// One toggle governs both surfaces: flipping the source while the window is open re-scopes it live.
TEST_CASE(
    "InputCalibrationWindow re-scopes live when the source toggle flips", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, false};

    auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");

    // The Calibrate button stays visible in both modes; the tooltip and enablement carry the
    // read-only state instead.
    CHECK(calibrate.getTooltip().isEmpty());

    window.setReadOnlyGameReflection(true);
    CHECK(calibrate.isVisible());
    CHECK_FALSE(calibrate.isEnabled());
    CHECK(calibrate.getTooltip() == "Derived from game settings");

    window.setReadOnlyGameReflection(false);
    CHECK(calibrate.isVisible());
    CHECK(calibrate.getTooltip().isEmpty());
}

} // namespace rock_hero::editor::ui
