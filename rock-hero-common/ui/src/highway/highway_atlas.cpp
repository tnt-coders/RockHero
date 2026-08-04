#include "highway/highway_atlas.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <juce_graphics/juce_graphics.h>
#include <span>
#include <string>

namespace rock_hero::common::ui
{

namespace
{

// Glyph atlas: printable ASCII '!'..'~' (94 glyphs) in a 10x10 grid.
constexpr int g_glyph_texture_size = 512;
constexpr int g_glyph_cell_size = 51;
constexpr char g_first_glyph = '!';
constexpr char g_last_glyph = '~';

// Uploads a JUCE ARGB image as an immutable BGRA8 bgfx texture. JUCE's ARGB is premultiplied
// BGRA in memory on little-endian Windows, which BGRA8 maps to natively on D3D11 — no swizzle.
// createTexture2D with initial data expects tightly packed rows, and JUCE's lineStride may be
// wider, so rows are copied individually when they differ.
[[nodiscard]] UniqueBgfxHandle<bgfx::TextureHandle> uploadAtlas(const juce::Image& image)
{
    const int width = image.getWidth();
    const int height = image.getHeight();
    const auto row_bytes = static_cast<std::size_t>(width) * 4U;

    const juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::readOnly};
    const bgfx::Memory* memory =
        bgfx::alloc(static_cast<std::uint32_t>(row_bytes * static_cast<std::size_t>(height)));
    for (int row = 0; row < height; ++row)
    {
        std::memcpy(
            memory->data + (static_cast<std::size_t>(row) * row_bytes),
            bitmap.getLinePointer(row),
            row_bytes);
    }

    return UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::BGRA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        memory)};
}

} // namespace

int HighwayAtlasLayout::columns() const noexcept
{
    return cell_size > 0 ? texture_width / cell_size : 0;
}

int HighwayAtlasLayout::rows() const noexcept
{
    return cell_size > 0 ? texture_height / cell_size : 0;
}

int HighwayAtlasLayout::capacity() const noexcept
{
    return columns() * rows();
}

std::array<float, 4> HighwayAtlasLayout::cellRect(const int index) const noexcept
{
    const int cells_per_row = columns();
    if (cells_per_row <= 0 || rows() <= 0)
    {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }

    const int clamped = std::clamp(index, 0, capacity() - 1);
    const int column = clamped % cells_per_row;
    const int row = clamped / cells_per_row;

    const auto width = static_cast<float>(texture_width);
    const auto height = static_cast<float>(texture_height);
    const auto cell = static_cast<float>(cell_size);
    return {
        ((static_cast<float>(column) * cell) / width) + (0.5F / width),
        ((static_cast<float>(row) * cell) / height) + (0.5F / height),
        ((static_cast<float>(column + 1) * cell) / width) - (0.5F / width),
        ((static_cast<float>(row + 1) * cell) / height) - (0.5F / height),
    };
}

std::optional<int> highwayGlyphCellIndex(const char character) noexcept
{
    if (character < g_first_glyph || character > g_last_glyph)
    {
        return std::nullopt;
    }
    return character - g_first_glyph;
}

UploadedTexture uploadPngTexture(const std::span<const std::byte> png_bytes)
{
    if (png_bytes.empty())
    {
        return {};
    }
    juce::MemoryInputStream stream{png_bytes.data(), png_bytes.size(), false};
    const juce::Image decoded = juce::PNGImageFormat{}.decodeImage(stream);
    if (decoded.isNull())
    {
        return {};
    }
    // Normalize to ARGB so RGB-only PNGs (the reference note atlas) gain the opaque alpha the
    // BGRA8 upload expects.
    return UploadedTexture{
        .handle = uploadAtlas(decoded.convertedToFormat(juce::Image::ARGB)),
        .width = decoded.getWidth(),
        .height = decoded.getHeight(),
    };
}

HighwayAtlases makeHighwayAtlases(const std::span<const std::byte> note_atlas_png)
{
    HighwayAtlases atlases;
    atlases.glyph_layout = HighwayAtlasLayout{
        .texture_width = g_glyph_texture_size,
        .texture_height = g_glyph_texture_size,
        .cell_size = g_glyph_cell_size,
    };

    // Head atlas: the reference 4x5 channel-scheme asset (one art set for every consumer)
    // uploads verbatim when it decodes; empty or undecodable bytes leave the handle invalid
    // and the layout empty, which the renderer rejects at create — required product content,
    // never silently substituted.
    if (!note_atlas_png.empty())
    {
        juce::MemoryInputStream stream{note_atlas_png.data(), note_atlas_png.size(), false};
        const juce::Image decoded = juce::PNGImageFormat{}.decodeImage(stream);
        if (!decoded.isNull() && decoded.getWidth() >= 4)
        {
            // Normalize to ARGB so an RGB-only PNG gains the opaque alpha the BGRA8 upload
            // expects.
            atlases.heads = uploadAtlas(decoded.convertedToFormat(juce::Image::ARGB));
            atlases.head_layout = HighwayAtlasLayout{
                .texture_width = decoded.getWidth(),
                .texture_height = decoded.getHeight(),
                .cell_size = decoded.getWidth() / 4,
            };
        }
    }

    // Glyph atlas: transparent base, white glyphs — the shape lives in alpha.
    {
        const juce::Image image{
            juce::Image::ARGB,
            g_glyph_texture_size,
            g_glyph_texture_size,
            true,
            juce::SoftwareImageType{}
        };
        juce::Graphics graphics{image};
        graphics.setColour(juce::Colours::white);
        graphics.setFont(
            juce::Font{juce::FontOptions{static_cast<float>(g_glyph_cell_size) * 0.82F}.withStyle(
                "Bold")});

        const int columns = g_glyph_texture_size / g_glyph_cell_size;
        for (char character = g_first_glyph; character <= g_last_glyph; ++character)
        {
            const int index = character - g_first_glyph;
            const int column = index % columns;
            const int row = index / columns;
            graphics.drawText(
                juce::String::charToString(static_cast<juce::juce_wchar>(character)),
                column * g_glyph_cell_size,
                row * g_glyph_cell_size,
                g_glyph_cell_size,
                g_glyph_cell_size,
                juce::Justification::centred);
        }
        atlases.glyphs = uploadAtlas(image);
    }

    return atlases;
}

} // namespace rock_hero::common::ui
