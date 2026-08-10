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
\brief Reports whether a package-relative reference stays inside its workspace.

The one authority for the path-escape rule, and the security boundary for a package authored
somewhere else: it is public rather than private to the package format because `common/audio`
validates the same tone- and plugin-document references the package reader does, and a second copy
of a rule like this agrees with the first only by luck.

Every component is tested rather than the joined string, so the check can never route through
`path::string()`, which throws on Windows for a path outside the active code page — from inside APIs
that report failure as a typed value. A rooted path is rejected by the root-name and root-directory
tests, which together cover an absolute path under either platform's convention.

\param path Package-relative path taken from a song document, tone document, or archive entry.
\return True when the path is relative, non-empty, and never escapes the workspace root.
*/
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);

/*!
\brief Resolves an existing workspace file to a safe workspace-relative path.
\param workspace_directory Workspace directory that should contain the file.
\param asset_path File path that may be absolute or relative to the workspace.
\return Safe workspace-relative path, or empty when the file is missing or outside the workspace.
*/
[[nodiscard]] std::optional<std::filesystem::path> relativeWorkspacePath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& asset_path);

} // namespace rock_hero::common::core
