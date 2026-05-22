/*!
\file audio_device_type_policy.h
\brief Private editor UI policy for ordering audio device systems in the picker.
*/

#pragma once

#include <juce_core/juce_core.h>

namespace rock_hero::editor::ui
{

/*!
\brief Returns audio-device systems in the order the editor's picker presents them.

The result keeps every JUCE-reported audio system visible. Ordering reflects a product judgment
about which backend a guitar player most likely wants by default on each platform (low-latency
families first, legacy families later, unrecognized families last), rather than a measured
latency ranking. The function does not open hardware to compute the order, so the picker stays
responsive.

\param available_type_names Device-system names reported by JUCE.
\return Device-system names ordered for the picker default.
*/
[[nodiscard]] juce::StringArray audioDeviceTypePickerOrder(
    const juce::StringArray& available_type_names);

} // namespace rock_hero::editor::ui
