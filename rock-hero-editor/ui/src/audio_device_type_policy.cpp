#include "audio_device_type_policy.h"

#include <algorithm>
#include <vector>

namespace rock_hero::editor::ui
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

// Treats JUCE's Windows Audio modes as the WASAPI family exposed by the OS.
[[nodiscard]] bool isWasapiType(const juce::String& type_name) noexcept
{
    return type_name.startsWith(g_windows_audio_type_prefix);
}

// Ranks WASAPI variants for the picker default. Low Latency Mode wins because it is JUCE's
// documented low-latency path. Shared mode is preferred over Exclusive Mode because exclusive
// routes typically need extra device-side configuration to open reliably; defaulting to shared
// means the picker lands on a route most users can connect to without fiddling. The picker
// keeps every variant visible, so users who want exclusive can still select it.
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

// Ranks backend families for the picker default. ASIO leads because it is the standard low-
// latency Windows backend; the WASAPI band follows as the recommended modern fallback; legacy
// Windows backends sit below them. Unknown types sort last because we cannot defend ranking an
// unrecognized backend above a recognized one without knowing its real-world characteristics.
[[nodiscard]] int deviceTypePickerRank(const juce::String& type_name) noexcept
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

} // namespace

// Applies the editor's default picker ordering to JUCE's discovered device-system names.
juce::StringArray audioDeviceTypePickerOrder(const juce::StringArray& available_type_names)
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
        return deviceTypePickerRank(lhs) < deviceTypePickerRank(rhs);
    });

    juce::StringArray result;
    for (const auto& type_name : ordered_type_names)
    {
        result.add(type_name);
    }
    return result;
}

} // namespace rock_hero::editor::ui
