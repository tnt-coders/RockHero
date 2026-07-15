#include "audio_device/audio_device_status_text.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::editor::core
{

// Closed, failed, and missing device routes share the compact REAPER-style closed text.
TEST_CASE("Audio device status text maps closed device", "[core][audio-device-status]")
{
    CHECK(audioDeviceStatusText(common::audio::AudioDeviceStatus{}) == "[audio device closed]");
}

// Open device text keeps the low-latency route details without REAPER's recording-format token.
TEST_CASE("Audio device status text formats open device", "[core][audio-device-status]")
{
    const common::audio::AudioDeviceStatus status{
        .open = true,
        .device_name = "Interface A",
        .backend_name = "ASIO",
        .sample_rate_hz = 48000.0,
        .bit_depth = 24,
        .input_channels = 8,
        .output_channels = 8,
        .buffer_size_samples = 128,
        .input_latency_ms = 5.1,
        .output_latency_ms = 8.5,
        .unavailable_reason = {},
    };

    CHECK(audioDeviceStatusText(status) == "[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]");
}

// JUCE names the Windows WASAPI route "Windows Audio"; the editor presents the backend family.
TEST_CASE("Audio device status text maps Windows Audio", "[core][audio-device-status]")
{
    const common::audio::AudioDeviceStatus status{
        .open = true,
        .device_name = "Default Output",
        .backend_name = "Windows Audio",
        .sample_rate_hz = 44100.0,
        .bit_depth = 24,
        .input_channels = 2,
        .output_channels = 2,
        .buffer_size_samples = 512,
        .input_latency_ms = 7.5,
        .output_latency_ms = 30.0,
        .unavailable_reason = {},
    };

    CHECK(audioDeviceStatusText(status) == "[44.1kHz 24bit: 2/2ch 512spls ~7.5/30ms WASAPI]");
}

} // namespace rock_hero::editor::core
