#include "highway/bgfx_color.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>

namespace rock_hero::common::ui
{

// The default theme is the look both products draw, so every role is pinned at its value, alpha
// included: a role whose alpha byte were normalized or re-derived would change pixels silently.
TEST_CASE("Default highway theme pins every role's color and alpha", "[ui][highway-theme]")
{
    const HighwayTheme& theme = defaultHighwayTheme();

    CHECK(theme.backdrop == 0xFF000000U);
    CHECK(theme.beat_bar == 0xFF0F3B5EU);
    CHECK(theme.attack_line == 0xFF00D2D5U);
    CHECK(theme.lit_lane == 0x402590E8U);
    CHECK(theme.lit_lane_dotted == 0x40185C94U);
    CHECK(theme.lane_border == 0xFF07928FU);
    CHECK(theme.fret_line == 0xFF202020U);
    CHECK(theme.fret_line_lit == 0xFFC0C0C0U);
    CHECK(theme.fret_number_lit == 0xFF87DDF6U);
    CHECK(theme.fret_number_dim == 0x8007928FU);
    CHECK(theme.fret_number_target == 0xFFFFA821U);
    CHECK(theme.strike_fretting == 0xFFFFB040U);
    CHECK(theme.strike_picking == 0xFFFFFFFFU);
    CHECK(theme.chord_box == 0xFF00D2D5U);
    CHECK(theme.chord_box_dark == 0xFF003C3DU);
    CHECK(theme.pick_furniture == 0xFFC0C7D8U);
    CHECK(theme.full_mute_mark == 0xFF52798AU);
    CHECK(theme.arpeggio == 0xFFC040FFU);
    CHECK(theme.capo_shade == 0xB4000000U);
    CHECK(theme.capo_bar == 0xFFB9BEC6U);
    CHECK(theme.capo_bar_rim == 0xFF14161AU);
    CHECK(theme.inlay_tint == 0xFFFFFFFFU);
    CHECK(theme.label_text == 0xFFFFFFFFU);
}

// The backdrop reaches bgfx as a clear color, which packs 0xRRGGBBAA: the default's opaque black
// is 0x000000ff, and a color with four distinct channels shows the alpha moving to the low byte.
TEST_CASE("Backdrop packs into the clear word with alpha in the low byte", "[ui][highway-theme]")
{
    CHECK(packRgba(defaultHighwayTheme().backdrop) == 0x000000ffU);
    CHECK(packRgba(0x11223344U) == 0x22334411U);
}

} // namespace rock_hero::common::ui
