#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <rock_hero/common/audio/input/live_input_sample.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_controller.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <string>
#include <utility>
#include <variant>
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
    startInputCalibrationMeasurement(common::audio::PickupClass pickups) override
    {
        start_count += 1;
        last_start_pickups = pickups;
        return start_result;
    }

    [[nodiscard]] common::audio::LiveInputSample sampleInputCalibration() override
    {
        sample_count += 1;
        return sample;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputMonitorError> applyInputCalibration(
        double gain_db) override
    {
        apply_count += 1;
        last_applied_gain_db = gain_db;
        return apply_result;
    }

    void stopInputCalibrationMeasurement() override
    {
        stop_count += 1;
    }

    void closeInputCalibration() override
    {
        close_count += 1;
    }

    std::expected<void, common::audio::LiveInputMonitorError> start_result{};
    std::expected<void, common::audio::LiveInputMonitorError> apply_result{};
    common::audio::LiveInputSample sample{};
    std::optional<double> last_applied_gain_db{};
    std::optional<common::audio::PickupClass> last_start_pickups{};
    int start_count{0};
    int sample_count{0};
    int apply_count{0};
    int stop_count{0};
    int close_count{0};
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

// The popup opens with the route's stored gain, or the neutral gain for an uncalibrated route, and
// the idle message offering the two ways to a gain.
TEST_CASE("Input calibration controller opens idle", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController calibrated{host, prompt(2.0)};
    calibrated.attachView(view);

    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == idleText());

    InputCalibrationController uncalibrated{
        host,
        InputCalibrationPrompt{
            .route = common::audio::testing::makeInputDeviceIdentity(),
            .stored_gain_db = std::nullopt,
        },
    };
    uncalibrated.attachView(view);
    CHECK(view.lastState().gain_db == Catch::Approx(common::audio::defaultGainDb()));
}

// Apply stores the shown gain through the host, whose success ends the prompt; the popup asks for
// no close of its own.
TEST_CASE("Input calibration controller applies the shown gain", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(-0.04)};
    controller.attachView(view);

    controller.onManualGainChanged(3.5);
    controller.onApplyRequested();

    CHECK(host.apply_count == 1);
    CHECK(host.last_applied_gain_db == std::optional{3.5});
    CHECK(host.close_count == 0);
}

// A refused store keeps the popup open with the reason.
TEST_CASE("Input calibration controller keeps a refused Apply open", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    host.apply_result = std::unexpected{routeError("The calibration store is unavailable.")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(0.0)};
    controller.attachView(view);

    controller.onApplyRequested();

    CHECK(host.close_count == 0);
    CHECK(view.lastState().message == "The calibration store is unavailable.");
}

// Measure measures the chosen pickups and follows the capture from waiting to listening; while
// it runs, the gain, the pickups and Apply wait.
TEST_CASE("Input calibration controller follows a measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);
    controller.onPickupsSelected(common::audio::PickupClass::SingleCoil);

    controller.onMeasureRequested();
    CHECK(host.last_start_pickups == std::optional{common::audio::PickupClass::SingleCoil});
    CHECK(view.lastState().measuring);
    CHECK(view.lastState().message == measuringText(common::audio::InputCalibrationWaiting{}));

    const common::audio::InputCalibrationListening listening{.windows_remaining = 271};
    host.sample = sampleWith(listening);
    controller.onSampleTick();
    CHECK(view.lastState().message == "Keep playing that hard. 10 s left.");

    controller.onManualGainChanged(5.0);
    controller.onPickupsSelected(common::audio::PickupClass::Humbucker);
    controller.onApplyRequested();
    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
    CHECK(view.lastState().pickups == common::audio::PickupClass::SingleCoil);
    CHECK(host.apply_count == 0);
}

// A finished measurement fills the gain with its result; Apply then stores it like any other.
TEST_CASE("Input calibration controller fills in a measured gain", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    host.sample =
        sampleWith(common::audio::InputCalibrationMeasured{.gain = common::audio::Gain{8.0}});
    controller.onSampleTick();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().gain_db == Catch::Approx(8.0));
    CHECK(
        view.lastState().message ==
        "Measured +8.0 dB with humbucker pickups. Click Apply to save it.");
    CHECK(host.apply_count == 0);

    controller.onApplyRequested();
    CHECK(host.last_applied_gain_db == std::optional{8.0});
}

// A failed measurement ends with its reason, the gain untouched.
TEST_CASE("Input calibration controller reports a failed measurement", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    host.sample = sampleWith(common::audio::InputCalibrationFailed{"Input clipped."});
    controller.onSampleTick();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == "Input clipped.");
    CHECK(view.lastState().gain_db == Catch::Approx(2.0));
}

// A refused start says why and starts nothing.
TEST_CASE("Input calibration controller reports start failure", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    host.start_result = std::unexpected{routeError("No input route is selected.")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    controller.onMeasureRequested();

    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == "No input route is selected.");
}

// Measure again stops the measurement through the host; Cancel closes.
TEST_CASE("Input calibration controller stops and closes", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);
    controller.onMeasureRequested();

    controller.onMeasureRequested();
    CHECK(host.stop_count == 1);
    CHECK_FALSE(view.lastState().measuring);
    CHECK(view.lastState().message == idleText());

    controller.onCloseRequested();
    CHECK(host.close_count == 1);
}

// The meter previews the shown gain against the chosen pickups' peak target; while a measurement
// runs it shows the raw input, as the measurement hears it, with no target to play to.
TEST_CASE("Input calibration controller meters the candidate gain", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0)};
    controller.attachView(view);

    host.sample = sampleWith(std::nullopt);
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

// The one gain formatter: signed, one decimal, and zero without a sign.
TEST_CASE("Signed gain text prints sign and one decimal", "[core][input-calibration]")
{
    CHECK(signedGainText(2.3) == "+2.3");
    CHECK(signedGainText(-0.5) == "-0.5");
    CHECK(signedGainText(0.0) == "0.0");
    CHECK(signedGainText(-0.04) == "0.0");
}

// The countdown reaches 1 s before it ends, never 0 s.
TEST_CASE("Measuring text counts down whole seconds", "[core][input-calibration]")
{
    const auto at = [](std::size_t windows_remaining) {
        return measuringText(
            common::audio::InputCalibrationListening{.windows_remaining = windows_remaining});
    };
    CHECK(at(271) == "Keep playing that hard. 10 s left.");
    CHECK(at(270) == "Keep playing that hard. 9 s left.");
    CHECK(at(1) == "Keep playing that hard. 1 s left.");
}

} // namespace rock_hero::editor::core
