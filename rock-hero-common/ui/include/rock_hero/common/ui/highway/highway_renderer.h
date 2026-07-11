/*!
\file highway_renderer.h
\brief Shared bgfx renderer for the 3D note highway, consumed by the game and the editor preview.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::ui
{

/*! \brief One shader program's compiled stage binaries. */
struct HighwayShaderPair
{
    /*! \brief Compiled vertex-stage binary. */
    std::vector<std::byte> vertex;

    /*! \brief Compiled fragment-stage binary. */
    std::vector<std::byte> fragment;
};

/*!
\brief The compiled shader programs the highway renderer links at creation.

Each product resolves these through its own resource loading (the game's resource pack, the
editor's deployed preview resources) from the shared shader sources in rock-hero-common/ui/shaders;
the renderer itself never touches the filesystem.
*/
struct HighwayShaderSet
{
    /*! \brief Flat vertex-color geometry (board furniture, rails, boxes, overlay rects). */
    HighwayShaderPair color;

    /*! \brief Vertex color with a Z-ramp alpha fade (beat bars fading toward the horizon). */
    HighwayShaderPair color_fade;

    /*!
    \brief Atlas-textured quads with the reference channel scheme: texture R multiplies the tint
    color, G adds white highlight, B is the alpha mask — one atlas serves every string color.
    */
    HighwayShaderPair texture_tint;

    /*! \brief Glyph-atlas text (fret numbers, section labels). */
    HighwayShaderPair glyph;
};

/*! \brief Stable reasons the highway renderer can fail to come up. */
enum class HighwayRendererErrorCode : std::uint8_t
{
    /*! \brief bgfx rejected a shader binary or failed to link a program. */
    ProgramCreationFailed,
};

/*! \brief Typed boundary error for highway renderer startup failures. */
struct [[nodiscard]] HighwayRendererError
{
    /*! \brief Stable failure reason for program branching. */
    HighwayRendererErrorCode code{};

    /*! \brief Human-readable diagnostic for logs and stderr. */
    std::string message;
};

/*! \brief One screen-space rectangle for the overlay view, in pixels from the top-left corner. */
struct HighwayOverlayRect
{
    /*! \brief Left edge in pixels. */
    float left{0.0F};

    /*! \brief Top edge in pixels. */
    float top{0.0F};

    /*! \brief Right edge in pixels. */
    float right{0.0F};

    /*! \brief Bottom edge in pixels. */
    float bottom{0.0F};

    /*! \brief Packed ABGR color (alpha-blended). */
    std::uint32_t abgr{0};
};

/*!
\brief Renders the note highway from the shared headless scene model.

One renderer serves both products (the user's 2026-07-11 promotion decision superseding plan 44's
duplicated-thin-drawers recommendation): the game shell and the editor preview each own a bgfx
device and feed this renderer their compiled shaders, view state, and per-frame time. bgfx never
appears in this header — the framework stays isolated to implementation files, the same treatment
Tracktion receives in common/audio.

Lifetime: create only while a bgfx device is live, destroy before bgfx shutdown (every owned GPU
resource dies with this object). All methods run on the bgfx API thread.

The renderer is a pure consumer: it never derives song time itself and draws exactly what the
view state plus the per-frame time arguments describe (plan 25's event feed and technique phases
extend the drawers, not this contract).
*/
class HighwayRenderer
{
public:
    /*!
    \brief Links the shader programs and rasterizes the highway atlases.

    \param shaders Compiled stage binaries for the four highway programs.
    \return The renderer, or a typed error naming the program that failed.
    */
    [[nodiscard]] static std::expected<HighwayRenderer, HighwayRendererError> create(
        const HighwayShaderSet& shaders);

    /*! \brief Destroys every owned GPU resource; must run before bgfx shutdown. */
    ~HighwayRenderer();

    /*!
    \brief Transfers renderer ownership; the source becomes empty and tears nothing down.
    \param other Renderer losing ownership.
    */
    HighwayRenderer(HighwayRenderer&& other) noexcept;

    /*!
    \brief Replaces this renderer with another; the source becomes empty.
    \param other Renderer losing ownership.
    \return This renderer.
    */
    HighwayRenderer& operator=(HighwayRenderer&& other) noexcept;

    HighwayRenderer(const HighwayRenderer&) = delete;
    HighwayRenderer& operator=(const HighwayRenderer&) = delete;

    /*!
    \brief Replaces the chart content and rebuilds the retained board geometry.

    \param state Seconds-resolved highway content from the shared projection.
    */
    void setViewState(common::core::HighwayViewState state);

    /*!
    \brief Encodes one frame of the highway into the render views.

    \param now_seconds Playback song time for this frame (from the consumer's clock port).
    \param dt_seconds Frame delta in seconds driving camera smoothing.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void draw(double now_seconds, double dt_seconds, std::uint32_t width, std::uint32_t height);

    /*!
    \brief Encodes screen-space rectangles onto the overlay view (diagnostics panels).

    Call after \ref draw within the same frame; the rectangles composite over the scene in list
    order with alpha blending.

    \param rects Rectangles in pixel coordinates.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void drawOverlayRects(
        std::span<const HighwayOverlayRect> rects, std::uint32_t width, std::uint32_t height);

private:
    struct Impl;

    // Only create() constructs a renderer.
    explicit HighwayRenderer(std::unique_ptr<Impl> impl) noexcept;

    // All bgfx-facing state lives behind this pointer (framework isolation).
    std::unique_ptr<Impl> m_impl;
};

} // namespace rock_hero::common::ui
