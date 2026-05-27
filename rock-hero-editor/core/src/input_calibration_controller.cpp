#include "input_calibration_controller.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

constexpr int g_input_calibration_measurement_seconds{8};
constexpr int g_input_calibration_wait_seconds{10};
constexpr int g_input_calibration_sample_count{
    inputCalibrationMeterHz() * g_input_calibration_measurement_seconds
};
constexpr int g_input_calibration_wait_sample_count{
    inputCalibrationMeterHz() * g_input_calibration_wait_seconds
};
// Discard the first half-second so backend gain resets and meter windows settle before capture.
constexpr int g_input_calibration_settle_sample_count{inputCalibrationMeterHz() / 2};

// Keeps tiny negative values that display at one decimal from showing up as "-0.0 dB".
[[nodiscard]] double canonicalInputGainDb(double gain_db)
{
    const double rounded_tenths = std::round(gain_db * 10.0);
    return rounded_tenths == 0.0 ? 0.0 : gain_db;
}

[[nodiscard]] std::string decimalText(double value, int precision)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

[[nodiscard]] std::string inputCalibrationTargetText()
{
    return std::string{"Target: "} + decimalText(common::audio::inputCalibrationTargetRmsDb(), 0) +
           " dBFS average, " + decimalText(common::audio::inputCalibrationTargetPeakDb(), 0) +
           " dBFS peak";
}

[[nodiscard]] std::string inputCalibrationReadyText()
{
    return "Click \"Calibrate\" to run automatic calibration, or adjust gain manually and click "
           "\"Apply\".";
}

[[nodiscard]] std::string inputCalibrationWaitingText()
{
    return "Waiting for input... Strum all open strings at a steady, moderate volume.";
}

[[nodiscard]] std::string inputCalibrationMeasuringText()
{
    return "Keep strumming all open strings at a steady, moderate volume.";
}

[[nodiscard]] std::string inputCalibrationCompleteText(double gain_db)
{
    return std::string{"Calibration complete. Gain set to "} +
           decimalText(canonicalInputGainDb(gain_db), 1) + " dB.";
}

[[nodiscard]] std::string inputManualCalibrationCompleteText(double gain_db)
{
    return std::string{"Manual calibration saved. Gain set to "} +
           decimalText(canonicalInputGainDb(gain_db), 1) + " dB.";
}

} // namespace

// Seeds the headless popup controller with the current committed calibration gain.
InputCalibrationController::InputCalibrationController(
    IInputCalibrationWorkflow& workflow, const common::audio::ILiveInput* live_input,
    InputCalibrationPrompt prompt)
    : m_workflow(workflow)
    , m_live_input(live_input)
    , m_capture(
          g_input_calibration_settle_sample_count, g_input_calibration_wait_sample_count,
          g_input_calibration_sample_count)
    , m_input_gain_db(canonicalInputGainDb(prompt.input_gain_db))
    , m_committed_input_gain_db(m_input_gain_db)
    , m_measurement_restore_gain_db(m_input_gain_db)
{
    m_state.target_text = inputCalibrationTargetText();
    m_state.status_text = inputCalibrationReadyText();
    m_state.input_gain_db = m_input_gain_db;
}

// Connects the controller to its concrete view after both objects have been created.
void InputCalibrationController::attachView(IInputCalibrationView& view)
{
    m_view = &view;
    updateView();
}

// Starts automatic calibration after the editor workflow has prepared the raw route.
void InputCalibrationController::onAutomaticCalibrationRequested()
{
    if (m_live_input == nullptr)
    {
        setStatusText("Live input is unavailable.");
        m_state.calibrate_enabled = true;
        m_state.manual_gain_enabled = true;
        m_state.manual_apply_enabled = true;
        updateView();
        return;
    }

    auto started = m_workflow.onInputCalibrationMeasurementStarted();
    if (!started.has_value())
    {
        setStatusText(started.error().message);
        m_state.calibrate_enabled = true;
        m_state.manual_gain_enabled = true;
        m_state.manual_apply_enabled = true;
        updateView();
        return;
    }

    m_measurement_restore_gain_db = m_committed_input_gain_db;
    setDisplayedInputGain(common::audio::defaultGainDb());
    m_capture.start();
    m_last_capture_phase = m_capture.phase();
    // rawInputMeterLevel() clears the Tracktion meter reader, so this primes retry runs after the
    // editor workflow has reset input gain and rebuilt the calibration route.
    (void)m_live_input->rawInputMeterLevel();
    m_state.calibrate_enabled = false;
    m_state.manual_gain_enabled = false;
    m_state.manual_apply_enabled = false;
    m_state.close_button_text = "Dismiss";
    setStatusText(inputCalibrationWaitingText());
    updateView();
}

// Updates manual preview gain only while no automatic capture is active.
void InputCalibrationController::onManualGainChanged(double gain_db)
{
    if (m_capture.active())
    {
        return;
    }

    m_input_gain_db = canonicalInputGainDb(gain_db);
    m_state.input_gain_db = m_input_gain_db;
    refreshInputMeter();
    updateView();
}

