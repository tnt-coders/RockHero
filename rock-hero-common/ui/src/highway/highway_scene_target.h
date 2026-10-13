/*!
\file highway_scene_target.h
\brief The off-screen target a theme's CRT look renders the highway scene into.
*/

#pragma once

#include "highway/bgfx_handle.h"

#include <bgfx/bgfx.h>
#include <cstdint>

namespace rock_hero::common::ui
{

/*!
\brief Owns the CRT look's off-screen scene: the color attachment the present reads, its depth,
and the frame buffer joining them.

A full-resolution scene is multisampled, as the window is. A scene the present reads at reduced
resolution instead carries a mip chain, which bgfx regenerates each frame, so averaging a block of
the scene is one sample; it gives up multisampling, since bgfx cannot resolve a multisampled
target into a mip chain, and the mip averaging smooths its edges in its place. Created lazily and
refitted only when the window's size or that choice changes, so the first CRT frame and a resize
are one path. Every handle dies with this object, which its owner destroys before bgfx
shutdown (the UniqueBgfxHandle contract).
*/
class HighwaySceneTarget
{
public:
    /*!
    \brief Makes the target the given size and kind, recreating it only when either differs from
    the one last fitted.

    A target bgfx refused is not retried until the size or kind changes, so a failure costs one
    attempt, not one per frame.

    \param width Target width in pixels; non-zero.
    \param height Target height in pixels; non-zero.
    \param mipmapped True for a scene the present reads at reduced resolution, false for a
           multisampled full-resolution one.
    \return True when the target is ready to render into.
    */
    [[nodiscard]] bool fit(std::uint32_t width, std::uint32_t height, bool mipmapped);

    /*!
    \brief The frame buffer the scene views render into.
    \return The frame buffer; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::FrameBufferHandle frameBuffer() const noexcept;

    /*!
    \brief The color attachment the present reads, single-sampled: bgfx resolves a multisampled
    scene into it, or regenerates its mip chain, when the frame leaves the target.
    \return The color texture; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::TextureHandle color() const noexcept;

private:
    // The size and kind last fitted, successfully or not; zero before the first fit.
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
    bool m_mipmapped{false};

    // The attachments, then the frame buffer that references them (bgfx counts the references, so
    // destruction order is free).
    UniqueBgfxHandle<bgfx::TextureHandle> m_color;
    UniqueBgfxHandle<bgfx::TextureHandle> m_depth;
    UniqueBgfxHandle<bgfx::FrameBufferHandle> m_frame_buffer;
};

} // namespace rock_hero::common::ui
