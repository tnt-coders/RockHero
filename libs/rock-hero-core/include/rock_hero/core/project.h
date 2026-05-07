/*!
\file project.h
\brief Open Rock Hero project package context and workspace ownership.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/core/i_project_importer.h>
#include <rock_hero/core/song.h>
#include <rock_hero/core/timeline.h>
#include <string>

namespace rock_hero::core
{

/*!
\brief Editor-only state persisted by a native project package.

This state is intentionally separate from Song so published playable packages can treat song data
and editor session state as different concerns.
*/
struct ProjectEditorState
{
    /*! \brief Cursor position to restore when the project is opened. */
    TimePosition cursor_position{};

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
\brief Open project package context.

Project owns the extracted workspace directory because loaded Song audio asset paths can point
into it. Call close() when cleanup failures need to be reported. Destroying the Project removes
that workspace on a best-effort basis. Session owns the editable Song; Project owns only the
package/workspace context needed to load and save it.
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

    /*! \brief Transfers package state and workspace ownership from another project. */
    Project(Project&& other) noexcept;

    /*!
    \brief Replaces this project with another project's package state and workspace ownership.
    \param other Project to move from.
    \return Reference to this project.
    */
    Project& operator=(Project&& other) noexcept;

    /*!
    \brief Loads a Rock Hero project package into this project context.
    \param path Filesystem path to an .rhp archive.
    \return Parsed song data, or a failure message.
    */
    [[nodiscard]] std::expected<Song, std::string> load(const std::filesystem::path& path);

    /*!
    \brief Imports a foreign project package into a new unsaved workspace.
    \param source_path Foreign project package to import.
    \param importer Importer that understands the source project format.
    \return Imported song data, or a failure message.
    */
    [[nodiscard]] std::expected<Song, std::string> import(
        const std::filesystem::path& source_path, IProjectImporter& importer);

    /*!
    \brief Saves the supplied song to the currently open project package.
    \param song Song data to persist.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> save(const Song& song);

    /*!
    \brief Saves the supplied song and editor state to the currently open project package.
    \param song Song data to persist.
    \param editor_state Editor-only project state to persist.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> save(
        const Song& song, ProjectEditorState editor_state);

    /*!
    \brief Saves the supplied song to a package path and associates this project with it.
    \param path Destination .rhp package path.
    \param song Song data to persist.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> saveAs(
        const std::filesystem::path& path, const Song& song);

    /*!
    \brief Saves the supplied song and editor state to a package path and associates it.
    \param path Destination .rhp package path.
    \param song Song data to persist.
    \param editor_state Editor-only project state to persist.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> saveAs(
        const std::filesystem::path& path, const Song& song, ProjectEditorState editor_state);

    /*!
    \brief Publishes the supplied song to a runtime package without changing this project path.
    \param path Destination package path.
    \param song Song data to publish.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> publish(
        const std::filesystem::path& path, const Song& song);

    /*!
    \brief Closes the current project context and removes its extracted workspace.
    \return Empty success, or a failure message when workspace removal fails.
    */
    [[nodiscard]] std::expected<void, std::string> close();

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

} // namespace rock_hero::core
