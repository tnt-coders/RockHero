#include "shared/mark_outline.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <rock_hero/common/ui/tab/tab_paint_core.h>

namespace rock_hero::editor::ui
{

namespace
{

// A chip 20 wide standing square on a 3-pixel rail 100 wide, their left edges shared: the ruler's
// span chip on its lane's top rail. The rail's ring stroke is 1.5, so the outline lies 0.75 outside
// both boxes: x from -0.75 to 100.75, the chip's top at 49.25, the step at the rail's top (67.25).
const juce::Rectangle<float> g_chip{0.0f, 50.0f, 20.0f, 18.0f};
const juce::Rectangle<float> g_rail{0.0f, 68.0f, 100.0f, 3.0f};

// Whether two rectangles agree to a thousandth of a pixel on every edge.
[[nodiscard]] bool sameBounds(const juce::Rectangle<float> lhs, const juce::Rectangle<float> rhs)
{
    using Catch::Matchers::WithinAbs;
    return WithinAbs(rhs.getX(), 1e-3).match(lhs.getX()) &&
           WithinAbs(rhs.getY(), 1e-3).match(lhs.getY()) &&
           WithinAbs(rhs.getRight(), 1e-3).match(lhs.getRight()) &&
           WithinAbs(rhs.getBottom(), 1e-3).match(lhs.getBottom());
}

} // namespace

// The outline is the chip and the rail as one stepped shape, never their bounding box: the empty
// band beside the chip and above the rail lies outside it, and the concave corner where the chip
// meets the rail stays square.
TEST_CASE("The span row outline steps from the chip down to its rail", "[ui][mark-outline]")
{
    const MarkOutline outline = spanRowOutline(g_chip, g_rail);
    const juce::Path& path = outline.path;

    CHECK(sameBounds(path.getBounds(), juce::Rectangle<float>{-0.75f, 49.25f, 101.5f, 22.5f}));
    CHECK_THAT(outline.extent, Catch::Matchers::WithinAbs(3.0, 1e-6));
    CHECK(path.contains(10.0f, 60.0f));
    CHECK(path.contains(50.0f, 69.5f));
    // The band beside the chip, which a bounding box would ring.
    CHECK_FALSE(path.contains(25.0f, 60.0f));
    CHECK_FALSE(path.contains(90.0f, 66.0f));
    // Just outside and just inside the square concave corner at (20.75, 67.25).
    CHECK_FALSE(path.contains(21.25f, 66.75f));
    CHECK(path.contains(21.25f, 67.75f));
    CHECK(path.contains(20.25f, 66.75f));
}

// A rail clipped at the view's edge can end before the chip does: the step then runs along the
// chip's base back to the rail.
TEST_CASE("The span row outline steps back to a rail narrower than its chip", "[ui][mark-outline]")
{
    const juce::Rectangle<float> rail{0.0f, 68.0f, 12.0f, 3.0f};
    const juce::Path path = spanRowOutline(g_chip, rail).path;

    CHECK(sameBounds(path.getBounds(), juce::Rectangle<float>{-0.75f, 49.25f, 21.5f, 22.5f}));
    CHECK(path.contains(18.0f, 68.5f));
    CHECK_FALSE(path.contains(18.0f, 70.0f));
    CHECK(path.contains(6.0f, 70.0f));
}

// Right edges closer than the ring's stroke would make a jog no one could read as a step, so the
// outline runs straight down at the wider of the two.
TEST_CASE("The span row outline merges right edges closer than its stroke", "[ui][mark-outline]")
{
    const juce::Rectangle<float> chip{0.0f, 50.0f, 100.0f, 18.0f};
    const juce::Rectangle<float> rail{0.0f, 68.0f, 100.5f, 3.0f};
    const juce::Path path = spanRowOutline(chip, rail).path;

    CHECK(sameBounds(path.getBounds(), juce::Rectangle<float>{-0.75f, 49.25f, 102.0f, 22.5f}));
    CHECK(path.contains(101.0f, 60.0f));
}

// With no chip — a none hold, or a span whose chip the ruler does not place — the outline is the
// rail's own ring; with no visible rail — a lane scrolled up under the ruler — the chip's.
TEST_CASE("The span row outline falls back to a lone box's ring", "[ui][mark-outline]")
{
    const juce::Rectangle<float> rail_ring = boxRingOutline(g_rail, 0.0f).path.getBounds();
    CHECK(sameBounds(spanRowOutline(juce::Rectangle<float>{}, g_rail).path.getBounds(), rail_ring));

    const MarkOutline chip_alone = spanRowOutline(g_chip, juce::Rectangle<float>{});
    CHECK(sameBounds(
        chip_alone.path.getBounds(),
        boxRingOutline(g_chip, common::ui::g_lane_chip_corner_radius).path.getBounds()));
    CHECK_THAT(chip_alone.extent, Catch::Matchers::WithinAbs(18.0, 1e-6));
}

// A boxed mark's ring lies wholly outside the box: its outline is the box grown by half the
// stroke, so the stroke's inner edge meets the box's own edge.
TEST_CASE("A box ring outline lies half a stroke outside its box", "[ui][mark-outline]")
{
    const MarkOutline outline = boxRingOutline(g_rail, 0.0f);
    CHECK_THAT(overlayRingStroke(outline.extent), Catch::Matchers::WithinAbs(1.5, 1e-6));
    CHECK(sameBounds(outline.path.getBounds(), g_rail.expanded(0.75f)));
}

} // namespace rock_hero::editor::ui
