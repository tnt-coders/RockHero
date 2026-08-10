#include "package/workspace_paths.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace rock_hero::common::core
{

// The single path-escape rule for every reference a package carries, shared by the package reader,
// the package writer, and common/audio's tone- and plugin-document validation.
bool isSafeRelativePath(const std::filesystem::path& path)
{
    // A rooted path is what "absolute" means under either platform convention (POSIX: a root
    // directory; Windows: a root name and a root directory), so these two tests already cover it.
    if (path.empty() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    // Per component rather than over the joined string: generic_string() is the encoding-safe
    // spelling, while path::string() throws for a path outside the active Windows code page.
    for (const std::filesystem::path& part : path)
    {
        const std::string text = part.generic_string();
        if (text.empty() || text == "." || text == ".." || text.find(':') != std::string::npos)
        {
            return false;
        }
    }

    return true;
}

// Resolves an asset path and reports its workspace-relative spelling. lexically_relative can walk
// upward out of the workspace, so the result still passes the escape rule above.
std::optional<std::filesystem::path> relativeWorkspacePath(
    const std::filesystem::path& workspace_directory, const std::filesystem::path& asset_path)
{
    const std::filesystem::path workspace = workspace_directory.lexically_normal();
    const std::filesystem::path resolved_path =
        (asset_path.is_absolute() ? asset_path : workspace / asset_path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }

    // Intentionally non-const so return-by-value can move the path.
    std::filesystem::path relative_path = resolved_path.lexically_relative(workspace);
    if (!isSafeRelativePath(relative_path))
    {
        return std::nullopt;
    }

    return relative_path;
}

} // namespace rock_hero::common::core
