/*!
\file editor_settings.h
\brief App-local settings for Rock Hero Editor.
*/

#pragma once

#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/input_calibration_state.h>
#include <rock_hero/editor/core/i_editor_settings.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Stores editor settings that live outside project packages.

EditorSettings is the JUCE-backed implementation of IEditorSettings used by production app
composition. These settings are per-user application state, not `.rhp` project data and not
`.rock` package data.
*/
class EditorSettings final : public IEditorSettings
{
public:
    /*! \brief Opens the editor settings file using the app's standard per-user location. */
    EditorSettings();

    /*!
    \brief Opens editor settings at an explicit native path.
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
    \brief Reads the opaque serialized audio-device state stored by a previous editor session.
    \return Stored state, or empty when no audio-device state should be restored.
    */
    [[nodiscard]] std::optional<std::string> audioDeviceState() const override;

    /*!
    \brief Stores or clears the opaque serialized audio-device state.
    \param serialized_state Serialized state to restore on next launch, or empty to clear it.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setAudioDeviceState(
        std::optional<std::string> serialized_state) override;

    /*!
    \brief Reads the app-local resume cursor stored for an editor project path.
    \param project_file Project path whose cursor should be restored.
    \return Cursor position, absence, or a typed settings failure.
    */
    [[nodiscard]] std::expected<std::optional<common::core::TimePosition>, EditorSettingsError>
    projectCursorPositionFor(const std::filesystem::path& project_file) const override;

    /*!
    \brief Stores or replaces the app-local resume cursor for an editor project path.
    \param project_file Project path that owns the cursor.
    \param cursor_position Cursor position to restore next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveProjectCursorPosition(
        const std::filesystem::path& project_file,
        common::core::TimePosition cursor_position) override;

    /*!
    \brief Reads app-local input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed settings failure.
    */
    [[nodiscard]] std::expected<
        std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
    inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const override;

    /*!
    \brief Stores or replaces app-local input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> saveInputCalibration(
        common::audio::InputCalibrationState calibration_state) override;

    /*!
    \brief Removes app-local input calibration for one physical input route.
    \param identity Physical input route to remove.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> removeInputCalibration(
        const common::audio::InputDeviceIdentity& identity) override;

private:
    juce::PropertiesFile m_properties;
};

} // namespace rock_hero::editor::core
