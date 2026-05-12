/*!
\file rock_importer.h
\brief Rock Hero runtime package importer.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/i_project_importer.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Imports playable Rock Hero runtime packages into editor workspaces.

`.rock` packages store runtime song content at the archive root. The importer extracts that flat
content into the caller-provided song workspace so the imported package can be saved later as a
native `.rhp` editor project.
*/
class RockImporter final : public IProjectImporter
{
public:
    /*!
    \brief Imports a flat `.rock` package into a song workspace.
    \param source_path Runtime package to import.
    \param workspace_directory Existing song workspace directory to populate.
    \return Imported song data, or a failure message.
    */
    [[nodiscard]] std::expected<Song, std::string> importProject(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};

} // namespace rock_hero::common::core
