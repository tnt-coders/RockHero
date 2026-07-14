#include "controller/editor_controller_impl.h"

#include <expected>
#include <optional>
#include <utility>

namespace rock_hero::editor::core
{

// Builds the two-bool context the shared live-input monitor evaluates. live_input_ready carries
// its documented meaning — the raw live input path is up (an open device with an input route), so
// a signal can be measured — matching the game's device-applied semantics; calibration therefore
// needs no loaded project. Session readiness gates only active processed monitoring, so it rides
// on arrangement_loaded: an arrangement counts as loaded here only once its audio and live-rig
// restore have committed (m_project_audio_ready), which keeps the live-tone gate exactly as strict
// as before.
common::audio::LiveInputMonitoringContext EditorController::Impl::monitoringContext() const
{
    return common::audio::LiveInputMonitoringContext{
        .live_input_ready = m_audio_devices.currentInputDeviceIdentity().has_value(),
        // The Tone Designer's resting rig is session-ready the moment it stands up: monitoring
        // through it is the designer's whole purpose, and no project audio needs preparing.
        .arrangement_loaded =
            (m_project_audio_ready && hasLoadedArrangement()) || m_tone_designer.active,
    };
}

// Opens the required calibration prompt on explicit user request.
void EditorController::Impl::onInputCalibrationRequested()
{
    if (m_live_input_monitor.requestPrompt(monitoringContext()) && m_transport.state().playing)
    {
        m_transport.pause();
    }
    updateView();
}

// Prepares the current input route for a raw calibration measurement.
std::expected<void, common::audio::LiveInputMonitorError> EditorController::Impl::
    onInputCalibrationMeasurementStarted()
{
    auto started = m_live_input_monitor.beginMeasurement(monitoringContext());
    updateView();
    return started;
}

// Stops an in-progress measurement without closing the calibration prompt.
void EditorController::Impl::onInputCalibrationMeasurementCancelled()
{
    const auto restored = m_live_input_monitor.cancelMeasurement();
    if (!restored.has_value())
    {
        reportError(restored.error().message);
    }
    updateView();
}

// Applies a successful calibration gain, persists it, and enables processed monitoring.
std::expected<void, common::audio::LiveInputMonitorError> EditorController::Impl::
    onInputCalibrationSucceeded(double gain_db)
{
    auto committed = m_live_input_monitor.commitCalibration(gain_db, std::nullopt);
    updateView();
    return committed;
}

// Applies a user-entered calibration gain without requiring an active automatic attempt.
std::expected<void, common::audio::LiveInputMonitorError> EditorController::Impl::
    onInputCalibrationManuallySet(double gain_db)
{
    auto committed = m_live_input_monitor.setManualCalibration(gain_db);
    updateView();
    return committed;
}

// Closes the calibration prompt without enabling uncalibrated live monitoring.
void EditorController::Impl::onInputCalibrationDismissed()
{
    const auto restored = m_live_input_monitor.cancelMeasurement();
    if (!restored.has_value())
    {
        reportError(restored.error().message);
    }
    m_live_input_monitor.closePrompt();
    updateView();
}

} // namespace rock_hero::editor::core
