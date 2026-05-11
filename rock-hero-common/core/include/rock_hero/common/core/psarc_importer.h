/*!
\file psarc_importer.h
\brief Rocksmith PSARC importer.
*/

#pragma once

#include <rock_hero/common/core/i_project_importer.h>

namespace rock_hero::common::core
{

/*!
\brief Imports the minimal playable song data from a Rocksmith PSARC package.

The importer converts Rocksmith SNG arrangements to XML and copies them into the caller-provided
workspace. Embedded arrangement XML is used only when no SNG arrangement XML can be produced. It
does not parse note data from the arrangement XML yet.
*/
class PsarcImporter final : public IProjectImporter
{
public:
    /*!
    \brief Imports PSARC metadata, arrangement XML, and backing audio into a workspace.
    \param source_path PSARC package to import.
    \param workspace_directory Existing workspace directory to populate.
    \return Imported song data, or a failure message.
    */
    [[nodiscard]] std::expected<Song, std::string> importProject(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};

} // namespace rock_hero::common::core
