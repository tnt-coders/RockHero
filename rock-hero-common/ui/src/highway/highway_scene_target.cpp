#include "highway/highway_scene_target.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>
#include <optional>
#include <variant>

namespace rock_hero::common::ui
{

namespace
{

// The multisample level the scene renders at. The scene is thin-line-heavy (fret lines, strings,
// lane ribbons), so its edges alias visibly without it. bgfx clamps the request to the device's
// support for the backbuffer format, and every Direct3D 11 device supports 4x.
constexpr std::uint64_t g_scene_msaa = BGFX_TEXTURE_RT_MSAA_X4;

// RGBA8, not sRGB: the format the window's backbuffer has (RenderDevice requests no sRGB
// backbuffer), so blending in the target matches what the window did when the scene drew there,
// and the present copies texels without a conversion. If the backbuffer ever becomes sRGB, this
// must follow it or the copy encodes twice.
constexpr bgfx::TextureFormat::Enum g_scene_color_format = bgfx::TextureFormat::RGBA8;

// The depth the board's prepass tests against: the format bgfx gives the backbuffer's own depth.
constexpr bgfx::TextureFormat::Enum g_scene_depth_format = bgfx::TextureFormat::D24S8;

// The frame of a look made of virtual pixels scale window pixels wide: the full-resolution scene
// rounded UP to whole scale x scale blocks, so the block pass reads only texels inside the scene
// and the scene still covers the window, and the block canvas holding one texel per block. At
// scale 1 a block is one pixel, so a look keeping the scene's own colors needs no canvas: the
// present reads the scene itself.
[[nodiscard]] HighwaySceneFrame blockFrame(
    const std::uint32_t width, const std::uint32_t height, const int scale, const double scanlines,
    const HighwayBlockColors block_colors)
{
    const auto edge = static_cast<std::uint32_t>(scale);
    const std::uint32_t columns = (width + edge - 1U) / edge;
    const std::uint32_t rows = (height + edge - 1U) / edge;
    std::optional<HighwaySceneTargetSpec> blocks;
    if (scale > 1 || block_colors != HighwayBlockColors::Mean)
    {
        blocks = HighwaySceneTargetSpec{.width = columns, .height = rows};
    }
    return HighwaySceneFrame{
        .target = HighwaySceneTargetSpec{.width = columns * edge, .height = rows * edge},
        .blocks = blocks,
        .block_colors = block_colors,
        .present = HighwayPresent::Copy,
        .present_scale = static_cast<float>(scale),
        .scanlines = static_cast<float>(scanlines),
        .scanline_height = 1.0F,
    };
}

// Snapped to the theme's colors, or the scene's own.
[[nodiscard]] HighwayBlockColors keptColors(const bool theme_colors_only)
{
    return theme_colors_only ? HighwayBlockColors::Snapped : HighwayBlockColors::Mean;
}

} // namespace

HighwaySceneFrame makeHighwaySceneFrame(
    const std::uint32_t width, const std::uint32_t height, const HighwayRenderLook& look)
{
    if (const auto* const pixel = std::get_if<HighwayPixelLook>(&look))
    {
        return blockFrame(
            width, height, pixel->scale, pixel->scanlines, keptColors(pixel->theme_colors_only));
    }
    if (const auto* const crt = std::get_if<HighwayCrtLook>(&look))
    {
        HighwaySceneFrame frame =
            blockFrame(width, height, 1, crt->scanlines, keptColors(crt->theme_colors_only));
        frame.scanline_height = static_cast<float>(crt->line_height);
        return frame;
    }
    if (const auto* const dither = std::get_if<HighwayDitherLook>(&look))
    {
        return blockFrame(width, height, dither->scale, 0.0, HighwayBlockColors::Dithered);
    }
    if (const auto* const halftone = std::get_if<HighwayHalftoneLook>(&look))
    {
        return HighwaySceneFrame{
            .target = HighwaySceneTargetSpec{.width = width, .height = height},
            .blocks = std::nullopt,
            .block_colors = HighwayBlockColors::Mean,
            .present = HighwayPresent::Halftone,
            .present_scale = static_cast<float>(halftone->cell),
            .scanlines = 0.0F,
            .scanline_height = 1.0F,
        };
    }
    return blockFrame(width, height, 1, 0.0, HighwayBlockColors::Mean);
}

std::array<float, 4> highwayPresentColor(const ArgbColor color)
{
    const auto channel = [color](const int shift) {
        return static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    };
    return {channel(16), channel(8), channel(0), 1.0F};
}

HighwaySceneTarget::HighwaySceneTarget(const HighwaySceneTargetKind kind) noexcept
    : m_kind{kind}
{}

// Releases the old target before creating the new one, so a resize never holds both. The color
// attachment is sampled (never write-only) so bgfx keeps a single-sampled copy to resolve into;
// the scene's depth is write-only, which bgfx requires of a multisampled depth attachment.
bool HighwaySceneTarget::fit(const HighwaySceneTargetSpec& spec)
{
    if (m_spec == spec)
    {
        return m_frame_buffer.isValid();
    }
    m_spec = spec;
    m_frame_buffer.reset();
    m_color.reset();
    m_depth.reset();

    const bool scene = m_kind == HighwaySceneTargetKind::Scene;
    const std::uint64_t samples = scene ? g_scene_msaa : BGFX_TEXTURE_RT;
    const auto width = static_cast<std::uint16_t>(spec.width);
    const auto height = static_cast<std::uint16_t>(spec.height);
    m_color = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width,
        height,
        false,
        1,
        g_scene_color_format,
        samples | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP)};
    if (!m_color.isValid())
    {
        return false;
    }
    if (!scene)
    {
        const bgfx::TextureHandle attachment = m_color.get();
        m_frame_buffer = UniqueBgfxHandle<bgfx::FrameBufferHandle>{bgfx::createFrameBuffer(
            1, &attachment, false)};
        return m_frame_buffer.isValid();
    }

    m_depth = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width, height, false, 1, g_scene_depth_format, samples | BGFX_TEXTURE_RT_WRITE_ONLY)};
    if (!m_depth.isValid())
    {
        return false;
    }
    // Never destroyTextures: the wrappers own the attachments (bgfx_handle.h).
    const std::array<bgfx::TextureHandle, 2> attachments{m_color.get(), m_depth.get()};
    m_frame_buffer = UniqueBgfxHandle<bgfx::FrameBufferHandle>{bgfx::createFrameBuffer(
        static_cast<std::uint8_t>(attachments.size()), attachments.data(), false)};
    return m_frame_buffer.isValid();
}

bgfx::FrameBufferHandle HighwaySceneTarget::frameBuffer() const noexcept
{
    return m_frame_buffer.get();
}

bgfx::TextureHandle HighwaySceneTarget::color() const noexcept
{
    return m_color.get();
}

} // namespace rock_hero::common::ui
