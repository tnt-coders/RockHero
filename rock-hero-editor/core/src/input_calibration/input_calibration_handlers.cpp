#include "controller/editor_controller_impl.h"

#include <expected>
#include <optional>
#include <utility>

namespace rock_hero::editor::core
{

// Builds the two-bool session context the shared live-input monitor gate evaluates. The monitor
// samples the current input route identity itself, so the driver supplies only session facts.
common::audio::LiveInputMonitoringContext EditorController::Impl::monitoringContext() const
{
    return common::audio::LiveInputMonitoringContext{
        .session_audio_ready = m_project_audio_ready,
        .arrangement_loaded = hasLoadedArrangement(),
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
