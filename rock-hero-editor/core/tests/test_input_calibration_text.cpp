#include "input_calibration/input_calibration_text.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace rock_hero::editor::core
{

// Each signal-chain calibration status maps to a fixed English disabled message. This pins the
// editor English the text module owns after the calibration workflow moved to common/audio.
TEST_CASE("Input calibration text maps each status to its disabled message", "[core][text]")
{
    CHECK(inputCalibrationDisabledMessageFor(InputCalibrationStatus::Calibrated).empty());
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::NoActiveInputDevice) ==
        "Live input disabled: no audio input device selected.");
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::MissingCalibration) ==
        "Live input disabled: input calibration required.");
    CHECK(
        inputCalibrationDisabledMessageFor(InputCalibrationStatus::Unavailable) ==
        "Live input disabled: live input backend unavailable.");
}

} // namespace rock_hero::editor::core
