/*!
\file highway_renderer.h
\brief bgfx renderer for the 3D note highway: views, programs, atlases, and drawers.
*/

#pragma once

#include "highway/highway_atlas.h"
#include "surface/bgfx_handle.h"

#include <bgfx/bgfx.h>
#include <cstdint>
#include <expected>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/game/core/resources/game_resources.h>
#include <string>
#include <vector>

namespace rock_hero::game::ui
{

/*! \brief Stable reasons the highway renderer can fail to come up. */
enum class HighwayRendererErrorCode : std::uint8_t
{
    /*! \brief A compiled shader binary could not be resolved or read from the resource pack. */
    ResourceLoadFailed,

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

/*!
\brief Renders the note highway from the shared headless scene model.

Owns every bgfx resource the highway needs (programs, uniforms, atlases, retained board
geometry) through RAII handles, so the instance must be destroyed before the RenderDevice that
owns bgfx shutdown — the shell guarantees that structurally by declaration order. All methods
run on the render thread (the main thread under loop model L2).

The renderer is a pure consumer: it never derives song time itself and draws exactly what the
view state plus the per-frame time arguments describe (plan 25's event feed and technique
phases extend the drawers, not this contract).
*/
class HighwayRenderer
{
public:
    /*!
    \brief Loads the highway's shader programs through the resource pack and builds the atlases.

    \param resources Resolver over the deployed resource-pack tree.
    \return The renderer, or a typed error naming the failing resource or program.
    */
    [[nodiscard]] static std::expected<HighwayRenderer, HighwayRendererError> create(
        const core::GameResources& resources);

    /*!
    \brief Replaces the chart content and rebuilds the retained board geometry.

    \param state Seconds-resolved highway content from the shared projection.
    */
    void setViewState(common::core::HighwayViewState state);

    /*!
    \brief Encodes one frame of the highway into the render views.

    \param now_seconds Playback song time for this frame (from the frame clock).
    \param dt_seconds Frame delta in seconds driving camera smoothing.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void draw(double now_seconds, double dt_seconds, std::uint32_t width, std::uint32_t height);

private:
    HighwayRenderer() = default;

    // Rebuilds the retained board-face buffers (strings, frets, inlays) from the current state.
    void rebuildBoardFace();

    // Shader programs, one per GameShaderProgram enumerator the highway uses.
    UniqueBgfxHandle<bgfx::ProgramHandle> m_color_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> m_color_fade_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> m_texture_tint_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> m_glyph_program;

    // Custom uniforms (predefined ones like u_modelViewProj are never created by hand).
    UniqueBgfxHandle<bgfx::UniformHandle> m_fade_params;
    UniqueBgfxHandle<bgfx::UniformHandle> m_atlas_sampler;

    HighwayAtlases m_atlases;

    // Retained board-face geometry; rebuilt on chart load, streamed content uses transients.
    UniqueBgfxHandle<bgfx::VertexBufferHandle> m_face_vertices;
    UniqueBgfxHandle<bgfx::IndexBufferHandle> m_face_indices;
    std::uint32_t m_face_index_count{0};

    common::core::HighwayViewState m_state;
    std::vector<double> m_sustain_prefix_max;
    common::core::HighwayMetrics m_metrics;
    common::core::HighwayCamera m_camera;

    // Player scroll speed; a free setting later (25-Q3), constant 1.0 for the skeleton.
    double m_scroll_speed{1.0};

    // One warning per process when a transient batch is dropped (budget exceeded is a bug
    // signal, not an expected runtime path).
    bool m_reported_transient_drop{false};
};

} // namespace rock_hero::game::ui
