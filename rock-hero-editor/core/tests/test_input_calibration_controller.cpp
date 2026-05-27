#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/editor/core/input_calibration_controller.h>

namespace rock_hero::editor::core
{

namespace
{

// Captures state pushed by the headless calibration controller.
class FakeInputCalibrationView final : public IInputCalibrationView
{
public:
    void setState(const InputCalibrationViewState& state) override
    {
        last_state = state;
        set_state_count += 1;
    }

    void requestClose() override
    {
        close_count += 1;
    }

    std::optional<InputCalibrationViewState> last_state;
    int set_state_count{0};
    int close_count{0};
};

// Records workflow calls that would normally be handled by EditorController.
class FakeInputCalibrationWorkflow final : public IInputCalibrationWorkflow
{
public:
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() override
    {
        measurement_start_count += 1;
        if (start_error.has_value())
        {
            common::audio::LiveInputError error = std::move(*start_error);
            start_error.reset();
            return std::unexpected{std::move(error)};
        }
        return {};
    }

    void onInputCalibrationMeasurementCancelled() override
    {
        measurement_cancel_count += 1;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db) override
    {
        succeeded_gain_db = gain_db;
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db) override
    {
        manual_gain_db = gain_db;
        return {};
    }

    void onInputCalibrationDismissed() override
    {
        dismissed_count += 1;
    }

    std::optional<common::audio::LiveInputError> start_error;
    std::optional<double> succeeded_gain_db;
    std::optional<double> manual_gain_db;
    int measurement_start_count{0};
    int measurement_cancel_count{0};
    int dismissed_count{0};
};

// Supplies deterministic raw meter samples to the input calibration controller.
class FakeLiveInput final : public common::audio::ILiveInput
{
public:
    [[nodiscard]] common::audio::Gain inputGain() const override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setInputGain(
        common::audio::Gain gain) override
    {
        (void)gain;
        return {};
    }

    [[nodiscard]] common::audio::AudioMeterLevel rawInputMeterLevel() const override
    {
        return raw_level;
    }

    [[nodiscard]] bool liveInputMonitoringEnabled() const override
    {
        return false;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setLiveInputMonitoringEnabled(
        bool enabled) override
    {
        (void)enabled;
        return {};
    }

    [[nodiscard]] bool calibrationInputMonitoringEnabled() const override
    {
        return false;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    setCalibrationInputMonitoringEnabled(bool enabled) override
    {
        (void)enabled;
        return {};
    }

    mutable common::audio::AudioMeterLevel raw_level{};
};

} // namespace

// Verifies manual calibration is handled in core without requiring JUCE widget interaction.
TEST_CASE("Input calibration controller applies manual gain", "[core][input-calibration]")
{
    FakeInputCalibrationWorkflow workflow;
    FakeLiveInput live_input;
    FakeInputCalibrationView view;
    InputCalibrationController controller{
        workflow,
        &live_input,
        InputCalibrationPrompt{.input_gain_db = 0.0},
    };
    controller.attachView(view);

    controller.onManualGainChanged(3.5);
    controller.onManualCalibrationRequested();

    CHECK(workflow.manual_gain_db == std::optional{3.5});
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->input_gain_db == Catch::Approx(3.5));
    CHECK(view.last_state->status_text == "Manual calibration saved. Gain set to 3.5 dB.");
    CHECK(view.last_state->close_button_text == "Close");
}

// Verifies automatic capture is coordinated by the controller while measurement math stays shared.
TEST_CASE("Input calibration controller completes automatic capture", "[core][input-calibration]")
{
    FakeInputCalibrationWorkflow workflow;
    FakeLiveInput live_input;
    live_input.raw_level = common::audio::AudioMeterLevel{.peak_db = -24.0};
    FakeInputCalibrationView view;
    InputCalibrationController controller{
        workflow,
        &live_input,
        InputCalibrationPrompt{.input_gain_db = 0.0},
    };
    controller.attachView(view);

    controller.onAutomaticCalibrationRequested();
    for (int tick = 0; tick < (inputCalibrationMeterHz() / 2) + (inputCalibrationMeterHz() * 8);
         ++tick)
    {
        controller.onMeterTick();
    }

    CHECK(workflow.measurement_start_count == 1);
    CHECK(workflow.succeeded_gain_db == std::optional{12.0});
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->input_gain_db == Catch::Approx(12.0));
    CHECK(view.last_state->status_text == "Calibration complete. Gain set to 12.0 dB.");
    CHECK(view.last_state->manual_gain_enabled);
    CHECK(view.last_state->close_button_text == "Close");
}

// Verifies dismiss is routed through the workflow and asks the host view to close.
TEST_CASE("Input calibration controller dismisses through workflow", "[core][input-calibration]")
{
    FakeInputCalibrationWorkflow workflow;
    FakeLiveInput live_input;
    FakeInputCalibrationView view;
    InputCalibrationController controller{
        workflow,
        &live_input,
        InputCalibrationPrompt{.input_gain_db = 0.0},
    };
    controller.attachView(view);

    controller.onDismissRequested();

    CHECK(workflow.dismissed_count == 1);
    CHECK(view.close_count == 1);
}

} // namespace rock_hero::editor::core
