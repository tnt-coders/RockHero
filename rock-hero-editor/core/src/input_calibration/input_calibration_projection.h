/*!
\file input_calibration_projection.h
\brief Editor projection of the shared live-input monitor into signal-chain view state.
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

/*! \brief Editor projection derived from the shared live-input monitor. */
struct InputCalibrationProjection
{
    /*! \brief Calibration status shown by the signal-chain panel. */
    InputCalibrationStatus status{InputCalibrationStatus::NoActiveInputDevice};

    /*! \brief True when the user may open the calibration prompt. */
    bool calibrate_enabled{false};

    /*! \brief True when audio-device settings may be opened. */
    bool audio_device_settings_enabled{true};

    /*! \brief Disabled-state message shown by the signal-chain panel. */
    std::string disabled_message;

    /*! \brief Prompt request shown by the editor view, if calibration UI should be visible. */
    std::optional<InputCalibrationPrompt> prompt;
};

/*!
\brief Reports the route's calibration status, every fact from the monitor's last gate run.

Independent of whether the session allows monitoring right now, so the signal chain keeps
showing a route's calibration while no project is open or the settings window is up.
\param monitor Shared live-input monitoring service driven by the controller.
\return Signal-chain calibration status for the current route.
*/
[[nodiscard]] InputCalibrationStatus inputCalibrationStatusFor(
    const common::audio::LiveInputMonitor& monitor);

/*!
\brief Builds the editor calibration projection from the monitor and the editor's own windows.
\param monitor Shared live-input monitoring service driven by the controller.
\param prompt_open True while the calibration prompt is open.
\param settings_open True while the audio-device settings window stages a route.
\return Projection consumed by the signal-chain panel and action-condition gate.
*/
[[nodiscard]] InputCalibrationProjection makeInputCalibrationProjection(
    const common::audio::LiveInputMonitor& monitor, bool prompt_open, bool settings_open);

} // namespace rock_hero::editor::core
