#include "highway/highway_texture_contract.h"

#include "highway/highway_atlas.h"
#include "highway/structural_art.h"

#include <format>
#include <juce_graphics/juce_graphics.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rock_hero::common::ui
{

namespace
{

using common::core::HighwayTexture;
using common::core::highwayTextureFileName;

// What each structural asset's load-time measurement reads: the expectation an unanalyzable file
// misses.
constexpr std::string_view g_head_art_expectation =
    "art whose standard and tech heads share one coverage silhouette and whose node head is a "
    "diamond";
constexpr std::string_view g_chord_marks_expectation =
    "two equal stacked cells, palm mute above full mute, each one X glyph in the structural "
    "channel scheme";

// Builds the error for one art file, prefixing the file's name so every diagnostic names it.
[[nodiscard]] HighwayTextureContractError contractError(
    const HighwayTextureContractErrorCode code, const std::string_view file_name,
    const std::string_view expectation)
{
    return HighwayTextureContractError{
        .code = code,
        .message = std::format("{} must be {}", file_name, expectation),
    };
}

// Translates a structural-art failure into the contract's terms. Switches without a default so a
// new StructuralArtError has to be translated here.
[[nodiscard]] HighwayTextureContractError structuralArtError(
    const StructuralArtError error, const std::string_view file_name,
    const std::string_view unmeasurable)
{
    switch (error)
    {
        case StructuralArtError::UndecodableImage:
        {
            return contractError(
                HighwayTextureContractErrorCode::UndecodableImage, file_name, "a decodable PNG");
        }
        case StructuralArtError::AlphaBearingImage:
        {
            return contractError(
                HighwayTextureContractErrorCode::AlphaBearingImage,
                file_name,
                "a PNG with no alpha channel: coverage lives in its blue channel and opacity is "
                "applied per draw");
        }
        case StructuralArtError::UnanalyzableArt:
        {
            return contractError(
                HighwayTextureContractErrorCode::UnmeasurableArt, file_name, unmeasurable);
        }
    }
    std::unreachable();
}

// Whether a decoded sheet is exactly the given number of rows of the atlas's columns of
// fixed-size cells: both sheets must be, for their rows to stack into the one atlas.
[[nodiscard]] bool isSheetOfRows(const juce::Image& sheet, const int rows)
{
    return sheet.getWidth() == g_head_atlas_columns * g_atlas_cell_texels &&
           sheet.getHeight() == rows * g_atlas_cell_texels;
}

// States the exact grid isSheetOfRows requires, for the refusal that names it.
[[nodiscard]] std::string sheetGridText(const int rows)
{
    return std::format(
        "{}x{} ({} columns by {} rows of {}-texel cells)",
        g_head_atlas_columns * g_atlas_cell_texels,
        rows * g_atlas_cell_texels,
        g_head_atlas_columns,
        rows,
        g_atlas_cell_texels);
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
    const std::string_view notes_file = highwayTextureFileName(HighwayTexture::Notes);
    std::expected<juce::Image, StructuralArtError> notes =
        decodeStructuralArtPng(bytesOf(textures, HighwayTexture::Notes));
    if (!notes.has_value())
    {
        return std::unexpected{structuralArtError(
            notes.error(), notes_file, g_head_art_expectation)};
    }
    // Exactly the head rows: a sheet that still carries the technique-mark rows below them is the
    // likeliest stale art, and is named as such rather than measured.
    if (!isSheetOfRows(*notes, g_head_sheet_rows))
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::WrongGrid,
            notes_file,
            std::format(
                "the heads-only sheet, {}; technique marks live on the fixed sheet. It is {}x{}",
                sheetGridText(g_head_sheet_rows),
                notes->getWidth(),
                notes->getHeight()))};
    }
    const std::expected<HeadArtProfile, StructuralArtError> head_art =
        measureHeadArtProfile(*notes);
    if (!head_art.has_value())
    {
        return std::unexpected{structuralArtError(
            head_art.error(), notes_file, g_head_art_expectation)};
    }

    // The inlay is ordinary tinted art with real alpha, so it decodes without the structural rule.
    const std::string_view inlays_file = highwayTextureFileName(HighwayTexture::Inlays);
    const std::span<const std::byte> inlay_bytes = bytesOf(textures, HighwayTexture::Inlays);
    juce::MemoryInputStream inlay_stream{inlay_bytes.data(), inlay_bytes.size(), false};
    const juce::Image inlays =
        inlay_bytes.empty() ? juce::Image{} : juce::PNGImageFormat{}.decodeImage(inlay_stream);
    if (inlays.isNull())
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::UndecodableImage, inlays_file, "a decodable PNG")};
    }
    // The renderer maps the dot onto a world square, so the art must be square too.
    if (inlays.getWidth() != inlays.getHeight())
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::WrongGrid,
            inlays_file,
            std::format("one square cell; it is {}x{}", inlays.getWidth(), inlays.getHeight()))};
    }

    // The chord marks' grid, two equal stacked cells, is part of what their measurement reads.
    const std::expected<BoxMuteProfiles, StructuralArtError> box_mutes =
        measureBoxMuteProfiles(bytesOf(textures, HighwayTexture::ChordMarks));
    if (!box_mutes.has_value())
    {
        return std::unexpected{structuralArtError(
            box_mutes.error(),
            highwayTextureFileName(HighwayTexture::ChordMarks),
            g_chord_marks_expectation)};
    }

    return HighwayTextureMeasurements{.head_art = *head_art, .box_mutes = *box_mutes};
}

// Decodes the mark sheet through the structural seam (refusing alpha), then pins its grid to the
// mark rows the composed atlas expects below the heads.
std::expected<void, HighwayTextureContractError> checkHighwayMarkSheet(
    const std::span<const std::byte> marks_png)
{
    constexpr std::string_view marks_file = common::core::g_highway_mark_sheet_file_name;
    const std::string expectation =
        std::format("the technique-mark sheet, {}", sheetGridText(g_mark_sheet_rows));
    const std::expected<juce::Image, StructuralArtError> marks = decodeStructuralArtPng(marks_png);
    if (!marks.has_value())
    {
        return std::unexpected{structuralArtError(marks.error(), marks_file, expectation)};
    }
    if (!isSheetOfRows(*marks, g_mark_sheet_rows))
    {
        return std::unexpected{contractError(
            HighwayTextureContractErrorCode::WrongGrid,
            marks_file,
            std::format("{}; it is {}x{}", expectation, marks->getWidth(), marks->getHeight()))};
    }
    return {};
}

} // namespace rock_hero::common::ui
