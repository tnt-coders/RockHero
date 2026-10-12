#include "highway_theme/highway_theme_derived_colors.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>

namespace rock_hero::common::ui
{

namespace
{

// The L* of a 2D span ink's luminance, 1.05 / 4.45 - 0.05, from which a byte's rounding moves an
// ink by a few hundredths.
constexpr double g_span_ink_lightness = 50.21;

} // namespace

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

// Every 2D span ink sits at the one luminance under which white reads at 4.45:1, whatever its base:
// light, dark, saturated, grey or black.
TEST_CASE("A 2D span ink holds every base to one lightness", "[ui][highway-theme]")
{
    // The original teal and purple, the lane's hand-tuned blue and purple inks, the ice theme's
    // cyan and purple, a navy too dark to scale, black and a mid grey.
    for (const ArgbColor base :
         {0xFF00D2D5U,
          0xFFC040FFU,
          0xFF4173DEU,
          0xFF9061C7U,
          0xFF5CD3F5U,
          0xFFA361FFU,
          0xFF000080U,
          0xFF000000U,
          0xFF808080U})
    {
        INFO(base);
        CHECK_THAT(
            cielabOf(laneShapeInk(base)).lightness,
            Catch::Matchers::WithinAbs(g_span_ink_lightness, 0.5));
    }

    // The hand-tuned inks already sit at that luminance, so each is its own ink.
    CHECK(laneShapeInk(0xFF4173DEU) == 0xFF4173DEU);
    CHECK(laneShapeInk(0xFF9061C7U) == 0xFF9061C7U);
}

// A base that can reach the ink's luminance by scaling keeps its chromaticity: its hue stays, a
// channel it lacks stays absent, and its alpha carries over.
TEST_CASE("A 2D span ink scales a bright base along its own hue", "[ui][highway-theme]")
{
    constexpr ArgbColor teal = 0x80'00D2D5U;
    const ArgbColor ink = laneShapeInk(teal);
    CHECK(ink == 0x80'008587U);

    const Cielab base_lab = cielabOf(teal);
    const Cielab ink_lab = cielabOf(ink);
    const double hue_turn = std::remainder(
        std::atan2(ink_lab.b, ink_lab.a) - std::atan2(base_lab.b, base_lab.a),
        2.0 * std::numbers::pi);
    CHECK_THAT(hue_turn, Catch::Matchers::WithinAbs(0.0, std::numbers::pi / 180.0));
}

// A base too dark to scale, whose brightest channel would pass full, blends toward white instead:
// still at the ink's lightness, keeping the blue's lead over the two channels it lacks.
TEST_CASE("A 2D span ink blends a dark base toward white", "[ui][highway-theme]")
{
    const ArgbColor ink = laneShapeInk(0xFF000080U);
    const ArgbColor red = (ink >> 16U) & 0xFFU;
    const ArgbColor green = (ink >> 8U) & 0xFFU;
    const ArgbColor blue = ink & 0xFFU;
    CHECK(red == green);
    CHECK(green < blue);
    CHECK_THAT(cielabOf(ink).lightness, Catch::Matchers::WithinAbs(g_span_ink_lightness, 0.5));
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
