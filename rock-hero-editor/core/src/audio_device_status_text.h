/*!
\file audio_device_status_text.h
\brief Formats audio-device state for the editor menu-bar status button.
*/

#pragma once

#include <rock_hero/common/audio/audio_device_status.h>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

/*!
\brief Sentinel text rendered for closed, failed, stopped, or unavailable device states.

Exposed so EditorViewState can use the same default without duplicating the literal.
*/
inline constexpr std::string_view g_closed_audio_device_text{"[audio device closed]"};

/*!
\brief Formats an audio-device snapshot as compact menu-bar status text.

The bracketed format mirrors REAPER's status idiom so users coming from REAPER recognize the
fields at a glance; per-rule comments inside the implementation explain only the deviations.
Closed device states render as #g_closed_audio_device_text. Open devices render their sample
rate, bit depth, active channels, buffer size, latency, and backend type.

\param status Audio-device status snapshot to display.
\return Bracketed status text for the editor menu bar.
*/
[[nodiscard]] std::string audioDeviceStatusText(const common::audio::AudioDeviceStatus& status);

} // namespace rock_hero::editor::core
