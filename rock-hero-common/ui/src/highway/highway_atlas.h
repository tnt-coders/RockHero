/*!
\file highway_atlas.h
\brief Runtime-rasterized texture atlases for the highway: note heads and text glyphs.
*/

#pragma once

#include "highway/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace juce
{
class Image;
} // namespace juce

namespace rock_hero::common::ui
{

/*!
\brief Mip levels, past level 0, at which every atlas cell still samples only its own art.

The far end of the board minifies a head about 8.6:1, between levels 3 and 4, so trilinear
filtering never reads past level 4 there. Two layout rules hold through this level, both from one
texel of level L covering 2^L texels of level 0: a gutter (\ref g_atlas_gutter_texels) keeps the
cells apart, and every atlas edge is a multiple of 2^L (static_asserts beside each layout) so each
level is the one above halved exactly and a normalized coordinate lands on the same art at every
level. Deeper levels halve with a remainder and blend neighbours; a cell is under four texels wide
there.
*/
inline constexpr int g_atlas_exact_mip_levels = 4;

/*!
\brief Transparent texels around every cell of the GPU atlases, on each side.

A linear sample at a cell's edge reads up to 1.5 texels of its level beyond the edge, so two
gutters keep cells apart through level L while 2g >= 1.5 * 2^L - 0.5: twelve texels for
\ref g_atlas_exact_mip_levels. Sixteen is the smallest past that which also keeps the head atlas's
edges multiples of 2^L (five rows of 64 + 2g).
*/
inline constexpr int g_atlas_gutter_texels = 16;

/*!
\brief Pure grid layout of equal square cells inside a rectangular atlas texture, each cell
centered in a slot that adds a transparent gutter on every side.

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

    /*! \brief Cell edge length in texels, the art alone. */
    int cell_size{0};

    /*! \brief Transparent texels around each cell on every side; 0 packs cells edge to edge. */
    int gutter{0};

    /*!
    \brief Distance from one cell's slot to the next, the cell and both its gutters.
    \return Slot edge length in texels.
    */
    [[nodiscard]] constexpr int pitch() const noexcept
    {
        return cell_size + (2 * gutter);
    }

    /*!
    \brief Number of cells per row.
    \return Cells per row; zero when the layout is empty.
    */
    [[nodiscard]] constexpr int columns() const noexcept
    {
        return pitch() > 0 ? texture_width / pitch() : 0;
    }

