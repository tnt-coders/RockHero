#include "highway/highway_texture_contract.h"

#include "highway/highway_atlas.h"
#include "highway/structural_art.h"

#include <format>
#include <juce_graphics/juce_graphics.h>
#include <span>
#include <string_view>
#include <utility>

namespace rock_hero::common::ui
{

namespace
{

using common::core::HighwayTexture;

// What each structural asset's load-time measurement reads: the expectation an unanalyzable file
// misses.
constexpr std::string_view g_head_art_expectation =
    "art whose standard and tech heads share one coverage silhouette and whose node head is a "
    "diamond";
constexpr std::string_view g_chord_marks_expectation =
    "two equal stacked cells, palm mute above full mute, each one X glyph in the structural "
    "channel scheme";

// Builds the error for one asset, prefixing the asset's file name so every diagnostic names it.
[[nodiscard]] HighwayTextureContractError contractError(
    const HighwayTextureContractErrorCode code, const HighwayTexture asset,
    const std::string_view expectation)
{
    return HighwayTextureContractError{
        .code = code,
        .asset = asset,
        .message =
            std::format("{} must be {}", common::core::highwayTextureFileName(asset), expectation),
    };
}

// Translates a structural-art failure into the contract's terms. Switches without a default so a
// new StructuralArtError has to be translated here.
[[nodiscard]] HighwayTextureContractError structuralArtError(
    const StructuralArtError error, const HighwayTexture asset, const std::string_view unmeasurable)
{
    switch (error)
    {
        case StructuralArtError::UndecodableImage:
        {
            return contractError(
                HighwayTextureContractErrorCode::UndecodableImage, asset, "a decodable PNG");
        }
        case StructuralArtError::AlphaBearingImage:
        {
            return contractError(
                HighwayTextureContractErrorCode::AlphaBearingImage,
                asset,
                "a PNG with no alpha channel: coverage lives in its blue channel and opacity is "
                "applied per draw");
        }
        case StructuralArtError::UnanalyzableArt:
        {
            return contractError(
                HighwayTextureContractErrorCode::UnmeasurableArt, asset, unmeasurable);
        }
    }
    std::unreachable();
}

[[nodiscard]] std::span<const std::byte> bytesOf(
    const HighwayTextureSet& textures, const HighwayTexture asset)
{
    return textures.at(common::core::indexOf(asset));
}

} // namespace

// Checks each asset in table order: decode (refusing alpha on structural art), grid, then the
// asset's own measurement, so the first failure reported is the most basic one.
std::expected<HighwayTextureMeasurements, HighwayTextureContractError> measureHighwayTextures(
    const HighwayTextureSet& textures)
{
    std::expected<juce::Image, StructuralArtError> notes =
        decodeStructuralArtPng(bytesOf(textures, HighwayTexture::Notes));
    if (!notes.has_value())
    {
        return std::unexpected{structuralArtError(
            notes.error(), HighwayTexture::Notes, g_head_art_expectation)};
    }
    // Four columns of square cells, as the atlas layout derives its cells from the width.
    const int cell_size = notes->getWidth() / g_head_atlas_columns;
    if (cell_size <= 0 || notes->getWidth() % g_head_atlas_columns != 0 ||
        notes->getHeight() % cell_size != 0 ||
        (notes->getHeight() / cell_size) * g_head_atlas_columns < g_head_cell_count)
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::WrongGrid,
            HighwayTexture::Notes,
            std::format(
                "{} columns of square cells holding at least {} cells; it is {}x{}",
                g_head_atlas_columns,
                g_head_cell_count,
                notes->getWidth(),
                notes->getHeight()))};
    }
    const std::expected<HeadArtProfile, StructuralArtError> head_art =
        measureHeadArtProfile(*notes);
    if (!head_art.has_value())
    {
        return std::unexpected{structuralArtError(
            head_art.error(), HighwayTexture::Notes, g_head_art_expectation)};
    }

    // The inlay is ordinary tinted art with real alpha, so it decodes without the structural rule.
    const std::span<const std::byte> inlay_bytes = bytesOf(textures, HighwayTexture::Inlays);
    juce::MemoryInputStream inlay_stream{inlay_bytes.data(), inlay_bytes.size(), false};
    const juce::Image inlays =
        inlay_bytes.empty() ? juce::Image{} : juce::PNGImageFormat{}.decodeImage(inlay_stream);
    if (inlays.isNull())
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::UndecodableImage,
            HighwayTexture::Inlays,
            "a decodable PNG")};
    }
    // The renderer maps the dot onto a world square, so the art must be square too.
    if (inlays.getWidth() != inlays.getHeight())
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::WrongGrid,
            HighwayTexture::Inlays,
            std::format("one square cell; it is {}x{}", inlays.getWidth(), inlays.getHeight()))};
    }

    // The chord marks' grid, two equal stacked cells, is part of what their measurement reads.
    const std::expected<BoxMuteProfiles, StructuralArtError> box_mutes =
        measureBoxMuteProfiles(bytesOf(textures, HighwayTexture::ChordMarks));
    if (!box_mutes.has_value())
    {
        return std::unexpected{structuralArtError(
            box_mutes.error(), HighwayTexture::ChordMarks, g_chord_marks_expectation)};
    }

    return HighwayTextureMeasurements{.head_art = *head_art, .box_mutes = *box_mutes};
}

} // namespace rock_hero::common::ui
