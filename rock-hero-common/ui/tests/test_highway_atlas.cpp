#include "highway/highway_atlas.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// An RGB sheet, the format a structural PNG decodes to on Windows, whose every pixel is distinct,
// so a composition that moved, blended or dropped any pixel cannot pass by coincidence.
[[nodiscard]] juce::Image patternSheet(const int width, const int height, const juce::uint8 seed)
{
    juce::Image sheet{juce::Image::RGB, width, height, false, juce::SoftwareImageType{}};
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            sheet.setPixelAt(
                x, y, juce::Colour{static_cast<juce::uint8>(x), static_cast<juce::uint8>(y), seed});
        }
    }
    return sheet;
}

// An ARGB image whose every pixel is one color; the structural scheme reads B as coverage.
[[nodiscard]] juce::Image solidImage(const int width, const int height, const juce::Colour color)
{
    juce::Image image{juce::Image::ARGB, width, height, true, juce::SoftwareImageType{}};
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            image.setPixelAt(x, y, color);
        }
    }
    return image;
}

// One BGRA8 texel of a mip chain, addressed by level and position: the chain packs the levels
// consecutively from level 0, each the one before halved (rounding down, never below one).
struct ChainTexel
{
    std::uint8_t blue{0};
    std::uint8_t green{0};
    std::uint8_t red{0};
    std::uint8_t alpha{0};
};

[[nodiscard]] ChainTexel chainTexel(
    const std::vector<std::uint8_t>& chain, int width, int height, const int level, const int x,
    const int y)
{
    std::size_t offset = 0;
    for (int skipped = 0; skipped < level; ++skipped)
    {
        offset += static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
    }
    REQUIRE(x < width);
    REQUIRE(y < height);
    offset += ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
               static_cast<std::size_t>(x)) *
              4U;
    REQUIRE(offset + 4U <= chain.size());
    return ChainTexel{
        .blue = chain[offset],
        .green = chain[offset + 1],
        .red = chain[offset + 2],
        .alpha = chain[offset + 3],
    };
}

} // namespace

// The renderer draws heads and marks from one texture, so the two sheets compose into the atlas
// every cell index addresses: head rows first, mark rows below, each pixel exactly as decoded.
TEST_CASE("Composing the head atlas stacks heads over marks pixel for pixel", "[ui][highway]")
{
    const juce::Image heads = patternSheet(256, 128, 1);
    const juce::Image marks = patternSheet(256, 192, 2);
    const juce::Image composed = composeHeadAtlas(heads, marks);

    REQUIRE(composed.getWidth() == 256);
    REQUIRE(composed.getHeight() == 320);
    CHECK(composed.getFormat() == juce::Image::ARGB);
    bool exact = true;
    for (int y = 0; y < composed.getHeight(); ++y)
    {
        for (int x = 0; x < composed.getWidth(); ++x)
        {
            const juce::Colour expected = y < heads.getHeight()
                                              ? heads.getPixelAt(x, y)
                                              : marks.getPixelAt(x, y - heads.getHeight());
            exact = exact && composed.getPixelAt(x, y) == expected;
        }
    }
    CHECK(exact);

    // The composed atlas carries exactly the cells the vocabulary names.
    const HighwayAtlasLayout layout{
        .texture_width = composed.getWidth(),
        .texture_height = composed.getHeight(),
        .cell_size = composed.getWidth() / g_head_atlas_columns,
    };
    CHECK(layout.capacity() == g_head_cell_count);
}

// Rows of different widths cannot stack into one grid, and a sheet that did not decode has no
// rows to give, so either yields no atlas for the renderer to reject.
TEST_CASE("Composing the head atlas refuses mismatched or missing sheets", "[ui][highway]")
{
    CHECK(composeHeadAtlas(patternSheet(256, 128, 1), patternSheet(128, 192, 2)).isNull());
    CHECK(composeHeadAtlas(juce::Image{}, patternSheet(256, 192, 2)).isNull());
    CHECK(composeHeadAtlas(patternSheet(256, 128, 1), juce::Image{}).isNull());
}

