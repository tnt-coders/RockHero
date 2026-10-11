/*!
\file library_scan_roots.h
\brief Resolves the effective library scan roots from settings and the default song folder.
*/

#pragma once

#include <filesystem>
#include <span>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Composes the library scan roots: the default song folder first, then the custom roots.

Pure: the default folder is injected so the resolution is testable without touching the real
per-user directory. The default per-user song folder always leads, matching the decided default
(user-specific, survives reinstalls); the user's custom directories follow in order. Duplicates are
dropped by normalized comparison so a custom root equal to the default (or to an earlier custom)
is not scanned twice. The caller creates the default folder on demand before scanning; this
function performs no IO.

\param default_songs_folder The per-user default song folder; the app passes `songsFolder()`.
\param custom_roots User-added custom song directories, in user order.
\return The effective scan roots: the default folder followed by the deduplicated custom roots.
*/
[[nodiscard]] std::vector<std::filesystem::path> resolveLibraryScanRoots(
    const std::filesystem::path& default_songs_folder,
    std::span<const std::filesystem::path> custom_roots);

} // namespace rock_hero::game::core
