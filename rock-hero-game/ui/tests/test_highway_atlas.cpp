#include "highway/highway_atlas.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <optional>

namespace rock_hero::game::ui
{

TEST_CASE("Highway atlas layout reports its grid capacity", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_size = 512, .cell_size = 51};

    CHECK(layout.columns() == 10);
    CHECK(layout.capacity() == 100);
}

TEST_CASE("Highway atlas cells tile the texture with a half-texel inset", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_size = 256, .cell_size = 128};
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
}

TEST_CASE("Highway atlas layout clamps out-of-range cells", "[ui][highway]")
{
    const HighwayAtlasLayout layout{.texture_size = 256, .cell_size = 128};

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

} // namespace rock_hero::game::ui
