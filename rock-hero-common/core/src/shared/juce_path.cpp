#include "shared/juce_path.h"

#include <string>

namespace rock_hero::common::core
{

// One generic UTF-8 bridge for every platform (plan 33 Phase 1, replacing per-OS branches):
// path::u8string() converts the native representation to UTF-8 correctly everywhere — on
// Windows through the wide encoding, never the lossy active code page that path::string() can
// use — and POSIX native bytes are already treated as UTF-8. The char8_t-to-char copy is the
// standard-blessed way to hand the bytes to a char API without a reinterpret_cast.
juce::String juceStringFromPath(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    std::string utf8;
    utf8.reserve(encoded.size());
    for (const char8_t byte : encoded)
    {
        utf8.push_back(static_cast<char>(byte));
    }

    return juce::String::fromUTF8(utf8.c_str(), static_cast<int>(utf8.size()));
}

// Wraps the shared string conversion for APIs that require juce::File.
juce::File juceFileFromPath(const std::filesystem::path& path)
{
    return juce::File{juceStringFromPath(path)};
}

// The reverse UTF-8 bridge: juce::String::toStdString() is UTF-8 by contract
// (juce_String.cpp: return std::string(toRawUTF8())), and constructing a path from
// std::u8string interprets the bytes as UTF-8 on every platform, converting to the native
// wide representation on Windows.
std::filesystem::path pathFromJuceString(const juce::String& value)
{
    const std::string utf8 = value.toStdString();
    std::u8string encoded;
    encoded.reserve(utf8.size());
    for (const char byte : utf8)
    {
        encoded.push_back(static_cast<char8_t>(byte));
    }

    return std::filesystem::path{encoded};
}

// Reads the full native path from a juce::File and converts it once through the shared path bridge.
std::filesystem::path pathFromJuceFile(const juce::File& file)
{
    return pathFromJuceString(file.getFullPathName());
}

// Interprets the caller's bytes as UTF-8 (std::u8string) so the path constructor converts them to
// the native wide representation on Windows, never through the lossy active code page.
std::filesystem::path pathFromUtf8(const std::string_view utf8)
{
    std::u8string encoded;
    encoded.reserve(utf8.size());
    for (const char byte : utf8)
    {
        encoded.push_back(static_cast<char8_t>(byte));
    }

    return std::filesystem::path{encoded};
}

// generic_u8string() yields forward-slash UTF-8 on every platform; the char8_t-to-char copy hands
// the bytes to a char API without a reinterpret_cast, mirroring juceStringFromPath.
std::string utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string encoded = path.generic_u8string();
    std::string utf8;
    utf8.reserve(encoded.size());
    for (const char8_t byte : encoded)
    {
        utf8.push_back(static_cast<char>(byte));
    }

    return utf8;
}

} // namespace rock_hero::common::core
