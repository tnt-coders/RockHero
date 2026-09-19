#include "input_calibration/input_calibration_text.h"

namespace rock_hero::editor::core
{

std::string inputCalibrationDisabledMessageFor(InputCalibrationStatus status)
{
    switch (status)
    {
        case InputCalibrationStatus::NoActiveInputDevice:
        {
            return "Live input disabled: no audio input device selected.";
        }
        case InputCalibrationStatus::MissingCalibration:
        case InputCalibrationStatus::CalibrationRouteMismatch:
        {
            // One message for both: the route in front of the user needs calibrating either way,
            // and only the settings window, where calibration is reached, draws the distinction.
            // The panel is a message and nothing else, so the message names where that window is.
            return "Live input disabled: input calibration required. Calibrate the input in Audio "
                   "Device Settings.";
        }
        case InputCalibrationStatus::Calibrated:
        {
            return {};
        }
        case InputCalibrationStatus::Unavailable:
        {
            return "Live input disabled: live input backend unavailable.";
        }
    }

    return "Live input disabled.";
}

} // namespace rock_hero::editor::core
