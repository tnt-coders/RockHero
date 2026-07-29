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

// Fallback head atlas: the same 4x5 grid shape as the composed reference atlas, so the
// runtime-rasterized cells (the standard head, the bend marker) sit at identical indices in
// both paths.
constexpr int g_head_cell_size = 128;
constexpr int g_head_grid_columns = 4;
constexpr int g_head_grid_rows = 5;

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

// Paints the standard note head into its cell using the channel scheme: R carries the tint
// mask, G the white highlight, B the alpha mask; A stays opaque everywhere so premultiplication
// cannot disturb the channels.
void paintStandardHead(juce::Graphics& graphics, const juce::Rectangle<float> cell)
{
    const juce::Rectangle<float> head = cell.reduced(cell.getWidth() * 0.08F);

    // Outer ring: dimmer tint, full alpha coverage.
    graphics.setColour(juce::Colour::fromRGBA(150, 0, 255, 255));
    graphics.fillEllipse(head);

    // Inner fill: strong tint with a slight white lift.
    graphics.setColour(juce::Colour::fromRGBA(235, 30, 255, 255));
    graphics.fillEllipse(head.reduced(head.getWidth() * 0.09F));

    // Specular highlight toward the upper left: mostly white channel.
    const float highlight_size = head.getWidth() * 0.28F;
    graphics.setColour(juce::Colour::fromRGBA(160, 130, 255, 255));
    graphics.fillEllipse(
        head.getX() + (head.getWidth() * 0.18F),
        head.getY() + (head.getHeight() * 0.16F),
        highlight_size,
        highlight_size);
}

// Paints the bend marker into its fifth-row cell in the channel scheme: a curved arrow — an
// arc swooping right then up into an arrowhead, the source-game notation's "bend away from
// here" gesture — reading mostly white (high G) with a slight string tint (low R), shape
// carried by B as the alpha mask (the source-game composition and the fallback share this, so
// the cell index agrees in both paths).
void paintBendSymbolRow(juce::Graphics& graphics)
{
    const int column = g_head_cell_bend % g_head_grid_columns;
    const int row = g_head_cell_bend / g_head_grid_columns;
    const juce::Rectangle<float> cell{
        static_cast<float>(column * g_head_cell_size),
        static_cast<float>(row * g_head_cell_size),
        static_cast<float>(g_head_cell_size),
        static_cast<float>(g_head_cell_size),
    };
    const juce::Rectangle<float> box = cell.reduced(cell.getWidth() * 0.16F);

    graphics.setColour(juce::Colour::fromRGBA(70, 225, 255, 255));

    // The swoop: flat exit at the bottom-left curving up toward the arrowhead.
    juce::Path swoop;
    swoop.startNewSubPath(box.getX(), box.getBottom() - (box.getHeight() * 0.10F));
    swoop.quadraticTo(
        box.getRight() - (box.getWidth() * 0.22F),
        box.getBottom() - (box.getHeight() * 0.16F),
        box.getRight() - (box.getWidth() * 0.28F),
        box.getY() + (box.getHeight() * 0.34F));
    graphics.strokePath(
        swoop,
        juce::PathStrokeType{
            box.getWidth() * 0.16F,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded,
        });

    // The arrowhead pointing up, at the swoop's tip.
    juce::Path arrowhead;
    arrowhead.addTriangle(
        box.getRight() - (box.getWidth() * 0.52F),
        box.getY() + (box.getHeight() * 0.34F),
        box.getRight() - (box.getWidth() * 0.04F),
        box.getY() + (box.getHeight() * 0.34F),
        box.getRight() - (box.getWidth() * 0.28F),
        box.getY());
    graphics.fillPath(arrowhead);
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

    // Head atlas: the reference 4x4 channel-scheme asset when it decodes, composed into a
    // five-row canvas whose appended row carries the runtime-rasterized bend-chevron cells;
    // else the procedural fallback in the same grid shape (same indices) — a missing asset
    // degrades the art, never the game.
    if (!note_atlas_png.empty())
    {
        juce::MemoryInputStream stream{note_atlas_png.data(), note_atlas_png.size(), false};
        const juce::Image decoded = juce::PNGImageFormat{}.decodeImage(stream);
        if (!decoded.isNull() && decoded.getWidth() >= 4)
        {
            const int cell = decoded.getWidth() / 4;
            const juce::Image composed{
                juce::Image::ARGB,
                decoded.getWidth(),
                cell * g_head_grid_rows,
                true,
                juce::SoftwareImageType{}
            };
            {
                juce::Graphics graphics{composed};
                // Opaque black base (A=0xFF, channels zero) so untouched texels contribute
                // nothing (B = 0 masks them out) while staying premultiplication-proof; the
                // reference rows draw over it and the chevron row scales with the asset's
                // cell size.
                graphics.fillAll(juce::Colour::fromRGBA(0, 0, 0, 255));
                graphics.drawImageAt(decoded.convertedToFormat(juce::Image::ARGB), 0, 0);
                const float scale = static_cast<float>(cell) / static_cast<float>(g_head_cell_size);
                graphics.addTransform(juce::AffineTransform::scale(scale));
                paintBendSymbolRow(graphics);
            }
            atlases.heads = uploadAtlas(composed);
            atlases.head_layout = HighwayAtlasLayout{
                .texture_width = decoded.getWidth(),
                .texture_height = cell * g_head_grid_rows,
                .cell_size = cell,
            };
            atlases.reference_cells = atlases.heads.isValid();
        }
    }
    if (!atlases.reference_cells)
    {
        atlases.head_layout = HighwayAtlasLayout{
            .texture_width = g_head_cell_size * g_head_grid_columns,
            .texture_height = g_head_cell_size * g_head_grid_rows,
            .cell_size = g_head_cell_size,
        };

        // Opaque black base (A=0xFF, channels zero) so untouched texels contribute nothing
        // (B = 0 masks them out) while staying premultiplication-proof.
        const juce::Image image{
            juce::Image::ARGB,
            atlases.head_layout.texture_width,
            atlases.head_layout.texture_height,
            true,
            juce::SoftwareImageType{}
        };
        juce::Graphics graphics{image};
        graphics.fillAll(juce::Colour::fromRGBA(0, 0, 0, 255));

        const auto cell_rect = juce::Rectangle<float>{
            0.0F, 0.0F, static_cast<float>(g_head_cell_size), static_cast<float>(g_head_cell_size)
        };
        paintStandardHead(graphics, cell_rect);
        paintBendSymbolRow(graphics);

        atlases.heads = uploadAtlas(image);
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
        // The "½" figure past the ASCII cells: bend amounts display quarter-tone curls as a
        // real half figure (U+00BD), addressed by g_glyph_cell_half rather than a character.
        graphics.drawText(
            juce::String::charToString(static_cast<juce::juce_wchar>(0x00BD)),
            (g_glyph_cell_half % columns) * g_glyph_cell_size,
            (g_glyph_cell_half / columns) * g_glyph_cell_size,
            g_glyph_cell_size,
            g_glyph_cell_size,
            juce::Justification::centred);

        atlases.glyphs = uploadAtlas(image);
    }

    return atlases;
}

} // namespace rock_hero::common::ui
