#include "highway/head_art_profile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <juce_graphics/juce_graphics.h>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// The synthetic atlas mirrors the shipped grid: four 64-texel columns, the standard head in cell
// 0 and the node-head base in cell 4 directly below it. Coverage is the B channel; the images
// carry no alpha, matching the structural-art contract.
constexpr int g_atlas_width = 256;
constexpr int g_atlas_height = 320;
constexpr int g_cell = 64;

[[nodiscard]] juce::Image blankAtlas()
{
    return juce::Image{
        juce::Image::RGB, g_atlas_width, g_atlas_height, true, juce::SoftwareImageType{}
    };
}

void setCoverage(juce::Image& image, const int x, const int y, const int coverage)
{
    image.setPixelAt(x, y, juce::Colour::fromRGB(0, 0, static_cast<juce::uint8>(coverage)));
}

// A hard-edged solid rectangle in cell 0: columns [x0, x1] by rows [y0, y1], all in cell-local
// indices. Hard edges make every expected value exact: the 50% crossing of a 0-to-255 step sits
// half a texel outside the last solid pixel.
void paintRectangle(
    juce::Image& image, const int x0, const int x1, const int y0, const int y1, const int coverage)
{
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            setCoverage(image, x, y, coverage);
        }
    }
}

// A hard-edged solid diamond |dx| + |dy| <= span in cell 4, centred on cell-local integer index
// (32, 32) — image row 64 + 32.
void paintDiamond(juce::Image& image, const int span)
{
    for (int y = 0; y < g_cell; ++y)
    {
        for (int x = 0; x < g_cell; ++x)
        {
            if (std::abs(x - 32) + std::abs(y - 32) <= span)
            {
                setCoverage(image, x, g_cell + y, 255);
            }
        }
    }
}

// A supersampled rounded rectangle in cell 0, centred at index (31.5, 31.5) — the drawn quad's
// exact centre, a half-texel boundary: coverage is the sub-pixel area fraction, so the 50%
// contour lands on the true analytic outline, the corner fit has a known-radius arc to recover,
// and the measured centre offsets should read zero.
void paintRoundedRectangle(
    juce::Image& image, const double half_w, const double half_h, const double radius)
{
    constexpr int subsamples = 4;
    for (int y = 0; y < g_cell; ++y)
    {
        for (int x = 0; x < g_cell; ++x)
        {
            int inside = 0;
            for (int sy = 0; sy < subsamples; ++sy)
            {
                for (int sx = 0; sx < subsamples; ++sx)
                {
                    const double px = x + ((sx + 0.5) / subsamples) - 0.5 - 31.5;
                    const double py = y + ((sy + 0.5) / subsamples) - 0.5 - 31.5;
                    const double qx = std::abs(px) - (half_w - radius);
                    const double qy = std::abs(py) - (half_h - radius);
                    const bool in_body = std::abs(px) <= half_w && std::abs(py) <= half_h &&
                                         (qx <= 0.0 || qy <= 0.0);
                    const bool in_corner =
                        qx > 0.0 && qy > 0.0 && ((qx * qx) + (qy * qy)) <= radius * radius;
                    if (in_body || in_corner)
                    {
                        ++inside;
                    }
                }
            }
            const int total = subsamples * subsamples;
            setCoverage(image, x, y, (inside * 255 + (total / 2)) / total);
        }
    }
}

// The profile enforces that the tech head (cell 1) carries coverage byte-identical to the
// standard head's; the synthetic atlases satisfy that contract the same way the shipped one
// does, by stamping the standard cell into the tech cell.
void mirrorStandardToTech(juce::Image& image)
{
    for (int y = 0; y < g_cell; ++y)
    {
        for (int x = 0; x < g_cell; ++x)
        {
            image.setPixelAt(g_cell + x, y, image.getPixelAt(x, y));
        }
    }
}

[[nodiscard]] std::vector<std::byte> encodePng(const juce::Image& image)
{
    juce::MemoryOutputStream bytes;
    juce::PNGImageFormat{}.writeImageToStream(image, bytes);
    const auto* data = static_cast<const std::byte*>(bytes.getData());
    return {data, data + bytes.getDataSize()};
}

} // namespace

