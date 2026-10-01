/*!
\file input_calibration_text.h
\brief The one formatter for calibration gains, shared by the popup's sentences and its slider.
*/

#pragma once

#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Formats a calibration gain as a signed number at the slider's 0.1 dB resolution.

The status line and the gain slider both print through this, so they can never disagree on a sign
or a decimal. Each surface appends its own unit.
\param gain_db Gain in decibels.
\return "+2.3", "-0.5" or "0.0"; zero carries no sign.
*/
[[nodiscard]] std::string signedGainText(double gain_db);

} // namespace rock_hero::editor::core
