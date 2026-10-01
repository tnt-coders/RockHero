#include "input_calibration/input_calibration_controller.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <string>
#include <utility>
#include <variant>

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

// Status text for an uncalibrated route's prompt, which says what stays off until it is calibrated.
[[nodiscard]] std::string inputCalibrationUncalibratedText()
{
    return "Live input stays off until this input is calibrated.";
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
// the displayed tenth (matching juce::String{value, 1}) before truncating the fractional tail, so
// values such as 2.06 dB show as "2.1" rather than "2.0".
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

// Seeds popup state from the prompt the editor controller projects.
InputCalibrationController::InputCalibrationController(
    Host& host, const InputCalibrationPrompt& prompt)
    : m_host(host)
    , m_committed_input_gain_db(prompt.stored_gain_db)
{
    m_state.status_message = m_committed_input_gain_db.has_value()
                                 ? inputCalibrationReadyText()
                                 : inputCalibrationUncalibratedText();
    setDisplayedInputGain(m_committed_input_gain_db.value_or(common::audio::defaultGainDb()));
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

// Updates the preview gain while no measurement holds the route.
void InputCalibrationController::onManualGainChanged(double gain_db)
{
    if (m_state.measuring)
    {
        return;
    }

    setDisplayedInputGain(gain_db);
    publishState();
}

// Applies the currently displayed manual gain through the editor-runtime host.
void InputCalibrationController::onManualApplyRequested()
{
    if (m_state.measuring)
    {
        return;
    }

    const auto applied = m_host.applyManualInputCalibration(m_state.input_gain_db);
    if (!applied.has_value())
    {
        m_state.status_message = applied.error().message;
        publishState();
        return;
    }

    m_state.status_message = inputManualCalibrationCompleteText(m_state.input_gain_db);
    m_committed_input_gain_db = m_state.input_gain_db;
    publishState();
}

// Starts an automatic measurement once the host has handed over the route.
void InputCalibrationController::onMeasurementStartRequested()
{
    if (m_state.measuring)
    {
        return;
    }

    const auto started = m_host.startInputCalibrationMeasurement();
    if (!started.has_value())
    {
        m_state.status_message = started.error().message;
        publishState();
        return;
    }

    setDisplayedInputGain(common::audio::defaultGainDb());
    m_state.measuring = true;
    m_state.status_message = inputCalibrationWaitingText();
    publishState();
}

// Shows the raw input through the previewed gain, and follows a measurement in progress.
void InputCalibrationController::onSampleTick()
{
    const common::audio::LiveInputSample sample = m_host.sampleInputCalibration();
    m_last_raw_meter_level = sample.raw_level;
    m_state.input_meter_level = applyDisplayGain(sample.raw_level, m_state.input_gain_db);
    // While measuring the monitor reports every end, so a sample without progress only meters.
    if (!m_state.measuring || !sample.measurement.has_value())
    {
        publishState();
        return;
    }

    const common::audio::InputCalibrationProgress& progress = *sample.measurement;
    if (const auto* const stage = std::get_if<common::audio::InputCalibrationStage>(&progress))
    {
        m_state.status_message = *stage == common::audio::InputCalibrationStage::Measuring
                                     ? inputCalibrationMeasuringText()
                                     : inputCalibrationWaitingText();
        publishState();
        return;
    }
    if (const auto* const committed =
            std::get_if<common::audio::InputCalibrationCommitted>(&progress))
    {
        finishMeasurementSuccess(committed->gain.db);
        return;
    }
    finishMeasurementError(std::get<common::audio::InputCalibrationFailed>(progress).message);
}

// Reports a failed help request without coupling the core controller to filesystem lookup.
void InputCalibrationController::onDocumentationUnavailable()
{
    m_state.status_message = inputCalibrationDocumentationUnavailableText();
    publishState();
}

// Forwards dismissal to the host, which ends any measurement in progress.
void InputCalibrationController::onDismissRequested()
{
    m_host.dismissInputCalibration();
}

// Updates the gain preview and recomputes the display meter from the last sampled raw level.
void InputCalibrationController::setDisplayedInputGain(double gain_db)
{
    m_state.input_gain_db = canonicalInputGainDb(gain_db);
    m_state.input_meter_level = applyDisplayGain(m_last_raw_meter_level, m_state.input_gain_db);
}

// Moves the popup into its completed state once the monitor has stored the measured gain.
void InputCalibrationController::finishMeasurementSuccess(double gain_db)
{
    setDisplayedInputGain(gain_db);
    m_state.status_message = inputCalibrationCompleteText(m_state.input_gain_db);
    m_committed_input_gain_db = m_state.input_gain_db;
    m_state.measuring = false;
    publishState();
}

// Returns the popup to the last committed gain; the monitor already handed the route back.
void InputCalibrationController::finishMeasurementError(std::string message)
{
    setDisplayedInputGain(m_committed_input_gain_db.value_or(common::audio::defaultGainDb()));
    m_state.status_message = std::move(message);
    m_state.measuring = false;
    publishState();
}

// Pushes the cached state to the attached view if one is present. The dismissal words follow the
// committed gain alone: an uncalibrated input is put off until later, a calibrated one closed.
void InputCalibrationController::publishState()
{
    m_state.dismiss_button_text = m_committed_input_gain_db.has_value() ? "Close" : "Later";
    if (m_view != nullptr)
    {
        m_view->setState(m_state);
    }
}

} // namespace rock_hero::editor::core
