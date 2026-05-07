/*!
\file project_io.h
\brief Private Rock Hero project and package persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/core/project.h>
#include <rock_hero/core/song.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::core::project_io
{

/*! \brief Fixed directory name used for runtime song content inside native projects. */
inline constexpr std::string_view g_song_directory_name{"song"};

/*!
\brief Extracts a package archive into an existing workspace directory.
\param package_path Package archive to extract.
\param workspace_directory Existing directory that receives extracted package entries.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> extractPackageToWorkspace(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory);

/*!
\brief Reads runtime song data from an extracted song directory.
\param directory Directory containing song.json and the files referenced by it.
\return Parsed song data, or a failure message.
*/
[[nodiscard]] std::expected<Song, std::string> readSong(const std::filesystem::path& directory);

/*!
\brief Reads editor-only project state from an extracted native project root.
\param workspace_directory Extracted native project workspace containing project.json.
\return Parsed editor state, or a failure message.
*/
[[nodiscard]] std::expected<ProjectEditorState, std::string> readProjectDocument(
    const std::filesystem::path& workspace_directory);

/*!
\brief Writes editor-only project state to an extracted native project root.
\param workspace_directory Extracted native project workspace that receives project.json.
\param editor_state Editor-only state to persist.
\param arrangement_ids Arrangement IDs available in the paired song document.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> writeProjectDocument(
    const std::filesystem::path& workspace_directory, const ProjectEditorState& editor_state,
    const std::vector<std::string>& arrangement_ids);

/*!
\brief Writes native project files into an extracted project workspace.
\param workspace_directory Extracted native project workspace to update.
\param song Song data to persist under the song directory.
\param editor_state Editor-only project state to persist at the workspace root.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::optional<std::string> writeProjectFiles(
    const std::filesystem::path& workspace_directory, const Song& song,
    const ProjectEditorState& editor_state);

/*!
\brief Writes runtime song files into a package-content directory.
\param song_directory Directory that receives song.json and referenced runtime content.
\param song Song data to persist.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::expected<void, std::string> writeSongFiles(
    const std::filesystem::path& song_directory, const Song& song);

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

} // namespace rock_hero::core::project_io
