/*!
\file structural_art.h
\brief Shared decode-and-validate seam for structural-scheme art (chords.png, notes.png).
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace juce
{
class Image;
} // namespace juce

namespace rock_hero::common::ui
{

/*! \brief Why a structural-art measurement failed; the renderer reports it as an invalid
    asset. */
enum class StructuralArtError : std::uint8_t
{
    /*! \brief The bytes are empty or do not decode as an image. */
    UndecodableImage,

    /*! \brief The art does not carry the shape its measurement contract requires. */
    UnanalyzableArt,

    /*!
    \brief The PNG file carries a real alpha channel. The contract requires opacity in the
    coverage channel instead, because JUCE premultiplies an alpha-bearing PNG at decode and would
    silently scale every channel of the structural scheme by it. This is a fact about the file,
    not about the decoded image: macOS decodes every PNG to ARGB whatever the file's color type.
    */
    AlphaBearingImage,
};

/*!
\brief Decodes structural-scheme PNG bytes and rejects a file that carries real alpha.

This is the one place the file's own alpha state is still known — the decoded image cannot answer
that question portably (see \ref StructuralArtError::AlphaBearingImage) — so it is where an
alpha-bearing re-bake is rejected for every structural asset, and each measurement's byte entry
point funnels through it.

\param png_bytes The PNG file contents.
\return The decoded image, or the decode or alpha failure.
*/
[[nodiscard]] std::expected<juce::Image, StructuralArtError> decodeStructuralArtPng(
    std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
