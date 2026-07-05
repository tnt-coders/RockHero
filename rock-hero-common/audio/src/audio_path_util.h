/*!
\file audio_path_util.h
\brief Path, encoding, and byte-string helpers shared by the engine translation units.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Formats a filesystem path as UTF-8 text for stable IDs and logs.

path::string() can lossy-convert through the active code page on Windows and drop non-ANSI
characters from plugin paths, so ID and log paths route through this helper instead.

\param path Path to format.
\return UTF-8 text for the path.
*/
[[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path);

/*!
\brief Builds a normalized, lowercased UTF-8 key for path-based deduplication.
\param path Path to normalize.
\return Stable comparison key for the path.
*/
[[nodiscard]] std::string normalizedPathKey(const std::filesystem::path& path);

/*!
\brief Reports whether a path names a VST3 bundle or module.
\param path Path to test.
\return True when the path carries the .vst3 extension.
*/
[[nodiscard]] bool hasVst3Extension(const std::filesystem::path& path);

/*!
\brief Normalizes a persisted VST3 reference to its bundle directory.

JUCE may persist a Windows VST3 either as the bundle directory or as the architecture-specific
module inside Contents; both forms normalize to the bundle for UI display and path deduping.

\param path Persisted plugin path in either form.
\return Bundle path for display and deduplication.
*/
[[nodiscard]] std::filesystem::path vst3DisplayPath(const std::filesystem::path& path);

/*!
\brief Builds the deduplication key for a plugin path after VST3 bundle normalization.
\param path Persisted plugin path in either bundle or module form.
\return Stable comparison key for the plugin path.
*/
[[nodiscard]] std::string normalizedPluginPathKey(const std::filesystem::path& path);

/*!
\brief Converts UTF-8 text from the public startup boundary into JUCE text.
\param text UTF-8 text to convert.
\return Equivalent JUCE string.
*/
[[nodiscard]] juce::String toJuceString(std::string_view text);

/*!
\brief Converts a signed JUCE plugin ID into the hex text used by JUCE and Tracktion state.
\param value Signed plugin ID.
\return Hex text for persisted state.
*/
[[nodiscard]] std::string toHexString(int value);

/*!
\brief Converts hex text from tone JSON back into the JUCE plugin ID integer domain.
\param value Hex text persisted by toHexString().
\return Signed plugin ID.
*/
[[nodiscard]] int fromHexString(const std::string& value);

/*!
\brief Reports whether a package-relative reference stays inside the song workspace.
\param path Package-relative path to validate.
\return True when the path never escapes the package root.
*/
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);

/*!
\brief Converts text into the opaque byte shape carried by PluginInstanceState.
\param text Text to convert.
\return Byte copy of the text.
*/
[[nodiscard]] std::vector<std::byte> bytesFromString(std::string_view text);

/*!
\brief Converts an opaque memento payload back into text for Tracktion XML parsing.
\param bytes Byte payload previously produced by bytesFromString().
\return Text copy of the payload.
*/
[[nodiscard]] std::string stringFromBytes(const std::vector<std::byte>& bytes);

} // namespace rock_hero::common::audio
