/*!
\file whitespace_trim.h
\brief The one trim of surrounding whitespace from a line of typed or stored text.
*/

#pragma once

#include <cstddef>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Strips the spaces and tabs surrounding a line of text.

Spaces and tabs are the whole of "surrounding whitespace" here: every caller trims one line — a
prompt or dialog field, an imported label, a stored token — none of which can carry a newline.
Every producer of a stored name stores what this returns and every rule over that name measures
through it, so the stored text and the accepted text can never disagree.

\param text Text to trim.
\return A view into \p text without its surrounding spaces and tabs; empty when \p text holds
        nothing else. It refers to the caller's characters, so it lives no longer than they do.
*/
[[nodiscard]] constexpr std::string_view trimmedWhitespace(std::string_view text) noexcept
{
    constexpr std::string_view whitespace{" \t"};
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

} // namespace rock_hero::common::core
