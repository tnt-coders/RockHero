/*!
\file juce_path.h
\brief Lossless path conversion helpers for JUCE filesystem APIs.
*/

#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>

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

} // namespace rock_hero::common::core
