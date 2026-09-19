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
        {
            return "Live input disabled: input calibration required.";
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
