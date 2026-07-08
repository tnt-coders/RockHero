/*!
\file project_io.h
\brief Private editor project persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/editor/core/project/project.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core::project_io
{

/*! \brief Fixed directory name used for native song content inside editor project packages. */
inline constexpr std::string_view g_song_directory_name{"song"};

/*!
\brief Validates the editor project manifest at an extracted editor project root.
\param workspace_directory Extracted editor project workspace containing project.json.
\return Empty success when project.json is a supported manifest, or a typed project failure.
*/
[[nodiscard]] std::expected<void, ProjectError> readProjectDocument(
    const std::filesystem::path& workspace_directory);

/*!
\brief Writes the editor project manifest to an extracted editor project root.
\param workspace_directory Extracted editor project workspace that receives project.json.
\return Empty success, or a typed project failure.
*/
[[nodiscard]] std::expected<void, ProjectError> writeProjectDocument(
    const std::filesystem::path& workspace_directory);

/*!
\brief Writes editor project files into an extracted project workspace.
\param workspace_directory Extracted editor project workspace to update.
\param song Song data to persist under the song directory.
\return Empty success, or a typed project failure.
*/
[[nodiscard]] std::expected<void, ProjectError> writeProjectFiles(
    const std::filesystem::path& workspace_directory, const common::core::Song& song);

} // namespace rock_hero::editor::core::project_io