// Applies a knowledgeable user's manually entered calibration value.
void InputCalibrationController::onManualCalibrationRequested()
{
    if (m_capture.active())
    {
        return;
    }

    m_input_gain_db = canonicalInputGainDb(m_input_gain_db);
    m_state.input_gain_db = m_input_gain_db;
    auto applied = m_workflow.onInputCalibrationManuallySet(m_input_gain_db);
    if (!applied.has_value())
    {
        setStatusText(applied.error().message);
        m_state.calibrate_enabled = true;
        m_state.manual_gain_enabled = true;
        m_state.manual_apply_enabled = true;
        updateView();
        return;
    }

    setStatusText(inputManualCalibrationCompleteText(m_input_gain_db));
    m_committed_input_gain_db = m_input_gain_db;
    m_measurement_restore_gain_db = m_input_gain_db;
    m_state.calibrate_enabled = true;
    m_state.manual_gain_enabled = true;
    m_state.manual_apply_enabled = true;
    m_state.close_button_text = "Close";
    updateView();
}

// Samples the live input meter and advances automatic capture when one is active.
void InputCalibrationController::onMeterTick()
{
    common::audio::AudioMeterLevel level{};
    if (m_live_input != nullptr)
    {
        level = m_live_input->rawInputMeterLevel();
    }
    m_state.input_level = displayLevel(level);

    if (!m_capture.active())
    {
        updateView();
        return;
    }

    const common::audio::InputCalibrationCaptureUpdate update = m_capture.pushSample(level);
    if (update.phase == common::audio::InputCalibrationCapturePhase::Measuring &&
        m_last_capture_phase != common::audio::InputCalibrationCapturePhase::Measuring)
    {
        setStatusText(inputCalibrationMeasuringText());
    }
    m_last_capture_phase = update.phase;
    if (update.error.has_value())
    {
        finishMeasurementError(update.error->message);
        return;
    }
    if (update.result.has_value())
    {
        finishMeasurementSuccess(*update.result, level);
        return;
    }

    updateView();
}

// Closes the workflow and asks the host window to disappear immediately.
void InputCalibrationController::onDismissRequested()
{
    m_workflow.onInputCalibrationDismissed();
    if (m_view != nullptr)
    {
        m_view->requestClose();
    }
}

// Pushes the latest derived state to the attached view.
void InputCalibrationController::updateView()
{
    if (m_view != nullptr)
    {
        m_view->setState(m_state);
    }
}

// Samples the current raw input meter for manual-gain preview.
void InputCalibrationController::refreshInputMeter()
{
    if (m_live_input != nullptr)
    {
        m_state.input_level = displayLevel(m_live_input->rawInputMeterLevel());
    }
}

// Updates the displayed gain consistently for automatic and manual flows.
void InputCalibrationController::setDisplayedInputGain(double gain_db)
{
    m_input_gain_db = canonicalInputGainDb(gain_db);
    m_state.input_gain_db = m_input_gain_db;
}

// Stores a new status message without forcing the view to update immediately.
void InputCalibrationController::setStatusText(std::string text)
{
    m_state.status_text = std::move(text);
}

// Applies a successful automatic capture and mirrors the result in the manual gain control.
void InputCalibrationController::finishMeasurementSuccess(
    const common::audio::InputCalibrationResult& result, common::audio::AudioMeterLevel level)
{
    setDisplayedInputGain(result.calibration_gain.db);
    m_state.input_level = displayLevel(level);

    auto applied = m_workflow.onInputCalibrationSucceeded(m_input_gain_db);
    if (!applied.has_value())
    {
        finishMeasurementError(applied.error().message);
        return;
    }

    setStatusText(inputCalibrationCompleteText(m_input_gain_db));
    m_capture.reset();
    m_last_capture_phase = m_capture.phase();
    m_committed_input_gain_db = m_input_gain_db;
    m_measurement_restore_gain_db = m_input_gain_db;
    m_state.calibrate_enabled = true;
    m_state.manual_gain_enabled = true;
    m_state.manual_apply_enabled = true;
    m_state.close_button_text = "Close";
    updateView();
}

// Stops backend calibration monitoring and leaves the popup open for another attempt.
void InputCalibrationController::finishMeasurementError(std::string message)
{
    m_workflow.onInputCalibrationMeasurementCancelled();
    m_capture.reset();
    m_last_capture_phase = m_capture.phase();
    setDisplayedInputGain(m_measurement_restore_gain_db);
    refreshInputMeter();
    setStatusText(std::move(message));
    m_state.calibrate_enabled = true;
    m_state.manual_gain_enabled = true;
    m_state.manual_apply_enabled = true;
    updateView();
}

// Applies the currently displayed preview gain to the raw meter level for presentation.
common::audio::AudioMeterLevel InputCalibrationController::displayLevel(
    common::audio::AudioMeterLevel level) const
{
    if (level.peak_db <= common::audio::minimumAudioMeterDb())
    {
        return level;
    }

    level.peak_db =
        std::clamp(level.peak_db + m_input_gain_db, common::audio::minimumAudioMeterDb(), 12.0);
    level.clipping = level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb();
    return level;
}

} // namespace rock_hero::editor::core
