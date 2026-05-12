#include "editor_settings.h"

namespace rock_hero::editor::ui
{

namespace
{

constexpr const char* g_last_open_project_key{"lastOpenProject"};

// Converts a stored JUCE string into the platform-native filesystem path type.
[[nodiscard]] std::filesystem::path pathFromSettingsValue(const juce::String& value)
{
#if JUCE_WINDOWS
    return std::filesystem::path{value.toWideCharPointer()};
#else
    return std::filesystem::path{value.toStdString()};
#endif
}

// Converts a native filesystem path into the JUCE file object used by PropertiesFile.
[[nodiscard]] juce::File fileFromPath(const std::filesystem::path& path)
{
#if JUCE_WINDOWS
    return juce::File{juce::String{path.wstring().c_str()}};
#else
    return juce::File{juce::String{path.string()}};
#endif
}

// Converts a filesystem path into a JUCE string without losing Windows wide characters.
[[nodiscard]] juce::String settingsValueFromPath(const std::filesystem::path& path)
{
#if JUCE_WINDOWS
    return juce::String{path.wstring().c_str()};
#else
    return juce::String{path.string()};
#endif
}

// Builds the per-user settings file options used by the editor app.
[[nodiscard]] juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Rock Hero Editor";
    options.filenameSuffix = ".settings";
    options.folderName = "Rock Hero";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

} // namespace

// Opens the JUCE properties file that backs app-local editor settings.
EditorSettings::EditorSettings()
    : m_properties(editorSettingsOptions())
{}

// Opens an explicit settings file so app-shell behavior can be exercised in isolation.
EditorSettings::EditorSettings(const std::filesystem::path& settings_file)
    : m_properties(fileFromPath(settings_file), editorSettingsOptions())
{}

// Reads the last editor project path stored by a previous allowed editor exit.
std::optional<std::filesystem::path> EditorSettings::lastOpenProject() const
{
    const juce::String value = m_properties.getValue(g_last_open_project_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return pathFromSettingsValue(value);
}

// Stores or clears the editor project path to restore on the next editor launch.
void EditorSettings::setLastOpenProject(std::optional<std::filesystem::path> project_file)
{
    if (project_file.has_value() && !project_file->empty())
    {
        m_properties.setValue(g_last_open_project_key, settingsValueFromPath(*project_file));
    }
    else
    {
        m_properties.removeValue(g_last_open_project_key);
    }

    m_properties.saveIfNeeded();
}

} // namespace rock_hero::editor::ui
