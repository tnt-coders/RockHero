/*!
\file highway_atlas.h
\brief Runtime-rasterized texture atlases for the highway: note heads and text glyphs.
*/

#pragma once

#include "highway/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <optional>
#include <span>

namespace rock_hero::common::ui
{

/*!
\brief Pure grid layout of equal square cells inside a rectangular atlas texture.

Kept free of any rasterization or bgfx state so the arithmetic is unit-testable headlessly.
*/
struct HighwayAtlasLayout
{
    /*! \brief Atlas texture width in texels. */
    int texture_width{0};

    /*! \brief Atlas texture height in texels. */
    int texture_height{0};

    /*! \brief Cell edge length in texels. */
    int cell_size{0};

    /*!
    \brief Number of cells per row.
    \return Cells per row; zero when the layout is empty.
    */
    [[nodiscard]] int columns() const noexcept;

    /*!
    \brief Number of cell rows.
    \return Cell rows; zero when the layout is empty.
    */
    [[nodiscard]] int rows() const noexcept;

    /*!
    \brief Total number of cells the layout holds.
    \return Cell capacity.
    */
    [[nodiscard]] int capacity() const noexcept;

    /*!
    \brief Returns a cell's normalized texture rectangle.

    The rectangle is inset by half a texel on every side so linear filtering never bleeds
    neighboring cells into a quad's edge.

    \param index Cell index in row-major order; out-of-range indices clamp to the last cell.
    \return {u0, v0, u1, v1} normalized coordinates.
    */
    [[nodiscard]] std::array<float, 4> cellRect(int index) const noexcept;
};

/*!
\brief Maps a character to its glyph-atlas cell index.

The glyph atlas rasterizes the printable ASCII range '!'..'~' in cell order; space and every
character outside the range have no cell (callers advance the pen without drawing).

\param character Character to map.
\return Cell index, or empty when the character has no glyph.
*/
[[nodiscard]] std::optional<int> highwayGlyphCellIndex(char character) noexcept;

/*! \brief The highway's runtime-built atlases and their layouts. */
struct HighwayAtlases
{
    /*!
    \brief Note-head atlas in the reference channel scheme: R multiplies the string tint, G adds
    white highlight, B is the alpha mask — the encoding Charter's note atlas ships in.
    */
    UniqueBgfxHandle<bgfx::TextureHandle> heads;

    /*! \brief Cell layout of the head atlas. */
    HighwayAtlasLayout head_layout{};

    /*! \brief Glyph atlas: white-on-transparent text, shape carried by alpha alone. */
    UniqueBgfxHandle<bgfx::TextureHandle> glyphs;

    /*! \brief Cell layout of the glyph atlas. */
    HighwayAtlasLayout glyph_layout{};
};

/*! \brief Cell index of the standard note head inside the head atlas. */
inline constexpr int g_head_cell_standard = 0;

/*! \brief Cell index of the anticipation ring. */
inline constexpr int g_head_cell_anticipation = 1;

// The rest of the cell vocabulary (row-major indices). One art set serves every head-composite
// consumer deliberately (user rule 2026-08-01: absolute consistency, no dedicated variants);
// repeat-box mute marks render through the SDF program instead of any cell, because their line
// weights must hold across arbitrary box aspects.

/*! \brief Technique note head: the base head variant under left-hand technique markers. */
inline constexpr int g_head_cell_tech = 2;

/*! \brief Arpeggio bracket for a fretted posture note. */
inline constexpr int g_head_cell_arpeggio_fret_bracket = 3;

/*! \brief Hammer-on marker. */
inline constexpr int g_head_cell_hammer_on = 4;

/*! \brief Pull-off marker. */
inline constexpr int g_head_cell_pull_off = 5;

/*! \brief Tap marker. */
inline constexpr int g_head_cell_tap = 6;

/*! \brief Arpeggio bracket end for an open posture string. */
inline constexpr int g_head_cell_arpeggio_open_bracket = 7;

/*! \brief Palm-mute marker. */
inline constexpr int g_head_cell_palm_mute = 8;

/*! \brief Full-mute marker. */
inline constexpr int g_head_cell_full_mute = 9;

/*! \brief Accent marker. */
inline constexpr int g_head_cell_accent = 10;

/*!
\brief Bend marker: the chevron announcing a bent note on its head, authored into the
reference asset's formerly empty cell (judged 2026-07-28).
*/
inline constexpr int g_head_cell_bend = 11;

/*! \brief Natural-harmonic head marker. */
inline constexpr int g_head_cell_harmonic = 12;

/*! \brief Pinch-harmonic head marker. */
inline constexpr int g_head_cell_pinch_harmonic = 13;

/*! \brief Slap (bass) marker. */
inline constexpr int g_head_cell_slap = 14;

/*! \brief Pop (bass) marker. */
inline constexpr int g_head_cell_pop = 15;

/*! \brief Cells the renderer requires the head atlas to carry (the full 4x4 grid). */
inline constexpr int g_head_cell_count = 16;

/*!
\brief Builds the highway atlases and uploads them as immutable bgfx textures.

The head atlas uploads the supplied reference PNG (the Charter-derived 4x4 channel-scheme
atlas) verbatim when the bytes decode; empty or undecodable bytes leave the heads handle
invalid and the layout empty, which the renderer treats as a startup error — texture assets
are required product content, never silently substituted. The glyph atlas is always
runtime-rasterized (text, not an asset).

Must be called after bgfx initialization and the results destroyed before shutdown (structural
via the shell's declaration order).

\param note_atlas_png Reference note-atlas PNG bytes; empty or undecodable bytes leave the
       heads handle invalid for the caller to reject.
\return The uploaded atlases with their layouts.
*/
[[nodiscard]] HighwayAtlases makeHighwayAtlases(std::span<const std::byte> note_atlas_png);

/*!
\brief An uploaded texture paired with the pixel dimensions it decoded at.

Callers that address the texture by sub-cell (an atlas grid) need the dimensions to inset UVs by
a half texel, which stops neighboring cells bleeding into each other under minification.
*/
struct UploadedTexture
{
    /*! \brief The uploaded texture, or an invalid handle when decoding failed. */
    UniqueBgfxHandle<bgfx::TextureHandle> handle;

    /*! \brief Decoded width in texels; zero when decoding failed. */
    int width{0};

    /*! \brief Decoded height in texels; zero when decoding failed. */
    int height{0};
};

/*!
\brief Decodes a PNG and uploads it as an immutable BGRA8 bgfx texture.

\param png_bytes PNG file contents.
\return The uploaded texture and its decoded dimensions, or an invalid handle with zero
        dimensions when decoding fails.
*/
[[nodiscard]] UploadedTexture uploadPngTexture(std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
