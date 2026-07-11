#include "highway/highway_atlas.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <juce_graphics/juce_graphics.h>
#include <string>

namespace rock_hero::game::ui
{

namespace
{

// Head atlas: a handful of large cells (one used today, room for technique variants).
constexpr int g_head_texture_size = 256;
constexpr int g_head_cell_size = 128;

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

} // namespace

int HighwayAtlasLayout::columns() const noexcept
{
    return cell_size > 0 ? texture_size / cell_size : 0;
}

int HighwayAtlasLayout::capacity() const noexcept
{
    return columns() * columns();
}

std::array<float, 4> HighwayAtlasLayout::cellRect(const int index) const noexcept
{
    const int cells_per_row = columns();
    if (cells_per_row <= 0)
    {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }

    const int clamped = std::clamp(index, 0, capacity() - 1);
    const int column = clamped % cells_per_row;
    const int row = clamped / cells_per_row;

    const auto size = static_cast<float>(texture_size);
    const float half_texel = 0.5F / size;
    const auto cell = static_cast<float>(cell_size);
    return {
        ((static_cast<float>(column) * cell) / size) + half_texel,
        ((static_cast<float>(row) * cell) / size) + half_texel,
        ((static_cast<float>(column + 1) * cell) / size) - half_texel,
        ((static_cast<float>(row + 1) * cell) / size) - half_texel,
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

HighwayAtlases makeHighwayAtlases()
{
    HighwayAtlases atlases;
    atlases.head_layout =
        HighwayAtlasLayout{.texture_size = g_head_texture_size, .cell_size = g_head_cell_size};
    atlases.glyph_layout =
        HighwayAtlasLayout{.texture_size = g_glyph_texture_size, .cell_size = g_glyph_cell_size};

    // Head atlas: opaque black base (A=0xFF, channels zero) so untouched texels contribute
    // nothing (B = 0 masks them out) while staying premultiplication-proof.
    {
        juce::Image image{
            juce::Image::ARGB,
            g_head_texture_size,
            g_head_texture_size,
            true,
            juce::SoftwareImageType{}
        };
        juce::Graphics graphics{image};
        graphics.fillAll(juce::Colour::fromRGBA(0, 0, 0, 255));

        const auto cell_rect = juce::Rectangle<float>{
            0.0F, 0.0F, static_cast<float>(g_head_cell_size), static_cast<float>(g_head_cell_size)
        };
        paintStandardHead(graphics, cell_rect);

        atlases.heads = uploadAtlas(image);
    }

    // Glyph atlas: transparent base, white glyphs — the shape lives in alpha.
    {
        juce::Image image{
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

} // namespace rock_hero::game::ui
