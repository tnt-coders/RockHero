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

// Fallback head atlas: the same 4x4 grid shape as the reference atlas, so the
// runtime-rasterized cells (the standard head, the bend marker) sit at identical indices in
// both paths.
constexpr int g_head_cell_size = 128;
constexpr int g_head_grid_columns = 4;
constexpr int g_head_grid_rows = 4;

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

// Fallback paint of the bend marker cell: the shipped PNG carries the authored chevron in
// cell 11 (baked from the offline design harness), so this runs only when the asset is
// missing. Same geometry — squared butt-capped legs, sharp mitered apex, squat flattened
// proportions (2026-07-28 sixth pass on sight) — layered as a tinted body around a white core
// in the channel scheme, without the authored halo (the fallback degrades the art, never the
// game).
void paintBendSymbolCell(juce::Graphics& graphics)
{
    const int column = g_head_cell_bend % g_head_grid_columns;
    const int row = g_head_cell_bend / g_head_grid_columns;
    const auto size = static_cast<float>(g_head_cell_size);
    const juce::Point<float> origin{
        static_cast<float>(column) * size, static_cast<float>(row) * size
    };
    // Proportions matching the authored cell (2026-07-29: a tad taller and thicker than the
    // first bake), vertically centered in the cell so the marker quad's 180-degree
    // inverted-lane flip keeps the glyph on its offset station.
    const juce::Point<float> apex = origin + juce::Point<float>{0.5F * size, 0.445F * size};
    const juce::Point<float> left = origin + juce::Point<float>{0.3125F * size, 0.555F * size};
    const juce::Point<float> right = origin + juce::Point<float>{0.6875F * size, 0.555F * size};

    // Solves p0 + t*d0 == p1 + u*d1 for the miter points.
    const auto intersect = [](const juce::Point<float> p0,
                              const juce::Point<float>
                                  d0,
                              const juce::Point<float>
                                  p1,
                              const juce::Point<float>
                                  d1) {
        const float det = (d1.x * d0.y) - (d0.x * d1.y);
        const float t = (((p1.x - p0.x) * -d1.y) + (d1.x * (p1.y - p0.y))) / det;
        return p0 + (d0 * t);
    };
    const auto outline = [&](const float width) {
        const float h = width / 2.0F;
        const auto unit = [](juce::Point<float> v) { return v / v.getDistanceFromOrigin(); };
        const juce::Point<float> dir1 = unit(apex - left);
        const juce::Point<float> dir2 = unit(right - apex);
        // Outer normals point up (negative y) so the miter grows past the apex tip: dir1 runs
        // up-right and dir2 down-right, so their upward perpendiculars rotate opposite ways.
        const juce::Point<float> n1{dir1.y, -dir1.x};
        const juce::Point<float> n2{dir2.y, -dir2.x};
        const juce::Point<float> apex_outer =
            intersect(left + (n1 * h), dir1, apex + (n2 * h), dir2);
        const juce::Point<float> apex_inner =
            intersect(left - (n1 * h), dir1, apex - (n2 * h), dir2);
        juce::Path path;
        path.startNewSubPath(left + (n1 * h));
        path.lineTo(apex_outer);
        path.lineTo(right + (n2 * h));
        path.lineTo(right - (n2 * h));
        path.lineTo(apex_inner);
        path.lineTo(left - (n1 * h));
        path.closeSubPath();
        return path;
    };

    graphics.setColour(juce::Colour::fromRGBA(255, 40, 255, 255));
    graphics.fillPath(outline(0.085F * size));
    graphics.setColour(juce::Colour::fromRGBA(200, 235, 255, 255));
    graphics.fillPath(outline(0.036F * size));
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

    // Head atlas: the reference 4x4 channel-scheme asset (the authored bend chevron lives in
    // its formerly empty cell 11) uploads verbatim when it decodes; else the procedural
    // fallback in the same grid shape (same indices) — a missing asset degrades the art,
    // never the game.
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
        paintBendSymbolCell(graphics);

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
        atlases.glyphs = uploadAtlas(image);
    }

    return atlases;
}

} // namespace rock_hero::common::ui
