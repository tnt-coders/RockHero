/*!
\file i_project_loader.h
\brief Framework-free project-loader contract for Rock Hero project packages.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <rock_hero/core/song.h>
#include <string>

namespace rock_hero::core
{

/*!
\brief Project data returned by a project loader.

Project owns the extracted cache directory because parsed audio asset paths can point into it.
Destroying the Project removes that directory on a best-effort basis.
*/
struct Project
{
    /*!
    \brief Creates a loaded project over parsed song data and an extracted cache directory.
    \param song_data Song data parsed from the project package.
    \param extracted_cache_directory Directory removed when this project is destroyed or replaced.
    */
    Project(Song song_data, std::filesystem::path extracted_cache_directory);

    /*! \brief Removes the owned cache directory, if one is present. */
    ~Project();

    /*! \brief Copying is disabled because cache ownership is unique. */
    Project(const Project&) = delete;

    /*! \brief Copy assignment is disabled because cache ownership is unique. */
    Project& operator=(const Project&) = delete;

    /*! \brief Transfers project data and cache ownership from another project. */
    Project(Project&& other);

    /*!
    \brief Replaces this project with another project's data and cache ownership.
    \param other Project to move from.
    \return Reference to this project.
    */
    Project& operator=(Project&& other);

    /*! \brief Song data parsed from the loaded project. */
    Song song;

    /*! \brief Directory containing extracted project files referenced by song data. */
    std::filesystem::path cache_directory;

private:
    // Removes the currently owned cache directory and clears ownership.
    void resetCache() noexcept;
};

/*!
\brief Result produced by loading a project.
*/
struct ProjectLoadResult
{
    /*! \brief Loaded project data when loading succeeded. */
    std::optional<Project> project;

    /*! \brief Human-readable failure detail when project is empty. */
    std::string error_message;

    /*!
    \brief Reports whether project loading succeeded.
    \return True when project contains loaded data.
    */
    [[nodiscard]] bool succeeded() const noexcept
    {
        return project.has_value();
    }
};

/*! \brief Framework-free project-load port consumed by apps and workflow code. */
class IProjectLoader
{
public:
    /*! \brief Destroys the project-loader interface. */
    virtual ~IProjectLoader() = default;

    /*!
    \brief Loads a Rock Hero project.
    \param project_path Filesystem path selected by the user.
    \return Loaded project data, or a failure message.
    */
    [[nodiscard]] virtual ProjectLoadResult loadProject(
        const std::filesystem::path& project_path) = 0;

protected:
    /*! \brief Creates the project-loader interface. */
    IProjectLoader() = default;

    /*! \brief Copies the project-loader interface. */
    IProjectLoader(const IProjectLoader&) = default;

    /*! \brief Moves the project-loader interface. */
    IProjectLoader(IProjectLoader&&) = default;

    /*!
    \brief Assigns the project-loader interface from another interface.
    \return Reference to this project-loader interface.
    */
    IProjectLoader& operator=(const IProjectLoader&) = default;

    /*!
    \brief Move-assigns the project-loader interface from another interface.
    \return Reference to this project-loader interface.
    */
    IProjectLoader& operator=(IProjectLoader&&) = default;
};

} // namespace rock_hero::core
