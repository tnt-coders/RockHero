#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <optional>
#include <rock_hero/editor/core/input_calibration_controller.h>
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

// Records host intents and lets tests inject typed live-input failures.
class RecordingInputCalibrationHost final : public InputCalibrationController::Host
{
public:
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    startInputCalibrationMeasurement() override
    {
        start_count += 1;
        return start_result;
    }

    void cancelInputCalibrationMeasurement() override
    {
        cancel_count += 1;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> applyAutomaticInputCalibration(
        double gain_db) override
    {
        automatic_apply_count += 1;
        last_automatic_gain_db = gain_db;
        return automatic_apply_result;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> applyManualInputCalibration(
        double gain_db) override
    {
        manual_apply_count += 1;
        last_manual_gain_db = gain_db;
        return manual_apply_result;
    }

    void dismissInputCalibration() override
    {
        dismiss_count += 1;
    }

    std::expected<void, common::audio::LiveInputError> start_result{};
    std::expected<void, common::audio::LiveInputError> automatic_apply_result{};
    std::expected<void, common::audio::LiveInputError> manual_apply_result{};
    std::optional<double> last_automatic_gain_db{};
    std::optional<double> last_manual_gain_db{};
    int start_count{0};
    int cancel_count{0};
    int automatic_apply_count{0};
    int manual_apply_count{0};
    int dismiss_count{0};
};

[[nodiscard]] InputCalibrationPrompt prompt(double input_gain_db = 2.0)
{
    return InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = input_gain_db,
    };
}

[[nodiscard]] InputCalibrationController::CaptureSettings fastCaptureSettings()
{
    return InputCalibrationController::CaptureSettings{
        .settle_sample_count = 0,
        .wait_sample_count = 2,
        .measurement_sample_count = common::audio::minimumInputCalibrationActiveSampleCount(),
    };
}

[[nodiscard]] common::audio::AudioMeterLevel meterSample(double peak_db)
{
    return common::audio::AudioMeterLevel{
        .peak_db = peak_db,
        .clipping = false,
    };
}

[[nodiscard]] common::audio::LiveInputError routeError(std::string message)
{
    return common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        std::move(message),
    };
}

} // namespace

// Verifies manual calibration is committed through the narrow host contract.
TEST_CASE("Input calibration controller applies manual gain", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(-0.04), fastCaptureSettings()};
    controller.attachView(view);

    controller.onManualGainChanged(3.5);
    controller.onManualApplyRequested();

    CHECK(host.manual_apply_count == 1);
    CHECK(host.last_manual_gain_db == std::optional{3.5});
    CHECK(view.lastState().input_gain_db == Catch::Approx(3.5));
    CHECK(view.lastState().status_message == "Manual calibration saved. Gain set to 3.5 dB.");
    CHECK(view.lastState().manual_gain_controls_enabled);
    CHECK(view.lastState().dismiss_button_text == "Close");
}

// Verifies automatic capture owns sampling policy and commits the calculated gain.
TEST_CASE("Input calibration controller completes automatic capture", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0), fastCaptureSettings()};
    controller.attachView(view);

    REQUIRE(controller.onMeasurementStartRequested());
    for (std::size_t index = 0; index < common::audio::minimumInputCalibrationActiveSampleCount();
         ++index)
    {
        controller.onMeterSampled(meterSample(-20.0));
    }

    CHECK(host.start_count == 1);
    CHECK(host.automatic_apply_count == 1);
    CHECK(host.last_automatic_gain_db == std::optional{8.0});
    CHECK(host.cancel_count == 0);
    CHECK(view.lastState().input_gain_db == Catch::Approx(8.0));
    CHECK(view.lastState().status_message == "Calibration complete. Gain set to 8.0 dB.");
    CHECK(view.lastState().start_measurement_enabled);
    CHECK(view.lastState().manual_gain_controls_enabled);
    CHECK(view.lastState().dismiss_button_text == "Close");
}

// Verifies failed captures ask the host to restore measurement state and leave the popup open.
TEST_CASE("Input calibration controller cancels failed capture", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{
        host,
        prompt(2.0),
        InputCalibrationController::CaptureSettings{
            .settle_sample_count = 0,
            .wait_sample_count = 1,
            .measurement_sample_count = common::audio::minimumInputCalibrationActiveSampleCount(),
        }
    };
    controller.attachView(view);

    REQUIRE(controller.onMeasurementStartRequested());
    controller.onMeterSampled(meterSample(common::audio::minimumAudioMeterDb()));

    CHECK(host.cancel_count == 1);
    CHECK(host.automatic_apply_count == 0);
    CHECK(view.lastState().input_gain_db == Catch::Approx(2.0));
    CHECK(
        view.lastState().status_message == "No usable input signal was detected. Check the "
                                           "input and try again.");
    CHECK(view.lastState().start_measurement_enabled);
    CHECK(view.lastState().manual_gain_controls_enabled);
    CHECK(view.lastState().dismiss_button_text == "Dismiss");
}

// Verifies route setup failures stay in popup state and do not start capture.
TEST_CASE("Input calibration controller reports start failure", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    host.start_result = std::unexpected{routeError("Input route changed during calibration")};
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0), fastCaptureSettings()};
    controller.attachView(view);

    CHECK_FALSE(controller.onMeasurementStartRequested());
    controller.onMeterSampled(meterSample(-20.0));

    CHECK(host.start_count == 1);
    CHECK(host.automatic_apply_count == 0);
    CHECK(host.cancel_count == 0);
    CHECK(view.lastState().status_message == "Input route changed during calibration");
    CHECK(view.lastState().start_measurement_enabled);
    CHECK(view.lastState().manual_gain_controls_enabled);
}

// Verifies dismissal delegates restoration to the host and stops local capture processing.
TEST_CASE("Input calibration controller dismisses active capture", "[core][input-calibration]")
{
    RecordingInputCalibrationHost host;
    RecordingInputCalibrationView view;
    InputCalibrationController controller{host, prompt(2.0), fastCaptureSettings()};
    controller.attachView(view);

    REQUIRE(controller.onMeasurementStartRequested());
    controller.onDismissRequested();
    for (std::size_t index = 0; index < common::audio::minimumInputCalibrationActiveSampleCount();
         ++index)
    {
        controller.onMeterSampled(meterSample(-20.0));
    }

    CHECK(host.dismiss_count == 1);
    CHECK(host.automatic_apply_count == 0);
    CHECK(host.cancel_count == 0);
}

} // namespace rock_hero::editor::core
