#include "input_calibration/input_calibration_controller.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Keeps tiny negative values that display at one decimal from showing up as "-0.0 dB".
[[nodiscard]] double canonicalInputGainDb(double gain_db)
{
    const double rounded_tenths = std::round(gain_db * 10.0);
    return std::is_eq(rounded_tenths <=> 0.0) ? 0.0 : gain_db;
}

// Applies the preview gain to raw meter samples so the popup reflects the candidate calibration.
[[nodiscard]] common::audio::AudioMeterLevel applyDisplayGain(
    common::audio::AudioMeterLevel level, double gain_db)
{
    if (level.peak_db <= common::audio::minimumAudioMeterDb())
    {
        return level;
    }

    level.peak_db = std::clamp(level.peak_db + gain_db, common::audio::minimumAudioMeterDb(), 12.0);
    level.clipping = level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb();
    return level;
}

// Status text for the idle popup state, shared by startup and retry paths.
[[nodiscard]] std::string inputCalibrationReadyText()
{
    return "Click \"Calibrate\" to run automatic calibration, or adjust gain manually and click "
           "\"Apply\".";
}

// Status text shown while the capture is waiting for the first usable signal.
[[nodiscard]] std::string inputCalibrationWaitingText()
{
    return "Waiting for input... Strum all open strings at a steady, moderate volume.";
}

// Status text shown during the fixed active measurement window.
[[nodiscard]] std::string inputCalibrationMeasuringText()
{
    return "Keep strumming all open strings at a steady, moderate volume.";
}

// Formats a one-decimal gain value without depending on JUCE formatting in editor core. Rounds to
// the displayed tenth (matching the old juce::String{value, 1} behavior) before truncating the
// fractional tail, so values such as 2.06 dB show as "2.1" rather than "2.0".
[[nodiscard]] std::string gainText(double gain_db)
{
    const double rounded_gain_db = canonicalInputGainDb(std::round(gain_db * 10.0) / 10.0);
    const std::string text = std::to_string(rounded_gain_db);
    const std::size_t dot = text.find('.');
    if (dot == std::string::npos)
    {
        return text + ".0";
    }

    return text.substr(0, dot + 2);
}

// Status text shown after automatic calibration has been committed by the host.
[[nodiscard]] std::string inputCalibrationCompleteText(double gain_db)
{
    return "Calibration complete. Gain set to " + gainText(gain_db) + " dB.";
}

// Status text shown after manual calibration has been committed by the host.
[[nodiscard]] std::string inputManualCalibrationCompleteText(double gain_db)
{
    return "Manual calibration saved. Gain set to " + gainText(gain_db) + " dB.";
}

// Status text shown when docs are unavailable from both install and build-tree locations.
[[nodiscard]] std::string inputCalibrationDocumentationUnavailableText()
{
    return "Input calibration guide is unavailable. Build the docs target and try again.";
}

} // namespace

// Seeds popup state from the prompt produced by InputCalibrationWorkflow.
InputCalibrationController::InputCalibrationController(
    Host& host, const InputCalibrationPrompt& prompt, CaptureSettings capture_settings)
    : m_host(host)
    , m_capture(
          capture_settings.settle_sample_count, capture_settings.wait_sample_count,
          capture_settings.measurement_sample_count)
{
    const double initial_gain_db = canonicalInputGainDb(prompt.input_gain_db);
    m_state.input_gain_db = initial_gain_db;
    m_state.status_message = inputCalibrationReadyText();
    m_committed_input_gain_db = initial_gain_db;
    m_measurement_restore_gain_db = initial_gain_db;
    setDisplayedInputGain(initial_gain_db);
}

// Attaches a view after construction or replacement and synchronizes it immediately.
void InputCalibrationController::attachView(IInputCalibrationView& view)
{
    m_view = &view;
    publishState();
}

// Clears the raw view pointer only when the currently attached view is leaving.
void InputCalibrationController::detachView(IInputCalibrationView& view) noexcept
{
    if (m_view == &view)
    {
        m_view = nullptr;
    }
}

// Updates the preview gain while no automatic capture owns the route.
void InputCalibrationController::onManualGainChanged(double gain_db)
{
    if (m_capture.active())
    {
        return;
    }

    setDisplayedInputGain(gain_db);
    publishState();
}

// Applies the currently displayed manual gain through the editor-runtime host.
void InputCalibrationController::onManualApplyRequested()
{
    if (m_capture.active())
    {
        return;
    }

    const auto applied = m_host.applyManualInputCalibration(m_state.input_gain_db);
    if (!applied.has_value())
    {
        m_state.status_message = applied.error().message;
        m_state.start_measurement_enabled = true;
        m_state.manual_gain_controls_enabled = true;
        publishState();
        return;
    }

    m_state.status_message = inputManualCalibrationCompleteText(m_state.input_gain_db);
    m_committed_input_gain_db = m_state.input_gain_db;
    m_measurement_restore_gain_db = m_state.input_gain_db;
    m_state.start_measurement_enabled = true;
    m_state.manual_gain_controls_enabled = true;
    m_state.dismiss_button_text = "Close";
    publishState();
}

