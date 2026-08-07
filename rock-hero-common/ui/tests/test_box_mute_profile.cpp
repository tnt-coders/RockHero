#include "highway/box_mute_profile.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <span>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// One authored band of a mark's cross-section, in the structural channel scheme's terms.
struct Band
{
    /*! Coverage the band reaches where the rasterized stroke is fully inside it, in [0, 1]. */
    float coverage{1.0F};

    /*! Tint weight the band carries, in [0, 1]. */
    float weight{1.0F};
};

// One cell's authored glyph: a corner-to-corner X of `stroke_width`, optionally with a narrower
// inner cross repainted as a distinct core band (the contract's faint or hollow cores).
struct CellArt
{
    juce::Rectangle<float> rect;
    float stroke_width{0.0F};
    Band rim;
    float core_stroke_width{0.0F};
    std::optional<Band> core;
};

// Rasterizes one corner-to-corner X into an antialiased coverage mask, clipped to the cell rect
// so the tips are the contract's axis-aligned corners and the rect is the glyph rect.
[[nodiscard]] juce::Image crossMask(
    const int width, const int height, const juce::Rectangle<float> cell, const float stroke_width)
{
    const juce::Image mask{juce::Image::ARGB, width, height, true, juce::SoftwareImageType{}};
    juce::Graphics graphics{mask};
    graphics.reduceClipRegion(cell.toNearestInt());
    graphics.setColour(juce::Colours::white);
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
    return mask;
}

// Assembles the art the authoring contract expects: an RGB image with NO alpha channel, where R
// is the tint weight, G is the achromatic lift (always 0 here — neither shipped mark has one),
// and B is coverage. Coverage comes from an antialiased mask so the measurement sees real falloff
// at the stroke edges, exactly as the baked asset does.
[[nodiscard]] juce::Image structuralArt(
    const int width, const int height, const std::span<const CellArt> cells)
{
    juce::Image art{juce::Image::RGB, width, height, true, juce::SoftwareImageType{}};
    for (const CellArt& cell : cells)
    {
        const juce::Image rim_mask = crossMask(width, height, cell.rect, cell.stroke_width);
        // The core's mask and its band are both constant across the cell's pixel sweep, so they
        // are resolved once here rather than per pixel — and reading the optional inside its own
        // has_value guard is the form the optional-access analysis can follow.
        juce::Image core_mask;
        Band core_band{};
        if (cell.core.has_value())
        {
            core_mask = crossMask(width, height, cell.rect, cell.core_stroke_width);
            core_band = *cell.core;
        }
        const juce::Image::BitmapData rim_bits{rim_mask, juce::Image::BitmapData::readOnly};
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const float mask_coverage = rim_bits.getPixelColour(x, y).getFloatAlpha();
                if (mask_coverage <= 0.0F)
                {
                    continue;
                }
                Band band = cell.rim;
                if (core_mask.isValid() && core_mask.getPixelAt(x, y).getFloatAlpha() > 0.5F)
                {
                    band = core_band;
                }
                art.setPixelAt(
                    x,
                    y,
                    juce::Colour::fromFloatRGBA(
                        band.weight, 0.0F, mask_coverage * band.coverage, 1.0F));
            }
        }
    }
    return art;
}

// The tint weight, lift, and coverage of one ramp sample, as ints.
[[nodiscard]] int rampWeight(const BoxMuteProfile& profile, const std::size_t sample)
{
    return profile.ramp.at(sample * 4);
}

[[nodiscard]] int rampLift(const BoxMuteProfile& profile, const std::size_t sample)
{
    return profile.ramp.at((sample * 4) + 1);
}

[[nodiscard]] int rampCoverage(const BoxMuteProfile& profile, const std::size_t sample)
{
    return profile.ramp.at((sample * 4) + 2);
}

} // namespace

