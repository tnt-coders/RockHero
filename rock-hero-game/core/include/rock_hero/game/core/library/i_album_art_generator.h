/*!
\file i_album_art_generator.h
\brief Port that turns a package's album art into a cached image for the library.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/game/core/library/album_art_error.h>
#include <string>

namespace rock_hero::game::core
{

/*! \brief Outcome of generating (or reusing) one package's album-art image. */
struct AlbumArt
{
    /*!
    \brief Cached album-art image file name beside the library index.

    Empty when the package carries no album art — an absence, not a failure. Populated once
    docs/plans/roadmap/43-song-information-and-art.md adds art and the JUCE-backed adapter decodes it.
    */
    std::string image_file_name;
};

/*!
\brief Produces cached album-art images for library packages.

This is deliberately NOT `common::audio::IThumbnail`, which renders audio-waveform channels into a
`juce::Graphics`; this port decodes a package's album art into a small cached image file. The real
adapter (docs/plans/roadmap/43-song-information-and-art.md) needs `common/ui`'s album-art decoder and so
lives outside headless `game/core`; NullAlbumArtGenerator keeps the scan headless until then.
*/
class IAlbumArtGenerator
{
public:
    /*! \brief Destroys the album-art generator interface. */
    virtual ~IAlbumArtGenerator() = default;

    /*!
    \brief Produces or reuses the cached album-art image for one package.

    An empty `image_file_name` means the package has no album art and is not a failure. A typed
    failure means art was present but could not be decoded or cached; the scan records it as a
    warning entry and continues, so one unreadable image never aborts the library.

    \param package_path Native `.rock` package whose album art is rendered to an image.
    \return The cached album-art outcome, or a typed generation failure.
    */
    [[nodiscard]] virtual std::expected<AlbumArt, AlbumArtError> generate(
        const std::filesystem::path& package_path) = 0;

protected:
    /*! \brief Creates the album-art generator interface. */
    IAlbumArtGenerator() = default;

    /*! \brief Copies the album-art generator interface. */
    IAlbumArtGenerator(const IAlbumArtGenerator&) = default;

    /*! \brief Moves the album-art generator interface. */
    IAlbumArtGenerator(IAlbumArtGenerator&&) = default;

    /*!
    \brief Assigns the album-art generator interface from another instance.
    \return Reference to this album-art generator interface.
    */
    IAlbumArtGenerator& operator=(const IAlbumArtGenerator&) = default;

    /*!
    \brief Move-assigns the album-art generator interface from another instance.
    \return Reference to this album-art generator interface.
    */
    IAlbumArtGenerator& operator=(IAlbumArtGenerator&&) = default;
};

} // namespace rock_hero::game::core
