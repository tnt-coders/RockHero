/*!
\file workspace_paths.h
\brief Shared safe workspace path helpers.
*/

#pragma once

#include <filesystem>
#include <optional>

namespace rock_hero::common::core
{

/*!
\brief Resolves an existing workspace file to a safe workspace-relative path.
\param workspace_directory Workspace directory that should contain the file.
\param asset_path File path that may be absolute or relative to the workspace.
\return Safe workspace-relative path, or empty when the file is missing or outside the workspace.
*/
[[nodiscard]] std::optional<std::filesystem::path> relativeWorkspacePath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& asset_path);

} // namespace rock_hero::common::core
