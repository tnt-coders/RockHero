#include "highway/highway_texture_contract.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <expected>
#include <juce_graphics/juce_graphics.h>
#include <rock_hero/common/core/highway/highway_resources.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/ui/testing/highway_theme_fixtures.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

using common::core::HighwayTexture;

// The default theme's art, read from the source tree: a set that satisfies the contract, which
// each failure case below breaks in one asset.
[[nodiscard]] HighwayTextureSet shippedTextures()
{
    const juce::File directory = common::core::juceFileFromPath(testing::defaultThemeDirectory());
    HighwayTextureSet textures;
    for (const HighwayTexture texture : common::core::g_highway_textures)
    {
        juce::MemoryBlock block;
        REQUIRE(directory.getChildFile(std::string{common::core::highwayTextureFileName(texture)})
                    .loadFileAsData(block));
        const auto* const bytes = static_cast<const std::byte*>(block.getData());
        textures.at(common::core::indexOf(texture)).assign(bytes, bytes + block.getSize());
    }
    return textures;
}

// A blank image of the format and size, encoded as PNG: RGB encodes with no alpha channel, ARGB
// with one.
[[nodiscard]] std::vector<std::byte> blankPng(
    const juce::Image::PixelFormat format, const int width, const int height)
{
    const juce::Image image{format, width, height, true, juce::SoftwareImageType{}};
    juce::MemoryOutputStream encoded;
    REQUIRE(juce::PNGImageFormat{}.writeImageToStream(image, encoded));
    const auto* const bytes = static_cast<const std::byte*>(encoded.getData());
    return {bytes, bytes + encoded.getDataSize()};
}

// The shipped set with one asset replaced.
[[nodiscard]] HighwayTextureSet shippedTexturesWith(
    const HighwayTexture asset, std::vector<std::byte> bytes)
{
    HighwayTextureSet textures = shippedTextures();
    textures.at(common::core::indexOf(asset)) = std::move(bytes);
    return textures;
}

// Asserts the set breaks the contract in the given asset, for the given reason, with a message
// that names the asset's file.
void checkBreaks(
    const HighwayTextureSet& textures, const HighwayTexture asset,
    const HighwayTextureContractErrorCode code)
{
    const std::expected<HighwayTextureMeasurements, HighwayTextureContractError> measured =
        measureHighwayTextures(textures);
    REQUIRE_FALSE(measured.has_value());
    if (measured.has_value())
    {
        return;
    }
    CHECK(measured.error().asset == asset);
    CHECK(measured.error().code == code);
    CHECK_THAT(
        measured.error().message,
        Catch::Matchers::StartsWith(std::string{common::core::highwayTextureFileName(asset)}));
}

} // namespace

// The default theme's art passes, and the measurements the renderer draws with come back from the
// same entry point that validated them.
TEST_CASE("The default theme's art passes the texture contract", "[ui][highway]")
{
    const std::expected<HighwayTextureMeasurements, HighwayTextureContractError> measured =
        measureHighwayTextures(shippedTextures());
    REQUIRE(measured.has_value());
    if (!measured.has_value())
    {
        return;
    }
    CHECK(measured->head_art.half_width_texels > 4.0);
    CHECK(measured->head_art.node_half_span_texels > 4.0);
    CHECK(measured->box_mutes.palm.stroke_half_fraction > 0.0);
    CHECK(measured->box_mutes.full.stroke_half_fraction > 0.0);
}

// Empty or garbage bytes are an undecodable asset, whichever asset carries them.
TEST_CASE("The texture contract refuses an undecodable asset", "[ui][highway]")
{
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, {}),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::UndecodableImage);
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Inlays, {std::byte{1}, std::byte{2}}),
        HighwayTexture::Inlays,
        HighwayTextureContractErrorCode::UndecodableImage);
}

// Structural art carries its coverage in blue; an alpha channel the decoder would premultiply into
// every channel is the likeliest mistake in a user's re-saved art, so it is named as such.
TEST_CASE("The texture contract refuses alpha-bearing structural art", "[ui][highway]")
{
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, blankPng(juce::Image::ARGB, 256, 320)),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::AlphaBearingImage);
    checkBreaks(
        shippedTexturesWith(HighwayTexture::ChordMarks, blankPng(juce::Image::ARGB, 128, 128)),
        HighwayTexture::ChordMarks,
        HighwayTextureContractErrorCode::AlphaBearingImage);
}

// The head atlas is four columns of square cells with a cell for every index the atlas names;
// the inlay is one square cell.
TEST_CASE("The texture contract refuses art off its grid", "[ui][highway]")
{
    // Not a whole number of square cells tall.
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, blankPng(juce::Image::RGB, 256, 300)),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::WrongGrid);
    // Square cells, but too few of them.
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, blankPng(juce::Image::RGB, 256, 256)),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::WrongGrid);
    // A width that four columns do not divide.
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, blankPng(juce::Image::RGB, 250, 320)),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::WrongGrid);
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Inlays, blankPng(juce::Image::ARGB, 256, 128)),
        HighwayTexture::Inlays,
        HighwayTextureContractErrorCode::WrongGrid);
}

// Art on the right grid that carries none of the shapes its measurement reads is refused rather
// than measured as zero.
TEST_CASE("The texture contract refuses art it cannot measure", "[ui][highway]")
{
    checkBreaks(
        shippedTexturesWith(HighwayTexture::Notes, blankPng(juce::Image::RGB, 256, 320)),
        HighwayTexture::Notes,
        HighwayTextureContractErrorCode::UnmeasurableArt);
    checkBreaks(
        shippedTexturesWith(HighwayTexture::ChordMarks, blankPng(juce::Image::RGB, 128, 128)),
        HighwayTexture::ChordMarks,
        HighwayTextureContractErrorCode::UnmeasurableArt);
}

} // namespace rock_hero::common::ui
