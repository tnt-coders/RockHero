#include "controller/editor_controller_impl.h"

#include <expected>
#include <optional>
#include <variant>

namespace rock_hero::editor::core
{

// Processed monitoring runs once arrangement audio and the live rig have committed
// (m_project_audio_ready), or the Tone Designer's resting rig is up -- monitoring through it is
// the designer's whole purpose -- and never while the audio-device settings window holds the route.
common::audio::LiveInputMonitoringContext EditorController::Impl::monitoringContext() const
{
    return common::audio::LiveInputMonitoringContext{
        .session_ready =
            ((m_project_audio_ready && hasLoadedArrangement()) || m_tone_designer.active) &&
            !m_audio_device_settings_open,
    };
}

// Marks the project's audio not ready and takes live input off before the rig is replaced. The
// gate is off whatever monitoringContext() says, since a resting Tone Designer rig would otherwise
// keep it on while the rig under it is torn down.
void EditorController::Impl::beginRigReplacement()
{
    m_project_audio_ready = false;
    m_live_input_monitor.refresh(common::audio::LiveInputMonitoringContext{.session_ready = false});
}

// The one projection of the monitor and the editor's own calibration and settings windows, read by
// the view derivation, the action gates and the handlers alike.
InputCalibrationProjection EditorController::Impl::inputCalibrationProjection() const
{
    return makeInputCalibrationProjection(
        m_live_input_monitor, m_calibration_prompt_route, m_audio_device_settings_open);
}

// The one live-input edge: re-runs the gate, closes a prompt whose route is gone, and offers
// calibration once per uncalibrated route. Every refresh that can change the route or its
// calibration goes through here.
void EditorController::Impl::refreshLiveInput()
{
    m_live_input_monitor.refresh(monitoringContext());
    const std::optional<common::audio::InputDeviceIdentity>& route = m_live_input_monitor.route();
    if (m_calibration_prompt_route != route)
    {
        m_calibration_prompt_route.reset();
    }

    const InputCalibrationProjection projection = inputCalibrationProjection();
    if (projection.status == InputCalibrationStatus::MissingCalibration &&
        projection.calibrate_enabled && m_calibration_offered_route != route)
    {
        m_calibration_offered_route = route;
        openCalibrationPrompt();
    }
}

// Opens the calibration prompt for the current route, pausing playback under it.
void EditorController::Impl::openCalibrationPrompt()
{
    m_calibration_prompt_route = m_live_input_monitor.route();
    if (m_transport.state().playing)
    {
        m_transport.pause();
    }
}

// Opens the calibration prompt for the current route on explicit user request, after a Cancel
// too. The gate re-reads the store first, so the prompt opens on a gain the other
// product may have saved since.
void EditorController::Impl::onInputCalibrationRequested()
{
    refreshLiveInput();
    if (inputCalibrationProjection().calibrate_enabled)
    {
        openCalibrationPrompt();
    }
    updateView();
}

// Hands the current input route to a raw calibration measurement.
std::expected<void, common::audio::LiveInputMonitorError> EditorController::Impl::
    onInputCalibrationMeasurementStarted(common::audio::PickupClass pickups)
{
    auto started = m_live_input_monitor.beginMeasurement(pickups, monitoringContext());
    updateView();
    return started;
}

// Reads the raw input for the prompt. A measurement that ended here changed what the gate did,
// so the view is re-derived; a running one changed nothing outside the prompt.
common::audio::LiveInputSample EditorController::Impl::onInputCalibrationSampled()
{
    common::audio::LiveInputSample sample = m_live_input_monitor.sample(monitoringContext());
    if (sample.measurement.has_value() &&
        !std::holds_alternative<common::audio::InputCalibrationRunning>(*sample.measurement))
    {
        updateView();
    }
    return sample;
}

// Stores the prompt's gain, typed or measured, for the route the prompt was opened for: a device
// change since then cannot file the gain under the wrong route.
std::expected<void, common::audio::LiveInputMonitorError> EditorController::Impl::
    onInputCalibrationApplied(double gain_db)
{
    const std::optional<common::audio::InputDeviceIdentity> route = m_calibration_prompt_route;
    if (!route.has_value())
    {
        return std::unexpected{common::audio::LiveInputMonitorError{
            common::audio::LiveInputMonitorErrorCode::InvalidRequest,
            "No input calibration prompt is open.",
        }};
    }

    auto committed = m_live_input_monitor.commitCalibration(
        *route, common::audio::Gain{gain_db}, monitoringContext());
    if (committed.has_value())
    {
        // A stored gain is what the prompt was for, so it ends here.
        m_calibration_prompt_route.reset();
    }
    updateView();
    return committed;
}

// Closes the prompt without storing (Cancel or the window's close button), ending any measurement;
// live input stays off unless the route is calibrated.
void EditorController::Impl::onInputCalibrationClosed()
{
    m_live_input_monitor.cancelMeasurement(monitoringContext());
    m_calibration_prompt_route.reset();
    updateView();
}

// Stops a measurement the player gave up on; the prompt stays open for a retry.
void EditorController::Impl::onInputCalibrationMeasurementStopped()
{
    m_live_input_monitor.cancelMeasurement(monitoringContext());
    updateView();
}

} // namespace rock_hero::editor::core
