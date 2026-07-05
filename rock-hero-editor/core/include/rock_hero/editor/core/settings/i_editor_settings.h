/*!
\file i_editor_settings.h
\brief Framework-light persistence contract for app-local editor settings.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/settings/editor_settings_error.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Stores editor settings that live outside project packages.

This port represents app-local editor state such as startup restore paths, serialized audio-device
state, and input calibration. Production code persists it through EditorSettings; tests can use an
in-memory implementation when they only need controller settings behavior.
*/
class IEditorSettings
{
public:
    /*! \brief Destroys the editor-settings interface. */
    virtual ~IEditorSettings() = default;

    /*!
    \brief Reads the editor project path stored by a previous allowed editor exit.
    \return Stored project path, or empty when no project should be restored.
    */
    [[nodiscard]] virtual std::optional<std::filesystem::path> lastOpenProject() const = 0;

    /*!
    \brief Stores or clears the editor project path to restore on the next editor launch.
    \param project_file Project path to restore, or empty to clear restore state.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setLastOpenProject(
        std::optional<std::filesystem::path> project_file) = 0;

    /*!
    \brief Reads the project path whose previous startup restore did not finish.
    \return Interrupted restore path, or empty when startup restore can proceed normally.
    */
    [[nodiscard]] virtual std::optional<std::filesystem::path> interruptedRestoreProject()
        const = 0;

    /*!
    \brief Stores or clears the project path whose startup restore is in progress.
    \param project_file Interrupted restore path, or empty to clear the recovery prompt state.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setInterruptedRestoreProject(
        std::optional<std::filesystem::path> project_file) = 0;

    /*!
    \brief Reads the opaque serialized audio-device state stored by a previous editor session.
    \return Stored state, or empty when no audio-device state should be restored.
    */
    [[nodiscard]] virtual std::optional<std::string> audioDeviceState() const = 0;

    /*!
    \brief Stores or clears the opaque serialized audio-device state.
    \param serialized_state Serialized state to restore on next launch, or empty to clear it.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setAudioDeviceState(
        std::optional<std::string> serialized_state) = 0;

    /*!
    \brief Reads the app-local resume cursor stored for an editor project path.
    \param project_file Project path whose cursor should be restored.
    \return Cursor position, absence, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<
        std::optional<common::core::TimePosition>, EditorSettingsError>
    projectCursorPositionFor(const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local resume cursor for an editor project path.
    \param project_file Project path that owns the cursor.
    \param cursor_position Cursor position to restore next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectCursorPosition(
        const std::filesystem::path& project_file, common::core::TimePosition cursor_position) = 0;

    /*!
    \brief Reads the app-local timeline grid note value stored for an editor project path.
    \param project_file Project path whose grid note value should be restored.
    \return Grid step as a fraction of a whole note, absence, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<std::optional<common::core::Fraction>, EditorSettingsError>
    projectGridNoteValueFor(const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local timeline grid note value for an editor project path.
    \param project_file Project path that owns the grid note value.
    \param grid_note_value Grid step as a fraction of a whole note.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectGridNoteValue(
        const std::filesystem::path& project_file, common::core::Fraction grid_note_value) = 0;

    /*!
    \brief Reads the app-local timeline zoom stored for an editor project path.
    \param project_file Project path whose zoom should be restored.
    \return Zoom in pixels per second, absence, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<std::optional<double>, EditorSettingsError>
    projectTimelineZoomFor(const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local timeline zoom for an editor project path.
    \param project_file Project path that owns the zoom.
    \param pixels_per_second Horizontal timeline scale to restore on next open.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectTimelineZoom(
        const std::filesystem::path& project_file, double pixels_per_second) = 0;

    /*!
    \brief Reads app-local input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state, absence, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<
        std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
    inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const = 0;

    /*!
    \brief Stores or replaces app-local input calibration for its physical route.
    \param calibration_state Calibration state to save.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveInputCalibration(
        common::audio::InputCalibrationState calibration_state) = 0;

    /*!
    \brief Removes app-local input calibration for one physical input route.
    \param identity Physical input route to remove.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> removeInputCalibration(
        const common::audio::InputDeviceIdentity& identity) = 0;

protected:
    /*! \brief Creates the editor-settings interface. */
    IEditorSettings() = default;

    /*! \brief Copies the editor-settings interface. */
    IEditorSettings(const IEditorSettings&) = default;

    /*! \brief Moves the editor-settings interface. */
    IEditorSettings(IEditorSettings&&) = default;

    /*!
    \brief Assigns the editor-settings interface from another interface.
    \return Reference to this editor-settings interface.
    */
    IEditorSettings& operator=(const IEditorSettings&) = default;

    /*!
    \brief Move-assigns the editor-settings interface.
    \return Reference to this editor-settings interface.
    */
    IEditorSettings& operator=(IEditorSettings&&) = default;
};

} // namespace rock_hero::editor::core