// Two 96x48 cells stacked; each carries a corner-to-corner X of known stroke and tint weight.
TEST_CASE("Box mute profiles measure painted cross-sections", "[ui][highway]")
{
    const std::vector<CellArt> cells{
        {.rect = {8.0F, 8.0F, 80.0F, 32.0F},
         .stroke_width = 8.0F,
         .rim = {.coverage = 1.0F, .weight = 0.5F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
        {.rect = {8.0F, 56.0F, 80.0F, 32.0F},
         .stroke_width = 8.0F,
         .rim = {.coverage = 1.0F, .weight = 0.94F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
    };
    const std::expected<BoxMuteProfiles, BoxMuteProfileError> profiles =
        measureBoxMuteProfiles(structuralArt(96, 96, cells));
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // The 50%-coverage crossing sits half the stroke from the centerline; the glyph rect is
    // 32px tall, so the fraction is (8 / 2) / 32 = 0.125 within antialiasing tolerance.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.125).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.125).margin(0.02));

    // Inner samples carry each mark's authored coverage and tint weight; neither mark lifts
    // toward white, and the tail is forced to zero coverage so sampling past the extent
    // dissolves.
    CHECK(rampCoverage(profiles->palm, 0) > 240);
    CHECK(rampWeight(profiles->palm, 0) == Catch::Approx(128).margin(6));
    CHECK(rampWeight(profiles->full, 0) == Catch::Approx(240).margin(6));
    CHECK(rampLift(profiles->palm, 0) == 0);
    CHECK(rampLift(profiles->full, 0) == 0);
    const std::size_t tail = g_box_mute_ramp_samples - 1;
    CHECK(rampCoverage(profiles->palm, tail) == 0);
    CHECK(rampCoverage(profiles->full, tail) == 0);

    // The fourth byte of a live sample is only padding for the RGBA8 upload.
    CHECK(static_cast<int>(profiles->palm.ramp[3]) == 255);
}

// Solid crosses whose interiors are then repainted as a faint (palm) or cleared (full) core band,
// leaving a ~2px solid rim — the outline-dominant authoring the contract allows.
TEST_CASE("Box mute profiles anchor faint and hollow cores to the rim", "[ui][highway]")
{
    const std::vector<CellArt> cells{
        {.rect = {8.0F, 8.0F, 80.0F, 32.0F},
         .stroke_width = 12.0F,
         .rim = {.coverage = 1.0F, .weight = 0.62F},
         .core_stroke_width = 8.0F,
         .core = Band{.coverage = 0.235F, .weight = 0.62F}},
        {.rect = {8.0F, 56.0F, 80.0F, 32.0F},
         .stroke_width = 12.0F,
         .rim = {.coverage = 1.0F, .weight = 0.94F},
         .core_stroke_width = 8.0F,
         .core = Band{.coverage = 0.0F, .weight = 0.0F}},
    };
    const std::expected<BoxMuteProfiles, BoxMuteProfileError> profiles =
        measureBoxMuteProfiles(structuralArt(96, 96, cells));
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // The stroke edge is the rim's outer 50%-coverage falloff — (12 / 2) / 32 of the glyph rect
    // — not the sub-50% start of the core at the centerline.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));

    // Centerline samples read the faint and hollow core respectively.
    CHECK(rampCoverage(profiles->palm, 0) < 100);
    CHECK(rampCoverage(profiles->full, 0) < 30);
}

// A rim the art did not carry to full coverage still anchors at its own edge: the stroke boundary
// tracks half the glyph's PEAK coverage, not a fixed 50% the authored level would straddle.
TEST_CASE("Box mute profiles anchor partly covered rims at half the peak", "[ui][highway]")
{
    const std::vector<CellArt> cells{
        {.rect = {8.0F, 8.0F, 80.0F, 32.0F},
         .stroke_width = 12.0F,
         .rim = {.coverage = 0.502F, .weight = 1.0F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
        {.rect = {8.0F, 56.0F, 80.0F, 32.0F},
         .stroke_width = 12.0F,
         .rim = {.coverage = 0.502F, .weight = 0.94F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
    };
    const std::expected<BoxMuteProfiles, BoxMuteProfileError> profiles =
        measureBoxMuteProfiles(structuralArt(96, 96, cells));
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // (12 / 2) / 32 of the glyph rect, at the art's own half-peak falloff.
    CHECK(profiles->palm.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));
    CHECK(profiles->full.stroke_half_fraction == Catch::Approx(0.1875).margin(0.02));

    // The ramp preserves the authored partial coverage rather than normalizing it away.
    CHECK(rampCoverage(profiles->palm, 0) > 100);
    CHECK(rampCoverage(profiles->palm, 0) < 160);
}

// An alpha-bearing image has already been premultiplied by the decoder, silently scaling the
// structural channels. The contract rejects it rather than measuring scaled signals — this is the
// guard that keeps a future re-bake from reintroducing an alpha channel unnoticed.
TEST_CASE("Box mute profiles reject an alpha-bearing image", "[ui][highway]")
{
    // Otherwise measurable art — same geometry as the accepted cases — but carrying alpha.
    const juce::Image alpha_bearing{juce::Image::ARGB, 96, 96, true, juce::SoftwareImageType{}};
    const juce::Graphics graphics{alpha_bearing};
    graphics.drawImageAt(crossMask(96, 96, {8.0F, 8.0F, 80.0F, 32.0F}, 12.0F), 0, 0);
    graphics.drawImageAt(crossMask(96, 96, {8.0F, 56.0F, 80.0F, 32.0F}, 12.0F), 0, 0);

    const std::expected<BoxMuteProfiles, BoxMuteProfileError> result =
        measureBoxMuteProfiles(alpha_bearing);
    REQUIRE_FALSE(result.has_value());
    if (!result.has_value())
    {
        CHECK(result.error() == BoxMuteProfileError::AlphaBearingImage);
    }
}

// The shipped asset has to satisfy its own contract. Everything above measures synthetic art, so
// this is the only check that covers the real file — the guard that a future re-bake shipping an
// alpha channel, or art the measurement cannot read, fails here instead of at application startup.
// Plan 54 Phase 3 generalizes this shape to every themed asset.
TEST_CASE("The shipped box mute art satisfies its authoring contract", "[ui][highway]")
{
    const juce::File art = juce::File{ROCK_HERO_TEXTURES_DIR}.getChildFile("chords.png");
    REQUIRE(art.existsAsFile());
    juce::MemoryBlock bytes;
    REQUIRE(art.loadFileAsData(bytes));

    const std::expected<BoxMuteProfiles, BoxMuteProfileError> profiles = measureBoxMuteProfiles(
        std::span{static_cast<const std::byte*>(bytes.getData()), bytes.getSize()});
    REQUIRE(profiles.has_value());
    if (!profiles.has_value())
    {
        return;
    }

    // Both marks must measure a stroke the renderer can lay out — thicker than a hairline, well
    // inside the glyph — with visible art reaching past that stroke edge.
    for (const BoxMuteProfile& profile : {profiles->palm, profiles->full})
    {
        CHECK(profile.stroke_half_fraction > 0.05);
        CHECK(profile.stroke_half_fraction < 0.33);
        CHECK(profile.extent_fraction > profile.stroke_half_fraction);
    }
}

// A blank image has no glyphs to measure, a uniformly faint one has no rim to anchor, and
// an odd height cannot split into the two authored cells.
TEST_CASE("Box mute profiles reject unanalyzable art", "[ui][highway]")
{
    // A blank image has no coverage to measure; the renderer treats this as an invalid asset.
    const juce::Image blank{juce::Image::RGB, 96, 96, true, juce::SoftwareImageType{}};
    CHECK_FALSE(measureBoxMuteProfiles(blank).has_value());

    // A glyph whose coverage never reaches 25% has no rim edge to anchor the stroke to.
    const std::vector<CellArt> faint_cells{
        {.rect = {8.0F, 8.0F, 80.0F, 32.0F},
         .stroke_width = 8.0F,
         .rim = {.coverage = 0.235F, .weight = 1.0F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
        {.rect = {8.0F, 56.0F, 80.0F, 32.0F},
         .stroke_width = 8.0F,
         .rim = {.coverage = 0.235F, .weight = 1.0F},
         .core_stroke_width = 0.0F,
         .core = std::nullopt},
    };
    CHECK_FALSE(measureBoxMuteProfiles(structuralArt(96, 96, faint_cells)).has_value());

    // Odd height cannot split into the two authored cells.
    const juce::Image odd{juce::Image::RGB, 96, 95, true, juce::SoftwareImageType{}};
    CHECK_FALSE(measureBoxMuteProfiles(odd).has_value());
}

} // namespace rock_hero::common::ui
