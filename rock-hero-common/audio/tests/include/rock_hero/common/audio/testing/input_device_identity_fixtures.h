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
//
// An empty input_channel_name derives the name from the index the way audio backends label
// channels, so varying the index cannot silently leave a name that describes another channel. Pass
// the name explicitly only to model a backend that relabels the same physical route, which is what
// samePhysicalInputRoute deliberately ignores.
[[nodiscard]] inline InputDeviceIdentity makeInputDeviceIdentity(
    std::string backend_name = "ASIO", std::string input_device_name = "Interface A",
    int input_channel_index = 0, std::string input_channel_name = {})
{
    if (input_channel_name.empty())
    {
        input_channel_name = "Input " + std::to_string(input_channel_index + 1);
    }

    return InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = input_channel_index,
        .input_channel_name = std::move(input_channel_name),
    };
}

} // namespace rock_hero::common::audio::testing
