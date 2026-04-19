/*!
\file juce_interop.h
\brief Internal helpers for bridging between standard library types and JUCE types.
*/

#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>

namespace rock_hero::audio
{

/*!
\brief Converts a std::filesystem::path to a juce::File without corrupting non-ASCII characters.

On Windows, std::filesystem::path stores a wchar_t sequence, and path::string() narrows it
through the current locale, which can drop or replace characters outside the locale's range.
This helper uses the native wide string on Windows so paths containing accented, CJK, or other
non-ASCII characters survive the conversion.

\param path The filesystem path to convert.
\return A juce::File referencing the same location on disk.
*/
inline juce::File toJuceFile(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto native_path = path.wstring();
    return juce::File(juce::String(native_path.c_str()));
#else
    return juce::File(juce::String(path.string()));
#endif
}

} // namespace rock_hero::audio
