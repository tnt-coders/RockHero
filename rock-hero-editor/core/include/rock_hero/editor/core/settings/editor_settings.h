/*!
\file editor_settings.h
\brief App-local settings for Rock Hero Editor.
*/

#pragma once

#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Stores editor settings that live outside project packages.

EditorSettings is the JUCE-backed implementation of IEditorSettings used by production app
composition. Scalar values and XML-valued histories are stored in the app properties file. These
settings are per-user application state, not `.rhp` project data and not `.rock` package data.
The audio configuration is not editor state: it lives in the AudioConfigStore both products
share, which app composition owns.
*/
class EditorSettings final : public IEditorSettings
{
public:
    /*! \brief Opens the editor settings file using the app's standard per-user location. */
    EditorSettings();

    /*!
    \brief Opens editor settings at an explicit native path, so lifecycle behavior can be exercised
    in isolation.

    \param settings_file Settings file path used for persisted editor state.
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

    /*! \brief Destroys the EditorSettings. */
    ~EditorSettings() override = default;

    /*!
    \brief Reads the editor project path stored by a previous allowed editor exit.
    \return Stored project path, or empty when no project should be restored.
    */
    [[nodiscard]] std::optional<std::filesystem::path> lastOpenProject() const override;

    /*!
    \brief Stores or clears the editor project path to restore on the next editor launch.
    \param project_file Project path to restore, or empty to clear restore state.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setLastOpenProject(
        std::optional<std::filesystem::path> project_file) override;

    /*!
    \brief Reads the project path whose previous startup restore did not finish.
    \return Interrupted restore path, or empty when startup restore can proceed normally.
    */
    [[nodiscard]] std::optional<std::filesystem::path> interruptedRestoreProject() const override;

    /*!
    \brief Stores or clears the project path whose startup restore is in progress.
    \param project_file Interrupted restore path, or empty to clear the recovery prompt state.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setInterruptedRestoreProject(
        std::optional<std::filesystem::path> project_file) override;

    /*!
    \brief Reads the app-wide waveform visibility preference for the timeline's tablature lane.
    \return Stored visibility, or empty when the user has never toggled it.
    */
    [[nodiscard]] std::optional<bool> waveformVisible() const override;

    /*!
    \brief Stores the app-wide waveform visibility preference.
    \param visible True when the waveform should draw behind the tablature lane.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setWaveformVisible(
        bool visible) override;

    /*!
    \brief Reads the directory the tone-file choosers should start in.
    \return Last-used tone file directory, or empty when the user has never saved or opened one.
    */
    [[nodiscard]] std::optional<std::filesystem::path> toneFileDirectory() const override;

    /*!
    \brief Stores the directory the tone-file choosers should start in.
    \param directory Directory of the most recently opened or saved tone file.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setToneFileDirectory(
        std::filesystem::path directory) override;

    /*!
    \brief Reads the app-wide minimum number of tablature string lanes to display.
    \return Stored minimum, or empty when the user has never chosen one.
    */
    [[nodiscard]] std::optional<int> tabMinimumDisplayedStrings() const override;

    /*!
    \brief Stores the app-wide minimum number of tablature string lanes to display.
    \param minimum_strings Minimum lane count; zero means match the chart's string count.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setTabMinimumDisplayedStrings(
        int minimum_strings) override;

    /*!
    \brief Reads the persisted keymap-override XML for the editor's command registry.
    \return Stored keymap XML, or empty when only the defaults apply.
    */
    [[nodiscard]] std::optional<std::string> keymapXml() const override;

    /*!
    \brief Stores or clears the persisted keymap-override XML.
    \param keymap_xml Diff XML to store, or empty when the keymap is back to pure defaults.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setKeymapXml(
        std::optional<std::string> keymap_xml) override;

    /*!
    \brief Reads the app-local resume marker stored for an editor project path.
    \param project_file Project path whose marker should be restored.
    \return Stored marker, or absence when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] std::optional<EditorProjectMarker> projectMarkerFor(
        const std::filesystem::path& project_file) const override;

    /*!
    \brief Stores or replaces the app-local resume marker for an editor project path.
    \param project_file Project path that owns the marker.
    \param marker Marker to restore next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectMarker(
        const std::filesystem::path& project_file, const EditorProjectMarker& marker) override;

    /*!
    \brief Reads the app-local timeline grid note value stored for an editor project path.
    \param project_file Project path whose grid note value should be restored.
    \return Grid step as a fraction of a whole note, or absence when none is stored or unreadable.
    */
    [[nodiscard]] std::optional<common::core::Fraction> projectGridNoteValueFor(
        const std::filesystem::path& project_file) const override;

    /*!
    \brief Stores or replaces the app-local timeline grid note value for an editor project path.
    \param project_file Project path that owns the grid note value.
    \param grid_note_value Grid step as a fraction of a whole note.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectGridNoteValue(
        const std::filesystem::path& project_file, common::core::Fraction grid_note_value) override;

    /*!
    \brief Reads the app-local timeline zoom stored for an editor project path.
    \param project_file Project path whose zoom should be restored.
    \return Zoom in pixels per second, or absence when none is stored or the value is unreadable.
    */
    [[nodiscard]] std::optional<double> projectTimelineZoomFor(
        const std::filesystem::path& project_file) const override;

    /*!
    \brief Stores or replaces the app-local timeline zoom for an editor project path.
    \param project_file Project path that owns the zoom.
    \param pixels_per_second Horizontal timeline scale to restore on next open.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectTimelineZoom(
        const std::filesystem::path& project_file, double pixels_per_second) override;

    /*!
    \brief Reads the app-local arrangement to display first for an editor project path.
    \param project_file Project path whose displayed arrangement should be restored.
    \return Stored arrangement id, or absence when none is stored or the value is unreadable.
    */
    [[nodiscard]] std::optional<std::string> projectSelectedArrangementFor(
        const std::filesystem::path& project_file) const override;

    /*!
    \brief Stores or replaces the app-local arrangement to display first for an editor project path.
    \param project_file Project path that owns the displayed-arrangement choice.
    \param arrangement_id Arrangement id to display next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectSelectedArrangement(
        const std::filesystem::path& project_file, std::string arrangement_id) override;

private:
    juce::PropertiesFile m_properties;
};

} // namespace rock_hero::editor::core
