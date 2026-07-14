/*!
\file library_scan.h
\brief Synchronous driver that runs a full library scan to completion.
*/

#pragma once

#include <filesystem>
#include <rock_hero/common/core/shared/cancellation_token.h>
#include <rock_hero/game/core/library/i_album_art_generator.h>
#include <rock_hero/game/core/library/i_library_directory_lister.h>
#include <rock_hero/game/core/library/i_library_package_describer.h>
#include <rock_hero/game/core/library/library_index.h>
#include <span>

namespace rock_hero::game::core
{

/*!
\brief Scans the given roots to a complete library index on the calling thread.

Composes a LibraryScanEngine over the three ports, begins a fresh scan (no prior index), and pumps
it to completion, returning the built index. This is the simple synchronous entry point a startup
sequence uses before menus exist; a background, index-cached, cancellable scan is the LibraryScanEngine's
own step() surface. A requested cancellation stops the pump and returns the partial index.

\param scan_roots Directories to scan for packages.
\param lister Enumerates package files under each root.
\param describer Peeks each package's description.
\param album_art_generator Produces cached album-art images (the null default until plan 43).
\param token Cooperative cancellation handle.
\return The scanned library index (partial if cancelled).
*/
[[nodiscard]] LibraryIndex scanLibrary(
    std::span<const std::filesystem::path> scan_roots, ILibraryDirectoryLister& lister,
    ILibraryPackageDescriber& describer, IAlbumArtGenerator& album_art_generator,
    const common::core::CancellationToken& token);

} // namespace rock_hero::game::core
