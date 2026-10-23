/*!
\file legal_file_name.h
\brief Turns display text into a file-name stem every supported OS accepts.
*/

#pragma once

#include <string>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Makes a user-facing file-name stem legal on Windows, macOS and Linux.

This names a file the USER sees and keeps — a suggested project name such as
"AC_DC - Back in Black" — so it changes as little of the text as it can, and non-ASCII text
passes untouched. It is NOT the package writer's `sanitizeFileName`
(`rock_song_package_write.cpp`), which names package-internal entries and folds everything but
ASCII letters, digits, `.`, `-` and `_`; the two answer different questions and must stay apart.

The rule, over the UTF-8 bytes:
- each of `< > : " / | ? *` and the backslash becomes `_`: each is illegal in a file name on at
  least one of the three OSes, and `_` rather than `-` so the replacement never reads as a second
  " - " separator;
- control bytes (below 0x20, and 0x7F) are dropped: no OS shows them, and Windows refuses them.
  Every byte of a multi-byte UTF-8 sequence is 0x80 or above, so no character is split;
- a leading `.` becomes `_`, since a dot-file is hidden on macOS and Linux;
- the result is capped at 120 bytes, cut back to a code-point boundary, keeping the whole name
  well inside every OS's 255-byte component limit once the extension is added.

Windows' reserved device names (`CON`, `NUL`, ...) and its silent stripping of trailing dots and
spaces are not handled, because neither can arise from the stems this serves: every caller passes
an "Artist - Song" stem, which is never a bare device name, and appends an extension, so the
stem's tail is never the name's tail.

\param stem File-name stem, without its extension.
\return The stem made legal; empty only when \p stem holds nothing but control bytes.
*/
[[nodiscard]] std::string legalFileName(std::string_view stem);

} // namespace rock_hero::common::core
