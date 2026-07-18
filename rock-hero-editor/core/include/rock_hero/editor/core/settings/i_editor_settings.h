/*!
\file i_editor_settings.h
\brief Framework-light persistence contract for app-local editor settings.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/settings/editor_settings_error.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief The app-local resume caret for one project: its exact musical address plus string.

Persisted as-is (never as a time value) so reopening a project lands the caret on the same grid
slot it was on — no tempo edit can happen without an open session (the caret model, 2026-07-17).
*/
struct EditorProjectCaret
{
    /*! \brief Musical grid position of the caret. */
    common::core::GridPosition position{};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*!
    \brief Compares two stored carets for equal value.
    \param lhs Left-hand caret.
    \param rhs Right-hand caret.
    \return True when both carets store equal values.
    */
    friend bool operator==(const EditorProjectCaret& lhs, const EditorProjectCaret& rhs) = default;
};

/*!
\brief Stores editor settings that live outside project packages.

This port represents app-local editor state such as startup restore paths and input calibration.
The active audio-device route lives on the editor's per-app AudioConfigStore, not here. Production
code persists it through EditorSettings; tests can use an in-memory implementation when they only
need controller settings behavior.
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
    \brief Reads the app-wide waveform visibility preference for the timeline's tablature lane.
    \return Stored visibility, or empty when the user has never toggled it.
    */
    [[nodiscard]] virtual std::optional<bool> waveformVisible() const = 0;

    /*!
    \brief Stores the app-wide waveform visibility preference.
    \param visible True when the waveform should draw behind the tablature lane.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setWaveformVisible(
        bool visible) = 0;

    /*!
    \brief Reads the directory the tone-file choosers should start in.
    \return Last-used tone file directory, or empty when the user has never saved or opened one.
    */
    [[nodiscard]] virtual std::optional<std::filesystem::path> toneFileDirectory() const = 0;

    /*!
    \brief Stores the directory the tone-file choosers should start in.
    \param directory Directory of the most recently opened or saved tone file.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setToneFileDirectory(
        std::filesystem::path directory) = 0;

    /*!
    \brief Reads whether the editor sources the game's audio configuration instead of its own.

    Editor workflow state, not audio config: it selects a read source and never affects the game.
    Absence means the user has never chosen and resolves to off via useGameAudioSettingsOrDefault;
    a stored on is only ever written when adopting the game's configuration actually succeeded.

    \return Stored choice, or empty when the user has never set it.
    */
    [[nodiscard]] virtual std::optional<bool> useGameAudioSettings() const = 0;

    /*!
    \brief Stores whether the editor sources the game's audio configuration instead of its own.
    \param enabled True to source the game's audio configuration, false to source the editor's own.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setUseGameAudioSettings(
        bool enabled) = 0;

    /*!
    \brief Reads whether the startup game-audio recommendation prompt is suppressed.

    The prompt recommends adopting the game's audio configuration when the toggle is off and a
    calibrated game configuration exists; its "don't show this message again" checkbox persists
    this flag. It suppresses only that recommendation — never the error popups that report a game
    configuration the editor was asked to use but cannot.

    \return Stored suppression, or empty when the user has never suppressed the prompt.
    */
    [[nodiscard]] virtual std::optional<bool> suppressGameAudioRecommendation() const = 0;

    /*!
    \brief Stores whether the startup game-audio recommendation prompt is suppressed.
    \param suppressed True to stop showing the startup recommendation prompt.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError>
    setSuppressGameAudioRecommendation(bool suppressed) = 0;

    /*!
    \brief Reads the app-wide minimum number of tablature string lanes to display.
    \return Stored minimum, or empty when the user has never chosen one.
    */
    [[nodiscard]] virtual std::optional<int> tabMinimumDisplayedStrings() const = 0;

    /*!
    \brief Stores the app-wide minimum number of tablature string lanes to display.
    \param minimum_strings Minimum lane count; zero means match the chart's string count.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> setTabMinimumDisplayedStrings(
        int minimum_strings) = 0;

    /*!
    \brief Reads the app-local resume caret stored for an editor project path.

    The caret persists as its exact musical address — grid position plus string — never as a
    time value: no tempo edit can happen without an open session, so the address round-trips
    to the same grid slot it was on (the caret model, 2026-07-17).

    \param project_file Project path whose caret should be restored.
    \return Stored caret, or absence when none is stored or the stored value is unreadable.
    */
    [[nodiscard]] virtual std::optional<EditorProjectCaret> projectCaretFor(
        const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local resume caret for an editor project path.
    \param project_file Project path that owns the caret.
    \param caret Caret to restore next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectCaret(
        const std::filesystem::path& project_file, const EditorProjectCaret& caret) = 0;

    /*!
    \brief Reads the app-local timeline grid note value stored for an editor project path.
    \param project_file Project path whose grid note value should be restored.
    \return Grid step as a fraction of a whole note, or absence when none is stored or unreadable.
    */
    [[nodiscard]] virtual std::optional<common::core::Fraction> projectGridNoteValueFor(
        const std::filesystem::path& project_file) const = 0;

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
    \return Zoom in pixels per second, or absence when none is stored or the value is unreadable.
    */
    [[nodiscard]] virtual std::optional<double> projectTimelineZoomFor(
        const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local timeline zoom for an editor project path.
    \param project_file Project path that owns the zoom.
    \param pixels_per_second Horizontal timeline scale to restore on next open.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectTimelineZoom(
        const std::filesystem::path& project_file, double pixels_per_second) = 0;

    /*!
    \brief Reads the app-local arrangement to display first for an editor project path.
    \param project_file Project path whose displayed arrangement should be restored.
    \return Stored arrangement id, or absence when none is stored or the value is unreadable.
    */
    [[nodiscard]] virtual std::optional<std::string> projectSelectedArrangementFor(
        const std::filesystem::path& project_file) const = 0;

    /*!
    \brief Stores or replaces the app-local arrangement to display first for an editor project path.
    \param project_file Project path that owns the displayed-arrangement choice.
    \param arrangement_id Arrangement id to display next time this path is opened.
    \return Empty success, or a typed settings failure.
    */
    [[nodiscard]] virtual std::expected<void, EditorSettingsError> saveProjectSelectedArrangement(
        const std::filesystem::path& project_file, std::string arrangement_id) = 0;

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
