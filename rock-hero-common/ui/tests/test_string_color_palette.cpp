#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

// Lane-window behavior ported from the editor tab renderer: the six highest lanes keep the
// standard set anchored at the first standard color on the sixth-highest lane, a bass keeps the
// low-string colors, and extended-range lanes walk the extended tier downward. These pins mirror
// the editor's test expectations, which double as the bit-identical parity proof.
TEST_CASE("Palette colors strings by their standard-window position", "[ui][string-colors]")
{
    const StringColorPalette& palette = charterClassicPalette();

    CHECK(stringLaneColor(1, 6, palette) == 0xffed0000);
    CHECK(stringLaneColor(6, 6, palette) == 0xffd22cf8);

    // A four-string bass keeps the same low-string colors as a guitar's bottom four.
    CHECK(stringLaneColor(1, 4, palette) == 0xffed0000);
    CHECK(stringLaneColor(4, 4, palette) == 0xffff870a);

    // Extended-range lanes push the standard window up and take extended colors below it.
    CHECK(stringLaneColor(2, 7, palette) == 0xffed0000);
    CHECK(stringLaneColor(1, 7, palette) == 0xff00b5a0);
    CHECK(stringLaneColor(1, 8, palette) == 0xffb6b6b6);
    CHECK(stringLaneColor(2, 8, palette) == 0xff00b5a0);
}

// Known-value checks pinning the java Color semantics on edge channels: darker truncates at 0.7,
// brighter bumps small channels to 3 before dividing, black lifts to (3,3,3), and saturated
// channels stay clamped.
TEST_CASE("Palette derivation reproduces the java color semantics", "[ui][string-colors]")
{
    CHECK(darkerColor(0xffffffff) == 0xffb2b2b2); // int(255 * 0.7) == 178 == 0xb2
    CHECK(darkerColor(0xff000000) == 0xff000000);

    CHECK(brighterColor(0xff000000) == 0xff030303); // all-black lifts to the minimum bump
    CHECK(brighterColor(0xff010101) == 0xff040404); // 1 -> bumped to 3 -> int(3 / 0.7) == 4
    CHECK(brighterColor(0xff020202) == 0xff040404); // 2 -> bumped to 3 -> 4
    CHECK(brighterColor(0xffffffff) == 0xffffffff); // saturated channels stay clamped at 255

    CHECK(multiplyColor(0xffffffff, 0.8) == 0xffcccccc); // int(255 * 0.8) == 204 == 0xcc
    CHECK(multiplyColor(0xff000000, 0.8) == 0xff000000);
}

// Hand-computed chain pins for Charter red: every derived surface of StringLaneStyle must land
// on the exact integers the editor's previous in-module chain produced.
TEST_CASE("Palette style derivation matches the Charter chain for red", "[ui][string-colors]")
{
    const StringLaneStyle style{0xffed0000};

    CHECK(style.lane == 0xffbd0000);         // 237 * 0.8 == 189
    CHECK(style.border_inner == 0xffff0000); // 189 / 0.7 clamps to 255
    CHECK(style.inner == 0xff7c0000);        // 255 -> 178 -> 124
    CHECK(style.linked_inner == 0xff3c0000); // 124 -> 86 -> 60
    CHECK(style.tail == 0xff9c0000);         // 237 * 0.66 == 156
    CHECK(style.tail_edge == 0xffde0000);    // 156 / 0.7 == 222
    CHECK(style.accent == 0xffff0000);       // saturated red stays saturated
}

// The accent brightens without shifting hue: Charter's per-channel double brighter bleached
// saturated hues toward yellow (orange and green accents rendered yellow), so the accent scales
// all channels by one gain instead, capped where the hottest channel saturates.
TEST_CASE("Palette accent brightening preserves the string hue", "[ui][string-colors]")
{
    // Orange: ring (255, 154, 11) is already saturated, so the accent is the ring itself —
    // not the (255, 255, 22) yellow the java chain produced.
    const StringLaneStyle orange{0xffff870a};
    CHECK(orange.accent == orange.border_inner);
    CHECK(orange.accent == 0xffff9a0b);

    // Green likewise keeps its ring hue instead of bleaching toward (255, 255, 163).
    const StringLaneStyle green{0xff85e747};
    CHECK(green.accent == green.border_inner);

    // An unsaturated ring still gains java's double-brighter headroom: the gray eighth string's
    // ring (207, 207, 207) scales uniformly until the channels saturate at white.
    const StringLaneStyle gray{0xffb6b6b6};
    CHECK(gray.accent == 0xffffffff);
}

} // namespace rock_hero::common::ui
