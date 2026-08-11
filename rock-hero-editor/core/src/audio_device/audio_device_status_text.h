/*!
\file audio_device_status_text.h
\brief Formats audio-device state for the editor menu-bar status button.
*/

#pragma once

#include <rock_hero/common/audio/device/audio_device_status.h>
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
\brief Formats one sample rate as the Hz text every audio-device surface displays.

Shared by the menu-bar status text and the settings dialog's rate combo so the same rate never
renders two ways. **Hertz spelled out, not compressed to kHz** — that is what audio-device
settings conventionally show, so `44100 Hz` and `48000 Hz` rather than `44.1kHz` and `48kHz`.

The rendering carries no tolerance constant, which is the part worth keeping whatever the unit:
std::format's default floating-point form is the shortest text that round-trips the value, so an
integral rate prints no decimal point at all and an odd `44100.5 Hz` prints exactly, with no
fixed precision to flatten it and no integrality epsilon to get wrong. This replaced two
formatters that disagreed and three restatements of one 0.001 tolerance in two different units.

\param sample_rate_hz Sample rate in hertz, as reported by the audio device.
\return Sample-rate text in hertz.
*/
[[nodiscard]] std::string sampleRateText(double sample_rate_hz);

/*!
\brief Formats an audio-device snapshot as compact menu-bar status text.

The bracketed format mirrors REAPER's status idiom so users coming from REAPER recognize the
fields at a glance; per-rule comments inside the implementation explain only the deviations.
Closed device states render as `g_closed_audio_device_text`. Open devices render their sample
rate, bit depth, active channels, buffer size, latency, and backend type.

\param status Audio-device status snapshot to display.
\return Bracketed status text for the editor menu bar.
*/
[[nodiscard]] std::string audioDeviceStatusText(const common::audio::AudioDeviceStatus& status);

} // namespace rock_hero::editor::core
