/*!
\file i_editor_settings.h
\brief Framework-light persistence contract for app-local editor settings.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input_calibration_state.h>
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
    */
    virtual void setLastOpenProject(std::optional<std::filesystem::path> project_file) = 0;

    /*!
    \brief Reads the project path whose previous startup restore did not finish.
    \return Interrupted restore path, or empty when startup restore can proceed normally.
    */
    [[nodiscard]] virtual std::optional<std::filesystem::path> interruptedRestoreProject()
        const = 0;

    /*!
    \brief Stores or clears the project path whose startup restore is in progress.
    \param project_file Interrupted restore path, or empty to clear the recovery prompt state.
    */
    virtual void setInterruptedRestoreProject(
        std::optional<std::filesystem::path> project_file) = 0;

    /*!
    \brief Reads the opaque serialized audio-device state stored by a previous editor session.
    \return Stored state, or empty when no audio-device state should be restored.
    */
    [[nodiscard]] virtual std::optional<std::string> audioDeviceState() const = 0;

    /*!
    \brief Stores or clears the opaque serialized audio-device state.
    \param serialized_state Serialized state to restore on next launch, or empty to clear it.
    */
    virtual void setAudioDeviceState(std::optional<std::string> serialized_state) = 0;

    /*!
    \brief Reads app-local input calibration for one physical input route.
    \param identity Physical input route to look up.
    \return Calibration state for the supplied route, or empty when unavailable.
    */
    [[nodiscard]] virtual std::optional<common::audio::InputCalibrationState> inputCalibrationFor(
        const common::audio::InputDeviceIdentity& identity) const = 0;

    /*!
    \brief Stores or replaces app-local input calibration for its physical route.
    \param calibration_state Calibration state to save.
    */
    virtual void saveInputCalibration(common::audio::InputCalibrationState calibration_state) = 0;

    /*!
    \brief Removes app-local input calibration for one physical input route.
    \param identity Physical input route to remove.
    */
    virtual void removeInputCalibration(const common::audio::InputDeviceIdentity& identity) = 0;

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