TEST_CASE("Highway atlas layout reports its grid capacity", "[ui][highway]")
{
    const HighwayAtlasLayout layout{
        .texture_width = 880, .texture_height = 880, .cell_size = 56, .gutter = 16
    };

    CHECK(layout.columns() == 10);
    CHECK(layout.rows() == 10);
    // The glyph grid's covenant is a relationship, not a number: cellRect() clamps an
    // out-of-range index, so the cell size must leave room for the whole printable-ASCII range
    // ('!'..'~') or every character past the end silently draws the last glyph.
    CHECK(layout.capacity() >= '~' - '!' + 1);

    // The head atlas holds at least the cells the renderer names — the covenant the renderer's
    // own startup check states (the sheet supplies exactly the count, with no headroom).
    const HighwayAtlasLayout heads{.texture_width = 256, .texture_height = 320, .cell_size = 64};
    CHECK(heads.columns() == 4);
    CHECK(heads.rows() == 5);
    CHECK(heads.capacity() >= g_head_cell_count);

    // Rectangular grids still count rows by height, so a non-square asset cannot silently
    // renumber the cells.
    const HighwayAtlasLayout composed{
        .texture_width = 512, .texture_height = 640, .cell_size = 128
    };
    CHECK(composed.columns() == 4);
    CHECK(composed.rows() == 5);
    CHECK(composed.capacity() == 20);

    // A gutter widens every slot, so the same texture holds fewer cells of the same size: the
    // shipped head atlas, spread into its gutters, keeps its 4x5 grid in a 384x480 texture.
    const HighwayAtlasLayout spread{
        .texture_width = 384, .texture_height = 480, .cell_size = 64, .gutter = 16
    };
    CHECK(spread.pitch() == 96);
    CHECK(spread.columns() == 4);
    CHECK(spread.rows() == 5);
    CHECK(spread.capacity() == g_head_cell_count);
}

// A cell's rectangle is its art alone, so a quad mapped to it spans exactly cell_size texels per
// axis; the gutter lies outside it, where linear filtering at the edge reads only transparency.
TEST_CASE("Highway atlas cells map to their art inside the gutter", "[ui][highway]")
{
    const HighwayAtlasLayout packed{.texture_width = 256, .texture_height = 256, .cell_size = 128};
    CHECK(packed.cellOrigin(0) == std::array{0, 0});
    const auto first = packed.cellRect(0);
    CHECK_THAT(first[0], Catch::Matchers::WithinAbs(0.0F, 1e-7));
    CHECK_THAT(first[1], Catch::Matchers::WithinAbs(0.0F, 1e-7));
    CHECK_THAT(first[2], Catch::Matchers::WithinAbs(0.5F, 1e-7));
    CHECK_THAT(first[3], Catch::Matchers::WithinAbs(0.5F, 1e-7));

    // Row-major: index 3 of a 2x2 grid is the bottom-right cell.
    CHECK(packed.cellOrigin(3) == std::array{128, 128});
    const auto last = packed.cellRect(3);
    CHECK_THAT(last[0], Catch::Matchers::WithinAbs(0.5F, 1e-7));
    CHECK_THAT(last[1], Catch::Matchers::WithinAbs(0.5F, 1e-7));
    CHECK_THAT(last[2], Catch::Matchers::WithinAbs(1.0F, 1e-7));
    CHECK_THAT(last[3], Catch::Matchers::WithinAbs(1.0F, 1e-7));

    // With a gutter the art starts one gutter into its slot: the last row's first cell (index 16
    // of a 4x5 grid of 64 + 2 * 16 slots) sits at texel (16, 4 * 96 + 16) in a 384x480 texture.
    const HighwayAtlasLayout spread{
        .texture_width = 384, .texture_height = 480, .cell_size = 64, .gutter = 16
    };
    CHECK(spread.cellOrigin(16) == std::array{16, 400});
    const auto last_row_cell = spread.cellRect(16);
    CHECK_THAT(last_row_cell[0], Catch::Matchers::WithinAbs(16.0F / 384.0F, 1e-7));
    CHECK_THAT(last_row_cell[1], Catch::Matchers::WithinAbs(400.0F / 480.0F, 1e-7));
    CHECK_THAT(last_row_cell[2], Catch::Matchers::WithinAbs(80.0F / 384.0F, 1e-7));
    CHECK_THAT(last_row_cell[3], Catch::Matchers::WithinAbs(464.0F / 480.0F, 1e-7));
}

