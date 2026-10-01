#include "input_calibration/input_calibration_text.h"

#include <compare>
#include <format>
#include <rock_hero/common/audio/input/input_calibration.h>

namespace rock_hero::editor::core
{

std::string signedGainText(double gain_db)
{
    const double shown_db = common::audio::quantizeInputCalibrationGainDb(gain_db);
    if (std::is_eq(shown_db <=> 0.0))
    {
        return "0.0";
    }
    return std::format("{:+.1f}", shown_db);
}

} // namespace rock_hero::editor::core
