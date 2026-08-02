#include "highway/box_mute_profile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_graphics/juce_graphics.h>
#include <optional>

namespace rock_hero::common::ui
{

namespace
{

// Paints one corner-to-corner X of the given stroke width and color, clipped to the cell
// rect so the tips are the contract's axis-aligned corners and the rect is the glyph rect.
void paintCross(
    juce::Graphics& graphics, const juce::Rectangle<float> cell, const float stroke_width,
    const juce::Colour color)
{
    const juce::Graphics::ScopedSaveState save{graphics};
    graphics.reduceClipRegion(cell.toNearestInt());
    graphics.setColour(color);
    juce::Path path;
    path.startNewSubPath(cell.getX(), cell.getY());
    path.lineTo(cell.getRight(), cell.getBottom());
    path.startNewSubPath(cell.getX(), cell.getBottom());
    path.lineTo(cell.getRight(), cell.getY());
    graphics.strokePath(
        path,
        juce::PathStrokeType{
            stroke_width, juce::PathStrokeType::mitered, juce::PathStrokeType::butt
        });
}

// Repaints a solid cross's interior (inside core_width) as a faint color, or clears it
// entirely, leaving only the outer band of the original stroke solid.
void hollowCross(
    juce::Image& image, const juce::Rectangle<float> cell, const float core_width,
    const std::optional<juce::Colour> faint_core)
{
    juce::Image mask{
        juce::Image::ARGB, image.getWidth(), image.getHeight(), true, juce::SoftwareImageType{}
    };
    juce::Graphics graphics{mask};
    paintCross(graphics, cell, core_width, juce::Colours::white);
    const juce::Image::BitmapData mask_bits{mask, juce::Image::BitmapData::readOnly};
    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (mask_bits.getPixelColour(x, y).getFloatAlpha() > 0.5F)
            {
                image.setPixelAt(x, y, faint_core.value_or(juce::Colour{}));
            }
        }
    }
}

} // namespace

TEST_CASE("Box mute profiles measure painted cross-sections", "[ui][highway]")
{
    // Two 96x48 cells stacked; each carries a corner-to-corner X of known stroke and color.
    const juce::Image image{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    juce::Graphics graphics{image};
    paintCross(graphics, {8.0F, 8.0F, 80.0F, 32.0F}, 8.0F, juce::Colour{0xFF2060A0});
    paintCross(graphics, {8.0F, 56.0F, 80.0F, 32.0F}, 8.0F, juce::Colour{0xFFF0F0F0});

    const std::optional<BoxMuteProfiles> profiles = measureBoxMuteProfiles(image);
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // The 50%-alpha crossing sits half the stroke from the centerline; the glyph rect is
    // 32px tall, so the fraction is (8 / 2) / 32 = 0.125 within antialiasing tolerance.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.125).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.125).margin(0.02));

    // The ramp's inner samples carry each mark's painted color at full opacity, and its tail
    // is transparent by construction.
    const auto& palm_ramp = profiles->palm.ramp;
    CHECK(static_cast<int>(palm_ramp[3]) > 240); // inner alpha
    CHECK(static_cast<int>(palm_ramp[2]) > 120); // blue-dominant paint
    CHECK(static_cast<int>(palm_ramp[0]) < 100); // low red
    const auto& full_ramp = profiles->full.ramp;
    CHECK(static_cast<int>(full_ramp[0]) > 200); // near-white paint
    const std::size_t tail = (g_box_mute_ramp_samples - 1) * 4;
    CHECK(static_cast<int>(palm_ramp[tail + 3]) == 0);
    CHECK(static_cast<int>(full_ramp[tail + 3]) == 0);
}

TEST_CASE("Box mute profiles anchor faint and hollow cores to the rim", "[ui][highway]")
{
    // Solid crosses whose interiors are then repainted faint (palm) or cleared (full),
    // leaving a ~2px solid rim band — the outline-dominant authoring the contract allows.
    juce::Image image{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    juce::Graphics graphics{image};
    paintCross(graphics, {8.0F, 8.0F, 80.0F, 32.0F}, 12.0F, juce::Colour{0xFF20C0A0});
    paintCross(graphics, {8.0F, 56.0F, 80.0F, 32.0F}, 12.0F, juce::Colour{0xFFF0F0F0});
    hollowCross(image, {8.0F, 8.0F, 80.0F, 32.0F}, 8.0F, juce::Colour{0x3C20C0A0});
    hollowCross(image, {8.0F, 56.0F, 80.0F, 32.0F}, 8.0F, std::nullopt);

    const std::optional<BoxMuteProfiles> profiles = measureBoxMuteProfiles(image);
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // The stroke edge is the rim's outer 50%-alpha falloff — (12 / 2) / 32 of the glyph rect
    // — not the sub-50% start of the core at the centerline.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));

    // Centerline samples read the faint and hollow paint respectively.
    CHECK(static_cast<int>(profiles->palm.ramp[3]) < 100);
    CHECK(static_cast<int>(profiles->full.ramp[3]) < 30);
}

TEST_CASE("Box mute profiles anchor half-opacity rims at half the peak alpha", "[ui][highway]")
{
    // The palm rim is authored at the chord-box frame's 128/255 alpha; the stroke edge and
    // the glyph rect must track that art's own falloff, not a fixed 50% threshold that the
    // authored opacity would straddle.
    const juce::Image image{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    juce::Graphics graphics{image};
    paintCross(graphics, {8.0F, 8.0F, 80.0F, 32.0F}, 12.0F, juce::Colour{0x8000D2D5});
    paintCross(graphics, {8.0F, 56.0F, 80.0F, 32.0F}, 12.0F, juce::Colour{0x80F0F0F0});

    const std::optional<BoxMuteProfiles> profiles = measureBoxMuteProfiles(image);
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // (12 / 2) / 32 of the glyph rect, at the art's own half-peak falloff.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));

    // The ramp preserves the authored half opacity rather than normalizing it away.
    CHECK(static_cast<int>(profiles->palm.ramp[3]) > 100);
    CHECK(static_cast<int>(profiles->palm.ramp[3]) < 160);
}

TEST_CASE("Box mute profiles reject unanalyzable art", "[ui][highway]")
{
    // A blank image has no glyphs to measure; the renderer treats this as an invalid asset.
    const juce::Image blank{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    CHECK_FALSE(measureBoxMuteProfiles(blank).has_value());

    // A glyph whose alpha never reaches 50% has no rim edge to anchor the stroke to.
    const juce::Image faint{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    juce::Graphics faint_graphics{faint};
    paintCross(faint_graphics, {8.0F, 8.0F, 80.0F, 32.0F}, 8.0F, juce::Colour{0x3CFFFFFF});
    paintCross(faint_graphics, {8.0F, 56.0F, 80.0F, 32.0F}, 8.0F, juce::Colour{0x3CFFFFFF});
    CHECK_FALSE(measureBoxMuteProfiles(faint).has_value());

    // Odd height cannot split into the two authored cells.
    const juce::Image odd{juce::Image::ARGB, 96, 95, true, juce::SoftwareImageType{}};
    CHECK_FALSE(measureBoxMuteProfiles(odd).has_value());
}

} // namespace rock_hero::common::ui
