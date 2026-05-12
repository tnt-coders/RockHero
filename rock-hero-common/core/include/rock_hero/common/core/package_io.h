/*!
\file package_io.h
\brief Shared safe package archive helpers.
*/

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Extracts a package archive into an existing workspace directory.
\param package_path Package archive to extract.
\param workspace_directory Existing directory that receives extracted package entries.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> extractPackageToWorkspace(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory);

/*!
\brief Rewrites a package archive from a workspace directory.
\param workspace_directory Directory whose regular files become package entries.
\param package_path Destination package archive path.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> writeWorkspaceToPackage(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& package_path);

/*!
\brief Resolves an existing workspace file to a safe workspace-relative path.
\param workspace_directory Workspace directory that should contain the file.
\param asset_path File path that may be absolute or relative to the workspace.
\return Safe workspace-relative path, or empty when the file is missing or outside the workspace.
*/
[[nodiscard]] std::optional<std::filesystem::path> relativeWorkspacePath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& asset_path);

} // namespace rock_hero::common::core
