/*!
\file i_project_loader.h
\brief Framework-free project-loader contract for Rock Hero project packages.
*/

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <rock_hero/core/song.h>
#include <string>

namespace rock_hero::core
{

/*!
\brief Owns an extracted project cache directory for the lifetime of a loaded project.
*/
class LoadedProjectCache final
{
public:
    /*! \brief Creates an empty cache owner. */
    LoadedProjectCache() noexcept = default;

    /*!
    \brief Takes ownership of an extracted cache directory.
    \param directory Directory to remove when this owner is destroyed or replaced.
    */
    explicit LoadedProjectCache(std::filesystem::path directory) noexcept;

    /*! \brief Removes the owned cache directory, if one is present. */
    ~LoadedProjectCache();

    /*! \brief Copying is disabled because cache ownership is unique. */
    LoadedProjectCache(const LoadedProjectCache&) = delete;

    /*! \brief Copy assignment is disabled because cache ownership is unique. */
    LoadedProjectCache& operator=(const LoadedProjectCache&) = delete;

    /*! \brief Transfers cache ownership from another owner. */
    LoadedProjectCache(LoadedProjectCache&& other) noexcept;

    /*!
    \brief Replaces this cache owner with another owner.
    \param other Cache owner to move from.
    \return Reference to this cache owner.
    */
    LoadedProjectCache& operator=(LoadedProjectCache&& other) noexcept;

    /*!
    \brief Returns the owned cache directory.
    \return Cache directory path, or an empty path for an empty owner.
    */
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    // Removes the currently owned directory and clears ownership.
    void reset() noexcept;

    // Directory removed by reset() and the destructor.
    std::filesystem::path m_directory;
};

/*!
\brief Project data returned by a project loader.
*/
struct LoadedProject
{
    /*! \brief Song data parsed from the loaded project. */
    Song song;

    /*! \brief Arrangement index selected by the project manifest. */
    std::size_t selected_arrangement_index{0};

    /*! \brief Cache owner that keeps extracted project files alive. */
    LoadedProjectCache cache;
};

/*!
\brief Result produced by loading a project.
*/
struct ProjectLoadResult
{
    /*! \brief Loaded project data when loading succeeded. */
    std::optional<LoadedProject> project;

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
