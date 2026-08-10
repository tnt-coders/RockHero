#include "highway/highway_atlas.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>

namespace rock_hero::common::ui
{

TEST_CASE("Highway atlas layout reports its grid capacity", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_width = 512, .texture_height = 512, .cell_size = 51};

    CHECK(layout.columns() == 10);
    CHECK(layout.rows() == 10);
    // The glyph grid's covenant is a relationship, not a number: cellRect() clamps an
    // out-of-range index, so the cell size must leave room for the whole printable-ASCII range
    // ('!'..'~') or every character past the end silently draws the last glyph.
    CHECK(layout.capacity() >= '~' - '!' + 1);

    // The head atlas is square and holds exactly the cells the renderer names.
    const HighwayAtlasLayout heads{.texture_width = 256, .texture_height = 256, .cell_size = 64};
    CHECK(heads.columns() == 4);
    CHECK(heads.rows() == 4);
    CHECK(heads.capacity() == g_head_cell_count);

    // Rectangular grids still count rows by height, so a non-square asset cannot silently
    // renumber the cells.
    const HighwayAtlasLayout composed{
        .texture_width = 512, .texture_height = 640, .cell_size = 128
    };
    CHECK(composed.columns() == 4);
    CHECK(composed.rows() == 5);
    CHECK(composed.capacity() == 20);
}

TEST_CASE("Highway atlas cells tile the texture with a half-texel inset", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_width = 256, .texture_height = 256, .cell_size = 128};
    const float half_texel = 0.5F / 256.0F;

    const auto first = layout.cellRect(0);
    CHECK_THAT(first[0], Catch::Matchers::WithinAbs(half_texel, 1e-7));
    CHECK_THAT(first[1], Catch::Matchers::WithinAbs(half_texel, 1e-7));
    CHECK_THAT(first[2], Catch::Matchers::WithinAbs(0.5F - half_texel, 1e-7));
    CHECK_THAT(first[3], Catch::Matchers::WithinAbs(0.5F - half_texel, 1e-7));

    // Row-major: index 3 of a 2x2 grid is the bottom-right cell.
    const auto last = layout.cellRect(3);
    CHECK_THAT(last[0], Catch::Matchers::WithinAbs(0.5F + half_texel, 1e-7));
    CHECK_THAT(last[1], Catch::Matchers::WithinAbs(0.5F + half_texel, 1e-7));
    CHECK_THAT(last[2], Catch::Matchers::WithinAbs(1.0F - half_texel, 1e-7));
    CHECK_THAT(last[3], Catch::Matchers::WithinAbs(1.0F - half_texel, 1e-7));

    // A rectangular grid insets u by width and v by height, and the last row's first cell
    // (index 16 of a 4x5 grid) sits at the bottom-left.
    const HighwayAtlasLayout composed{
        .texture_width = 512, .texture_height = 640, .cell_size = 128
    };
    const auto chevron = composed.cellRect(16);
    CHECK_THAT(chevron[0], Catch::Matchers::WithinAbs(0.5F / 512.0F, 1e-7));
    CHECK_THAT(chevron[1], Catch::Matchers::WithinAbs(0.8F + (0.5F / 640.0F), 1e-7));
    CHECK_THAT(chevron[2], Catch::Matchers::WithinAbs(0.25F - (0.5F / 512.0F), 1e-7));
    CHECK_THAT(chevron[3], Catch::Matchers::WithinAbs(1.0F - (0.5F / 640.0F), 1e-7));
}

// The legato pair shares one cell, drawn upright for the hammer-on and vertically flipped for
// the pull-off, so the two can never disagree in weight or border the way separately authored
// art did. Swapping a cell's v coordinates is exactly that flip, and it stays inside the cell.
TEST_CASE("Highway atlas legato cell mirrors within its own bounds", "[ui][highway]")
{
    const HighwayAtlasLayout heads{.texture_width = 256, .texture_height = 256, .cell_size = 64};
    const auto cell = heads.cellRect(g_head_cell_legato);

    // The flip is a swap of the vertical pair, so it samples the same rows in reverse and
    // never reaches a neighbouring cell.
    CHECK(cell[1] < cell[3]);
    const float row_top = 1.0F / 4.0F;
    CHECK_THAT(cell[1], Catch::Matchers::WithinAbs(row_top + (0.5F / 256.0F), 1e-7));
    CHECK_THAT(cell[3], Catch::Matchers::WithinAbs((2.0F * row_top) - (0.5F / 256.0F), 1e-7));

    // Every named cell is inside the square grid the shipped asset provides.
    CHECK(g_head_cell_bend < g_head_cell_count);
    CHECK(g_head_cell_pinch_harmonic < g_head_cell_count);
    CHECK(heads.capacity() == g_head_cell_count);
}

TEST_CASE("Highway atlas layout clamps out-of-range cells", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_width = 256, .texture_height = 256, .cell_size = 128};

    CHECK(layout.cellRect(99) == layout.cellRect(3));
    CHECK(layout.cellRect(-5) == layout.cellRect(0));
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
