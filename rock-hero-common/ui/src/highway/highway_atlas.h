/*!
\file highway_atlas.h
\brief Runtime-rasterized texture atlases for the highway: note heads and text glyphs.
*/

#pragma once

#include "highway/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <optional>

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
    white highlight, B is the alpha mask. Authored fully opaque so JUCE's premultiplication is
    the identity and the channels survive rasterization untouched.
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

/*!
\brief Rasterizes the highway atlases with JUCE and uploads them as immutable bgfx textures.

Must be called after bgfx initialization and the results destroyed before shutdown (structural
via the shell's declaration order).

\return The uploaded atlases with their layouts.
*/
[[nodiscard]] HighwayAtlases makeHighwayAtlases();

} // namespace rock_hero::common::ui
