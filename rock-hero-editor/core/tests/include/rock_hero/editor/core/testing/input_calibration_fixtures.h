/*!
\file input_calibration_fixtures.h
\brief Shared builder for the input calibration prompt the editor opens.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <utility>

namespace rock_hero::editor::core::testing
{

// Builds a prompt for a route, the shared test route unless one is named, with the route's stored
// gain or none for an uncalibrated route.
[[nodiscard]] inline InputCalibrationPrompt makeInputCalibrationPrompt(
    std::optional<double> stored_gain_db,
    common::audio::InputDeviceIdentity route = common::audio::testing::makeInputDeviceIdentity())
{
    return InputCalibrationPrompt{
        .route = std::move(route),
        .stored_gain_db = stored_gain_db,
    };
}

} // namespace rock_hero::editor::core::testing
