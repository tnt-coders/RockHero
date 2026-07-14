/*!
\file input_calibration_text.h
\brief Editor projection of the shared input-calibration workflow into signal-chain view state.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/input_calibration_workflow.h>
#include <rock_hero/common/audio/input/live_input_monitoring_status.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Editor view slice derived from the shared input-calibration workflow. */
struct InputCalibrationViewSlice
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
\brief Returns the English disabled message for a signal-chain calibration status.
\param status Calibration status shown by the signal-chain panel.
\return Disabled message, empty when the route is calibrated and usable.
*/
[[nodiscard]] std::string inputCalibrationDisabledMessageFor(InputCalibrationStatus status);

/*!
\brief Builds the editor calibration view slice from the workflow's pure getters.
\param workflow Shared input-calibration workflow owned by the controller.
\param context Current controller facts used to project availability.
\return View slice consumed by the signal-chain panel and action-condition gate.
*/
[[nodiscard]] InputCalibrationViewSlice makeInputCalibrationViewState(
    const common::audio::InputCalibrationWorkflow& workflow,
    const common::audio::InputCalibrationWorkflow::Context& context);

} // namespace rock_hero::editor::core
