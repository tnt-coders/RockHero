/*!
\file gp_song_importer.h
\brief Guitar Pro .gp song importer.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/editor/core/project/i_song_importer.h>

namespace rock_hero::editor::core
{

/*!
\brief Imports Guitar Pro 7/8 .gp files into editor workspaces.

A .gp file is a zip container holding the score document (Content/score.gpif) and, for scores
authored against a backing track, the embedded audio asset. The importer converts every track
into an arrangement with a chart, builds the tempo map from the score's audio sync points so the
notes line up with the backing audio, and copies the embedded audio into the workspace. Scores
without embedded audio are rejected: an arrangement cannot exist without backing audio.
*/
class GpSongImporter final : public ISongImporter
{
public:
    /*!
    \brief Imports a Guitar Pro .gp file into a song workspace.
    \param source_path Guitar Pro file to import.
    \param workspace_directory Existing song workspace directory to populate.
    \return Imported song data, or a typed import failure.
    */
    [[nodiscard]] std::expected<common::core::Song, SongImportError> importSong(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};

} // namespace rock_hero::editor::core
