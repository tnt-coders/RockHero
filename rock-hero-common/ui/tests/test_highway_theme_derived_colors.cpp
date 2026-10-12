#include "highway_theme/highway_theme_derived_colors.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>

namespace rock_hero::common::ui
{

// CIELAB spans black to white over 100 L*, and equal RGB lies no distance apart.
TEST_CASE("CIELAB measures colors under the D65 white", "[ui][highway-theme]")
{
    CHECK_THAT(cielabOf(0xFFFFFFFFU).lightness, Catch::Matchers::WithinAbs(100.0, 1e-3));
    CHECK_THAT(cielabOf(0xFF000000U).lightness, Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK_THAT(deltaE(0xFFFFFFFFU, 0xFF000000U), Catch::Matchers::WithinAbs(100.0, 1e-3));
    CHECK_THAT(deltaE(0xFF5CD3F5U, 0x005CD3F5U), Catch::Matchers::WithinAbs(0.0, 1e-12));
}

// The picking hand keeps each color's hue at a quarter of its chroma and at its perceived
// lightness, so the pale counterpart looks as bright as the color it comes from.
TEST_CASE("A picking-hand color is its fretting color paler at the same hue", "[ui][highway-theme]")
{
    // The original teal and purple, and the ice theme's cyan and rose.
    CHECK(pickingHandColor(0xFF00D2D5U) == 0xFFB1CFCFU);
    CHECK(pickingHandColor(0xFFC040FFU) == 0xFFD4B4E0U);
    CHECK(pickingHandColor(0xFF5CD3F5U) == 0xFFBCD2DAU);
    CHECK(pickingHandColor(0xFFFF7FB3U) == 0xFFD5B4BFU);

    // A grey has no chroma to give up, so it is its own counterpart, and the alpha carries over.
    CHECK(pickingHandColor(0xFF808080U) == 0xFF808080U);
    CHECK(pickingHandColor(0x80808080U) == 0x80808080U);
    CHECK(pickingHandColor(0xFFFFFFFFU) == 0xFFFFFFFFU);
    CHECK(pickingHandColor(0xFF000000U) == 0xFF000000U);
}

// The fill's middle scales each channel by 0.286 and rounds, never truncates, keeping the alpha.
TEST_CASE("A box fill's middle is its color darkened and rounded", "[ui][highway-theme]")
{
    // 0xD5 scales to 60.9, which rounds to 0x3D where truncating would give 0x3C.
    CHECK(boxFillMiddle(0xFF00D2D5U) == 0xFF003C3DU);
    // 0xFA scales to exactly 71.5, which rounds half up.
    CHECK(boxFillMiddle(0x80FAFAFAU) == 0x80484848U);
    CHECK(boxFillMiddle(0xFF000000U) == 0xFF000000U);
}

// The dim fret number is the lane lines' color at half their alpha, rounded half up.
TEST_CASE("The dim fret number is lane_line at half alpha", "[ui][highway-theme]")
{
    HighwayTheme theme = testing::fixtureHighwayTheme();
    theme.lane_line = 0xFF6C7EB4U;
    CHECK(fretNumberDim(theme) == 0x806C7EB4U);
    theme.lane_line = 0x40408090U;
    CHECK(fretNumberDim(theme) == 0x20408090U);
    theme.lane_line = 0x01408090U;
    CHECK(fretNumberDim(theme) == 0x01408090U);
}

} // namespace rock_hero::common::ui
