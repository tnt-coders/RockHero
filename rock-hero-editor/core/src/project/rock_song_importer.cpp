#include "rock_song_importer.h"

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/archive_io.h>
#include <rock_hero/common/core/rock_song_package.h>
#include <string>
#include <system_error>
#include <utility>

namespace rock_hero::editor::core
{

// Imports a flat native song package into the caller-provided song workspace.
std::expected<common::core::Song, SongImportError> RockSongImporter::importSong(
    const std::filesystem::path& source_path, const std::filesystem::path& workspace_directory)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source_path, filesystem_error))
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::MissingSource,
            "Native song package does not exist: " + source_path.string(),
        }};
    }

    const auto extraction_error =
        common::core::extractArchiveToWorkspace(source_path, workspace_directory);
    if (!extraction_error.has_value())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::ExtractionFailed,
            "Could not extract native song package: " + extraction_error.error().message,
        }};
    }

    auto imported_song = common::core::readRockSongPackageDirectory(workspace_directory);
    if (!imported_song.has_value())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::InvalidImportedSong,
            std::move(imported_song.error().message),
        }};
    }

    return std::expected<common::core::Song, SongImportError>{
        std::in_place, std::move(*imported_song)
    };
}

} // namespace rock_hero::editor::core
