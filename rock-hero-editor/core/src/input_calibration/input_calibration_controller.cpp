#include "input_calibration/input_calibration_controller.h"

#include <algorithm>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/editor/core/input_calibration/input_calibration_text.h>
#include <string>
#include <utility>
#include <variant>

namespace rock_hero::editor::core
{

namespace
{

// Applies the shown gain to a raw meter reading, so the meter previews the candidate.
[[nodiscard]] common::audio::AudioMeterLevel applyDisplayGain(
    common::audio::AudioMeterLevel level, double gain_db)
{
    if (level.peak_db <= common::audio::minimumAudioMeterDb())
    {
        return level;
    }

    level.peak_db = std::clamp(
        level.peak_db + gain_db,
        common::audio::minimumAudioMeterDb(),
        common::audio::maximumAudioMeterDb());
    level.clipping = level.clipping || level.peak_db >= common::audio::clippingAudioMeterDb();
    return level;
}

} // namespace

// Opens with the route's stored gain on the slider when it has one.
InputCalibrationController::InputCalibrationController(
    IEditorController& editor, const InputCalibrationPrompt& prompt)
    : m_editor(editor)
{
    setGain(prompt.stored_gain_db.value_or(common::audio::defaultGainDb()));
    m_state.message = idleText();
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

void InputCalibrationController::onManualGainChanged(double gain_db)
{
    if (m_state.measuring)
    {
        return;
    }

    setGain(gain_db);
    m_state.message = idleText();
    publishState();
}

// Stores the shown gain; the editor then closes the prompt. A refused store stays with the reason.
void InputCalibrationController::onApplyRequested()
{
    if (m_state.measuring)
    {
        return;
    }

    // A stored gain ends the prompt, so the editor closes it; only a refusal comes back here.
    const auto applied = m_editor.onInputCalibrationApplied(m_state.gain_db);
    if (!applied.has_value())
    {
        m_state.message = applied.error().message;
        publishState();
    }
}

void InputCalibrationController::onPickupsSelected(common::audio::PickupClass pickups)
{
    if (m_state.measuring)
    {
        return;
    }

    m_state.pickups = pickups;
    publishState();
}

// Starts a measurement once the editor has handed over the route, or stops the running one. A
// refused start says why.
void InputCalibrationController::onMeasureRequested()
{
    if (m_state.measuring)
    {
        m_editor.onInputCalibrationMeasurementStopped();
        finishMeasurement(idleText());
        return;
    }

    const auto started = m_editor.onInputCalibrationMeasurementStarted(m_state.pickups);
    if (!started.has_value())
    {
        m_state.message = started.error().message;
        publishState();
        return;
    }
    m_state.measuring = true;
    m_state.message = measuringText(common::audio::InputCalibrationWaiting{});
    publishState();
}

// Meters the input and follows a running measurement: a finished one fills the gain for Apply.
void InputCalibrationController::onSampleTick()
{
    const common::audio::LiveInputSample sample = m_editor.onInputCalibrationSampled();
    m_last_raw_meter_level = sample.raw_level;
    if (!m_state.measuring || !sample.measurement.has_value())
    {
        publishState();
        return;
    }

    const common::audio::InputCalibrationProgress& progress = *sample.measurement;
    if (const auto* const running = std::get_if<common::audio::InputCalibrationRunning>(&progress))
    {
        // Every tick rewrites the message, so a guide-missing report made mid-measurement shows
        // for a tick; only a build without its docs can make one.
        m_state.message = measuringText(*running);
        publishState();
        return;
    }
    if (const auto* const measured =
            std::get_if<common::audio::InputCalibrationMeasured>(&progress))
    {
        setGain(measured->gain.db);
        finishMeasurement(measuredText(m_state.gain_db, m_state.pickups));
        return;
    }
    finishMeasurement(std::get<common::audio::InputCalibrationFailed>(progress).message);
}

// Reports a failed help request without coupling the core controller to filesystem lookup.
void InputCalibrationController::onDocumentationUnavailable()
{
    m_state.message = guideUnavailableText();
    publishState();
}

// Closes without storing; the editor ends any measurement in progress.
void InputCalibrationController::onCloseRequested()
{
    m_editor.onInputCalibrationClosed();
}

void InputCalibrationController::setGain(double gain_db)
{
    m_state.gain_db = common::audio::quantizeInputCalibrationGainDb(gain_db);
}

void InputCalibrationController::finishMeasurement(std::string message)
{
    m_state.measuring = false;
    m_state.message = std::move(message);
    publishState();
}

// Pushes the cached state to the attached view if one is present. The meter previews the shown
// gain against the chosen pickups' peak target, except while a measurement runs: it then shows
// the raw input as the measurement hears it, with no target to play to.
void InputCalibrationController::publishState()
{
    if (m_state.measuring)
    {
        m_state.input_meter_level = m_last_raw_meter_level;
        m_state.meter_target_db.reset();
    }
    else
    {
        m_state.input_meter_level = applyDisplayGain(m_last_raw_meter_level, m_state.gain_db);
        m_state.meter_target_db = common::audio::inputCalibrationTargetPeakDb(m_state.pickups);
    }
    if (m_view != nullptr)
    {
        m_view->setState(m_state);
    }
}

} // namespace rock_hero::editor::core
