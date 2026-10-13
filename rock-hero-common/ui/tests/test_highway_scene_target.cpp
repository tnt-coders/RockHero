#include "highway/highway_scene_target.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/ui/highway_theme/highway_render_look.h>

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
    CHECK_FALSE(frame.blocks.has_value());
    CHECK_THAT(frame.present_scale, Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(frame.scanlines, Catch::Matchers::WithinULP(0.0F, 0));
}

// The pixel frame renders the full-resolution scene rounded UP to whole blocks, so every block the
// block pass averages lies inside the scene and the scene still covers the window; its block
// canvas holds exactly one texel per block, and it carries the look's scanlines to the present.
TEST_CASE("The pixel frame renders a covering target of whole blocks", "[ui][highway]")
{
    const HighwayPixelLook look{.scale = 3, .scanlines = 0.25, .theme_colors_only = false};

    const HighwaySceneFrame exact = makeHighwaySceneFrame(1920, 1080, look);
    CHECK(exact.target == HighwaySceneTargetSpec{.width = 1920, .height = 1080});
    CHECK(exact.blocks == HighwaySceneTargetSpec{.width = 640, .height = 360});
    CHECK_THAT(exact.present_scale, Catch::Matchers::WithinULP(3.0F, 0));
    CHECK_THAT(exact.scanlines, Catch::Matchers::WithinULP(0.25F, 0));

    const HighwaySceneFrame ragged = makeHighwaySceneFrame(1921, 1082, look);
    CHECK(ragged.target == HighwaySceneTargetSpec{.width = 1923, .height = 1083});
    CHECK(ragged.blocks == HighwaySceneTargetSpec{.width = 641, .height = 361});

    const HighwaySceneFrame tiny = makeHighwaySceneFrame(
        1, 1, HighwayPixelLook{.scale = 8, .scanlines = 0.0, .theme_colors_only = false});
    CHECK(tiny.target == HighwaySceneTargetSpec{.width = 8, .height = 8});
    CHECK(tiny.blocks == HighwaySceneTargetSpec{.width = 1, .height = 1});
    CHECK(tiny.present == HighwayPresent::Copy);

    // One scanline band per virtual pixel row.
    CHECK_THAT(exact.scanline_height, Catch::Matchers::WithinULP(1.0F, 0));
}

// The CRT renders and copies the scene at the window's size, like the plain look, and hands its
// own band height and strength to the present.
TEST_CASE("The CRT frame copies the window-size scene under its own bands", "[ui][highway]")
{
    const HighwaySceneFrame frame = makeHighwaySceneFrame(
        1921, 1081, HighwayCrtLook{.line_height = 3, .scanlines = 0.4, .theme_colors_only = false});

    CHECK(frame.target == HighwaySceneTargetSpec{.width = 1921, .height = 1081});
    CHECK_FALSE(frame.blocks.has_value());
    CHECK(frame.block_colors == HighwayBlockColors::Mean);
    CHECK(frame.present == HighwayPresent::Copy);
    CHECK_THAT(frame.present_scale, Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(frame.scanlines, Catch::Matchers::WithinULP(0.4F, 0));
    CHECK_THAT(frame.scanline_height, Catch::Matchers::WithinULP(3.0F, 0));
}

// A look keeping to the theme's colors snaps them in the block pass, before the present's
// scanlines: the pixel look on its usual canvas, the CRT on a canvas of the window's own size.
TEST_CASE("A look keeping to the theme's colors snaps them on a block canvas", "[ui][highway]")
{
    const HighwaySceneFrame pixel = makeHighwaySceneFrame(
        1920, 1080, HighwayPixelLook{.scale = 3, .scanlines = 0.25, .theme_colors_only = true});
    CHECK(pixel.blocks == HighwaySceneTargetSpec{.width = 640, .height = 360});
    CHECK(pixel.block_colors == HighwayBlockColors::Snapped);
    CHECK(pixel.present == HighwayPresent::Copy);

    const HighwaySceneFrame crt = makeHighwaySceneFrame(
        1921, 1081, HighwayCrtLook{.line_height = 2, .scanlines = 0.4, .theme_colors_only = true});
    CHECK(crt.target == HighwaySceneTargetSpec{.width = 1921, .height = 1081});
    CHECK(crt.blocks == HighwaySceneTargetSpec{.width = 1921, .height = 1081});
    CHECK(crt.block_colors == HighwayBlockColors::Snapped);
    CHECK_THAT(crt.present_scale, Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(crt.scanline_height, Catch::Matchers::WithinULP(2.0F, 0));
}

// The dither sizes its targets like the pixel look and dithers in the block pass, so even at scale
// 1 it has a canvas; the halftone renders at the window's size with no block canvas, its ink
// tracing full-resolution edges, and hands its cell to the present.
TEST_CASE("The dither and halftone frames pick their target and present", "[ui][highway]")
{
    const HighwaySceneFrame dither =
        makeHighwaySceneFrame(1921, 1080, HighwayDitherLook{.scale = 2});
    CHECK(dither.target == HighwaySceneTargetSpec{.width = 1922, .height = 1080});
    CHECK(dither.blocks == HighwaySceneTargetSpec{.width = 961, .height = 540});
    CHECK(dither.block_colors == HighwayBlockColors::Dithered);
    CHECK(dither.present == HighwayPresent::Copy);
    CHECK_THAT(dither.present_scale, Catch::Matchers::WithinULP(2.0F, 0));
    CHECK_THAT(dither.scanlines, Catch::Matchers::WithinULP(0.0F, 0));

    const HighwaySceneFrame fine = makeHighwaySceneFrame(1921, 1080, HighwayDitherLook{.scale = 1});
    CHECK(fine.target == HighwaySceneTargetSpec{.width = 1921, .height = 1080});
    CHECK(fine.blocks == HighwaySceneTargetSpec{.width = 1921, .height = 1080});
    CHECK(fine.block_colors == HighwayBlockColors::Dithered);

    const HighwaySceneFrame halftone =
        makeHighwaySceneFrame(1920, 1080, HighwayHalftoneLook{.cell = 8});
    CHECK(halftone.target == HighwaySceneTargetSpec{.width = 1920, .height = 1080});
    CHECK_FALSE(halftone.blocks.has_value());
    CHECK(halftone.present == HighwayPresent::Halftone);
    CHECK_THAT(halftone.present_scale, Catch::Matchers::WithinULP(8.0F, 0));
}

// A present color is the packed color's channels from 0 to 1 with its alpha dropped: a translucent
// color presents as its opaque self.
TEST_CASE("A present color drops its alpha", "[ui][highway]")
{
    const std::array<float, 4> translucent = highwayPresentColor(0x40FF8000U);
    CHECK_THAT(translucent[0], Catch::Matchers::WithinULP(1.0F, 0));
    CHECK_THAT(translucent[1], Catch::Matchers::WithinULP(128.0F / 255.0F, 0));
    CHECK_THAT(translucent[2], Catch::Matchers::WithinULP(0.0F, 0));
    CHECK_THAT(translucent[3], Catch::Matchers::WithinULP(1.0F, 0));
}

} // namespace rock_hero::common::ui
