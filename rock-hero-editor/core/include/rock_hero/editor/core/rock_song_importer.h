/*!
\file rock_song_importer.h
\brief Native Rock Hero song package importer.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/editor/core/i_song_importer.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Imports native Rock Hero song packages into editor workspaces.

`.rock` packages store native song content at the archive root. The importer extracts that flat
content into the caller-provided song workspace so the song can be saved later as part of a native
`.rhp` editor project.
*/
class RockSongImporter final : public ISongImporter
{
public:
    /*!
    \brief Imports a flat `.rock` song package into a song workspace.
    \param source_path Native song package to import.
    \param workspace_directory Existing song workspace directory to populate.
    \return Imported song data, or a failure message.
    */
    [[nodiscard]] std::expected<common::core::Song, std::string> importSong(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};

} // namespace rock_hero::editor::core
