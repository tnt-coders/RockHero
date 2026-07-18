#include "audio_path_util.h"

namespace rock_hero::common::audio
{

// Formats filesystem paths as UTF-8 text for stable IDs and logs. path::string() can lossy-convert
// through the active code page on Windows and drop non-ANSI characters from plugin paths.
[[nodiscard]] std::string pathToUtf8String(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t byte : encoded)
    {
        result.push_back(static_cast<char>(byte));
    }

    return result;
}

[[nodiscard]] std::string normalizedPathKey(const std::filesystem::path& path)
{
    std::string key = pathToUtf8String(path.lexically_normal());
#if defined(_WIN32)
    std::ranges::transform(key, key.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return key;
}

[[nodiscard]] bool hasVst3Extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension == ".vst3";
}

// JUCE may persist a VST3 either as the bundle directory or as the architecture-specific module
// inside Contents; the <name>.vst3/Contents/<arch>/ bundle layout is identical on every
// platform, and the shape check makes the normalization self-guarding, so it runs
// unconditionally (plan 33 Phase 1 removed the old Windows-only guard). Normalize both forms
// to the bundle for UI display and path deduping.
[[nodiscard]] std::filesystem::path vst3DisplayPath(const std::filesystem::path& path)
{
    const std::filesystem::path architecture_path = path.parent_path();
    const std::filesystem::path contents_path = architecture_path.parent_path();
    // Not const: a const local would force the return to copy instead of move.
    std::filesystem::path bundle_path = contents_path.parent_path();
    if (contents_path.filename() == "Contents" && hasVst3Extension(bundle_path))
    {
        return bundle_path;
    }

    return path;
}

[[nodiscard]] std::string normalizedPluginPathKey(const std::filesystem::path& path)
{
    return normalizedPathKey(vst3DisplayPath(path));
}

// Converts UTF-8-ish command line text from the public startup boundary into JUCE text.
[[nodiscard]] juce::String toJuceString(std::string_view text)
{
    const std::string utf8_text{text};
    return text.empty() ? juce::String{} : juce::String::fromUTF8(utf8_text.c_str());
}

// Converts signed JUCE plugin IDs into the hex text used by JUCE and Tracktion state.
[[nodiscard]] std::string toHexString(int value)
{
    return juce::String::toHexString(value).toStdString();
}

// Converts hex text from tone JSON back to the JUCE plugin ID integer domain.
[[nodiscard]] int fromHexString(const std::string& value)
{
    return static_cast<int>(juce::String::fromUTF8(value.c_str()).getHexValue64());
}

// Reports whether a package-relative reference stays inside the song workspace.
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    for (const std::filesystem::path& part : path)
    {
        const std::string text = part.generic_string();
        if (text.empty() || text == "." || text == ".." || text.find(':') != std::string::npos)
        {
            return false;
        }
    }

    return true;
}

// Converts text to the opaque byte shape carried by PluginInstanceState.
[[nodiscard]] std::vector<std::byte> bytesFromString(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return bytes;
}

// Converts an opaque memento payload back into text for Tracktion XML parsing.
[[nodiscard]] std::string stringFromBytes(const std::vector<std::byte>& bytes)
{
    std::string text;
    text.reserve(bytes.size());
    for (const std::byte byte : bytes)
    {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }

    return text;
}

} // namespace rock_hero::common::audio
