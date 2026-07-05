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

// Formats filesystem paths as UTF-8 text for stable IDs and logs. path::string() can lossy-convert
// through the active code page on Windows and drop non-ANSI characters from plugin paths.
[[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path);

[[nodiscard]] std::string normalizedPathKey(const std::filesystem::path& path);

[[nodiscard]] bool hasVst3Extension(const std::filesystem::path& path);

// JUCE may persist a Windows VST3 either as the bundle directory or as the architecture-specific
// module inside Contents. Normalize both forms to the bundle for UI display and path deduping.
[[nodiscard]] std::filesystem::path vst3DisplayPath(const std::filesystem::path& path);

[[nodiscard]] std::string normalizedPluginPathKey(const std::filesystem::path& path);

// Converts UTF-8-ish command line text from the public startup boundary into JUCE text.
[[nodiscard]] juce::String toJuceString(std::string_view text);

// Converts signed JUCE plugin IDs into the hex text used by JUCE and Tracktion state.
[[nodiscard]] std::string toHexString(int value);

// Converts hex text from tone JSON back to the JUCE plugin ID integer domain.
[[nodiscard]] int fromHexString(const std::string& value);

// Reports whether a package-relative reference stays inside the song workspace.
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);

// Converts text to the opaque byte shape carried by PluginInstanceState.
[[nodiscard]] std::vector<std::byte> bytesFromString(std::string_view text);

// Converts an opaque memento payload back into text for Tracktion XML parsing.
[[nodiscard]] std::string stringFromBytes(const std::vector<std::byte>& bytes);

} // namespace rock_hero::common::audio
