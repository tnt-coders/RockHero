/*!
\file audio_device_sample_rate_choice.h
\brief Private editor UI helper that picks the default sample-rate selection for the settings
       dialog.
*/

#pragma once

#include <optional>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Returns the sample rate the device settings dialog should select by default.

The dialog's sample-rate combo populates from the currently staged device's reported choices,
but the staged device may not be open and therefore may not report a current rate. This helper
picks the best default in priority order:

1. The staged sample rate, when the user already chose one and it is still available.
2. The preview device's current rate, when the staged device reports one.
3. The active route's current rate, when the staged device names match the open device and the
   open device reports one.
4. 48 kHz, then 44.1 kHz, then the first available rate, as a studio-standard fallback.

A non-positive selection at any layer is treated as "no rate to offer," so the dialog never
defaults to zero just because a layer below could not contribute.

\param available_rates Sample rates reported by the staged device.
\param staged_rate Sample rate the user has staged, or non-positive when unstaged.
\param preview_device_rate Current rate reported by the staged device, or non-positive when the
       staged device is not open.
\param active_route_rate Current rate from the active device when its names match the staged
       route; empty when the staged route differs from the open one.
\return The sample rate the dialog should select.
*/
[[nodiscard]] double chooseDeviceSampleRate(
    const std::vector<double>& available_rates, double staged_rate, double preview_device_rate,
    std::optional<double> active_route_rate);

} // namespace rock_hero::editor::ui
