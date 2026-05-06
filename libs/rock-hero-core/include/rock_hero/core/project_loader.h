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

The loader extracts the archive into a temporary cache, reads the song document, and returns
the parsed project data. The cache stays alive through Project.
*/
class ProjectLoader final : public IProjectLoader
{
public:
    /*!
    \brief Loads an .rhp package by extracting it and reading song.json.
    \param package_path Filesystem path to an .rhp archive.
    \return Loaded project data and cache ownership.
    */
    [[nodiscard]] ProjectLoadResult loadProject(const std::filesystem::path& package_path) override;
};

} // namespace rock_hero::core
