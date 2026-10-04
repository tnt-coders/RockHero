/*!
\file time_signature_text.h
\brief Formats a meter as the ruler chip and the time-signature prompt spell it.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Spells a meter the way the ruler chip prints it and the prompt starts from: "7/8".
\param meter Meter to spell.
\return The numerator, a slash and the denominator.
*/
[[nodiscard]] std::string timeSignatureText(common::core::TimeSignature meter);

} // namespace rock_hero::editor::core
