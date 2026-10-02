#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/live_input_sample.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_controller.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <rock_hero/editor/core/testing/input_calibration_fixtures.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using testing::makeInputCalibrationPrompt;
using testing::RecordingEditorController;

// Records the controller's view-state pushes for direct assertions.
class RecordingInputCalibrationView final : public IInputCalibrationView
{
public:
    void setState(const InputCalibrationViewState& state) override
    {
        states.push_back(state);
    }

    [[nodiscard]] const InputCalibrationViewState& lastState() const
    {
        REQUIRE_FALSE(states.empty());
        return states.back();
    }

    std::vector<InputCalibrationViewState> states;
};

// A sample at a steady level carrying the given measurement progress.
[[nodiscard]] common::audio::LiveInputSample sampleWith(
    std::optional<common::audio::InputCalibrationProgress> measurement)
{
    return common::audio::LiveInputSample{
        .raw_level = common::audio::AudioMeterLevel{.peak_db = -20.0, .clipping = false},
        .measurement = std::move(measurement),
    };
}

[[nodiscard]] common::audio::LiveInputMonitorError routeError(std::string message)
{
    return common::audio::LiveInputMonitorError{
        common::audio::LiveInputMonitorErrorCode::BackendRejected,
        std::move(message),
    };
}

} // namespace

// The popup opens with the route's stored gain, or the neutral gain for an uncalibrated route, and
// the idle message offering the two ways to a gain.
TEST_CASE("Input calibration controller opens idle", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController calibrated{editor, makeInputCalibrationPrompt(2.0)};
    calibrated.attachView(view);

    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == idleText());

    InputCalibrationController uncalibrated{
        editor,
        makeInputCalibrationPrompt(std::nullopt),
    };
    uncalibrated.attachView(view);
    CHECK(view.lastState().gain_db == Catch::Approx(common::audio::defaultGainDb()));
}

// Apply stores the shown gain through the editor, whose success ends the prompt; the popup asks for
// no close of its own.
TEST_CASE("Input calibration controller applies the shown gain", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);

    controller.onManualGainChanged(3.5);
    controller.onApplyRequested();

    CHECK(editor.input_calibration_apply_count == 1);
    CHECK(editor.last_input_calibration_gain_db == std::optional{3.5});
    CHECK(editor.input_calibration_close_count == 0);
}

// A refused store keeps the popup open with the reason.
TEST_CASE("Input calibration controller keeps a refused Apply open", "[core][input-calibration]")
{
    RecordingEditorController editor;
    editor.input_calibration_apply_result =
        std::unexpected{routeError("The calibration store is unavailable.")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(0.0)};
    controller.attachView(view);

    controller.onApplyRequested();

    CHECK(editor.input_calibration_close_count == 0);
    CHECK(view.lastState().message == "The calibration store is unavailable.");
}

// Measure measures the chosen pickups and follows the capture from waiting to listening; while
// it runs, the gain, the pickups and Apply wait.
TEST_CASE("Input calibration controller follows a measurement", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);
    controller.onPickupsSelected(common::audio::PickupClass::SingleCoil);

    controller.onMeasureRequested();
    CHECK(
        editor.last_input_calibration_pickups ==
        std::optional{common::audio::PickupClass::SingleCoil});
    CHECK(view.lastState().measuring);
    CHECK(view.lastState().message == measuringText(common::audio::InputCalibrationWaiting{}));

    const common::audio::InputCalibrationListening listening{.windows_remaining = 271};
    editor.input_calibration_sample = sampleWith(listening);
    controller.onSampleTick();
    CHECK(view.lastState().message == "Keep strumming that hard. 10 s left.");

    controller.onManualGainChanged(5.0);
    controller.onPickupsSelected(common::audio::PickupClass::Humbucker);
    controller.onApplyRequested();
    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
    CHECK(view.lastState().pickups == common::audio::PickupClass::SingleCoil);
    CHECK(editor.input_calibration_apply_count == 0);
}

// A finished measurement fills the gain with its result; Apply then stores it like any other.
TEST_CASE("Input calibration controller fills in a measured gain", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    editor.input_calibration_sample = sampleWith(
        common::audio::InputCalibrationMeasured{
            .route = common::audio::testing::makeInputDeviceIdentity(),
            .gain = common::audio::Gain{8.0},
        });
    controller.onSampleTick();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().gain_db == Catch::Approx(8.0));
    CHECK(
        view.lastState().message ==
        "Measured +8.0 dB with humbucker pickups. Click Apply to save it.");
    CHECK(editor.input_calibration_apply_count == 0);

    controller.onApplyRequested();
    CHECK(editor.last_input_calibration_gain_db == std::optional{8.0});
}

// A failed measurement ends with its reason, the gain untouched.
TEST_CASE("Input calibration controller reports a failed measurement", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    editor.input_calibration_sample =
        sampleWith(common::audio::InputCalibrationFailure::InputClipped);
    controller.onSampleTick();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(
        view.lastState().message == common::audio::inputCalibrationFailureText(
                                        common::audio::InputCalibrationFailure::InputClipped));
    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
}

// A refused start says why and starts nothing.
TEST_CASE("Input calibration controller reports start failure", "[core][input-calibration]")
{
    RecordingEditorController editor;
    editor.input_calibration_start_result =
        std::unexpected{routeError("No input route is selected.")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);

    controller.onMeasureRequested();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == "No input route is selected.");
}

// Measure again stops the measurement through the editor; Cancel closes.
TEST_CASE("Input calibration controller stops and closes", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    controller.onMeasureRequested();
    CHECK(editor.input_calibration_stop_count == 1);
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == idleText());

    controller.onCloseRequested();
    CHECK(editor.input_calibration_close_count == 1);
}

// The meter previews the shown gain against the chosen pickups' peak target; while a measurement
// runs it shows the raw input, as the measurement hears it, with no target to play to.
TEST_CASE("Input calibration controller meters the candidate gain", "[core][input-calibration]")
{
    RecordingEditorController editor;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{editor, makeInputCalibrationPrompt(2.0)};
    controller.attachView(view);

    editor.input_calibration_sample = sampleWith(std::nullopt);
    controller.onSampleTick();
    CHECK(view.lastState().input_meter_level.peak_db == Catch::Approx(-18.0));
    CHECK(
        view.lastState().meter_target_db ==
        std::optional{common::audio::inputCalibrationTargetPeakDb(
            common::audio::PickupClass::Humbucker)});

    controller.onPickupsSelected(common::audio::PickupClass::SingleCoil);
    CHECK(
        view.lastState().meter_target_db ==
        std::optional{common::audio::inputCalibrationTargetPeakDb(
            common::audio::PickupClass::SingleCoil)});

    controller.onMeasureRequested();
    CHECK(view.lastState().input_meter_level.peak_db == Catch::Approx(-20.0));
    CHECK_FALSE(view.lastState().meter_target_db.has_value());
}

// The countdown reaches 1 s before it ends, never 0 s.
TEST_CASE("Measuring text counts down whole seconds", "[core][input-calibration]")
{
    const auto at = [](std::size_t windows_remaining) {
        return measuringText(
            common::audio::InputCalibrationListening{.windows_remaining = windows_remaining});
    };
    CHECK(at(271) == "Keep strumming that hard. 10 s left.");
    CHECK(at(270) == "Keep strumming that hard. 9 s left.");
    CHECK(at(1) == "Keep strumming that hard. 1 s left.");
}

} // namespace rock_hero::editor::core