    /*!
    \brief Number of cell rows.
    \return Cell rows; zero when the layout is empty.
    */
    [[nodiscard]] constexpr int rows() const noexcept
    {
        return pitch() > 0 ? texture_height / pitch() : 0;
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
    \brief Returns a cell's normalized texture rectangle: its art alone, the gutter outside it.

    A quad mapped to the rectangle spans exactly cell_size texels per axis, and linear filtering
    at its edge reads the gutter, which is transparent, never a neighbour.

    \param index Cell index in row-major order; out-of-range indices clamp to the last cell.
    \return {u0, v0, u1, v1} normalized coordinates; all zero for an empty layout.
    */
    [[nodiscard]] std::array<float, 4> cellRect(int index) const noexcept;

    /*!
    \brief Returns the texel at a cell's top-left corner, inside its gutter.
    \param index Cell index in row-major order; out-of-range indices clamp as cellRect() does.
    \return {x, y} in texels; {0, 0} for an empty layout.
    */
    [[nodiscard]] std::array<int, 2> cellOrigin(int index) const noexcept;
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
    white highlight, B is coverage — the encoding Charter's note atlas ships in.
    */
    UniqueBgfxHandle<bgfx::TextureHandle> heads;

    /*! \brief Cell layout of the head atlas. */
    HighwayAtlasLayout head_layout{};

    /*! \brief Glyph atlas: white-on-transparent text, shape carried by alpha alone. */
    UniqueBgfxHandle<bgfx::TextureHandle> glyphs;

    /*! \brief Cell layout of the glyph atlas. */
    HighwayAtlasLayout glyph_layout{};
};

// The cell vocabulary of the 4-column composed atlas, DECLARED semantically below rather than in
// cell order (the atlas itself is row-major; the declarations group by meaning). Two rules, one per
// half of the atlas — and one file per half: a theme's notes.png supplies the head rows, the fixed
// mark sheet the technique-mark rows:
//
// HEAD BASES take a row per SHAPE FAMILY, each complete with its hollow twin. Row 0 is the
// rectangle family (standard, tech, and the hollow rectangle the two of them share); row 1 is
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

/*! \brief Column count of the head atlas grid; the cell size derives as width over this. */
inline constexpr int g_head_atlas_columns = 4;

/*! \brief Edge length, in texels, of every cell on both sheets the head atlas composes. */
inline constexpr int g_atlas_cell_texels = 64;

/*! \brief Rows of a theme's notes.png: the head bases, rows 0 and 1 of the composed atlas. */
inline constexpr int g_head_sheet_rows = 2;

/*! \brief Rows of the fixed mark sheet: the technique marks, rows 2 to 4 of the composed atlas. */
inline constexpr int g_mark_sheet_rows = 3;

/*! \brief Cell index of the standard note head inside the head atlas. */
inline constexpr int g_head_cell_standard = 0;

/*! \brief Technique note head: the base head variant under left-hand technique markers. */
inline constexpr int g_head_cell_tech = 1;

/*! \brief Cell index of the hollow rectangle head: the note target and the pre-bend outline. */
inline constexpr int g_head_cell_hollow = 2;

// Cells 3, 6 and 7 are FREE — byte-identical empties, and each one is the growth slot of the
// family whose row it sits in: 3 completes the rectangle row, 6 and 7 the diamond row. There is
// no accent-ring cell: emphasis is a rendered light, because a mark drawn on the head could only
// ever say "accent", where the light says loud and quiet on one axis and says it identically on
// an open string, which has no head to wear a mark. They live on the theme's head sheet, so the
// fixed mark sheet stays full and intact.

/*!
\brief Arpeggio bracket for a posture stop the hand is ON — a fret slot, or a harmonic node placed
on its own wire.
*/
inline constexpr int g_head_cell_arpeggio_fret_bracket = 8;

/*! \brief Arpeggio bracket end for an open posture string. */
inline constexpr int g_head_cell_arpeggio_open_bracket = 9;

/*!
\brief Legato marker: the hammer-on triangle, which the pull-off draws flipped vertically.

One cell, not two: two separately authored triangles drift apart in border thickness and core
size, while flipping one cell makes the pair exact inverses by construction.
*/
inline constexpr int g_head_cell_legato = 10;

/*! \brief Bend marker: the chevron announcing a bent note on its head. */
inline constexpr int g_head_cell_bend = 11;

/*! \brief Tap marker. */
inline constexpr int g_head_cell_tap = 18;

/*!
\brief Pick-scrape marker: a plectrum split by a single 45-degree fracture.

Wears the picking hand's dark-interior treatment — fill tint weight 68 against a rim of 240 with 94
of white lift, the values tap, palm mute and pinch harmonic measure. The picking hand's rims fall
into two clusters and this cell joins the lifted one; slap and pop instead rim at 255 with no lift
at all. Interior darkness is this atlas's picking-hand signature, and it is load-bearing: the cell
ORDERING deliberately does not encode the hand, on the grounds that the art already does. Measured
as the median interior tint over each silhouette, the picking cells run 31 to 91 and the fretting
cells 246 to 255 — 155 counts of empty band between them, six and a half times the largest gap
within either cluster (the fretting side is a ramp, not a plateau). The split holds across cells
sharing a function (palm and full mute), a technique (natural and pinch harmonic), and a motion (tap
and legato), so it tracks the hand rather than the atlas row — which is exactly what lets the rows
carry the keybind pairs instead.

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
to recover it. Seated concentric on the head like the harmonic cell, at 1.5 x THE HEAD'S OWN
SOLID HEIGHT — the mark family's 1.07-of-pitch sizing law explicitly excepts it, because its design
law is covering the head's footprint rather than lane adjacency — measuring 31.149 x 32.966 texels,
which is 0.745 of the head art's width and 1.500 of its height by the 50%-contour extents: it
covers the head's own footprint, which is why a scrape wears this mark alone and no X beneath it.

The ratio is stated against the head's MEASURED extents (41.830 x 21.976 tx), never a literal, so
that a sub-texel recentring of the head cannot silently age the figure stated here. Its half-height
reaches 71.8% of the way to the next string's centre — the measured cost of the exception, and the
reason a larger size (matching the head's WIDTH, 1.90x, 91.1% of the way) is rejected.
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
\brief Natural-harmonic head marker, sized by the family law.

Drawn on the one uniform quad with its size carried in the cell's own art, which the mark
family's law sets like every technique mark's: 1.07 x the string pitch tall, and square (its
construction is three concentric circles, so squareness is exact by construction). Its own
cell rather than a bake into the bases because per-quad shader clamping is load-bearing: the
family highlight deliberately overdrives past white and the icon's translucent moat darkens
the CLAMPED result, which a single merged structural cell cannot express (measured 74 counts
off).
*/
inline constexpr int g_head_cell_harmonic = 14;

/*! \brief Pinch-harmonic head marker. */
inline constexpr int g_head_cell_pinch_harmonic = 15;

/*!
\brief Diamond harmonic base, FILLED, under a node-sitting head's marker.

A node head lands between fret wires wherever the overtone lives, so the family rectangle's
flat ears read as a misaligned ordinary note there; this diamond — the 2D lane's shape — has
no horizontal edge to disagree with a wire. Its size runs deliberately past the technique marks'
pitch standard, because the tips standing proud of the ring are what make the mark read: the
marker's ring CROSSES the diamond's inscribed radius by about 1.2 tx, the four points stand about
3.4 tx proud of the ring, and node heads at the lane pitch interpenetrate about 4.2 tx per side —
the accepted price for the strongest shape identity, with the outer-string tips reaching about
4.2 tx past the string grid's edge.
*/
inline constexpr int g_head_cell_harmonic_base = 4;

/*!
\brief Hollow twin of \ref g_head_cell_harmonic_base, derived from its base the way the
rectangle's hollow (\ref g_head_cell_hollow) derives from the standard head.

The note target and the pre-bend outline draw this for node heads, selected by the same
predicate as the base, so the approach can never preview a different shape than lands.
*/
inline constexpr int g_head_cell_harmonic_hollow = 5;

/*!
\brief Cells the composed head atlas carries: a theme's head rows above the fixed mark rows.

NOT a count of named cells — there are seventeen. It is one past the highest named index, so a
named index at or past it addresses no art.

The two sheets supply exactly this many, so the composed atlas has NO headroom: once the free
cells 3, 6 and 7 are spent, another head base needs a third head row and another mark a fourth
mark row.
*/
inline constexpr int g_head_cell_count = 20;

static_assert(
    (g_head_sheet_rows + g_mark_sheet_rows) * g_head_atlas_columns == g_head_cell_count,
    "the composed head atlas must carry exactly the cells the vocabulary names");

// The padded head atlas halves exactly through every level the gutters keep cells apart in.
static_assert(
    g_head_atlas_columns * (g_atlas_cell_texels + (2 * g_atlas_gutter_texels)) %
            (1 << g_atlas_exact_mip_levels) ==
        0,
    "the head atlas width must be a multiple of 2^g_atlas_exact_mip_levels");
static_assert(
    (g_head_sheet_rows + g_mark_sheet_rows) * (g_atlas_cell_texels + (2 * g_atlas_gutter_texels)) %
            (1 << g_atlas_exact_mip_levels) ==
        0,
    "the head atlas height must be a multiple of 2^g_atlas_exact_mip_levels");

/*!
\brief Stacks a theme's head sheet above the fixed mark sheet into the one head atlas.

The renderer draws heads and marks in one batch from one texture, so the two files compose into
the single image every cell index addresses: head rows first, mark rows below. Both images are
converted to ARGB first — the format the upload expects, and the one macOS decodes every PNG to
whatever its color type — and their rows are copied byte for byte, so the composed cells are
exactly the decoded pixels of each file.

\param heads The decoded notes.png of a theme.
\param marks The decoded fixed mark sheet.
\return The composed ARGB atlas, or a null image when either input is null or their widths
        differ.
*/
[[nodiscard]] juce::Image composeHeadAtlas(const juce::Image& heads, const juce::Image& marks);

/*!
\brief Spreads a grid of cells packed edge to edge into slots with a gutter around each.

Each cell's texels are copied byte for byte into its slot, and every gutter texel is zero:
transparent, and zero coverage under the structural channel scheme.

\param grid A non-null ARGB image of whole cells of \p cell_size texels, packed edge to edge.
\param cell_size Cell edge length in texels.
\param gutter Transparent texels around each cell on every side.
\return An ARGB image of the same cells in the same order, each centered in a slot of
        cell_size + 2 * gutter; a HighwayAtlasLayout with these three values describes it.
*/
[[nodiscard]] juce::Image padAtlasCells(const juce::Image& grid, int cell_size, int gutter);

/*!
\brief Packs an image and its full mip chain the way bgfx uploads them.

Level 0 is the image's own texels in BGRA8 rows with no padding; each next level halves both
edges, rounding down and never below 1, and averages the 2x2 texels it covers until a 1x1 level,
which is the chain bgfx expects for a texture created with mips. Where a level's edge is odd, its
last row or column has no texel of the next level; the atlases' edges keep that past
\ref g_atlas_exact_mip_levels.

The averaging rule is the structural channel scheme's, and both atlases meet its premise that B
is coverage. B and A average plainly. R and G are straight weights the shader shows only where B
covers, so they average weighted by B, which is exactly what the straight-alpha blend of the
level-0 texels sums to; an uncovered texel's weight never bleeds into the covered edge beside it.
The glyph atlas, white premultiplied text, has B equal to A and is read by alpha alone, so the
rule is exact for it too.

\param image A non-null ARGB image.
\return Every level, tightly packed and consecutive, from level 0 down.
*/
[[nodiscard]] std::vector<std::uint8_t> atlasMipChain(const juce::Image& image);

/*!
\brief Builds the highway atlases and uploads them as immutable, mipmapped bgfx textures.

The head atlas is the theme's head sheet composed over the fixed mark sheet
(\ref composeHeadAtlas), spread into gutters (\ref padAtlasCells) and uploaded with its mip chain
(\ref atlasMipChain); empty or undecodable bytes leave the heads handle invalid and the layout
empty, which the renderer treats as a startup error — texture assets are required product content,
never silently substituted. The glyph atlas is always runtime-rasterized (text, not an asset)
straight into its gutters and uploaded the same way.

Must be called after bgfx initialization and the results destroyed before shutdown (structural
via the shell's declaration order).

\param heads_png The theme's notes.png bytes.
\param marks_png The fixed mark sheet's bytes.
\return The uploaded atlases with their layouts.
*/
[[nodiscard]] HighwayAtlases makeHighwayAtlases(
    std::span<const std::byte> heads_png, std::span<const std::byte> marks_png);

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
