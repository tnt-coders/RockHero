#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/live_input_sample.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_controller.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

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

// Records host intents and returns the scripted sample the monitor would produce.
class RecordingInputCalibrationHost final : public InputCalibrationController::Host
{
public:
    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    startInputCalibrationMeasurement() override
    {
        start_count += 1;
        return start_result;
    }

    [[nodiscard]] common::audio::LiveInputSample sampleInputCalibration() override
    {
        sample_count += 1;
        return sample;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError>
    applyManualInputCalibration(double gain_db) override
    {
        manual_apply_count += 1;
        last_manual_gain_db = gain_db;
        return manual_apply_result;
    }

    void dismissInputCalibration() override
    {
        dismiss_count += 1;
    }

    std::expected<void, common::audio::LiveInputMonitorError> start_result{};
    std::expected<void, common::audio::LiveInputMonitorError> manual_apply_result{};
    common::audio::LiveInputSample sample{};
    std::optional<double> last_manual_gain_db{};
    int start_count{0};
    int sample_count{0};
    int manual_apply_count{0};
    int dismiss_count{0};
};

[[nodiscard]] InputCalibrationPrompt prompt(double input_gain_db = 2.0)
{
    return InputCalibrationPrompt{
        .route = common::audio::testing::makeInputDeviceIdentity(),
        .stored_gain_db = input_gain_db,
    };
}

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

// An uncalibrated route's prompt says what is off and starts from the neutral gain; a calibrated
// one starts from its stored gain.
TEST_CASE(
    "Input calibration controller words the prompt for its route", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController uncalibrated{
        host,
        InputCalibrationPrompt{
            .route = common::audio::testing::makeInputDeviceIdentity(),
            .stored_gain_db = std::nullopt,
        },
    };
    uncalibrated.attachView(view);

    CHECK(
        view.lastState().status_message == "Live input stays off until this input is calibrated.");
    CHECK(view.lastState().input_gain_db == Catch::Approx(common::audio::defaultGainDb()));
    CHECK(view.lastState().dismiss_button_text == "Later");
}

// Manual calibration is committed through the narrow host contract.
TEST_CASE("Input calibration controller applies manual gain", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(-0.04)};
    controller.attachView(view);

    controller.onManualGainChanged(3.5);
    controller.onManualApplyRequested();

    CHECK(host.manual_apply_count == 1);
    CHECK(host.last_manual_gain_db == std::optional{3.5});
    CHECK(view.lastState().input_gain_db == Catch::Approx(3.5));
    CHECK(view.lastState().status_message == "Manual calibration saved. Gain set to 3.5 dB.");
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().dismiss_button_text == "Close");
}

// A running measurement locks the controls and reports its stage.
TEST_CASE("Input calibration controller follows a running measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasurementStartRequested();
    host.sample = sampleWith(
        common::audio::InputCalibrationStageProgress{
            .stage = common::audio::InputCalibrationStage::Measuring,
            .windows_remaining = common::audio::inputCalibrationListenSampleCount() - 1,
        });
    controller.onSampleTick();

    CHECK(host.start_count == 1);
    CHECK(view.lastState().measuring);
    CHECK(view.lastState().status_message == "Keep playing that hard. 10 s left.");
}

// The countdown reads the capture's own windows and changes only when a whole second passes.
TEST_CASE("Input calibration controller counts down whole seconds", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);
    controller.onMeasurementStartRequested();

    const auto status_at = [&host, &controller, &view](std::size_t windows_remaining) {
        host.sample = sampleWith(
            common::audio::InputCalibrationStageProgress{
                .stage = common::audio::InputCalibrationStage::Measuring,
                .windows_remaining = windows_remaining,
            });
        controller.onSampleTick();
        return view.lastState().status_message;
    };

    CHECK(status_at(271) == "Keep playing that hard. 10 s left.");
    CHECK(status_at(270) == "Keep playing that hard. 9 s left.");
    CHECK(status_at(1) == "Keep playing that hard. 1 s left.");
}

// A measurement the monitor committed moves the popup into its completed state.
TEST_CASE("Input calibration controller completes a measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasurementStartRequested();
    host.sample =
        sampleWith(common::audio::InputCalibrationCommitted{.gain = common::audio::Gain{8.0}});
    controller.onSampleTick();

    CHECK(view.lastState().input_gain_db == Catch::Approx(8.0));
    CHECK(view.lastState().status_message == "Calibration complete. Gain set to 8.0 dB.");
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().dismiss_button_text == "Close");
}

// A failed measurement shows why and returns the popup to the last committed gain.
TEST_CASE("Input calibration controller reports a failed measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasurementStartRequested();
    host.sample = sampleWith(common::audio::InputCalibrationFailed{"No usable input signal."});
    controller.onSampleTick();

    CHECK(view.lastState().input_gain_db == Catch::Approx(2.0));
    CHECK(view.lastState().status_message == "No usable input signal.");
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().dismiss_button_text == "Close");
}

// Without a measurement the tick only shows the raw input through the previewed gain.
TEST_CASE("Input calibration controller meters the input while idle", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    host.sample = sampleWith(std::nullopt);
    controller.onSampleTick();

    CHECK(host.sample_count == 1);
    CHECK(view.lastState().input_meter_level.peak_db == Catch::Approx(-18.0));
    CHECK_FALSE(view.lastState().measuring);
}

// A refused start stays in popup state and does not lock the controls.
TEST_CASE("Input calibration controller reports start failure", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    host.start_result = std::unexpected{routeError("Input route changed during calibration")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasurementStartRequested();

    CHECK(host.start_count == 1);
    CHECK(view.lastState().status_message == "Input route changed during calibration");
    CHECK_FALSE(view.lastState().measuring);
}

// Dismissing hands the measurement's end to the host.
TEST_CASE(
    "Input calibration controller dismisses a running measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasurementStartRequested();
    controller.onDismissRequested();

    CHECK(host.dismiss_count == 1);
}

} // namespace rock_hero::editor::core
