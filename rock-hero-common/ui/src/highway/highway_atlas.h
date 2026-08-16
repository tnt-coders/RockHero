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

Kept free of any rasterization or bgfx state so the arithmetic is unit-testable headlessly. The
cell counts are constexpr so a grid's capacity can be tied to whatever addresses it at compile
time, rather than checked at runtime or not at all.
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
    [[nodiscard]] constexpr int columns() const noexcept
    {
        return cell_size > 0 ? texture_width / cell_size : 0;
    }

    /*!
    \brief Number of cell rows.
    \return Cell rows; zero when the layout is empty.
    */
    [[nodiscard]] constexpr int rows() const noexcept
    {
        return cell_size > 0 ? texture_height / cell_size : 0;
    }

    /*!
    \brief Total number of cells the layout holds.
    \return Cell capacity.
    */
    [[nodiscard]] constexpr int capacity() const noexcept
    {
        return columns() * rows();
    }

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

// The cell vocabulary, row-major on the 4-column grid, sorted semantically rather than in the
// Charter reference asset's order. Two rules, one per half of the sheet:
//
// HEAD BASES take a row per SHAPE FAMILY, each complete with its hollow twin. Row 0 is the
// rectangle family (standard, tech, and the anticipation ring the two of them share); row 1 is
// the diamond family (the node base and its hollow). A new silhouette is a new row, and a family
// is never split across the boundary.
//
// TECHNIQUE MARKS take two SIBLING PAIRS per row, and the pairs are the keybind pairs a charter
// actually types — `M`/`Shift+M` is palm and full mute, `H`/`Shift+H` is natural and pinch
// harmonic, so those sit side by side. That ordering is available because the HAND is carried by
// the art itself rather than by position: fill polarity is the mark family's hand signature (dark
// interior = picking, light = fretting, measured at 31..91 against 246..255 with no overlap), the
// same rule that lets the right-hand tap's dark T and the left tap's light T share one letter.
// Encoding the hand in the layout as well would spend the ordering on something already legible
// inside every cell. It falls out anyway: rows 2 and 4 are hand-pure, and the only two pairs that
// span hands — the mutes and the harmonics — are exactly the pairs that must stay together, so
// they sit in row 3 between them.
//
// One art set serves every head-composite consumer deliberately (absolute consistency, no
// dedicated variants); repeat-box mute marks render through the SDF program instead of any cell,
// because their line weights must hold across arbitrary box aspects.

/*! \brief Cell index of the standard note head inside the head atlas. */
inline constexpr int g_head_cell_standard = 0;

/*! \brief Technique note head: the base head variant under left-hand technique markers. */
inline constexpr int g_head_cell_tech = 1;

/*! \brief Cell index of the anticipation ring. */
inline constexpr int g_head_cell_anticipation = 2;

// Cells 3, 6 and 7 are FREE — byte-identical empties, and each one is the growth slot of the
// family whose row it sits in: 3 completes the rectangle row, 6 and 7 the diamond row. Cell 3 is
// the slot the accent ring vacated when emphasis became a rendered light, because a mark drawn on
// the head could only ever say "accent", where the light says loud and quiet on one axis and says
// it identically on an open string, which has no head to wear a mark.
//
// They cannot sit at the sheet's tail: seventeen filled cells on a four-wide grid is 4x4+1, so
// "every group row-aligned" and "all spares last" are arithmetically incompatible. The mark rows
// were kept intact and the spares stayed with the bases. A sixth row (256x384) would dissolve
// that, and would also buy back the headroom noted at g_head_cell_count.

/*! \brief Arpeggio bracket for a fretted posture note. */
inline constexpr int g_head_cell_arpeggio_fret_bracket = 8;

/*! \brief Arpeggio bracket end for an open posture string. */
inline constexpr int g_head_cell_arpeggio_open_bracket = 9;

/*!
\brief Legato marker: the hammer-on triangle, which the pull-off draws flipped vertically.

One cell, not two. Drawn separately they disagreed — the flat edges carried different border
thicknesses and the solid cores differed by 26 pixels, because each was authored rather than
mirrored (measured: 377 of 4096 pixels differed from a true mirror). Flipping one cell makes the
pair exact inverses by construction, and freed a cell at the time.
*/
inline constexpr int g_head_cell_legato = 10;

/*! \brief Bend marker: the chevron announcing a bent note on its head. */
inline constexpr int g_head_cell_bend = 11;

/*! \brief Tap marker. */
inline constexpr int g_head_cell_tap = 18;