// The legato pair shares one cell, drawn upright for the hammer-on and vertically flipped for
// the pull-off, so the two can never disagree in weight or border the way separately authored
// art would. Swapping a cell's v coordinates is exactly that flip, and it stays inside the cell.
TEST_CASE("Highway atlas legato cell mirrors within its own bounds", "[ui][highway]")
{
    const HighwayAtlasLayout heads{
        .texture_width = 384, .texture_height = 480, .cell_size = 64, .gutter = 16
    };
    const auto cell = heads.cellRect(g_head_cell_legato);

    // The flip is a swap of the vertical pair, so it samples the same rows in reverse and
    // never reaches a neighbouring cell. The expected band is derived from the cell's own index
    // rather than written in: the claim is "the flip stays inside WHATEVER row this cell is on",
    // and hardcoding the row makes the case fail on a vocabulary reorder that changed nothing it
    // is testing.
    CHECK(cell[1] < cell[3]);
    const float slot_height = 96.0F / 480.0F;
    const float gutter = 16.0F / 480.0F;
    // Four head cells per atlas row, so the row index is an integer division by intent.
    const int row_index = g_head_cell_legato / 4;
    const auto row = static_cast<float>(row_index);
    CHECK_THAT(cell[1], Catch::Matchers::WithinAbs((row * slot_height) + gutter, 1e-7));
    CHECK_THAT(cell[3], Catch::Matchers::WithinAbs(((row + 1.0F) * slot_height) - gutter, 1e-7));

    // Every named cell is inside the grid the shipped asset provides.
    CHECK(g_head_cell_bend < g_head_cell_count);
    CHECK(g_head_cell_harmonic_hollow < g_head_cell_count);
    CHECK(heads.capacity() >= g_head_cell_count);
}

TEST_CASE("Highway atlas layout clamps out-of-range cells", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_width = 256, .texture_height = 256, .cell_size = 128};

    CHECK(layout.cellRect(99) == layout.cellRect(3));
    CHECK(layout.cellRect(-5) == layout.cellRect(0));
    CHECK(layout.cellOrigin(99) == layout.cellOrigin(3));
    CHECK(layout.cellOrigin(-5) == layout.cellOrigin(0));
}

// Spreading a packed grid moves every cell byte for byte into its slot and leaves each gutter
// transparent with zero coverage, so nothing of a cell's art moves, blends or reaches a neighbour.
TEST_CASE("Spreading atlas cells into gutters keeps every cell exact", "[ui][highway]")
{
    // A 3x2 grid of 8-texel cells with a gutter of 2: slots of 12 in a 36x24 image.
    const juce::Image grid = patternSheet(24, 16, 7).convertedToFormat(juce::Image::ARGB);
    const juce::Image spread = padAtlasCells(grid, 8, 2);
    REQUIRE(spread.getWidth() == 36);
    REQUIRE(spread.getHeight() == 24);

    const HighwayAtlasLayout layout{
        .texture_width = spread.getWidth(),
        .texture_height = spread.getHeight(),
        .cell_size = 8,
        .gutter = 2,
    };
    REQUIRE(layout.capacity() == 6);
    bool cells_exact = true;
    for (int index = 0; index < layout.capacity(); ++index)
    {
        const auto [x0, y0] = layout.cellOrigin(index);
        const int source_x0 = (index % 3) * 8;
        const int source_y0 = (index / 3) * 8;
        for (int y = 0; y < 8; ++y)
        {
            for (int x = 0; x < 8; ++x)
            {
                cells_exact = cells_exact && spread.getPixelAt(x0 + x, y0 + y) ==
                                                 grid.getPixelAt(source_x0 + x, source_y0 + y);
            }
        }
    }
    CHECK(cells_exact);

    // Every texel outside a cell's art is fully zero.
    bool gutters_clear = true;
    for (int y = 0; y < spread.getHeight(); ++y)
    {
        for (int x = 0; x < spread.getWidth(); ++x)
        {
            const bool in_art = (x % 12 >= 2 && x % 12 < 10) && (y % 12 >= 2 && y % 12 < 10);
            gutters_clear = gutters_clear && (in_art || spread.getPixelAt(x, y).getARGB() == 0U);
        }
    }
    CHECK(gutters_clear);

    // A gutter of zero is the identity, so the packed grid and its spread are the same bytes.
    const juce::Image same = padAtlasCells(grid, 8, 0);
    REQUIRE(same.getWidth() == grid.getWidth());
    REQUIRE(same.getHeight() == grid.getHeight());
    bool identical = true;
    for (int y = 0; y < grid.getHeight(); ++y)
    {
        for (int x = 0; x < grid.getWidth(); ++x)
        {
            identical = identical && same.getPixelAt(x, y) == grid.getPixelAt(x, y);
        }
    }
    CHECK(identical);
}