// Starts automatic capture only after the editor host has rebuilt the live-input route.
bool InputCalibrationController::onMeasurementStartRequested()
{
    if (m_capture.active())
    {
        return false;
    }

    const auto started = m_host.startInputCalibrationMeasurement();
    if (!started.has_value())
    {
        m_state.status_message = started.error().message;
        m_state.start_measurement_enabled = true;
        m_state.manual_gain_controls_enabled = true;
        publishState();
        return false;
    }

    m_measurement_restore_gain_db = m_committed_input_gain_db;
    setDisplayedInputGain(common::audio::defaultGainDb());
    m_capture.start();
    m_last_capture_phase = m_capture.phase();
    m_state.start_measurement_enabled = false;
    m_state.manual_gain_controls_enabled = false;
    m_state.dismiss_button_text = "Dismiss";
    m_state.status_message = inputCalibrationWaitingText();
    publishState();
    return true;
}

// Updates display metering and feeds samples to the capture state machine when active.
void InputCalibrationController::onMeterSampled(common::audio::AudioMeterLevel raw_level)
{
    m_last_raw_meter_level = raw_level;
    m_state.input_meter_level = applyDisplayGain(raw_level, m_state.input_gain_db);

    if (!m_capture.active())
    {
        publishState();
        return;
    }

    const common::audio::InputCalibrationCaptureUpdate update = m_capture.pushSample(raw_level);
    if (update.phase == common::audio::InputCalibrationCapturePhase::Measuring &&
        m_last_capture_phase != common::audio::InputCalibrationCapturePhase::Measuring)
    {
        m_state.status_message = inputCalibrationMeasuringText();
    }
    m_last_capture_phase = update.phase;

    if (update.error.has_value())
    {
        finishMeasurementError(update.error->message);
        return;
    }

    if (update.result.has_value())
    {
        finishMeasurementSuccess(*update.result, raw_level);
        return;
    }

    publishState();
}

// Keeps missing meter-source failure text in the popup state rather than in JUCE code.
void InputCalibrationController::onMeterSourceUnavailable()
{
    if (m_capture.active())
    {
        finishMeasurementError("Live input is unavailable.");
        return;
    }

    m_state.status_message = "Live input is unavailable.";
    m_state.start_measurement_enabled = true;
    m_state.manual_gain_controls_enabled = true;
    publishState();
}

// Reports a failed help request without coupling the core controller to filesystem lookup.
void InputCalibrationController::onDocumentationUnavailable()
{
    m_state.status_message = inputCalibrationDocumentationUnavailableText();
    publishState();
}

// Forwards dismissal to the host, which owns any active measurement restoration.
void InputCalibrationController::onDismissRequested()
{
    m_capture.reset();
    m_last_capture_phase = m_capture.phase();
    m_host.dismissInputCalibration();
}

// Updates the gain preview and recomputes the display meter from the last sampled raw level.
void InputCalibrationController::setDisplayedInputGain(double gain_db)
{
    m_state.input_gain_db = canonicalInputGainDb(gain_db);
    m_state.input_meter_level = applyDisplayGain(m_last_raw_meter_level, m_state.input_gain_db);
}

// Commits an automatic capture through the host and moves the popup into its completed state.
void InputCalibrationController::finishMeasurementSuccess(
    const common::audio::InputCalibrationResult& result, common::audio::AudioMeterLevel raw_level)
{
    m_last_raw_meter_level = raw_level;
    setDisplayedInputGain(result.calibration_gain.db);

    const auto applied = m_host.applyAutomaticInputCalibration(m_state.input_gain_db);
    if (!applied.has_value())
    {
        finishMeasurementError(applied.error().message);
        return;
    }

    m_capture.reset();
    m_last_capture_phase = m_capture.phase();
    m_state.status_message = inputCalibrationCompleteText(m_state.input_gain_db);
    m_committed_input_gain_db = m_state.input_gain_db;
    m_measurement_restore_gain_db = m_state.input_gain_db;
    m_state.start_measurement_enabled = true;
    m_state.manual_gain_controls_enabled = true;
    m_state.dismiss_button_text = "Close";
    publishState();
}

// Cancels backend measurement state and restores the popup to the last committed display gain.
void InputCalibrationController::finishMeasurementError(std::string message)
{
    m_host.cancelInputCalibrationMeasurement();
    m_capture.reset();
    m_last_capture_phase = m_capture.phase();
    setDisplayedInputGain(m_measurement_restore_gain_db);
    m_state.status_message = std::move(message);
    m_state.start_measurement_enabled = true;
    m_state.manual_gain_controls_enabled = true;
    m_state.dismiss_button_text = "Dismiss";
    publishState();
}

// Pushes the cached state to the attached view if one is present.
void InputCalibrationController::publishState()
{
    if (m_view != nullptr)
    {
        m_view->setState(m_state);
    }
}

} // namespace rock_hero::editor::core
