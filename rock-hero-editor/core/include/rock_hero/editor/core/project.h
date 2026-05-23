/*!
\file project.h
\brief Editor project package context and workspace ownership.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/audio_loudness_metadata.h>
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/i_song_importer.h>
#include <rock_hero/editor/core/project_error.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Function that renders a loudness-normalized copy of a source audio file.

Injected into Project::import to keep the test surface at the public Project API. Production
composition defaults this alias to common::audio::normalizeAudioFile; tests pass fakes that
return a controlled AudioNormalizationOutcome without touching the loudness analyzer.
*/
using AudioNormalizeFunction = std::function<
    std::expected<common::audio::AudioNormalizationOutcome, common::audio::AudioNormalizationError>(
        const std::filesystem::path& input, const std::filesystem::path& output,
        const common::core::AudioNormalizationTarget& target)>;

/*!
\brief Editor-only state persisted by an editor project package.

This state is intentionally separate from Song so native song packages can treat song data
and editor session state as different concerns.
*/
struct ProjectEditorState
{
    /*! \brief Cursor position to restore when the project is opened. */
    common::core::TimePosition cursor_position{};

    /*! \brief Arrangement ID to display when the project is opened, if one was saved. */
    std::optional<std::string> selected_arrangement;

    /*!
    \brief Compares two editor-state values by their stored fields.
    \param lhs Left-hand editor state.
    \param rhs Right-hand editor state.
    \return True when both editor-state values store equal fields.
    */
    friend bool operator==(const ProjectEditorState& lhs, const ProjectEditorState& rhs) = default;
};

/*!
\brief Editor project package context.

Project owns the extracted workspace directory because loaded Song audio asset paths can point
into it. Call close() when cleanup failures need to be reported. Destroying the Project removes
that workspace on a best-effort basis. Session owns the editable Song; Project owns only the
project package and workspace context needed to load and save it.
*/
class Project
{
public:
    /*! \brief Creates an unopened project context. */
    Project() = default;

    /*! \brief Closes the project context without allowing cleanup errors to escape. */
    ~Project() noexcept;

    /*! \brief Copying is disabled because workspace ownership is unique. */
    Project(const Project&) = delete;

    /*! \brief Copy assignment is disabled because workspace ownership is unique. */
    Project& operator=(const Project&) = delete;

    /*! \brief Transfers project package state and workspace ownership from another project. */
    Project(Project&& other) noexcept;

    /*!
    \brief Replaces this project with another project's package path and workspace ownership.
    \param other Project to move from.
    \return Reference to this project.
    */
    Project& operator=(Project&& other) noexcept;

    /*!
    \brief Loads a Rock Hero project package into this project context.
    \param path Filesystem path to an .rhp archive.
    \return Parsed song data, or a typed project failure.
    */
    [[nodiscard]] std::expected<common::core::Song, ProjectError> load(
        const std::filesystem::path& path);

    /*!
    \brief Imports a song source into a new unsaved project workspace.

    Every imported arrangement's backing audio is rendered through normalize_audio so the project
    workspace only ever contains canonical normalized WAV assets. Raw imported audio is deleted
    after every arrangement has been retargeted to the normalized output.

    \param source_path Song source to import.
    \param importer Importer that understands the source song format.
    \param target Loudness target the imported backing audio is rendered against.
    \param normalize_audio Function used to render each unique imported audio asset. Defaults to
    common::audio::normalizeAudioFile; tests can inject a fake that captures invocations and
    returns a controlled AudioNormalizationOutcome.
    \return Imported song data, or a typed project failure.
    */
    [[nodiscard]] std::expected<common::core::Song, ProjectError> import(
        const std::filesystem::path& source_path, ISongImporter& importer,
        const common::core::AudioNormalizationTarget& target = {},
        const AudioNormalizeFunction& normalize_audio = common::audio::normalizeAudioFile);

    /*!
    \brief Saves the supplied song to the currently open project package.
    \param song Song data to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> save(const common::core::Song& song);

    /*!
    \brief Saves the supplied song and editor state to the currently open project package.
    \param song Song data to persist.
    \param editor_state Editor-only project state to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> save(
        const common::core::Song& song, ProjectEditorState editor_state);

    /*!
    \brief Saves the supplied song to a project package path and associates it.
    \param path Destination `.rhp` project package path.
    \param song Song data to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> saveAs(
        const std::filesystem::path& path, const common::core::Song& song);

    /*!
    \brief Saves the supplied song and editor state to a project package path and associates it.
    \param path Destination `.rhp` project package path.
    \param song Song data to persist.
    \param editor_state Editor-only project state to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> saveAs(
        const std::filesystem::path& path, const common::core::Song& song,
        ProjectEditorState editor_state);

    /*!
    \brief Publishes the supplied song to a native song package without changing this project path.
    \param path Destination native song package path.
    \param song Song data to publish.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> publish(
        const std::filesystem::path& path, const common::core::Song& song);

    /*!
    \brief Closes the current project context and removes its extracted workspace.
    \return Empty success, or a typed project failure when workspace removal fails.
    */
    [[nodiscard]] std::expected<void, ProjectError> close();

    /*!
    \brief Returns the file path associated with this project.
    \return Project file path, or an empty path before load succeeds.
    */
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /*!
    \brief Returns the directory containing extracted files referenced by loaded song data.
    \return Extracted project workspace directory, or an empty path before load succeeds.
    */
    [[nodiscard]] const std::filesystem::path& workspaceDirectory() const noexcept;

    /*!
    \brief Returns editor state read from the current project package.
    \return Editor-only project state, or defaults before load succeeds.
    */
    [[nodiscard]] const ProjectEditorState& editorState() const noexcept;

private:
    std::filesystem::path m_path;
    std::filesystem::path m_workspace_directory;
    ProjectEditorState m_editor_state;
};

} // namespace rock_hero::editor::core
