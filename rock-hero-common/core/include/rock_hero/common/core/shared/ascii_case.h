/*!
\file ascii_case.h
\brief ASCII case folding, and the case-insensitive comparisons built on it.
*/

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::common::core
{

/*!
\brief Folds a string's ASCII letters to lower case, leaving every other byte untouched.

The project's one case fold. It is deliberately byte-wise rather than Unicode-aware: every caller
folds either an ASCII-only token (a file extension, a filter keyword) or a path key on a
case-insensitive filesystem, and a locale- or Unicode-sensitive fold would make those comparisons
depend on the running locale.

\param text Text to fold; taken by value and folded in place.
\return The folded text.
*/
[[nodiscard]] inline std::string asciiLowered(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
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
    return asciiLowered(path.extension().string()) == extension;
}

} // namespace rock_hero::common::core