// Hard edges make every expected value exact: extents, centres, and the diamond's
// edge-law span.
TEST_CASE("Head art profile measures a hard-edged atlas exactly", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    // 41 x 21 solid rectangle, centred like the shipped art: half a texel off the quad centre.
    paintRectangle(atlas, 12, 52, 22, 42, 255);
    mirrorStandardToTech(atlas);
    paintDiamond(atlas, 15);

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(atlas);
    REQUIRE(profile.has_value());
    if (!profile.has_value())
    {
        return;
    }
    CHECK(profile->half_width_texels == Catch::Approx(20.5).margin(0.001));
    CHECK(profile->half_height_texels == Catch::Approx(10.5).margin(0.001));
    CHECK(profile->center_x_texels == Catch::Approx(0.5).margin(0.001));
    CHECK(profile->center_y_texels == Catch::Approx(-0.5).margin(0.001));
    // Hard corners: the fit's radius collapses toward its floor rather than inventing rounding.
    CHECK(profile->corner_texels < 1.0);
    // The hard-edged diamond's 50% contour sits half a texel outside the solid span on every
    // edge, so the edge-law span is the authored 15 plus that half texel.
    CHECK(profile->node_half_span_texels == Catch::Approx(15.5).margin(0.01));
    CHECK(profile->node_center_x_texels == Catch::Approx(0.5).margin(0.001));
    CHECK(profile->node_center_y_texels == Catch::Approx(-0.5).margin(0.001));
}

// The shipped art's fringe construction: the 50% crossing interpolates inside the fringe
// texel rather than snapping to it.
TEST_CASE("Head art profile interpolates a fringed edge", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    // The shipped art's construction: a solid rectangle wrapped in a one-texel fringe. The 50%
    // crossing then sits inside the fringe texel, between its value and the solid's.
    paintRectangle(atlas, 11, 53, 21, 43, 64);
    paintRectangle(atlas, 12, 52, 22, 42, 255);
    mirrorStandardToTech(atlas);
    paintDiamond(atlas, 15);

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(atlas);
    REQUIRE(profile.has_value());
    if (!profile.has_value())
    {
        return;
    }
    // Fringe 64/255 = 0.25098, solid 1.0: the crossing sits (0.5 - 0.25098) / (1 - 0.25098) =
    // 0.33246 past the fringe texel, symmetric on both sides.
    CHECK(profile->half_width_texels == Catch::Approx(20.66754).margin(0.001));
    CHECK(profile->half_height_texels == Catch::Approx(10.66754).margin(0.001));
    CHECK(profile->center_x_texels == Catch::Approx(0.5).margin(0.001));
    CHECK(profile->center_y_texels == Catch::Approx(-0.5).margin(0.001));
}

// A supersampled rounded rectangle with a known radius: the corner fit must recover it.
TEST_CASE("Head art profile recovers a known corner radius", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    paintRoundedRectangle(atlas, 20.0, 10.0, 3.0);
    mirrorStandardToTech(atlas);
    paintDiamond(atlas, 15);

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(atlas);
    REQUIRE(profile.has_value());
    if (!profile.has_value())
    {
        return;
    }
    CHECK(profile->half_width_texels == Catch::Approx(20.0).margin(0.06));
    CHECK(profile->half_height_texels == Catch::Approx(10.0).margin(0.06));
    CHECK(profile->center_x_texels == Catch::Approx(0.0).margin(0.02));
    CHECK(profile->center_y_texels == Catch::Approx(0.0).margin(0.02));
    CHECK(profile->corner_texels == Catch::Approx(3.0).margin(0.2));
}

// The byte entry point decodes and defers: an encoded atlas measures like its image.
TEST_CASE("Head art profile byte overload round-trips an encoded atlas", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    paintRectangle(atlas, 12, 52, 22, 42, 255);
    mirrorStandardToTech(atlas);
    paintDiamond(atlas, 15);
    const std::vector<std::byte> png = encodePng(atlas);

    const std::expected<HeadArtProfile, StructuralArtError> profile =
        measureHeadArtProfile(std::span<const std::byte>{png});
    REQUIRE(profile.has_value());
    if (!profile.has_value())
    {
        return;
    }
    CHECK(profile->half_width_texels == Catch::Approx(20.5).margin(0.001));
    CHECK(profile->node_half_span_texels == Catch::Approx(15.5).margin(0.01));
}

// The no-alpha half of the contract rejects at the byte boundary, the only place the
// file's own alpha state is known.
TEST_CASE("Head art profile rejects an alpha-bearing file", "[ui][highway]")
{
    // Same art painted into an ARGB image: the encoded PNG then carries a real alpha channel,
    // which the contract rejects at the byte boundary — decode-time premultiplication would
    // silently scale the structural channels.
    juce::Image atlas{
        juce::Image::ARGB, g_atlas_width, g_atlas_height, true, juce::SoftwareImageType{}
    };
    for (int y = 22; y <= 42; ++y)
    {
        for (int x = 12; x <= 52; ++x)
        {
            atlas.setPixelAt(x, y, juce::Colour::fromRGBA(0, 0, 255, 255));
        }
    }
    const std::vector<std::byte> png = encodePng(atlas);

    const std::expected<HeadArtProfile, StructuralArtError> profile =
        measureHeadArtProfile(std::span<const std::byte>{png});
    REQUIRE_FALSE(profile.has_value());
    if (profile.has_value())
    {
        return;
    }
    CHECK(profile.error() == StructuralArtError::AlphaBearingImage);
}

