#include "highway/bgfx_color.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::common::ui
{

// The backdrop reaches bgfx as a clear color, which packs 0xRRGGBBAA: opaque black is 0x000000ff,
// and a color with four distinct channels shows the alpha moving to the low byte.
TEST_CASE("Backdrop packs into the clear word with alpha in the low byte", "[ui][bgfx-color]")
{
    CHECK(packRgba(0xFF000000U) == 0x000000ffU);
    CHECK(packRgba(0x11223344U) == 0x22334411U);
}

} // namespace rock_hero::common::ui
