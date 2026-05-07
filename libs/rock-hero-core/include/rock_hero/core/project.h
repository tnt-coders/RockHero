/*!
\file project.h
\brief Open Rock Hero project package context and workspace ownership.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/core/i_project_importer.h>
#include <rock_hero/core/song.h>
#include <string>

namespace rock_hero::core
{

/*!
\brief Open project package context.

Project owns the extracted workspace directory because loaded Song audio asset paths can point
into it. Destroying the Project removes that workspace on a best-effort basis. Session owns the
editable Song; Project owns only the package/workspace context needed to load and save it.
*/
class Project
{
public:
    /*! \brief Creates an unopened project context. */
    Project() = default;

    /*! \brief Removes the owned workspace directory, if one is present. */
    ~Project();

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
    \brief Saves the supplied song to a package path and associates this project with it.
    \param path Destination .rhp package path.
    \param song Song data to persist.
    \return Empty success, or a failure message.
    */
    [[nodiscard]] std::expected<void, std::string> saveAs(
        const std::filesystem::path& path, const Song& song);

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

private:
    std::filesystem::path m_path;
    std::filesystem::path m_workspace_directory;

    // Removes the currently owned workspace directory and clears ownership.
    void resetWorkspace() noexcept;
};

} // namespace rock_hero::core
