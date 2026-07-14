/*!
\file input_calibration_projection.h
\brief Editor projection of the shared input-calibration workflow into signal-chain view state.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Editor projection derived from the shared input-calibration workflow. */
struct InputCalibrationProjection
{
    /*! \brief Calibration status shown by the signal-chain panel. */
    InputCalibrationStatus status{InputCalibrationStatus::NoActiveInputDevice};

    /*! \brief True when the user may open the calibration prompt. */
    bool calibrate_enabled{false};

    /*! \brief True when the current route may be auditioned through the live chain. */
    bool live_input_audition_available{false};

    /*! \brief True when audio-device settings may be opened. */
    bool audio_device_settings_enabled{true};

    /*! \brief Disabled-state message shown by the signal-chain panel. */
    std::string disabled_message;

    /*! \brief Prompt request shown by the editor view, if calibration UI should be visible. */
    std::optional<InputCalibrationPrompt> prompt;
};

/*!
\brief Maps a monitoring reason and backend availability to the signal-chain calibration status.

Restores the backend-availability distinction the pure workflow drops: an active route reports
\ref InputCalibrationStatus::Calibrated only when the backend accepted it, otherwise
\ref InputCalibrationStatus::Unavailable.
\param reason Monitoring reason reflecting the current route and stored calibration.
\param backend_available True when the matching calibrated route is live-input available.
\return Signal-chain calibration status for the supplied reason.
*/
[[nodiscard]] InputCalibrationStatus inputCalibrationStatusFor(
    common::audio::MonitoringDisabledReason reason, bool backend_available);

/*!
\brief Builds the editor calibration projection from the shared live-input monitor's read surface.
\param monitor Shared live-input monitoring service driven by the controller.
\param context Current session facts used to project availability.
\return Projection consumed by the signal-chain panel and action-condition gate.
*/
[[nodiscard]] InputCalibrationProjection makeInputCalibrationProjection(
    const common::audio::LiveInputMonitor& monitor,
    common::audio::LiveInputMonitoringContext context);

} // namespace rock_hero::editor::core
