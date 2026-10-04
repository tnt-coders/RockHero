/*!
\file ratio_text.h
\brief Strict reads of a positive whole number, and of a ratio of two, from typed or stored text.
*/

#pragma once

#include <optional>
#include <string_view>

namespace rock_hero::editor::core
{

/*! \brief Two positive whole numbers read from "n/d", exactly as written: 6/8 is not 3/4. */
struct TypedRatio
{
    /*! \brief The number before the slash. */
    int numerator{1};

    /*! \brief The number after the slash. */
    int denominator{1};
};

/*!
\brief Reads text as one positive whole number.

Spaces and tabs around it are allowed; a sign, a decimal point, zero and anything left over are
refused.

\param text Text to read.
\return The number, or nothing when the text is not exactly one.
*/
[[nodiscard]] std::optional<int> parsePositiveWhole(std::string_view text);

/*!
\brief Reads text as two positive whole numbers around one slash, spaces and tabs around either
allowed, kept as written.
\param text Text to read, such as "7/8".
\return The ratio, unreduced, or nothing when the text is not one.
*/
[[nodiscard]] std::optional<TypedRatio> parseTypedRatio(std::string_view text);

} // namespace rock_hero::editor::core
