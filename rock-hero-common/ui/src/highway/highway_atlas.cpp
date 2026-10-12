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

// Glyph atlas: printable ASCII '!'..'~' (94 glyphs) in a 10x10 grid. The cell is the largest
// under 64 whose padded texture edge halves exactly through g_atlas_exact_mip_levels.
constexpr int g_glyph_columns = 10;
constexpr int g_glyph_cell_size = 56;
constexpr int g_glyph_texture_size =
    g_glyph_columns * (g_glyph_cell_size + (2 * g_atlas_gutter_texels));
constexpr char g_first_glyph = '!';
constexpr char g_last_glyph = '~';

constexpr HighwayAtlasLayout g_glyph_layout{
    .texture_width = g_glyph_texture_size,
    .texture_height = g_glyph_texture_size,
    .cell_size = g_glyph_cell_size,
    .gutter = g_atlas_gutter_texels,
};

// cellRect() clamps an out-of-range index, so a grid the cell size left too small for the range
// would draw the last glyph for every character past it instead of failing.
static_assert(g_glyph_layout.capacity() >= g_last_glyph - g_first_glyph + 1);
static_assert(
    g_glyph_texture_size % (1 << g_atlas_exact_mip_levels) == 0,
    "the glyph atlas edge must be a multiple of 2^g_atlas_exact_mip_levels");

// Byte offsets of one BGRA8 texel's channels; B is coverage under the structural scheme.
constexpr std::size_t g_blue = 0;
constexpr std::size_t g_green = 1;
constexpr std::size_t g_red = 2;
constexpr std::size_t g_alpha = 3;
constexpr std::size_t g_texel_bytes = 4;

// A JUCE ARGB image's texels in tightly packed rows, the layout createTexture2D expects; JUCE's
// lineStride may be wider. JUCE's ARGB is premultiplied BGRA in memory on little-endian targets,
// which BGRA8 maps to natively — no swizzle.
[[nodiscard]] std::vector<std::uint8_t> packedTexels(const juce::Image& image)
{
    const std::size_t row_bytes = static_cast<std::size_t>(image.getWidth()) * g_texel_bytes;
    std::vector<std::uint8_t> texels(row_bytes * static_cast<std::size_t>(image.getHeight()));
    const juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::readOnly};
    for (int row = 0; row < image.getHeight(); ++row)
    {
        std::memcpy(
            texels.data() + (static_cast<std::size_t>(row) * row_bytes),
            bitmap.getLinePointer(row),
            row_bytes);
    }
    return texels;
}

// Uploads an ARGB image as an immutable BGRA8 bgfx texture: its texels alone, or with its full mip
// chain, which the default linear sampling then filters trilinearly.
[[nodiscard]] UniqueBgfxHandle<bgfx::TextureHandle> uploadTexture(
    const juce::Image& image, const bool mipmapped)
{
    const std::vector<std::uint8_t> texels = mipmapped ? atlasMipChain(image) : packedTexels(image);
    return UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        static_cast<std::uint16_t>(image.getWidth()),
        static_cast<std::uint16_t>(image.getHeight()),
        mipmapped,
        1,
        bgfx::TextureFormat::BGRA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        bgfx::copy(texels.data(), static_cast<std::uint32_t>(texels.size())))};
}

// One texel of the next mip level from the 2x2 it covers, each channel rounded to nearest: B and
// A plainly, R and G weighted by B (the rule atlasMipChain's contract states).
void averageTexels(const std::array<const std::uint8_t*, 4>& taps, std::uint8_t* target) noexcept
{
    const auto channel = [](const std::uint8_t* const tap, const std::size_t offset) {
        return static_cast<unsigned>(tap[offset]);
    };
    unsigned coverage = 0;
    unsigned alpha = 0;
    unsigned red = 0;
    unsigned green = 0;
    for (const std::uint8_t* const tap : taps)
    {
        coverage += channel(tap, g_blue);
        alpha += channel(tap, g_alpha);
        red += channel(tap, g_red) * channel(tap, g_blue);
        green += channel(tap, g_green) * channel(tap, g_blue);
    }
    const auto rounded = [](const unsigned total, const unsigned count) {
        return static_cast<std::uint8_t>((total + (count / 2U)) / count);
    };
    target[g_blue] = rounded(coverage, static_cast<unsigned>(taps.size()));
    target[g_alpha] = rounded(alpha, static_cast<unsigned>(taps.size()));
    target[g_red] = coverage == 0U ? std::uint8_t{0} : rounded(red, coverage);
    target[g_green] = coverage == 0U ? std::uint8_t{0} : rounded(green, coverage);
}

} // namespace

// UV rect of one cell's art; an unconfigured layout yields a zero rect.
std::array<float, 4> HighwayAtlasLayout::cellRect(const int index) const noexcept
{
    if (capacity() <= 0)
    {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }

    const auto [x, y] = cellOrigin(index);
    const auto width = static_cast<float>(texture_width);
    const auto height = static_cast<float>(texture_height);
    const auto cell = static_cast<float>(cell_size);
    return {
        static_cast<float>(x) / width,
        static_cast<float>(y) / height,
        (static_cast<float>(x) + cell) / width,
        (static_cast<float>(y) + cell) / height,
    };
}

