/*!
\file ascii_case.h
\brief ASCII case folding, and the case-insensitive comparisons built on it.
*/

#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::common::core
{

/*!
\brief Folds one ASCII upper-case letter to lower case, leaving every other byte untouched.

The project's one case-fold rule; the string wrappers below and every per-byte folding loop build
on it. It is deliberately explicit arithmetic rather than `std::tolower`: every caller folds either
an ASCII-only token (a file extension, a filter keyword) or a path key on a case-insensitive
filesystem, and the standard function is locale-sensitive for bytes past 0x7F, which would make
those comparisons depend on the running locale.

\param character Byte to fold.
\return The lower-case letter for A-Z, the byte unchanged otherwise.
*/
[[nodiscard]] constexpr char asciiLower(char character) noexcept
{
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
                                                : character;
}

/*!
\brief Folds one ASCII lower-case letter to upper case, leaving every other byte untouched.
\param character Byte to fold.
\return The upper-case letter for a-z, the byte unchanged otherwise.
*/
[[nodiscard]] constexpr char asciiUpper(char character) noexcept
{
    return character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A')
                                                : character;
}

/*!
\brief Folds a string's ASCII letters to lower case, leaving every other byte untouched.
\param text Text to fold; taken by value and folded in place.
\return The folded text.
*/
[[nodiscard]] inline std::string asciiLowered(std::string text)
{
    std::ranges::transform(text, text.begin(), asciiLower);
    return text;
}

/*!
\brief Folds a string's ASCII letters to upper case, leaving every other byte untouched.
\param text Text to fold; taken by value and folded in place.
\return The folded text.
*/
[[nodiscard]] inline std::string asciiUppered(std::string text)
{
    std::ranges::transform(text, text.begin(), asciiUpper);
    return text;
}

/*!
\brief Reports whether a path's extension matches one spelling, ignoring ASCII case.

Extensions reach this project from filesystems, archives, and score files that disagree about case,
so every extension test goes through here rather than case-folding at the call site.

\param path Path whose extension is tested.
\param extension Expected extension including its leading dot, in lower case.
\return True when the path's extension equals the expected one apart from ASCII case.
*/
[[nodiscard]] inline bool hasExtensionIgnoringCase(
    const std::filesystem::path& path, std::string_view extension)
{
    return asciiLowered(path.extension().generic_string()) == extension;
}

} // namespace rock_hero::common::core
