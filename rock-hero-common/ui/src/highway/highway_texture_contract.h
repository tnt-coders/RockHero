/*!
\file highway_texture_contract.h
\brief The one entry point that validates a theme's art and measures what the renderer reads.
*/

#pragma once

#include "highway/box_mute_profile.h"
#include "highway/head_art_profile.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <rock_hero/common/ui/highway_theme/highway_texture_set.h>
#include <span>
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

/*! \brief Why an art file breaks the texture contract, and the expectation it violates. */
struct [[nodiscard]] HighwayTextureContractError
{
    /*! \brief Stable failure reason for program branching. */
    HighwayTextureContractErrorCode code{};

    /*!
    \brief Diagnostic that starts with the offending file's name and states the expectation, with
    what it found.
    */
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
- notes.png: the heads-only sheet, a PNG with no alpha channel of exactly g_head_atlas_columns by
  g_head_sheet_rows cells of g_atlas_cell_texels (256x128), whose head cells measureHeadArtProfile
  can read — the technique marks live on the fixed sheet (\ref checkHighwayMarkSheet), so a
  sheet that still carries them is refused;
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

/*!
\brief Validates the fixed technique-mark sheet the renderer composes under a theme's heads.

The sheet is product art rather than a theme asset, so the theme loader never sees it and
HighwayRenderer::create runs this beside measureHighwayTextures. Its contract: a PNG with no alpha
channel of exactly g_head_atlas_columns by g_mark_sheet_rows cells of g_atlas_cell_texels
(256x192), so it stacks under any valid notes.png into the atlas every cell index addresses.

\param marks_png The sheet's PNG bytes.
\return Nothing, or how the sheet breaks its contract.
*/
[[nodiscard]] std::expected<void, HighwayTextureContractError> checkHighwayMarkSheet(
    std::span<const std::byte> marks_png);

} // namespace rock_hero::common::ui
