/*!
\file library_scan_roots.h
\brief Resolves the effective library scan roots from settings and the app-data location.
*/

#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace rock_hero::game::core
{

/*! \brief Name of the per-user default song folder under the application data root. */
inline constexpr std::string_view g_default_songs_folder_name = "Songs";

/*!
\brief Composes the library scan roots: the default Songs folder first, then the custom roots.

Pure: the app-data location is injected so the resolution is testable without touching the real
per-user directory. The default per-user Songs folder (under \p app_data_directory) always leads,
matching the decided default (user-specific, survives reinstalls); the user's custom directories
follow in order. Duplicates are dropped by normalized comparison so a custom root equal to the
default (or to an earlier custom) is not scanned twice. The caller creates the default folder on
demand before scanning; this function performs no IO.

\param app_data_directory Per-user application data root (the folder that also holds settings).
\param custom_roots User-added custom song directories, in user order.
\return The effective scan roots: default Songs folder followed by the deduplicated custom roots.
*/
[[nodiscard]] std::vector<std::filesystem::path> resolveLibraryScanRoots(
    const std::filesystem::path& app_data_directory,
    std::span<const std::filesystem::path> custom_roots);

} // namespace rock_hero::game::core
