/*!
\file project_loader.h
\brief UI-layer Rock Hero project loader.
*/

#pragma once

#include <filesystem>
#include <rock_hero/ui/i_project_loader.h>

namespace rock_hero::ui
{

/*!
\brief Concrete .rhp archive loader backed by JUCE ZipFile.

Archive extraction stays in the JUCE-facing UI module for now. The loader extracts the archive
into a temporary cache, reads the project manifest, and returns the selected arrangement data.
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

} // namespace rock_hero::ui
