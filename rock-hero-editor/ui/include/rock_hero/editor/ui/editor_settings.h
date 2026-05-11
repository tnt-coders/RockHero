/*!
\file editor_settings.h
\brief App-local settings for Rock Hero Editor.
*/

#pragma once

#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

namespace rock_hero::editor::ui
{

/*!
\brief Stores editor application settings that live outside project packages.

EditorSettings owns the JUCE properties file used by the editor executable. These settings are
per-user application state, not `.rhp` project data and not `.rock` package data.
*/
class EditorSettings final
{
public:
    /*! \brief Opens the editor settings file using the app's standard per-user location. */
    EditorSettings();

    /*!
    \brief Opens the editor settings file at an explicit native path.
    \param settings_file Settings file path used instead of the app's standard per-user location.
    */
    explicit EditorSettings(const std::filesystem::path& settings_file);

    /*! \brief Copying is disabled because juce::PropertiesFile is stateful file IO. */
    EditorSettings(const EditorSettings&) = delete;

    /*! \brief Copy assignment is disabled because juce::PropertiesFile is stateful file IO. */
    EditorSettings& operator=(const EditorSettings&) = delete;

    /*! \brief Moving is disabled because the settings file owns runtime file state. */
    EditorSettings(EditorSettings&&) = delete;

    /*! \brief Move assignment is disabled because the settings file owns runtime file state. */
    EditorSettings& operator=(EditorSettings&&) = delete;

    /*!
    \brief Reads the native project path stored by a previous allowed editor exit.
    \return Stored project path, or empty when no project should be restored.
    */
    [[nodiscard]] std::optional<std::filesystem::path> lastOpenProject() const;

    /*!
    \brief Stores or clears the native project path to restore on the next editor launch.
    \param project_file Project path to restore, or empty to clear restore state.
    */
    void setLastOpenProject(std::optional<std::filesystem::path> project_file);

private:
    juce::PropertiesFile m_properties;
};

} // namespace rock_hero::editor::ui
