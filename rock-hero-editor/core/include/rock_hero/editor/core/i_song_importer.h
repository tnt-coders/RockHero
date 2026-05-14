/*!
\file i_song_importer.h
\brief Editor-owned boundary for importing song sources into editable workspaces.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/song.h>
#include <rock_hero/editor/core/song_import_error.h>

namespace rock_hero::editor::core
{

/*!
\brief Imports a song source into a caller-provided workspace.

Implementations read a source archive or directory, copy or convert the minimal required files into
the workspace directory, and return the imported song data. The returned Song must reference files
inside the supplied workspace.
*/
class ISongImporter
{
public:
    /*! \brief Creates an importer interface base. */
    ISongImporter() = default;

    /*! \brief Allows cleanup through the importer interface. */
    virtual ~ISongImporter() = default;

    /*! \brief Copying is disabled because importer implementations own format-specific state. */
    ISongImporter(const ISongImporter&) = delete;

    /*! \brief Copy assignment is disabled because importer implementations own unique state. */
    ISongImporter& operator=(const ISongImporter&) = delete;

    /*! \brief Moving is disabled because importers are used through stable interface references. */
    ISongImporter(ISongImporter&&) = delete;

    /*! \brief Move assignment is disabled because importers are used through stable references. */
    ISongImporter& operator=(ISongImporter&&) = delete;

    /*!
    \brief Imports a song source into an existing workspace directory.
    \param source_path Song source to read.
    \param workspace_directory Existing workspace directory to populate.
    \return Imported song data, or a typed import failure.
    */
    [[nodiscard]] virtual std::expected<common::core::Song, SongImportError> importSong(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) = 0;
};

} // namespace rock_hero::editor::core
