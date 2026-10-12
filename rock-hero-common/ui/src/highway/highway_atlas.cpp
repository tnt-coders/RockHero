#include "highway/highway_atlas.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
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

constexpr HighwayAtlasLayout g_glyph_layout{
    .texture_width = g_glyph_texture_size,
    .texture_height = g_glyph_texture_size,
    .cell_size = g_glyph_cell_size,
};

// cellRect() clamps an out-of-range index, so a grid the cell size left too small for the range
// would draw the last glyph for every character past it instead of failing.
static_assert(g_glyph_layout.capacity() >= g_last_glyph - g_first_glyph + 1);

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

// UV rect of one cell, inset half a texel per side so neighbouring cells cannot bleed under
// minification; an unconfigured layout yields a zero rect.
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

// Maps a drawable character to its glyph-atlas cell; nullopt outside the baked range.
std::optional<int> highwayGlyphCellIndex(const char character) noexcept
{
    if (character < g_first_glyph || character > g_last_glyph)
    {
        return std::nullopt;
    }
    return character - g_first_glyph;
}

// Decodes PNG bytes and uploads them as an immutable bgfx texture with the decoded size;
// failure leaves the returned handle invalid so create()'s asset validation reports it.
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

// Stacks the head sheet's rows above the mark sheet's, one memcpy per row through BitmapData so
// no Graphics resampling or blending can touch a texel.
juce::Image composeHeadAtlas(const juce::Image& heads, const juce::Image& marks)
{
    if (heads.isNull() || marks.isNull() || heads.getWidth() != marks.getWidth())
    {
        return {};
    }
    // Normalize to ARGB so an RGB-only PNG gains the opaque alpha the BGRA8 upload expects, and so
    // every row copied below has the same four-byte pixel layout whatever each file decoded to.
    const juce::Image top = heads.convertedToFormat(juce::Image::ARGB);
    const juce::Image bottom = marks.convertedToFormat(juce::Image::ARGB);
    const int width = top.getWidth();
    const juce::Image composed{
        juce::Image::ARGB,
        width,
        top.getHeight() + bottom.getHeight(),
        false,
        juce::SoftwareImageType{}
    };

    const auto row_bytes = static_cast<std::size_t>(width) * 4U;
    const juce::Image::BitmapData target{composed, juce::Image::BitmapData::writeOnly};
    int target_row = 0;
    for (const juce::Image* const source : {&top, &bottom})
    {
        const juce::Image::BitmapData rows{*source, juce::Image::BitmapData::readOnly};
        for (int row = 0; row < source->getHeight(); ++row)
        {
            std::memcpy(target.getLinePointer(target_row), rows.getLinePointer(row), row_bytes);
            ++target_row;
        }
    }
    return composed;
}

// Builds both atlases: the glyph sheet is rasterized at runtime from the bundled font, while
// the note-head atlas uploads the composed head and mark sheets and derives its cell layout from
// the composed width.
HighwayAtlases makeHighwayAtlases(
    const std::span<const std::byte> heads_png, const std::span<const std::byte> marks_png)
{
    HighwayAtlases atlases;
    atlases.glyph_layout = g_glyph_layout;

    // Head atlas: the theme's heads over the fixed marks (one art set for every consumer)
    // upload verbatim when both decode; empty or undecodable bytes leave the handle invalid and
    // the layout empty, which the renderer rejects at create — required product content, never
    // silently substituted.
    const auto decode = [](const std::span<const std::byte> png) {
        if (png.empty())
        {
            return juce::Image{};
        }
        juce::MemoryInputStream stream{png.data(), png.size(), false};
        return juce::PNGImageFormat{}.decodeImage(stream);
    };
    const juce::Image composed = composeHeadAtlas(decode(heads_png), decode(marks_png));
    if (!composed.isNull() && composed.getWidth() >= g_head_atlas_columns)
    {
        atlases.heads = uploadAtlas(composed);
        atlases.head_layout = HighwayAtlasLayout{
            .texture_width = composed.getWidth(),
            .texture_height = composed.getHeight(),
            .cell_size = composed.getWidth() / g_head_atlas_columns,
        };
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

        const int columns = g_glyph_layout.columns();
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
