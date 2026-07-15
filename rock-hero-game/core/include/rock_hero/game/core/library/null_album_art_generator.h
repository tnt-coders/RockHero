/*!
\file null_album_art_generator.h
\brief The album-art generator that reports no art, shipped until art exists.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/game/core/library/album_art_error.h>
#include <rock_hero/game/core/library/i_album_art_generator.h>

namespace rock_hero::game::core
{

/*!
\brief Album-art generator that always reports "no album art".

The shipping default until docs/plans/roadmap/43-song-information-and-art.md adds album art and a
JUCE-backed decoder. It never reads a package and never fails, so the library scan runs fully
headless with no image decoding — the same code path plan 43 later swaps a real adapter into.
*/
class NullAlbumArtGenerator final : public IAlbumArtGenerator
{
public:
    /*!
    \brief Reports that the package has no album art.
    \param package_path Ignored; no package is read.
    \return An empty album-art outcome.
    */
    [[nodiscard]] std::expected<AlbumArt, AlbumArtError> generate(
        const std::filesystem::path& package_path) override;
};

} // namespace rock_hero::game::core
