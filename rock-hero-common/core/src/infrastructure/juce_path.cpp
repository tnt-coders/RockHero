#include "infrastructure/juce_path.h"

namespace rock_hero::common::core
{

// Preserves Windows paths as wide text; POSIX paths are treated as UTF-8 byte strings.
juce::String juceStringFromPath(const std::filesystem::path& path)
{
#if JUCE_WINDOWS
    return juce::String{path.wstring().c_str()};
#else
    return juce::String::fromUTF8(path.string().c_str());
#endif
}

// Wraps the shared string conversion for APIs that require juce::File.
juce::File juceFileFromPath(const std::filesystem::path& path)
{
    return juce::File{juceStringFromPath(path)};
}

// Converts JUCE path text back through the native platform representation.
std::filesystem::path pathFromJuceString(const juce::String& value)
{
#if JUCE_WINDOWS
    return std::filesystem::path{value.toWideCharPointer()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

// Reads the full native path from a juce::File and converts it once through the shared path bridge.
std::filesystem::path pathFromJuceFile(const juce::File& file)
{
    return pathFromJuceString(file.getFullPathName());
}

} // namespace rock_hero::common::core
