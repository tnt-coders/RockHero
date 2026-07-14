/*!
\file input_calibration_text.h
\brief English disabled-state strings for the signal-chain input-calibration status.
*/

#pragma once

#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Returns the English disabled message for a signal-chain calibration status.
\param status Calibration status shown by the signal-chain panel.
\return Disabled message, empty when the route is calibrated and usable.
*/
[[nodiscard]] std::string inputCalibrationDisabledMessageFor(InputCalibrationStatus status);

} // namespace rock_hero::editor::core
