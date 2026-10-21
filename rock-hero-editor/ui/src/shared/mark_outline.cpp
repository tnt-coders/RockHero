#include "shared/mark_outline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <span>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Where a rounded corner's two control points stand, as a fraction of the radius from the corner:
// the same as juce::Path::addRoundedRectangle's, so a stepped outline's curves match a box ring's.
constexpr float g_corner_control_fraction{0.45f};

// One vertex of a closed polygon and the radius its corner rounds by; zero keeps it square.
struct OutlineCorner
{
    juce::Point<float> at;
    float radius{0.0f};
};

// Closes a polygon through its corners, each rounded by its own radius (held to half of either
// edge it joins, so neighbouring curves never cross).
[[nodiscard]] juce::Path roundedPolygon(const std::span<const OutlineCorner> corners)
{
    const std::size_t count = corners.size();
    juce::Path path;
    for (std::size_t index = 0; index < count; ++index)
    {
        const OutlineCorner& corner = corners[index];
        const juce::Point<float> before = corners[(index + count - 1) % count].at;
        const juce::Point<float> after = corners[(index + 1) % count].at;
        const float radius = std::min(
            {corner.radius,
             corner.at.getDistanceFrom(before) / 2.0f,
             corner.at.getDistanceFrom(after) / 2.0f});
        const auto toward = [&corner, radius](const juce::Point<float> other) {
            const juce::Point<float> offset = other - corner.at;
            const float length = offset.getDistanceFromOrigin();
            return length > 0.0f ? corner.at + (offset * (radius / length)) : corner.at;
        };
        const juce::Point<float> entry = toward(before);
        const juce::Point<float> exit = toward(after);
        if (index == 0)
        {
            path.startNewSubPath(entry);
        }
        else
        {
            path.lineTo(entry);
        }
        if (radius > 0.0f)
        {
            path.cubicTo(
                corner.at + ((entry - corner.at) * g_corner_control_fraction),
                corner.at + ((exit - corner.at) * g_corner_control_fraction),
                exit);
        }
    }
    path.closeSubPath();
    return path;
}

} // namespace

float overlayRingStroke(const float extent) noexcept
{
    return std::max(1.0f, extent / 15.0f) * 1.5f;
}

MarkOutline boxRingOutline(const juce::Rectangle<float> box, const float corner_radius)
{
    const float stroke = overlayRingStroke(box.getHeight());
    juce::Path outline;
    outline.addRoundedRectangle(box.expanded(stroke / 2.0f), corner_radius + (stroke / 2.0f));
    return MarkOutline{.path = std::move(outline), .extent = box.getHeight()};
}

// Rationale lives on the declaration in mark_outline.h. The polygon runs clockwise from the left
// edge's top: chip top, chip right, the step, rail right, rail base, left edge.
MarkOutline spanRowOutline(const juce::Rectangle<float> chip, const juce::Rectangle<float> top_rail)
{
    if (chip.isEmpty())
    {
        return boxRingOutline(top_rail, 0.0f);
    }
    if (top_rail.isEmpty())
    {
        return boxRingOutline(chip, common::ui::g_lane_chip_corner_radius);
    }

    const float stroke = overlayRingStroke(top_rail.getHeight());
    const float half = stroke / 2.0f;
    const juce::Rectangle<float> chip_ring = chip.expanded(half);
    const juce::Rectangle<float> rail_ring = top_rail.expanded(half);
    const float left = std::min(chip_ring.getX(), rail_ring.getX());
    const float chip_corner = common::ui::g_lane_chip_corner_radius + half;
    const auto outline = [extent = top_rail.getHeight()](juce::Path path) {
        return MarkOutline{.path = std::move(path), .extent = extent};
    };

    if (std::abs(chip_ring.getRight() - rail_ring.getRight()) < stroke)
    {
        const float right = std::max(chip_ring.getRight(), rail_ring.getRight());
        return outline(roundedPolygon(
            std::array{
                OutlineCorner{.at = {left, chip_ring.getY()}, .radius = chip_corner},
                OutlineCorner{.at = {right, chip_ring.getY()}, .radius = chip_corner},
                OutlineCorner{.at = {right, rail_ring.getBottom()}, .radius = half},
                OutlineCorner{.at = {left, rail_ring.getBottom()}, .radius = half},
            }));
    }

    // The step runs along the rail's top where the rail reaches further, else along the chip's
    // base; whichever box reaches further owns the convex corner there, the other the concave one.
    const bool rail_wider = rail_ring.getRight() > chip_ring.getRight();
    const float step_y = rail_wider ? rail_ring.getY() : chip_ring.getBottom();
    return outline(roundedPolygon(
        std::array{
            OutlineCorner{.at = {left, chip_ring.getY()}, .radius = chip_corner},
            OutlineCorner{.at = {chip_ring.getRight(), chip_ring.getY()}, .radius = chip_corner},
            OutlineCorner{.at = {chip_ring.getRight(), step_y}, .radius = rail_wider ? 0.0f : half},
            OutlineCorner{.at = {rail_ring.getRight(), step_y}, .radius = rail_wider ? half : 0.0f},
            OutlineCorner{.at = {rail_ring.getRight(), rail_ring.getBottom()}, .radius = half},
            OutlineCorner{.at = {left, rail_ring.getBottom()}, .radius = half},
        }));
}

void strokeMarkRing(juce::Graphics& g, const MarkOutline& mark, const juce::Colour colour)
{
    g.setColour(colour);
    g.strokePath(mark.path, juce::PathStrokeType{overlayRingStroke(mark.extent)});
}

} // namespace rock_hero::editor::ui