// A cell's art starts one gutter into its slot.
std::array<int, 2> HighwayAtlasLayout::cellOrigin(const int index) const noexcept
{
    if (capacity() <= 0)
    {
        return {0, 0};
    }

    const int clamped = std::clamp(index, 0, capacity() - 1);
    return {
        ((clamped % columns()) * pitch()) + gutter,
        ((clamped / columns()) * pitch()) + gutter,
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
        .handle = uploadTexture(decoded.convertedToFormat(juce::Image::ARGB), false),
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

// Copies each cell row by row from the packed grid to its slot in a cleared (all-zero) image whose
// layout is the grid's plus the gutter; both origins come from the one cellOrigin() rule.
juce::Image padAtlasCells(const juce::Image& grid, const int cell_size, const int gutter)
{
    const HighwayAtlasLayout packed{
        .texture_width = grid.getWidth(),
        .texture_height = grid.getHeight(),
        .cell_size = cell_size,
        .gutter = 0,
    };
    HighwayAtlasLayout padded{
        .texture_width = 0, .texture_height = 0, .cell_size = cell_size, .gutter = gutter
    };
    padded.texture_width = packed.columns() * padded.pitch();
    padded.texture_height = packed.rows() * padded.pitch();
    const juce::Image spread{
        juce::Image::ARGB,
        padded.texture_width,
        padded.texture_height,
        true,
        juce::SoftwareImageType{}
    };

    const juce::Image::BitmapData source{grid, juce::Image::BitmapData::readOnly};
    const juce::Image::BitmapData target{spread, juce::Image::BitmapData::writeOnly};
    const std::size_t cell_bytes = static_cast<std::size_t>(cell_size) * g_texel_bytes;
    for (int index = 0; index < packed.capacity(); ++index)
    {
        const auto [source_x, source_y] = packed.cellOrigin(index);
        const auto [target_x, target_y] = padded.cellOrigin(index);
        for (int row = 0; row < cell_size; ++row)
        {
            std::memcpy(
                target.getPixelPointer(target_x, target_y + row),
                source.getPixelPointer(source_x, source_y + row),
                cell_bytes);
        }
    }
    return spread;
}

// Appends each level after the one before it until a 1x1 level, averaging 2x2 taps clamped to the
// source level's edge so a one-texel-wide level still reads in range.
std::vector<std::uint8_t> atlasMipChain(const juce::Image& image)
{
    std::vector<std::uint8_t> chain = packedTexels(image);
    std::size_t level_offset = 0;
    auto width = static_cast<std::size_t>(image.getWidth());
    auto height = static_cast<std::size_t>(image.getHeight());
    while (width > 1 || height > 1)
    {
        const std::size_t next_width = std::max<std::size_t>(width / 2, 1);
        const std::size_t next_height = std::max<std::size_t>(height / 2, 1);
        const std::size_t next_offset = chain.size();
        chain.resize(next_offset + (next_width * next_height * g_texel_bytes));
        const auto tap = [&](const std::size_t x, const std::size_t y) {
            const std::size_t clamped_x = std::min(x, width - 1);
            const std::size_t clamped_y = std::min(y, height - 1);
            return chain.data() + level_offset +
                   (((clamped_y * width) + clamped_x) * g_texel_bytes);
        };
        for (std::size_t y = 0; y < next_height; ++y)
        {
            for (std::size_t x = 0; x < next_width; ++x)
            {
                averageTexels(
                    {tap(2 * x, 2 * y),
                     tap((2 * x) + 1, 2 * y),
                     tap(2 * x, (2 * y) + 1),
                     tap((2 * x) + 1, (2 * y) + 1)},
                    chain.data() + next_offset + (((y * next_width) + x) * g_texel_bytes));
            }
        }
        level_offset = next_offset;
        width = next_width;
        height = next_height;
    }
    return chain;
}

// Builds both atlases: the glyph sheet is rasterized at runtime from the bundled font, while
// the note-head atlas spreads the composed head and mark sheets into gutters and derives its cell
// layout from the composed width.
HighwayAtlases makeHighwayAtlases(
    const std::span<const std::byte> heads_png, const std::span<const std::byte> marks_png)
{
    HighwayAtlases atlases;
    atlases.glyph_layout = g_glyph_layout;

    // Head atlas: the theme's heads over the fixed marks (one art set for every consumer)
    // upload when both decode; empty or undecodable bytes leave the handle invalid and the
    // layout empty, which the renderer rejects at create — required product content, never
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
        const int cell_size = composed.getWidth() / g_head_atlas_columns;
        const juce::Image spread = padAtlasCells(composed, cell_size, g_atlas_gutter_texels);
        atlases.heads = uploadTexture(spread, true);
        atlases.head_layout = HighwayAtlasLayout{
            .texture_width = spread.getWidth(),
            .texture_height = spread.getHeight(),
            .cell_size = cell_size,
            .gutter = g_atlas_gutter_texels,
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

        for (char character = g_first_glyph; character <= g_last_glyph; ++character)
        {
            const auto [x, y] = g_glyph_layout.cellOrigin(character - g_first_glyph);
            graphics.drawText(
                juce::String::charToString(static_cast<juce::juce_wchar>(character)),
                x,
                y,
                g_glyph_cell_size,
                g_glyph_cell_size,
                juce::Justification::centred);
        }
        atlases.glyphs = uploadTexture(image, true);
    }

    return atlases;
}

} // namespace rock_hero::common::ui
