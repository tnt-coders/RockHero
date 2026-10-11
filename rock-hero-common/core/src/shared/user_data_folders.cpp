#include "shared/user_data_folders.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string_view>

namespace rock_hero::common::core
{

namespace
{

// Folder names are ASCII constants, but UTF-8 keeps the bridge correct for any spelling.
[[nodiscard]] juce::String juceText(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

// The folder JUCE keeps a per-user settings file in is the right per-user folder on every
// platform: %APPDATA% on Windows, and ~/Library/Application Support on macOS, where JUCE's
// userApplicationDataDirectory is ~/Library instead. Asking JUCE's settings location for it keeps
// that platform difference inside JUCE, and every location below derives from this one folder.
[[nodiscard]] std::filesystem::path userDataFolder()
{
    juce::PropertiesFile::Options options;
    options.applicationName = juceText(applicationDataFolderName());
    options.folderName = options.applicationName;
    options.osxLibrarySubFolder = "Application Support";
    return pathFromJuceFile(options.getDefaultFile().getParentDirectory());
}

[[nodiscard]] std::filesystem::path settingsFolder()
{
    return userDataFolder() / "settings";
}

// One file inside a folder, named by a token and an extension.
[[nodiscard]] std::filesystem::path namedFile(
    const std::filesystem::path& folder, std::string_view name, std::string_view extension)
{
    std::filesystem::path file = folder / pathFromUtf8(name);
    file += pathFromUtf8(extension);
    return file;
}

} // namespace

std::filesystem::path settingsFile(Product product)
{
    return namedFile(settingsFolder(), productToken(product), ".settings");
}

std::filesystem::path sharedSettingsFile()
{
    return namedFile(settingsFolder(), "shared", ".settings");
}

std::filesystem::path logFile(Product product)
{
    return namedFile(userDataFolder() / "logs", productToken(product), ".log");
}

std::filesystem::path themesFolder()
{
    return userDataFolder() / "themes";
}

std::filesystem::path songsFolder()
{
    return userDataFolder() / "songs";
}

std::filesystem::path tracktionFolder()
{
    return userDataFolder() / "tracktion";
}

// JUCE's temporary directory, which always answers, rather than
// std::filesystem::temp_directory_path, which throws when the environment names no usable folder.
std::filesystem::path scratchFolder()
{
    return pathFromJuceFile(
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(juceText(applicationDataFolderName())));
}

} // namespace rock_hero::common::core
