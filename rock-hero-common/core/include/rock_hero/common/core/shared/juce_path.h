/*!
\file juce_path.h
\brief Lossless path conversion helpers for JUCE filesystem APIs.
*/

#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>
#include <string>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Converts a standard filesystem path into JUCE text.

Both directions of the bridge go through UTF-8, which round-trips the native representation
losslessly on every platform (Windows wide paths included).

\param path Path to convert.
\return JUCE string holding the same path as UTF-8 text.
*/
[[nodiscard]] juce::String juceStringFromPath(const std::filesystem::path& path);

/*!
\brief Converts a standard filesystem path into a JUCE file.
\param path Path to convert.
\return JUCE file for the same native path.
*/
[[nodiscard]] juce::File juceFileFromPath(const std::filesystem::path& path);

/*!
\brief Converts JUCE text into a standard filesystem path.
\param value JUCE string containing native path text.
\return Standard filesystem path for the same native path.
*/
[[nodiscard]] std::filesystem::path pathFromJuceString(const juce::String& value);

/*!
\brief Converts a JUCE file into a standard filesystem path.
\param file JUCE file to convert.
\return Standard filesystem path for the same native path.
*/
[[nodiscard]] std::filesystem::path pathFromJuceFile(const juce::File& file);

/*!
\brief Builds a filesystem path from UTF-8 bytes.

The reverse of utf8FromPath, and the correct alternative to the narrow `std::filesystem::path`
constructor for package-supplied text: that constructor decodes bytes through the lossy active
code page on Windows, mojibake-ing non-ASCII names, whereas this interprets the bytes as UTF-8 on
every platform (converting to the native wide representation on Windows).

\param utf8 UTF-8 path text (a ZIP entry name or a package-relative reference).
\return Filesystem path holding the same characters.
*/
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);

/*!
\brief Converts a filesystem path into portable, forward-slash UTF-8 text.

Yields the generic (forward-slash) spelling as UTF-8, the form ZIP entry names and package-relative
references use. Unlike `std::filesystem::path::generic_string()`, it never narrows through the
Windows active code page (which loses non-ASCII characters and can throw for unrepresentable ones);
it converts through UTF-8 on every platform.

\param path Path to convert.
\return Generic-form UTF-8 text for the path.
*/
[[nodiscard]] std::string utf8FromPath(const std::filesystem::path& path);

} // namespace rock_hero::common::core
