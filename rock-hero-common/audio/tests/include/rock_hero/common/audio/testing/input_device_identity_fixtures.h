/*!
\file input_device_identity_fixtures.h
\brief Shared builder for the physical input route identity used across calibration tests.
*/

#pragma once

#include <rock_hero/common/audio/input/input_device_identity.h>
#include <string>
#include <utility>

namespace rock_hero::common::audio::testing
{

// Builds one stable physical input route, defaulting to a single-channel ASIO interface. Parameters
// follow the struct's own field order so a call site reads as the identity it produces.
[[nodiscard]] inline InputDeviceIdentity makeInputDeviceIdentity(
    std::string backend_name = "ASIO", std::string input_device_name = "Interface A",
    int input_channel_index = 0)
{
    return InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = input_channel_index,
    };
}

} // namespace rock_hero::common::audio::testing
