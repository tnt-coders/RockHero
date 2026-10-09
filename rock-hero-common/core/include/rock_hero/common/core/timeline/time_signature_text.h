/*!
\file time_signature_text.h
\brief Formats a meter as every chip, prompt and diagnostic spells it.
*/

#pragma once

#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Spells a meter the way the ruler chip prints it, the prompt starts from and a rule names it:
"7/8".
\param meter Meter to spell.
\return The numerator, a slash and the denominator.
*/
[[nodiscard]] std::string timeSignatureText(TimeSignature meter);

} // namespace rock_hero::common::core
