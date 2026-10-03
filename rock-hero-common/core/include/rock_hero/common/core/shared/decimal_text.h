/*!
\file decimal_text.h
\brief Strict, locale-independent parsing of one decimal number from text.
*/

#pragma once

#include <optional>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Reads text as one finite decimal number.

Surrounding whitespace is ignored; empty text, anything left over after the number, and a
non-finite value all read as absent, so a corrupt settings entry or a mistyped prompt never turns
into a bogus number. Locale-independent. The one parser every product's settings stores and text
prompts share, kept off std::from_chars because Apple's libc++ ships no floating-point overloads.

\param text UTF-8 text to read.
\return The number, or nothing when the text is not exactly one finite number.
*/
[[nodiscard]] std::optional<double> parseFiniteDecimal(std::string_view text);

} // namespace rock_hero::common::core