// Decode and measurement failures are typed, never silently zero-sized.
TEST_CASE("Head art profile rejects empty bytes and empty cells", "[ui][highway]")
{
    const std::expected<HeadArtProfile, StructuralArtError> empty_bytes =
        measureHeadArtProfile(std::span<const std::byte>{});
    REQUIRE_FALSE(empty_bytes.has_value());
    if (empty_bytes.has_value())
    {
        return;
    }
    CHECK(empty_bytes.error() == StructuralArtError::UndecodableImage);

    // A decodable atlas whose head cell is blank is unanalyzable, not silently zero-sized.
    const juce::Image blank = blankAtlas();
    const std::expected<HeadArtProfile, StructuralArtError> no_art = measureHeadArtProfile(blank);
    REQUIRE_FALSE(no_art.has_value());
    if (no_art.has_value())
    {
        return;
    }
    CHECK(no_art.error() == StructuralArtError::UnanalyzableArt);
}

// The tech cell must stay byte-identical to the standard: the renderer applies one
// measured silhouette to heads drawn from either cell.
TEST_CASE("Head art profile rejects a diverged tech head", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    paintRectangle(atlas, 12, 52, 22, 42, 255);
    mirrorStandardToTech(atlas);
    paintDiamond(atlas, 15);
    // One texel of coverage divergence between the standard and tech cells must fail the load:
    // the renderer applies cell 0's measured silhouette to heads drawn from cell 1.
    setCoverage(atlas, g_cell + 32, 32, 254);

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(atlas);
    REQUIRE_FALSE(profile.has_value());
    if (profile.has_value())
    {
        return;
    }
    CHECK(profile.error() == StructuralArtError::UnanalyzableArt);
}

// The node cell's measurement assumes the diamond edge law; a shape whose rows disagree about
// the span (a rectangle here) must fail as unanalyzable instead of getting a rhombus field
// fitted to a silhouette that is not there.
TEST_CASE("Head art profile rejects a non-diamond node cell", "[ui][highway]")
{
    juce::Image atlas = blankAtlas();
    paintRectangle(atlas, 12, 52, 22, 42, 255);
    mirrorStandardToTech(atlas);
    // A 31 x 21 solid rectangle in the node cell: every row spans the same half width, so the
    // edge-law estimate 15.5 + |dy| disagrees across rows by ~10 texels.
    paintRectangle(atlas, 17, 47, g_cell + 22, g_cell + 42, 255);

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(atlas);
    REQUIRE_FALSE(profile.has_value());
    if (profile.has_value())
    {
        return;
    }
    CHECK(profile.error() == StructuralArtError::UnanalyzableArt);
}

// The shipped asset has to satisfy its own contract. Everything above measures synthetic art,
// so this is the only check that covers the real file: a re-bake shipping an alpha channel, a
// diverged tech cell, or art the measurement cannot read fails here instead of at application
// startup, and the centring rule (every cell's art centred in its cell) is pinned as a measured
// property of the committed bytes.
TEST_CASE("The shipped head art satisfies its authoring contract", "[ui][highway]")
{
    const juce::File art = juce::File{ROCK_HERO_TEXTURES_DIR}.getChildFile(
        std::string{common::core::highwayTextureFileName(common::core::HighwayTexture::Notes)});
    REQUIRE(art.existsAsFile());
    juce::MemoryBlock bytes;
    REQUIRE(art.loadFileAsData(bytes));

    const std::expected<HeadArtProfile, StructuralArtError> profile = measureHeadArtProfile(
        std::span{static_cast<const std::byte*>(bytes.getData()), bytes.getSize()});
    REQUIRE(profile.has_value());
    if (!profile.has_value())
    {
        return;
    }

    // Centred art in both measured cells: the marks-final centring contract, read from the bytes.
    CHECK(std::abs(profile->center_x_texels) < 0.05);
    CHECK(std::abs(profile->center_y_texels) < 0.05);
    CHECK(std::abs(profile->node_center_x_texels) < 0.05);
    CHECK(std::abs(profile->node_center_y_texels) < 0.05);
    // Extents must be measurable art rather than degenerate slivers. The exact sizes are signed
    // in the atlas doc block and deliberately NOT pinned here: the load-time measurement exists
    // so the code adapts to a signed resize instead of breaking on it.
    CHECK(profile->half_width_texels > 4.0);
    CHECK(profile->half_height_texels > 4.0);
    CHECK(profile->node_half_span_texels > 4.0);
}

} // namespace rock_hero::common::ui
