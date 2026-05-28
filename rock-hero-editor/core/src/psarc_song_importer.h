/*!
\file psarc_song_importer.h
\brief Rocksmith PSARC song importer.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/editor/core/i_song_importer.h>

namespace rock_hero::editor::core
{

/*!
\brief Imports the minimal playable song data from a Rocksmith PSARC package.

The importer converts Rocksmith SNG arrangements to XML and copies them into the caller-provided
workspace. Embedded arrangement XML is used only when no SNG arrangement XML can be produced. It
does not parse note data from the arrangement XML yet.
*/
class PsarcSongImporter final : public ISongImporter
{
public:
    /*!
    \brief Imports PSARC song metadata, arrangement XML, and backing audio into a workspace.
    \param source_path PSARC package to import.
    \param workspace_directory Existing workspace directory to populate.
    \return Imported song data, or a typed import failure.
    */
    [[nodiscard]] std::expected<common::core::Song, SongImportError> importSong(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};

} // namespace rock_hero::editor::core
