#include "library/null_album_art_generator.h"

namespace rock_hero::game::core
{

std::expected<AlbumArt, AlbumArtError> NullAlbumArtGenerator::generate(
    const std::filesystem::path& /*package_path*/)
{
    // No album art exists until docs/plans/roadmap/43 adds it plus a JUCE-backed decoder; report none.
    return AlbumArt{};
}

} // namespace rock_hero::game::core
