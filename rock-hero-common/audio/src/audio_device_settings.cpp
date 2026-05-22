#include "audio_device_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// JUCE reports the shared WASAPI route as the bare "Windows Audio" name; the same literal also
// serves as the family prefix used to detect any WASAPI variant.
constexpr char g_asio_type_name[] = "ASIO";
constexpr char g_windows_audio_type_prefix[] = "Windows Audio";
constexpr char g_windows_audio_low_latency_type_name[] = "Windows Audio (Low Latency Mode)";
constexpr char g_windows_audio_exclusive_type_name[] = "Windows Audio (Exclusive Mode)";
constexpr char g_direct_sound_type_name[] = "DirectSound";
constexpr char g_wave_out_type_name[] = "WaveOut";
constexpr double g_sample_rate_match_tolerance{0.001};

// Treats JUCE's Windows Audio modes as the WASAPI family exposed by the OS.
[[nodiscard]] bool isWasapiType(const juce::String& type_name) noexcept
{
    return type_name.startsWith(g_windows_audio_type_prefix);
}

// Ranks WASAPI variants for Rock Hero defaults. Low Latency Mode wins because it is JUCE's
// documented low-latency path. Shared mode is preferred over Exclusive Mode because exclusive
// routes typically need extra device-side configuration to open reliably; defaulting to shared
// means settings land on a route most users can connect to without fiddling. Every variant stays
// visible, so users who want exclusive can still select it.
[[nodiscard]] int wasapiPreferenceRank(const juce::String& type_name) noexcept
{
    if (type_name == g_windows_audio_low_latency_type_name)
    {
        return 0;
    }

    if (type_name == g_windows_audio_type_prefix)
    {
        return 1;
    }

    if (type_name == g_windows_audio_exclusive_type_name)
    {
        return 2;
    }

    return 3;
}

// Ranks backend families for Rock Hero defaults. ASIO leads because it is the standard low-latency
// Windows backend; the WASAPI band follows as the recommended modern fallback; legacy Windows
// backends sit below them. Unknown types sort last because we cannot defend ranking an
// unrecognized backend above a recognized one without knowing its real-world characteristics.
[[nodiscard]] int deviceTypePreferenceRank(const juce::String& type_name) noexcept
{
    if (type_name == g_asio_type_name)
    {
        return 0;
    }

    if (isWasapiType(type_name))
    {
        return 10 + wasapiPreferenceRank(type_name);
    }

    if (type_name == g_direct_sound_type_name)
    {
        return 30;
    }

    if (type_name == g_wave_out_type_name)
    {
        return 40;
    }

    return 50;
}

// True when the rate list contains a value close enough to be considered the same selection.
[[nodiscard]] bool containsSampleRate(const std::vector<double>& rates, double rate)
{
    return std::ranges::any_of(rates, [rate](double available) {
        return std::abs(available - rate) < g_sample_rate_match_tolerance;
    });
}

// Picks 48 kHz, then 44.1 kHz, then the first available rate as the studio-standard fallback.
[[nodiscard]] double fallbackSampleRate(const std::vector<double>& rates)
{
    constexpr std::array preferred_rates{48000.0, 44100.0};
    for (const double rate : preferred_rates)
    {
        if (containsSampleRate(rates, rate))
        {
            return rate;
        }
    }
    return rates.empty() ? 0.0 : rates.front();
}

} // namespace

// Applies Rock Hero's default settings order to JUCE's discovered device-system names.
juce::StringArray preferredAudioDeviceTypeOrder(const juce::StringArray& available_type_names)
{
    // Copy into std::vector because juce::StringArray::sort accepts no custom comparator; the
    // round trip is the smallest way to apply a rank-based ordering.
    std::vector<juce::String> ordered_type_names;
    ordered_type_names.reserve(static_cast<std::size_t>(available_type_names.size()));

    for (const auto& type_name : available_type_names)
    {
        ordered_type_names.push_back(type_name);
    }

    std::ranges::stable_sort(ordered_type_names, [](const auto& lhs, const auto& rhs) {
        return deviceTypePreferenceRank(lhs) < deviceTypePreferenceRank(rhs);
    });

    juce::StringArray result;
    for (const auto& type_name : ordered_type_names)
    {
        result.add(type_name);
    }
    return result;
}

// Chooses a rate from route-specific hints before falling back to studio-standard defaults.
double chooseAudioDeviceSampleRate(
    const std::vector<double>& available_rates, double staged_rate, double preview_device_rate,
    std::optional<double> active_route_rate)
{
    for (const double candidate : {staged_rate, preview_device_rate})
    {
        if (candidate > 0.0 && containsSampleRate(available_rates, candidate))
        {
            return candidate;
        }
    }

    if (active_route_rate.has_value() && *active_route_rate > 0.0 &&
        containsSampleRate(available_rates, *active_route_rate))
    {
        return *active_route_rate;
    }

    return fallbackSampleRate(available_rates);
}

} // namespace rock_hero::common::audio
