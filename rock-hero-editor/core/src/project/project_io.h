/*!
\file project_io.h
\brief Private editor project persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/domain/song.h>
#include <rock_hero/editor/core/project/project.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core::project_io
{

/*! \brief Fixed directory name used for native song content inside editor project packages. */
inline constexpr std::string_view g_song_directory_name{"song"};

/*!
\brief Reads editor-only project state from an extracted editor project root.
\param workspace_directory Extracted editor project workspace containing project.json.
\return Parsed editor state, or a typed project failure.
*/
[[nodiscard]] std::expected<ProjectEditorState, ProjectError> readProjectDocument(
    const std::filesystem::path& workspace_directory);

/*!
\brief Writes editor-only project state to an extracted editor project root.
\param workspace_directory Extracted editor project workspace that receives project.json.
\param editor_state Editor-only state to persist.
\param arrangement_ids Arrangement IDs available in the paired song document.
\return Empty success, or a typed project failure.
*/
[[nodiscard]] std::expected<void, ProjectError> writeProjectDocument(
    const std::filesystem::path& workspace_directory, const ProjectEditorState& editor_state,
    const std::vector<std::string>& arrangement_ids);

/*!
\brief Writes editor project files into an extracted project workspace.
\param workspace_directory Extracted editor project workspace to update.
\param song Song data to persist under the song directory.
\param editor_state Editor-only project state to persist at the workspace root.
\return Empty success, or a typed project failure.
*/
[[nodiscard]] std::expected<void, ProjectError> writeProjectFiles(
    const std::filesystem::path& workspace_directory, const common::core::Song& song,
    const ProjectEditorState& editor_state);

} // namespace rock_hero::editor::core::project_io
