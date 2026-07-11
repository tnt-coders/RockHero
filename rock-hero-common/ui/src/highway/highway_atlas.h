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
\brief Pure grid layout of equal square cells inside a square atlas texture.

Kept free of any rasterization or bgfx state so the arithmetic is unit-testable headlessly.
*/
struct HighwayAtlasLayout
{
    /*! \brief Atlas texture edge length in texels. */
    int texture_size{0};

    /*! \brief Cell edge length in texels. */
    int cell_size{0};

    /*!
    \brief Number of cells per row (and per column — the atlas is square).
    \return Cells per row; zero when the layout is empty.
    */
    [[nodiscard]] int columns() const noexcept;

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

    /*!
    \brief True when the head atlas is the reference 4x4 asset with the full cell vocabulary
    (anticipation ring, technique overlays); false on the single-cell procedural fallback.
    */
    bool reference_cells{false};

    /*! \brief Glyph atlas: white-on-transparent text, shape carried by alpha alone. */
    UniqueBgfxHandle<bgfx::TextureHandle> glyphs;

    /*! \brief Cell layout of the glyph atlas. */
    HighwayAtlasLayout glyph_layout{};
};

/*! \brief Cell index of the standard note head inside the head atlas. */
inline constexpr int g_head_cell_standard = 0;

/*! \brief Cell index of the anticipation ring (reference atlas only; see reference_cells). */
inline constexpr int g_head_cell_anticipation = 1;

// The rest of the reference atlas's 4x4 cell vocabulary (row-major indices; reference_cells
// gates every use). Cell 11 is empty in the reference asset.

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

/*! \brief Natural-harmonic head marker. */
inline constexpr int g_head_cell_harmonic = 12;

/*! \brief Pinch-harmonic head marker. */
inline constexpr int g_head_cell_pinch_harmonic = 13;

/*! \brief Slap (bass) marker. */
inline constexpr int g_head_cell_slap = 14;

/*! \brief Pop (bass) marker. */
inline constexpr int g_head_cell_pop = 15;

/*!
\brief Builds the highway atlases and uploads them as immutable bgfx textures.

The head atlas decodes the supplied reference PNG (Charter's 4x4 channel-scheme atlas) when
bytes are given and they decode; otherwise a single-cell procedural head is rasterized with JUCE
as a fallback so a missing asset degrades the art, never the game. The glyph atlas is always
runtime-rasterized.

Must be called after bgfx initialization and the results destroyed before shutdown (structural
via the shell's declaration order).

\param note_atlas_png Reference note-atlas PNG bytes; empty selects the procedural fallback.
\return The uploaded atlases with their layouts.
*/
[[nodiscard]] HighwayAtlases makeHighwayAtlases(std::span<const std::byte> note_atlas_png);

/*!
\brief Decodes a PNG and uploads it as an immutable BGRA8 bgfx texture.

\param png_bytes PNG file contents.
\return The uploaded texture, or an invalid handle when decoding fails.
*/
[[nodiscard]] UniqueBgfxHandle<bgfx::TextureHandle> uploadPngTexture(
    std::span<const std::byte> png_bytes);

} // namespace rock_hero::common::ui
