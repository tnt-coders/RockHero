#include "highway/highway_scene_target.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstdint>

namespace rock_hero::common::ui
{

namespace
{

// The multisample level the scene renders at: the one the backbuffer has (render_device.cpp), so
// the board's thin fret lines, strings and lane ribbons are anti-aliased alike under every look.
// bgfx clamps the request to the device's support, and every Direct3D 11 device supports 4x.
constexpr std::uint64_t g_scene_msaa = BGFX_TEXTURE_RT_MSAA_X4;

// RGBA8, not sRGB: the format the window's backbuffer has (RenderDevice requests no sRGB
// backbuffer), so blending in the target matches what the window does when the scene draws there,
// and the present decodes the same gamma-encoded colors the plain board shows.
constexpr bgfx::TextureFormat::Enum g_scene_color_format = bgfx::TextureFormat::RGBA8;

// The depth the board's prepass tests against: the format bgfx gives the backbuffer's own depth.
constexpr bgfx::TextureFormat::Enum g_scene_depth_format = bgfx::TextureFormat::D24S8;

} // namespace

// Releases the old target before creating the new one, so a resize never holds both. The color
// attachment is sampled (never write-only), so bgfx keeps a single-sampled copy to resolve into;
// the depth is write-only, which bgfx requires of a multisampled depth attachment.
bool HighwaySceneTarget::fit(
    const std::uint32_t width, const std::uint32_t height, const bool mipmapped)
{
    if (width == m_width && height == m_height && mipmapped == m_mipmapped)
    {
        return m_frame_buffer.isValid();
    }
    m_width = width;
    m_height = height;
    m_mipmapped = mipmapped;
    m_frame_buffer.reset();
    m_color.reset();
    m_depth.reset();

    // A mip chain is regenerated from a single-sampled target only (see the class comment).
    const std::uint64_t samples = mipmapped ? BGFX_TEXTURE_RT : g_scene_msaa;
    const auto width16 = static_cast<std::uint16_t>(width);
    const auto height16 = static_cast<std::uint16_t>(height);
    m_color = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width16,
        height16,
        mipmapped,
        1,
        g_scene_color_format,
        samples | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP)};
    m_depth = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
        width16, height16, false, 1, g_scene_depth_format, samples | BGFX_TEXTURE_RT_WRITE_ONLY)};
    if (m_color.isValid() && m_depth.isValid())
    {
        // Never destroyTextures: the wrappers own the attachments (bgfx_handle.h).
        const std::array<bgfx::TextureHandle, 2> attachments{m_color.get(), m_depth.get()};
        m_frame_buffer = UniqueBgfxHandle<bgfx::FrameBufferHandle>{bgfx::createFrameBuffer(
            static_cast<std::uint8_t>(attachments.size()), attachments.data(), false)};
    }
    if (!m_frame_buffer.isValid())
    {
        // A refused size holds nothing until the next fit retries.
        m_color.reset();
        m_depth.reset();
        return false;
    }
    return true;
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