// bgfx reads an immutable texture's chain as level 0 followed by each level halved (rounding
// down, never below one) to 1x1, so the chain's size is that sum, and odd edges drop a trailing
// row or column rather than keep a half texel.
TEST_CASE("Atlas mip chain packs every level down to one texel", "[ui][highway]")
{
    const juce::Colour opaque_blue{0xFF0000FF};
    // 5x3: levels 5x3, 2x1, 1x1.
    CHECK(atlasMipChain(solidImage(5, 3, opaque_blue)).size() == (15U + 2U + 1U) * 4U);
    // 1x4: levels 1x4, 1x2, 1x1.
    CHECK(atlasMipChain(solidImage(1, 4, opaque_blue)).size() == (4U + 2U + 1U) * 4U);
    // 1x1 is its own whole chain.
    CHECK(atlasMipChain(solidImage(1, 1, opaque_blue)).size() == 4U);
    // The shipped head atlas, spread: 384x480 down to 1x1 in nine levels.
    const juce::Image heads = solidImage(384, 480, opaque_blue);
    CHECK(
        atlasMipChain(heads).size() ==
        4U * (184320U + 46080U + 11520U + 2880U + 720U + 180U + 42U + 9U + 1U));

    // A solid image stays solid at every level, and level 0 is the image's own texels.
    const std::vector<std::uint8_t> chain = atlasMipChain(solidImage(4, 4, opaque_blue));
    const ChainTexel top = chainTexel(chain, 4, 4, 0, 3, 3);
    CHECK(top.blue == 255);
    CHECK(top.alpha == 255);
    const ChainTexel bottom = chainTexel(chain, 4, 4, 2, 0, 0);
    CHECK(bottom.blue == 255);
    CHECK(bottom.alpha == 255);
}

// The structural scheme's R and G are straight weights shown only where B covers, so a level
// averages them weighted by B: an uncovered texel's weight never dilutes the covered edge beside
// it (a plain box would), while B and A average plainly, which is what the glyph atlas reads.
TEST_CASE("Atlas mip chain averages weights by coverage and alpha plainly", "[ui][highway]")
{
    juce::Image image{juce::Image::ARGB, 2, 2, true, juce::SoftwareImageType{}};
    // One covered texel (B 255) carrying R 100, G 200; three uncovered texels carrying full R
    // and G that the shader would never show.
    image.setPixelAt(0, 0, juce::Colour::fromRGBA(100, 200, 255, 255));
    image.setPixelAt(1, 0, juce::Colour::fromRGBA(255, 255, 0, 255));
    image.setPixelAt(0, 1, juce::Colour::fromRGBA(255, 255, 0, 255));
    image.setPixelAt(1, 1, juce::Colour::fromRGBA(255, 255, 0, 255));
    const ChainTexel texel = chainTexel(atlasMipChain(image), 2, 2, 1, 0, 0);
    CHECK(texel.blue == 64);
    CHECK(texel.red == 100);
    CHECK(texel.green == 200);
    CHECK(texel.alpha == 255);

    // No coverage anywhere leaves no weight to show.
    juce::Image uncovered{juce::Image::ARGB, 2, 2, true, juce::SoftwareImageType{}};
    uncovered.setPixelAt(0, 0, juce::Colour::fromRGBA(255, 255, 0, 255));
    const ChainTexel none = chainTexel(atlasMipChain(uncovered), 2, 2, 1, 0, 0);
    CHECK(none.blue == 0);
    CHECK(none.red == 0);
    CHECK(none.green == 0);

    // Glyph texels: white premultiplied ink in one of four texels averages to a quarter alpha.
    juce::Image glyph{juce::Image::ARGB, 2, 2, true, juce::SoftwareImageType{}};
    glyph.setPixelAt(0, 0, juce::Colours::white);
    const ChainTexel ink = chainTexel(atlasMipChain(glyph), 2, 2, 1, 0, 0);
    CHECK(ink.alpha == 64);
    CHECK(ink.blue == 64);
}

