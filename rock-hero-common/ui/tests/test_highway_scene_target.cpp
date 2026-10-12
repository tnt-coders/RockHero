#include "highway/highway_scene_target.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/ui/highway_theme/highway_render_look.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>

namespace rock_hero::common::ui
{

// The plain frame is the window itself: the scene renders at the window's size, and the present
// copies one scene pixel to one window pixel with no scanlines — the identity present, which is
// what keeps the off-screen path's picture the one the window drew when the scene rendered
// straight into it.
TEST_CASE("The plain frame renders at the window's size and presents unchanged", "[ui][highway]")
{
    const HighwaySceneFrame frame = makeHighwaySceneFrame(1920, 1080, HighwayPlainLook{});

    CHECK(frame.target == HighwaySceneTargetSpec{.width = 1920, .height = 1080});
    CHECK_THAT(frame.present_scale, Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(frame.scanlines, Catch::Matchers::WithinULP(0.0F, 0));
}

// The pixel frame renders the full-resolution scene rounded UP to whole blocks, so every block the
// present averages lies inside the target and the target still covers the window; it carries the
// look's scanlines to the present.
TEST_CASE("The pixel frame renders a covering target of whole blocks", "[ui][highway]")
{
    const HighwayPixelLook look{.scale = 3, .scanlines = 0.25};

    const HighwaySceneFrame exact = makeHighwaySceneFrame(1920, 1080, look);
    CHECK(exact.target == HighwaySceneTargetSpec{.width = 1920, .height = 1080});
    CHECK_THAT(exact.present_scale, Catch::Matchers::WithinULP(3.0F, 0));
    CHECK_THAT(exact.scanlines, Catch::Matchers::WithinULP(0.25F, 0));

    const HighwaySceneFrame ragged = makeHighwaySceneFrame(1921, 1082, look);
    CHECK(ragged.target == HighwaySceneTargetSpec{.width = 1923, .height = 1083});

    const HighwaySceneFrame tiny =
        makeHighwaySceneFrame(1, 1, HighwayPixelLook{.scale = 8, .scanlines = 0.0});
    CHECK(tiny.target == HighwaySceneTargetSpec{.width = 8, .height = 8});
    CHECK(tiny.present == HighwayPresent::Copy);
}

// The dither sizes its target like the pixel look and presents through its own program; the
// halftone renders at the window's size, its ink tracing full-resolution edges, and hands its cell
// to the present.
TEST_CASE("The dither and halftone frames pick their target and present", "[ui][highway]")
{
    const HighwaySceneFrame dither =
        makeHighwaySceneFrame(1921, 1080, HighwayDitherLook{.scale = 2});
    CHECK(dither.target == HighwaySceneTargetSpec{.width = 1922, .height = 1080});
    CHECK(dither.present == HighwayPresent::Dither);
    CHECK_THAT(dither.present_scale, Catch::Matchers::WithinULP(2.0F, 0));

    const HighwaySceneFrame halftone =
        makeHighwaySceneFrame(1920, 1080, HighwayHalftoneLook{.cell = 8});
    CHECK(halftone.target == HighwaySceneTargetSpec{.width = 1920, .height = 1080});
    CHECK(halftone.present == HighwayPresent::Halftone);
    CHECK_THAT(halftone.present_scale, Catch::Matchers::WithinULP(8.0F, 0));
}

// The dither palette is the theme's own colors, opaque, then black and white â€” nothing else.
TEST_CASE("The dither palette holds the theme's colors, black and white", "[ui][highway]")
{
    const HighwayTheme theme = testing::fixtureHighwayTheme();
    const StringColorPalette strings = testing::fixtureStringColors();
    const HighwayDitherPalette palette = makeHighwayDitherPalette(theme, strings);

    REQUIRE(palette.count >= strings.colors.size() + 3);
    const std::array<float, 4> backdrop = highwayPresentColor(theme.backdrop);
    CHECK(palette.colors.front() == backdrop);
    CHECK(palette.colors.at(palette.count - 3) == highwayPresentColor(strings.colors.back()));
    CHECK(palette.colors.at(palette.count - 2) == highwayPresentColor(0xFF000000U));
    CHECK(palette.colors.at(palette.count - 1) == highwayPresentColor(0xFFFFFFFFU));

    // Alpha is dropped: a translucent color presents as its opaque self.
    const std::array<float, 4> translucent = highwayPresentColor(0x40FF8000U);
    CHECK_THAT(translucent[0], Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(translucent[1], Catch::Matchers::WithinULP(128.0F / 255.0F, 0));
    CHECK_THAT(translucent[2], Catch::Matchers::WithinULP(0.0F, 0));
    CHECK_THAT(translucent[3], Catch::Matchers::WithinULP(1.0F, 0));
}

} // namespace rock_hero::common::ui
