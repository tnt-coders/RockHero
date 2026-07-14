#include "library/library_scan.h"

#include <rock_hero/game/core/library/library_scan_engine.h>

namespace rock_hero::game::core
{

LibraryIndex scanLibrary(
    const std::span<const std::filesystem::path> scan_roots, ILibraryDirectoryLister& lister,
    ILibraryPackageDescriber& describer, IAlbumArtGenerator& album_art_generator,
    const common::core::CancellationToken& token)
{
    LibraryScanEngine engine{lister, describer, album_art_generator};
    engine.begin(LibraryIndex{}, scan_roots);
    while (!engine.done())
    {
        // The per-step progress/commit signal is for a background runner; this synchronous driver
        // only needs the finished index.
        static_cast<void>(engine.step(token));
    }
    return engine.index();
}

} // namespace rock_hero::game::core
