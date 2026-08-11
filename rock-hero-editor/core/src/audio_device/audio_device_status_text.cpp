#include "audio_device_status_text.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

namespace
{

// User-facing label for an open device whose backend name the adapter failed to populate. JUCE's
// shipped backends always report a non-empty type name, so the assert in backendDisplayName fires
// during dev for any new adapter or fixture that forgets the field.
constexpr std::string_view g_unknown_backend_text{"Unknown"};

// JUCE reports the Windows WASAPI route as "Windows Audio"; the editor presents the API family
// name users recognize from REAPER's device list.
[[nodiscard]] std::string backendDisplayName(std::string_view backend_name)
{
    assert(!backend_name.empty() && "open device should report a backend name");
    if (backend_name.empty())
    {
        return std::string{g_unknown_backend_text};
    }
    if (backend_name.starts_with("Windows Audio"))
    {
        return "WASAPI";
    }

    return std::string{backend_name};
}

// Rounds latencies of at least ten milliseconds to whole units; keeps shorter delays precise
// enough to compare low-latency device routes.
[[nodiscard]] std::string latencyText(double latency_ms)
{
    const double bounded_latency_ms = std::max(0.0, latency_ms);
    if (bounded_latency_ms >= 10.0)
    {
        return std::format("{}", static_cast<int>(std::round(bounded_latency_ms)));
    }

    return std::format("{:.1f}", bounded_latency_ms);
}

} // namespace

// One sample-rate rendering for every audio-device surface. See the header for why the shortest
// round-tripping form replaces the integrality tolerance this used to carry.
std::string sampleRateText(double sample_rate_hz)
{
    return std::format("{} Hz", sample_rate_hz);
}

// Produces the menu-bar text consumed by EditorView without leaking formatting rules into UI code.
std::string audioDeviceStatusText(const common::audio::AudioDeviceStatus& status)
{
    if (!status.open)
    {
        return std::string{g_closed_audio_device_text};
    }

    return std::format(
        "[{} {}bit: {}/{}ch {}spls ~{}/{}ms {}]",
        sampleRateText(status.sample_rate_hz),
        status.bit_depth,
        status.input_channels,
        status.output_channels,
        status.buffer_size_samples,
        latencyText(status.input_latency_ms),
        latencyText(status.output_latency_ms),
        backendDisplayName(status.backend_name));
}

} // namespace rock_hero::editor::core
