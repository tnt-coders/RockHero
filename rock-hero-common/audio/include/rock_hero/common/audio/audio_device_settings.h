/*!
\file audio_device_settings.h
\brief Shared audio-device settings policy for Rock Hero applications.
*/

#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Returns audio-device systems in Rock Hero's preferred settings order.

The result keeps every JUCE-reported audio system visible. Ordering reflects a product judgment
about which backend a guitar player most likely wants by default on each platform (low-latency
families first, legacy families later, unrecognized families last), rather than a measured
latency ranking. The function does not open hardware to compute the order, so settings views stay
responsive.

\param available_type_names Device-system names reported by JUCE.
\return Device-system names ordered for Rock Hero's settings default.
*/
[[nodiscard]] juce::StringArray preferredAudioDeviceTypeOrder(
    const juce::StringArray& available_type_names);

/*!
\brief Reports whether two sample rates represent the same user-visible choice.

\param lhs First sample rate to compare.
\param rhs Second sample rate to compare.
\return True when the rates are close enough to select the same hardware setting.
*/
[[nodiscard]] bool sampleRatesMatch(double lhs, double rhs) noexcept;

/*!
\brief Returns the sample rate an audio-device settings view should select by default.

The available-rate list typically comes from a preview device. Some preview devices are not open
and therefore cannot report a current rate. This helper picks the best default in priority order:

1. The staged sample rate, when the user already chose one and it is still available.
2. The preview device's current rate, when the preview device reports one.
3. The active route's current rate, when it is known to describe the same hardware route.
4. 48 kHz, then 44.1 kHz, then the first available rate, as a studio-standard fallback.

A non-positive selection at any layer is treated as "no rate to offer," so callers never default
to zero just because a layer below could not contribute.

\param available_rates Sample rates reported by the preview device.
\param staged_rate Sample rate the user has staged, or non-positive when unstaged.
\param preview_device_rate Current rate reported by the preview device, or non-positive when the
       preview device is not open.
\param active_route_rate Current rate from the active device when it describes the same route;
       empty when the active route should not contribute.
\return The sample rate the settings view should select.
*/
[[nodiscard]] double chooseAudioDeviceSampleRate(
    const std::vector<double>& available_rates, double staged_rate, double preview_device_rate,
    std::optional<double> active_route_rate);

} // namespace rock_hero::common::audio
