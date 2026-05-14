/*!
\file archive_io.h
\brief Shared safe ZIP archive helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/archive_error.h>

namespace rock_hero::common::core
{

/*!
\brief Extracts a ZIP archive into an existing workspace directory.
\param archive_path Archive to extract.
\param workspace_directory Existing directory that receives extracted archive entries.
\return Empty success, or a typed archive failure.
*/
[[nodiscard]] std::expected<void, ArchiveError> extractArchiveToWorkspace(
    const std::filesystem::path& archive_path, const std::filesystem::path& workspace_directory);

/*!
\brief Rewrites a ZIP archive from a workspace directory.
\param workspace_directory Directory whose regular files become archive entries.
\param archive_path Destination archive path.
\return Empty success, or a typed archive failure.
*/
[[nodiscard]] std::expected<void, ArchiveError> writeWorkspaceToArchive(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& archive_path);

} // namespace rock_hero::common::core
