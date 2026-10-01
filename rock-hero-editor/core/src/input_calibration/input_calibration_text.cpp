#include "input_calibration/input_calibration_text.h"

namespace rock_hero::editor::core
{

std::string inputCalibrationDisabledMessageFor(InputCalibrationStatus status)
{
    switch (status)
    {
        case InputCalibrationStatus::NoActiveInputDevice:
        {
            return "No audio input device.";
        }
        case InputCalibrationStatus::MissingCalibration:
        {
            return "Input calibration required.";
        }
        case InputCalibrationStatus::Calibrated:
        {
            return {};
        }
        case InputCalibrationStatus::Unavailable:
        {
            return "Live input backend unavailable.";
        }
    }

    return "Live input disabled.";
}

} // namespace rock_hero::editor::core
