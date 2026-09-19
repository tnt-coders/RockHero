#include "input_calibration/input_calibration_text.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace rock_hero::editor::core
{

// Each signal-chain calibration status maps to a fixed English disabled message. This pins the
// editor English the text module owns after the calibration workflow moved to common/audio.
TEST_CASE(
    "Input calibration text maps each status to its disabled message", "[core][input-calibration]")
{
    CHECK(inputCalibrationDisabledMessageFor(InputCalibrationStatus::Calibrated).empty());
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::NoActiveInputDevice) ==
        "Live input disabled: no audio input device selected.");
    // The panel carries no calibrate control of its own, so its message names where calibration
    // lives instead. Both uncalibrated statuses say the same thing here: the distinction between
    // them is only actionable inside the settings window.
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::MissingCalibration) ==
        "Live input disabled: input calibration required. Calibrate the input in Audio Device "
        "Settings.");
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::CalibrationRouteMismatch) ==
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::MissingCalibration));
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::Unavailable) ==
        "Live input disabled: live input backend unavailable.");
}

} // namespace rock_hero::editor::core
