/*!
\file highway_texture_contract.h
\brief The one entry point that validates a theme's art and measures what the renderer reads.
*/

#pragma once

#include "highway/box_mute_profile.h"
#include "highway/head_art_profile.h"

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/ui/highway_theme/highway_texture_set.h>
#include <string>

namespace rock_hero::common::ui
{

/*! \brief Stable reasons an art file breaks the texture contract. */
enum class HighwayTextureContractErrorCode : std::uint8_t
{
    /*! \brief The bytes are empty or do not decode as a PNG. */
    UndecodableImage,

    /*!
    \brief Structural-scheme art whose file carries an alpha channel (\ref StructuralArtError).
    */
    AlphaBearingImage,

    /*! \brief The image's dimensions do not form the asset's cell grid. */
    WrongGrid,

    /*! \brief The art does not carry the shapes its load-time measurement reads. */
    UnmeasurableArt,
};

/*! \brief Which asset breaks the texture contract, and the expectation it violates. */
struct [[nodiscard]] HighwayTextureContractError
{
    /*! \brief Stable failure reason for program branching. */
    HighwayTextureContractErrorCode code{};

    /*! \brief The asset that breaks the contract. */
    common::core::HighwayTexture asset{};

    /*! \brief Diagnostic naming the asset's file and the expectation, with what it found. */
    std::string message;
};

/*! \brief What the renderer reads off a theme's art at load, beyond uploading its pixels. */
struct HighwayTextureMeasurements
{
    /*! \brief The head silhouettes notes.png carries. */
    HeadArtProfile head_art;

    /*! \brief The repeat-box mute marks' cross-sections chords.png carries. */
    BoxMuteProfiles box_mutes;
};

/*!
\brief Validates every asset of a theme against its contract and returns the measurements.

The contract, per asset in common::core::g_highway_textures order:
- notes.png: a PNG with no alpha channel, laid out as four columns of square cells and enough rows
  for every cell the atlas names (g_head_cell_count), whose head cells measureHeadArtProfile can
  read;
- inlays.png: a PNG holding one square cell;
- chords.png: a PNG with no alpha channel whose two stacked marks measureBoxMuteProfiles can read.

The theme loader runs it to refuse a broken theme before any renderer sees it, and
HighwayRenderer::create runs it for the measurements it draws with, so the contract is stated here
alone.

\param textures The theme's art.
\return The measurements, or the first asset that breaks its contract.
*/
[[nodiscard]] std::expected<HighwayTextureMeasurements, HighwayTextureContractError>
measureHighwayTextures(const HighwayTextureSet& textures);

} // namespace rock_hero::common::ui
