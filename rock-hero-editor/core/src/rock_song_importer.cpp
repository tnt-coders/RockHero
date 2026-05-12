#include "rock_song_importer.h"

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/archive_io.h>
#include <rock_hero/common/core/song_package.h>
#include <string>
#include <system_error>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Builds a failed native song package import result with a single message.
[[nodiscard]] std::expected<common::core::Song, std::string> failImport(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

} // namespace

// Imports a flat native song package into the caller-provided song workspace.
std::expected<common::core::Song, std::string> RockSongImporter::importSong(
    const std::filesystem::path& source_path, const std::filesystem::path& workspace_directory)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source_path, filesystem_error))
    {
        return failImport("Native song package does not exist: " + source_path.string());
    }

    const auto extraction_error =
        common::core::extractArchiveToWorkspace(source_path, workspace_directory);
    if (extraction_error.has_value())
    {
        return failImport("Could not extract native song package: " + *extraction_error);
    }

    auto imported_song = common::core::readSongPackageDirectory(workspace_directory);
    if (!imported_song.has_value())
    {
        return failImport(std::move(imported_song.error()));
    }

    return imported_song;
}

} // namespace rock_hero::editor::core