/*!
\brief Pick-scrape marker: a plectrum split by a single 45-degree fracture.

Wears the picking hand's dark-interior treatment — fill tint weight 68 against a rim of 240 with
94 of white lift, the values tap, palm mute and pinch harmonic measure. The picking hand's rims
fall into two clusters and this cell joins the lifted one; slap and pop instead rim at 255 with no
lift at all. Interior darkness is this atlas's picking-hand signature, and it is load-bearing:
the cell ORDERING deliberately does not encode the hand, on the grounds that the art already
does. Measured as the median interior tint over each silhouette, the picking cells run 31 to 91
and the fretting cells 246 to 255 — 155 counts of empty band between them, six and a half times
the largest gap within either cluster. (An earlier wording claimed "exactly 255"; the fretting
side is a ramp rather than a plateau, so the separation is real but that number overstated it.)
The split holds across cells sharing a function (palm and full mute), a technique (natural and
pinch harmonic), and a motion (tap and legato), so it tracks the hand rather than the atlas row —
which is exactly what lets the rows carry the keybind pairs instead.

One zig zag at 45 degrees — two arms offset by a single perpendicular step — splits the pick, and
the fracture carries the row's brightest white lift, 192, so the crack reads as light filling it
rather than as a hole. The crack is a six-vertex bolt: full thickness across the middle, tapering
to a single point at each end. Every edge is straight, including the offset step, which is a chord
rather than the arc a plain distance field would join the arms with. Each point lands just past the
silhouette's half-coverage line so the crack's zero and the pick's zero coincide, which is what
makes the split reach the outline instead of stopping short of it. The two ends cannot converge at
the same rate: the plectrum is mirror-symmetric rather than 180-degree-symmetric, so its point sits
nearer the crack than its shoulder does and the lower taper is the steeper of the two.

The kink sits at the plectrum's own 180-degree symmetry center, not the cell's. Both points are
pinned to the outline, so where the kink falls between them is the mark's only freedom, and placing
it by ink symmetry rather than by geometry is what makes the two arms read as equal — measured
against a plain cell-centered kink, their length ratio falls from 1.43 to 1.08 while the kink stays
on the cell's vertical center line. The offset is equal along the arms and across them, the one
construction that buys the balance without moving the kink off that line. Its across-the-arms half
is why the upper point cuts a shallower notch through the rim than the lower one: recovering that
notch means giving the balance back, and pushing the point further past the outline was measured not
to recover it. Seated concentric on the head like the harmonic cell, at 0.76 of the
head's solid width and 1.57 of its height: it covers the head's own footprint, which is why a
scrape wears this mark alone and no X beneath it.
*/
inline constexpr int g_head_cell_pick_slide = 19;

/*! \brief Slap (bass) marker. */
inline constexpr int g_head_cell_slap = 16;

/*! \brief Pop (bass) marker. */
inline constexpr int g_head_cell_pop = 17;

/*! \brief Palm-mute marker. */
inline constexpr int g_head_cell_palm_mute = 12;

/*! \brief Full-mute marker. */
inline constexpr int g_head_cell_full_mute = 13;

/*!
\brief Natural-harmonic head marker, authored at its approved seat scale.

Drawn on the one uniform quad with its seat scale carried in the cell's own art — 0.6767 of
the icon's original authoring resolution, the fitted size approved with the round base (the
original full-size art was retired with the rectangle base it fit). Its own cell rather than
a bake into the bases because per-quad shader clamping is load-bearing: the family highlight
deliberately overdrives past white and the icon's translucent moat darkens the CLAMPED
result, which a single merged structural cell cannot express (measured 74 counts off).
*/
inline constexpr int g_head_cell_harmonic = 14;

/*! \brief Pinch-harmonic head marker. */
inline constexpr int g_head_cell_pinch_harmonic = 15;

/*!
\brief Diamond harmonic base, FILLED, under a node-sitting head's marker.

A node head lands between fret wires wherever the overtone lives, so the family rectangle's
flat ears read as a misaligned ordinary note there; this diamond — the 2D lane's shape — has
no horizontal edge to disagree with a wire. Its EDGE length equals the regular head's height
(the head-height square rotated 45 degrees, vertex span about 30.6), the calibration chosen
from the 2026-08-15 candidate rounds (commit 21bfa768 holds all four): the marker's ring
lands inscribed in it, clearing the flats by a measured 0.08 tx, while the four points show
4.4 tx proud of the ring. The accepted price is stacking: node heads at the lane pitch
interpenetrate about 3.7 tx per side, the tradeoff taken for the strongest shape identity.
*/
inline constexpr int g_head_cell_harmonic_base = 4;

/*!
\brief Hollow twin of \ref g_head_cell_harmonic_base, derived from its base the way the
rectangle's anticipation ring (\ref g_head_cell_anticipation) derives from the standard head.

The landing ring and the pre-bend outline draw this for node heads, selected by the same
predicate as the base, so the approach can never preview a different shape than lands.
*/
inline constexpr int g_head_cell_harmonic_anticipation = 5;

/*!
\brief Cells the renderer requires the head atlas to carry (a 4-column grid, five rows).

NOT a count of named cells — there are seventeen. It is one past the highest named index, which
is what the startup check needs: capacity below this means some named index addresses no art.

The shipped 256x320 asset supplies exactly this many, so there is now NO headroom: a twenty-first
named cell needs a taller sheet (256x384 buys a sixth row). That is the price of packing the
spares in at 5-7 rather than leaving the vocabulary's gaps where they fell.
*/
inline constexpr int g_head_cell_count = 20;

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
