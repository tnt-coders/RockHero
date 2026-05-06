/*!
\file i_project_importer.h
\brief Project-owned boundary for importing foreign project formats.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/core/song.h>
#include <string>

namespace rock_hero::core
{

/*!
\brief Imports a foreign project format into a caller-provided workspace.

Implementations read a source package, copy or convert the minimal required files into the
workspace directory, and return the converted song data. The returned Song must reference files
inside the supplied workspace.
*/
class IProjectImporter
{
public:
    /*! \brief Allows cleanup through the importer interface. */
    virtual ~IProjectImporter() = default;

    /*!
    \brief Imports a foreign project package into an existing workspace directory.
    \param source_path Foreign project package to read.
    \param workspace_directory Existing workspace directory to populate.
    \return Imported song data, or a failure message.
    */
    [[nodiscard]] virtual std::expected<Song, std::string> importProject(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) = 0;
};

} // namespace rock_hero::core
