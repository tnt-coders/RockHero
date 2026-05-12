/*!
\file archive_io.h
\brief Shared safe ZIP archive helpers.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Extracts a ZIP archive into an existing workspace directory.
\param archive_path Archive to extract.
\param workspace_directory Existing directory that receives extracted archive entries.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> extractArchiveToWorkspace(
    const std::filesystem::path& archive_path, const std::filesystem::path& workspace_directory);

/*!
\brief Rewrites a ZIP archive from a workspace directory.
\param workspace_directory Directory whose regular files become archive entries.
\param archive_path Destination archive path.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> writeWorkspaceToArchive(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& archive_path);

} // namespace rock_hero::common::core
