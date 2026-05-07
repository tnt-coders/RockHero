#include "rock_importer.h"

#include "project_io.h"

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Builds a failed Rock package import result with a single message.
[[nodiscard]] std::expected<Song, std::string> failImport(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

} // namespace

// Imports a flat runtime package into the caller-provided song workspace.
std::expected<Song, std::string> RockImporter::importProject(
    const std::filesystem::path& source_path, const std::filesystem::path& workspace_directory)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source_path, filesystem_error))
    {
        return failImport("Rock package does not exist: " + source_path.string());
    }

    const auto extraction_error =
        project_io::extractPackageToWorkspace(source_path, workspace_directory);
    if (extraction_error.has_value())
    {
        return failImport(*extraction_error);
    }

    auto imported_song = project_io::readSong(workspace_directory);
    if (!imported_song.has_value())
    {
        return failImport(std::move(imported_song.error()));
    }

    return imported_song;
}

} // namespace rock_hero::core
