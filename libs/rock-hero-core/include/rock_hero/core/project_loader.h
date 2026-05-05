/*!
\file project_loader.h
\brief Core Rock Hero project package loader.
*/

#pragma once

#include <filesystem>
#include <rock_hero/core/i_project_loader.h>

namespace rock_hero::core
{

/*!
\brief Concrete .rhp archive loader backed by libzip.

The loader extracts the archive into a temporary cache, reads the project manifest, and returns
the selected arrangement data. The cache stays alive through LoadedProjectCache.
*/
class ProjectLoader final : public IProjectLoader
{
public:
    /*!
    \brief Loads an .rhp package by extracting it and reading the project manifest.
    \param package_path Filesystem path to an .rhp archive.
    \return Loaded project data, selected arrangement index, and cache ownership.
    */
    [[nodiscard]] ProjectLoadResult loadProject(const std::filesystem::path& package_path) override;
};

} // namespace rock_hero::core
