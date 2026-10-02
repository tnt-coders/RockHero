/*!
\file input_calibration_text.h
\brief The input calibration popup's sentences, and the one formatter for calibration gains.
*/

#pragma once

#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/pickup_types.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Formats a calibration gain as a signed number at the slider's 0.1 dB resolution.

Every sentence and the gain slider print through this, so they can never disagree on a sign or a
decimal. Each surface appends its own unit.
\param gain_db Gain in decibels.
\return "+2.3", "-0.5" or "0.0"; zero carries no sign.
*/
[[nodiscard]] std::string signedGainText(double gain_db);

/*!
\brief Words what the popup offers before anything happens: the two ways to a gain.
\return The idle message.
*/
[[nodiscard]] std::string idleText();

/*!
\brief Words why the guide did not open.
\return The message for a guide that is installed but would not open.
*/
[[nodiscard]] std::string guideUnavailableText();

/*!
\brief Words the gain formula, for a player typing a gain.
\return The formula against the one reference.
*/
[[nodiscard]] std::string gainFormulaText();

/*!
\brief Words what the meter's peak target means for the chosen pickups.
\param pickups The chosen pickups.
\return The sentence for the meter's tooltip.
*/
[[nodiscard]] std::string peakTargetText(common::audio::PickupClass pickups);

/*!
\brief Words what a running measurement asks of the player.
\param running The capture's report.
\return The setup and the playing until the first strum, then a countdown in whole seconds.
*/
[[nodiscard]] std::string measuringText(const common::audio::InputCalibrationRunning& running);

/*!
\brief Words a finished measurement's result, naming the pickups so a player who left the default
sees what was assumed.
\param gain_db The measured gain.
\param pickups The pickups the measurement assumed.
\return "Measured +3.0 dB with humbucker pickups. Click Apply to save it."
*/
[[nodiscard]] std::string measuredText(double gain_db, common::audio::PickupClass pickups);

} // namespace rock_hero::editor::core
