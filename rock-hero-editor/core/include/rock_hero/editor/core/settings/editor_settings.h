/*!
\file editor_settings.h
\brief App-local settings for Rock Hero Editor.
*/

#pragma once

#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/settings/i_audio_config_store.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Stores editor settings that live outside project packages.

EditorSettings is the JUCE-backed implementation of IEditorSettings used by production app
composition. Scalar values and XML-valued histories are stored in the app properties file. These
settings are per-user application state, not `.rhp` project data and not `.rock` package data.

The editor's audio configuration (active device route and input calibration) lives on a separate
per-app AudioConfigStore that EditorSettings owns, so it partitions cleanly from workflow state. App
composition injects that store, via audioConfigStore(), into the controller for device-route
persist/restore and into the shared LiveInputMonitor for calibration read/write; EditorSettings no
longer exposes calibration accessors of its own.
*/
class EditorSettings final : public IEditorSettings
{
public:
    /*! \brief Opens the editor settings file using the app's standard per-user location. */
    EditorSettings();

    /*!
    \brief Opens editor settings at an explicit native path.

    The owned audio-config store opens at a sibling of the settings file (see audioConfigFileFor)
    so lifecycle behavior can be exercised in isolation without a shared writer.

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
    \brief Reads whether the editor sources the game's audio configuration instead of its own.
    \return Stored choice, or empty when the user has never set it.
    */
    [[nodiscard]] std::optional<bool> useGameAudioSettings() const override;

    /*!
    \brief Stores whether the editor sources the game's audio configuration instead of its own.
    \param enabled True to source the game's audio configuration, false to source the editor's own.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] std::expected<void, EditorSettingsError> setUseGameAudioSettings(
        bool enabled) override;

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
    \brief Reads the app-local resume cursor stored for an editor project path.
    \param project_file Project path whose cursor should be restored.
    \return Cursor position, or absence when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] std::optional<common::core::TimePosition> projectCursorPositionFor(
        const std::filesystem::path& project_file) const override;

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

    /*!
    \brief Returns the editor's owned audio-config store for device-route persist/restore.

    App composition injects this into the controller so the device route and this settings object's
    delegated calibration share one file with exactly one writer.

    \return Audio-config store backing this editor's device route and input calibration.
    */
    [[nodiscard]] common::audio::IAudioConfigStore& audioConfigStore() noexcept;

    /*!
    \brief Reads the legacy serialized audio-device state from the pre-migration settings file.

    Migration-only accessor: reads the obsolete `audioDeviceState` key so the one-shot migration can
    move it onto the audio-config store. Not part of the persistence contract.

    \return Stored legacy device state, or empty when none is present.
    */
    [[nodiscard]] std::optional<std::string> readLegacyAudioDeviceState() const;

    /*! \brief Clears the legacy serialized audio-device state key after a successful migration. */
    void clearLegacyAudioDeviceState();

    /*!
    \brief Reads legacy input calibration records from the pre-migration settings file.

    Migration-only accessor: decodes the obsolete calibration history so the one-shot migration can
    re-save each record through the audio-config store. Malformed or incomplete records are dropped.

    \return Decoded legacy calibration records, or empty when none are present.
    */
    [[nodiscard]] std::vector<common::audio::InputCalibrationState> readLegacyInputCalibrations()
        const;

    /*! \brief Clears the legacy input calibration history key after a successful migration. */
    void clearLegacyInputCalibrations();

    /*!
    \brief Derives the audio-config file path a settings file's owned store opens.
    \param settings_file Editor settings file path.
    \return Sibling audio-config file path used by the owned AudioConfigStore.
    */
    [[nodiscard]] static std::filesystem::path audioConfigFileFor(
        const std::filesystem::path& settings_file);

private:
    juce::PropertiesFile m_properties;

    // Owned per-app audio-config store holding this editor's device route and input calibration.
    // Constructed after m_properties so both files open before any delegated access occurs.
    common::audio::AudioConfigStore m_audio_config_store;
};

} // namespace rock_hero::editor::core
