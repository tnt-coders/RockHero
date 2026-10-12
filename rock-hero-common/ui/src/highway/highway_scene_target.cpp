#include "highway/highway_scene_target.h"

#include "highway_theme/highway_theme_roles.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>
#include <tuple>
#include <variant>

namespace rock_hero::common::ui
{

namespace
{

// The multisample level a multisampled scene renders at. The scene is thin-line-heavy (fret
// lines, strings, lane ribbons), so its edges alias visibly without it. bgfx clamps the request to
// the device's support for the backbuffer format, and every Direct3D 11 device supports 4x.
constexpr std::uint64_t g_scene_msaa = BGFX_TEXTURE_RT_MSAA_X4;

// RGBA8, not sRGB: the format the window's backbuffer has (RenderDevice requests no sRGB
// backbuffer), so blending in the target matches what the window did when the scene drew there,
// and the present copies texels without a conversion. If the backbuffer ever becomes sRGB, this
// must follow it or the copy encodes twice.
constexpr bgfx::TextureFormat::Enum g_scene_color_format = bgfx::TextureFormat::RGBA8;

// The depth the board's prepass tests against: the format bgfx gives the backbuffer's own depth.
constexpr bgfx::TextureFormat::Enum g_scene_depth_format = bgfx::TextureFormat::D24S8;

// The scene at ceil(size / scale), one scene pixel to scale x scale window pixels, never
// multisampled: the pixel and dither looks' canvas.
[[nodiscard]] HighwaySceneTargetSpec virtualCanvas(
    const std::uint32_t width, const std::uint32_t height, const int scale)
{
    const auto edge = static_cast<std::uint32_t>(scale);
    return HighwaySceneTargetSpec{
        .width = (width + edge - 1U) / edge,
        .height = (height + edge - 1U) / edge,
        .multisampled = false,
    };
}

} // namespace

HighwaySceneFrame makeHighwaySceneFrame(
    const std::uint32_t width, const std::uint32_t height, const HighwayRenderLook& look)
{
    if (const auto* const pixel = std::get_if<HighwayPixelLook>(&look))
    {
        return HighwaySceneFrame{
            .target = virtualCanvas(width, height, pixel->scale),
            .present = HighwayPresent::Copy,
            .present_scale = static_cast<float>(pixel->scale),
            .scanlines = static_cast<float>(pixel->scanlines),
        };
    }
    if (const auto* const dither = std::get_if<HighwayDitherLook>(&look))
    {
        return HighwaySceneFrame{
            .target = virtualCanvas(width, height, dither->scale),
            .present = HighwayPresent::Dither,
            .present_scale = static_cast<float>(dither->scale),
            .scanlines = 0.0F,
        };
    }
    const HighwaySceneTargetSpec full{.width = width, .height = height, .multisampled = true};
    if (const auto* const halftone = std::get_if<HighwayHalftoneLook>(&look))
    {
        return HighwaySceneFrame{
            .target = full,
            .present = HighwayPresent::Halftone,
            .present_scale = static_cast<float>(halftone->cell),
            .scanlines = 0.0F,
        };
    }
    return HighwaySceneFrame{
        .target = full,
        .present = HighwayPresent::Copy,
        .present_scale = 1.0F,
        .scanlines = 0.0F,
    };
}

std::array<float, 4> highwayPresentColor(const ArgbColor color)
{
    const auto channel = [color](const int shift) {
        return static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    };
    return {channel(16), channel(8), channel(0), 1.0F};
}

// Roles in their table's order, then the strings highest first, then black and white.
HighwayDitherPalette makeHighwayDitherPalette(
    const HighwayTheme& theme, const StringColorPalette& strings)
{
    static_assert(
        g_highway_theme_roles.size() + std::tuple_size_v<decltype(StringColorPalette::colors)> +
                2 <=
            g_highway_dither_palette_capacity,
        "the dither palette must hold every role, every string, black and white");
    HighwayDitherPalette palette;
    const auto push = [&palette](const ArgbColor color) {
        palette.colors.at(palette.count) = highwayPresentColor(color);
        ++palette.count;
    };
    for (const HighwayThemeRole& role : g_highway_theme_roles)
    {
        push(theme.*role.member);
    }
    for (const ArgbColor color : strings.colors)
    {
        push(color);
    }
    push(0xFF000000U);
    push(0xFFFFFFFFU);
    return palette;
}

// Releases the old target before creating the new one, so a resize never holds both. The color
// attachment is sampled (never write-only) so bgfx keeps a single-sampled copy to resolve into;
// the depth is write-only, which bgfx requires of a multisampled depth attachment and which keeps
// a single-sampled one a plain render buffer.
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

    const std::uint64_t samples = spec.multisampled ? g_scene_msaa : BGFX_TEXTURE_RT;
    const auto width = static_cast<std::uint16_t>(spec.width);
    const auto height = static_cast<std::uint16_t>(spec.height);
    m_color = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width,
        height,
        false,
        1,
        g_scene_color_format,
        samples | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP)};
    m_depth = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width, height, false, 1, g_scene_depth_format, samples | BGFX_TEXTURE_RT_WRITE_ONLY)};
    if (!m_color.isValid() || !m_depth.isValid())
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
