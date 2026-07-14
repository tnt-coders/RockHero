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

// Sourcing a calibrated game config shows the game value read-only: the notice appears and the
// strum-to-calibrate action is removed so a two-hands-on-guitar player is never asked to interact.
TEST_CASE(
    "InputCalibrationWindow hides the measure action in the read-only game reflection",
    "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, true};

    const auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    const auto& apply =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_manual_apply_button");
    const auto& notice =
        findRequiredDescendant<juce::Label>(window, "input_calibration_game_notice");
    const auto& dismiss =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_cancel_button");

    CHECK_FALSE(calibrate.isVisible());
    CHECK_FALSE(apply.isVisible());
    CHECK(notice.isVisible());
    CHECK(notice.getText().contains("Game audio settings"));
    CHECK(dismiss.isVisible());
}

// The editable flow keeps the full strum-to-calibrate controls and hides the game-source notice.
TEST_CASE(
    "InputCalibrationWindow keeps the editable flow when sourcing the editor's own audio",
    "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, false};

    const auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    const auto& notice =
        findRequiredDescendant<juce::Label>(window, "input_calibration_game_notice");

    CHECK(calibrate.isVisible());
    CHECK_FALSE(notice.isVisible());
}

// One toggle governs both surfaces: flipping the source while the window is open re-scopes it live.
TEST_CASE(
    "InputCalibrationWindow re-scopes live when the source toggle flips", "[ui][input-calibration]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingEditorController controller;
    const core::InputCalibrationPrompt prompt = calibrationPrompt();

    InputCalibrationWindow window{controller, nullptr, prompt, nullptr, false};

    const auto& calibrate =
        findRequiredDescendant<juce::TextButton>(window, "input_calibration_start_button");
    const auto& notice =
        findRequiredDescendant<juce::Label>(window, "input_calibration_game_notice");

    window.setReadOnlyGameReflection(true);
    CHECK_FALSE(calibrate.isVisible());
    CHECK(notice.isVisible());

    window.setReadOnlyGameReflection(false);
    CHECK(calibrate.isVisible());
    CHECK_FALSE(notice.isVisible());
}

} // namespace rock_hero::editor::ui