// Through g_atlas_exact_mip_levels, a cell's slot plus the one-texel reach of a linear sample
// never holds a neighbour's coverage: a fully covered cell beside an empty one leaves the empty
// cell's slot at zero coverage at level 4, where a slot is six texels and a cell four.
TEST_CASE("Atlas gutters keep neighbouring cells apart through the exact levels", "[ui][highway]")
{
    const juce::Colour covered{0xFFFFFFFF};
    const juce::Colour empty{0xFF000000};
    juce::Image grid{
        juce::Image::ARGB,
        2 * g_atlas_cell_texels,
        g_atlas_cell_texels,
        true,
        juce::SoftwareImageType{}
    };
    for (int y = 0; y < grid.getHeight(); ++y)
    {
        for (int x = 0; x < grid.getWidth(); ++x)
        {
            grid.setPixelAt(x, y, x < g_atlas_cell_texels ? covered : empty);
        }
    }
    const juce::Image spread = padAtlasCells(grid, g_atlas_cell_texels, g_atlas_gutter_texels);
    REQUIRE(spread.getWidth() == 192);
    REQUIRE(spread.getHeight() == 96);
    const std::vector<std::uint8_t> chain = atlasMipChain(spread);

    // Level 4 is 12x6: slot 0 is columns 0..5, slot 1 columns 6..11, with the empty cell's art
    // at 7..10. A linear sample inside that art reads at most one texel past it: 6..11.
    const int level = g_atlas_exact_mip_levels;
    bool neighbour_clear = true;
    for (int y = 0; y < 6; ++y)
    {
        for (int x = 6; x < 12; ++x)
        {
            neighbour_clear = neighbour_clear && chainTexel(chain, 192, 96, level, x, y).blue == 0;
        }
    }
    CHECK(neighbour_clear);

    // The covered cell's own art (columns 1..4, rows 1..4 inside its gutter) is still fully
    // covered at this level: the gutter costs the art nothing.
    bool own_art_covered = true;
    for (int y = 1; y < 5; ++y)
    {
        for (int x = 1; x < 5; ++x)
        {
            own_art_covered =
                own_art_covered && chainTexel(chain, 192, 96, level, x, y).blue == 255;
        }
    }
    CHECK(own_art_covered);
}

TEST_CASE("Highway glyph mapping covers printable ASCII only", "[ui][highway]")
{
    // Locals + explicit guards: the optional-access checker cannot connect two separate calls,
    // so each value is bound once and dereferenced only inside its own has_value() branch.
    const std::optional<int> bang = highwayGlyphCellIndex('!');
    REQUIRE(bang.has_value());
    if (bang.has_value())
    {
        CHECK(*bang == 0);
    }
    const std::optional<int> zero = highwayGlyphCellIndex('0');
    REQUIRE(zero.has_value());
    if (zero.has_value())
    {
        CHECK(*zero == '0' - '!');
    }
    const std::optional<int> tilde = highwayGlyphCellIndex('~');
    REQUIRE(tilde.has_value());
    if (tilde.has_value())
    {
        CHECK(*tilde == '~' - '!');
    }
    CHECK_FALSE(highwayGlyphCellIndex(' ').has_value());
    CHECK_FALSE(highwayGlyphCellIndex('\n').has_value());
    CHECK_FALSE(highwayGlyphCellIndex(static_cast<char>(127)).has_value());
}

} // namespace rock_hero::common::ui
