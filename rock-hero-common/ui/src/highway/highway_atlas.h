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

// The cell vocabulary, row-major on the 4x4 grid, sorted semantically rather than in the
// Charter reference asset's order: head bases + emphasis, then one row per hand — the fretting
// hand's posture brackets, legato mark and bend, then the picking hand's marks — then damping +
// timbre. One art set serves every head-composite consumer deliberately (absolute consistency,
// no dedicated variants); repeat-box mute marks render through the SDF program instead of any
// cell, because their line weights must hold across arbitrary box aspects.

/*! \brief Cell index of the standard note head inside the head atlas. */
inline constexpr int g_head_cell_standard = 0;

/*! \brief Technique note head: the base head variant under left-hand technique markers. */
inline constexpr int g_head_cell_tech = 1;

/*! \brief Cell index of the anticipation ring. */
inline constexpr int g_head_cell_anticipation = 2;

/*! \brief Accent marker. */
inline constexpr int g_head_cell_accent = 3;

/*! \brief Arpeggio bracket for a fretted posture note. */
inline constexpr int g_head_cell_arpeggio_fret_bracket = 4;

/*! \brief Arpeggio bracket end for an open posture string. */
inline constexpr int g_head_cell_arpeggio_open_bracket = 5;

/*!
\brief Legato marker: the hammer-on triangle, which the pull-off draws flipped vertically.

One cell, not two. Drawn separately they disagreed — the flat edges carried different border
thicknesses and the solid cores differed by 26 pixels, because each was authored rather than
mirrored (measured: 377 of 4096 pixels differed from a true mirror). Flipping one cell makes the
pair exact inverses by construction, and frees the seventeenth cell that had forced the atlas to
a fifth row.
*/
inline constexpr int g_head_cell_legato = 6;

/*! \brief Bend marker: the chevron announcing a bent note on its head. */
inline constexpr int g_head_cell_bend = 7;

/*! \brief Tap marker. */
inline constexpr int g_head_cell_tap = 8;

/*!
\brief Pick-scrape marker: a plectrum split by a single 45-degree fracture.

Wears the picking hand's dark-interior treatment — fill tint weight 68 against a rim of 240 with
94 of white lift, the values tap, palm mute and pinch harmonic measure. The picking hand's rims
fall into two clusters and this cell joins the lifted one; slap and pop instead rim at 255 with no
lift at all. Interior darkness is this atlas's picking-hand signature: every
right-hand cell cores at or below 68 while every fretting-hand cell cores at exactly 255. That
split holds across cells sharing a function (palm and full mute), a technique (natural and pinch
harmonic), and a motion (tap and legato), so it tracks the hand rather than the atlas row.

One zig zag at 45 degrees — two arms offset by a single perpendicular step — splits the pick
through the cell center, and the fracture carries the row's brightest white lift, 192, so the crack
reads as light filling it rather than as a hole. The crack is a six-vertex bolt: full thickness
across the middle, tapering to a single point at each end. Every edge is straight, including the
offset step, which is a chord rather than the arc a plain distance field would join the arms with.
Each point lands just past the silhouette's half-coverage line so the crack's zero and the pick's
zero coincide, which is what makes the split reach the outline instead of stopping short of it. The
two ends cannot converge at the same rate: the plectrum is mirror-symmetric rather than
180-degree-symmetric, so its point sits nearer the crack than its shoulder does and the lower taper
is the steeper of the two. Seated concentric on the head like the harmonic cell, at 0.76 of the
head's solid width and 1.57 of its height: it covers the head's own footprint, which is why a
scrape wears this mark alone and no X beneath it.
*/
inline constexpr int g_head_cell_pick_slide = 9;

/*! \brief Slap (bass) marker. */
inline constexpr int g_head_cell_slap = 10;

/*! \brief Pop (bass) marker. */
inline constexpr int g_head_cell_pop = 11;

/*! \brief Palm-mute marker. */
inline constexpr int g_head_cell_palm_mute = 12;

/*! \brief Full-mute marker. */
inline constexpr int g_head_cell_full_mute = 13;

/*! \brief Natural-harmonic head marker. */
inline constexpr int g_head_cell_harmonic = 14;

/*! \brief Pinch-harmonic head marker. */
inline constexpr int g_head_cell_pinch_harmonic = 15;

/*! \brief Cells the renderer requires the head atlas to carry (a 4x4 grid, all 16 used). */
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
