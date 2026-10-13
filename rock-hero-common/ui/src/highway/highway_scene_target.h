/*!
\file highway_scene_target.h
\brief The off-screen target the highway scene renders into, and the frame that sizes it.
*/

#pragma once

#include "highway/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/ui/highway_theme/highway_render_look.h>
#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief What one of the scene's off-screen targets must be: its size.

Two frames that agree on this share one target, so a change of anything else the present reads
never recreates GPU resources.
*/
struct HighwaySceneTargetSpec
{
    /*! \brief Target width in pixels. */
    std::uint32_t width{0};

    /*! \brief Target height in pixels. */
    std::uint32_t height{0};

    /*!
    \brief Compares every field; all are integers, so the defaulted comparison is exact.
    \param other Spec to compare against.
    \return True when both describe the same target.
    */
    [[nodiscard]] bool operator==(const HighwaySceneTargetSpec& other) const = default;
};

/*! \brief Which present program carries the scene to the window, one per family of look. */
enum class HighwayPresent : std::uint8_t
{
    /*! \brief A copy with optional scanlines: the plain, pixel and CRT looks. */
    Copy,

    /*! \brief The ordered dither to the theme's palette. */
    Dither,

    /*! \brief The halftone dot screen with ink outlines. */
    Halftone,
};

/*!
\brief One frame's off-screen scene and how it is presented to a window of a given size.
*/
struct HighwaySceneFrame
{
    /*! \brief The target the scene renders into. */
    HighwaySceneTargetSpec target;

    /*!
    \brief The block canvas, one texel per virtual pixel, that the pixel and dither looks render
    between the scene and the present; empty when a virtual pixel is a window pixel, or under the
    halftone, where the present reads the scene itself.
    */
    std::optional<HighwaySceneTargetSpec> blocks;

    /*! \brief The program that presents it. */
    HighwayPresent present{HighwayPresent::Copy};

    /*!
    \brief The present's size in window pixels: one virtual pixel's edge for the copy and the
    dither, one screen cell's edge for the halftone.
    */
    float present_scale{1.0F};

    /*! \brief How much the copy darkens every other scanline band, 0 to 1. */
    float scanlines{0.0F};

    /*!
    \brief Height of one scanline band in rows of the image the present reads: one virtual pixel
    under the pixel look, the look's own line height under the CRT.
    */
    float scanline_height{1.0F};
};

/*!
\brief A packed color as a present uniform takes it: red, green and blue from 0 to 1, then 1.
\param color The color, 0xAARRGGBB; its alpha is dropped.
\return The color's channels.
*/
[[nodiscard]] std::array<float, 4> highwayPresentColor(ArgbColor color);

/*! \brief The most colors a dither palette holds: the present program's uniform array size. */
inline constexpr std::size_t g_highway_dither_palette_capacity = 32;

/*! \brief The colors the dither look may show, as the present's uniform array takes them. */
struct HighwayDitherPalette
{
    /*! \brief Each color's red, green and blue from 0 to 1, then 1; the first count are live. */
    std::array<std::array<float, 4>, g_highway_dither_palette_capacity> colors{};

    /*! \brief How many colors are live. */
    std::size_t count{0};
};

/*!
\brief The dither look's palette for a theme: every role color, then every string color, alpha
dropped, then black and white.

Built only from colors the theme states, so the dither can show nothing the loader's guardrails
did not approve.

\param theme The theme's color roles.
\param strings The string colors the board draws with.
\return The palette.
*/
[[nodiscard]] HighwayDitherPalette makeHighwayDitherPalette(
    const HighwayTheme& theme, const StringColorPalette& strings);

/*!
\brief The frame a window of the given size presents under a render look.

Every look renders the full-resolution scene, multisampled. The plain look renders at the window's
size and copies one pixel to one pixel. The pixel look rounds the size up to whole scale x scale
blocks, so the target still covers the window (up to scale - 1 pixels of it fall past one edge),
reduces each block to one texel of a block canvas, and presents each texel as one hard-edged
virtual pixel with the look's scanlines: a detail smaller than a block tints it by the share it
covers rather than popping in and out as it moves. The CRT look renders like the plain look and
darkens every other band of its line height. The dither look sizes its targets the same way
and dithers each texel of the canvas. The halftone look renders like the plain look — its ink
outlines trace the full-resolution scene — and presents through the dot screen.

\param width Window width in pixels; non-zero.
\param height Window height in pixels; non-zero.
\param look How the theme presents its frame.
\return The frame to render and present.
*/
[[nodiscard]] HighwaySceneFrame makeHighwaySceneFrame(
    std::uint32_t width, std::uint32_t height, const HighwayRenderLook& look);

/*! \brief Which off-screen target a HighwaySceneTarget is, which decides its attachments. */
enum class HighwaySceneTargetKind : std::uint8_t
{
    /*! \brief The scene itself: 4x multisampled color, resolved before it is read, and depth. */
    Scene,

    /*! \brief The block canvas: single-sampled color only, written by a full-screen pass. */
    Blocks,
};

/*!
\brief Owns one of the scene's off-screen targets: its attachments and the frame buffer joining
them.

Created lazily and refitted only when the spec changes, so creation, a window resize and a change
of look are one path. Every handle dies with this object, which its owner destroys before bgfx
shutdown (the UniqueBgfxHandle contract).
*/
class HighwaySceneTarget
{
public:
    /*!
    \brief Creates an empty target of one kind; the first fit allocates it.
    \param kind Which target this is.
    */
    explicit HighwaySceneTarget(HighwaySceneTargetKind kind) noexcept;

    /*!
    \brief Makes the target match the spec, recreating it only when the spec differs from the one
    last fitted.

    A spec bgfx refused is not retried until the spec changes, so a failure costs one attempt, not
    one per frame.

    \param spec The target this frame renders into.
    \return True when the target is ready to render into.
    */
    [[nodiscard]] bool fit(const HighwaySceneTargetSpec& spec);

    /*!
    \brief The frame buffer the scene views render into.
    \return The frame buffer; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::FrameBufferHandle frameBuffer() const noexcept;

    /*!
    \brief The color attachment the next pass reads, single-sampled: bgfx resolves the
    multisampled scene into it when the frame leaves the target.
    \return The color texture; invalid until a fit succeeds.
    */
    [[nodiscard]] bgfx::TextureHandle color() const noexcept;

private:
    // Which target this is; fixed for its life.
    HighwaySceneTargetKind m_kind;

    // The spec last fitted, successfully or not; empty before the first fit.
    std::optional<HighwaySceneTargetSpec> m_spec;

    // The attachments, then the frame buffer that references them (bgfx counts the references, so
    // destruction order is free). The block canvas has no depth.
    UniqueBgfxHandle<bgfx::TextureHandle> m_color;
    UniqueBgfxHandle<bgfx::TextureHandle> m_depth;
    UniqueBgfxHandle<bgfx::FrameBufferHandle> m_frame_buffer;
};

} // namespace rock_hero::common::ui
