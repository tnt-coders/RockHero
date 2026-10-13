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
\brief Owns the CRT look's off-screen scene: a multisampled color attachment the present reads,
its depth, and the frame buffer joining them.

Created lazily and refitted only when the window's size changes, so the first CRT frame and a
resize are one path. Every handle dies with this object, which its owner destroys before bgfx
shutdown (the UniqueBgfxHandle contract).
*/
class HighwaySceneTarget
{
public:
    /*!
    \brief Makes the target the given size, recreating it only when the size differs from the one
    last fitted.

    A size bgfx refused is not retried until the size changes, so a failure costs one attempt, not
    one per frame.

    \param width Target width in pixels; non-zero.
    \param height Target height in pixels; non-zero.
    \return True when the target is ready to render into.
    */
    [[nodiscard]] bool fit(std::uint32_t width, std::uint32_t height);

    /*!
    \brief The frame buffer the scene views render into.
    \return The frame buffer; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::FrameBufferHandle frameBuffer() const noexcept;

    /*!
    \brief The color attachment the present reads, single-sampled: bgfx resolves the multisampled
    scene into it when the frame leaves the target.
    \return The color texture; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::TextureHandle color() const noexcept;

private:
    // The size last fitted, successfully or not; zero before the first fit.
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};

    // The attachments, then the frame buffer that references them (bgfx counts the references, so
    // destruction order is free).
    UniqueBgfxHandle<bgfx::TextureHandle> m_color;
    UniqueBgfxHandle<bgfx::TextureHandle> m_depth;
    UniqueBgfxHandle<bgfx::FrameBufferHandle> m_frame_buffer;
};

} // namespace rock_hero::common::ui
