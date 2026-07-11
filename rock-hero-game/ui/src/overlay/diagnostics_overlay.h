/*!
\file diagnostics_overlay.h
\brief Screen-space dev-diagnostics overlay: the frame-time graph on the overlay view.
*/

#pragma once

#include "surface/bgfx_handle.h"

#include <array>
#include <bgfx/bgfx.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rock_hero::game::ui
{

/*!
\brief Draws the diagnostics frame-time graph over the scene (plan 20 Phase 4).

Owns its own color program so the overlay never couples to the highway renderer's resources; the
numeric readouts beside the graph stay on bgfx debug text (printed by the shell). Reserved
diagnostics panels (detection confidence, hit-window visualization) join this drawer as plans
22/24/25 land their data feeds.

Must be destroyed before the RenderDevice, like every bgfx-handle owner (declaration order in
the shell).
*/
class DiagnosticsOverlay
{
public:
    /*!
    \brief Links the overlay's color program from compiled stage binaries.

    \param color_vertex_shader Compiled color-program vertex binary.
    \param color_fragment_shader Compiled color-program fragment binary.
    \return The overlay, or empty when the program does not link (overlay disabled, game runs on).
    */
    [[nodiscard]] static std::optional<DiagnosticsOverlay> create(
        std::span<const std::byte> color_vertex_shader,
        std::span<const std::byte> color_fragment_shader);

    /*!
    \brief Records one frame delta into the rolling graph history.
    \param frame_delta Elapsed time of the frame being recorded.
    */
    void recordFrameDelta(std::chrono::nanoseconds frame_delta);

    /*!
    \brief Encodes the frame-time graph into the overlay view for this frame.
    \param width Backbuffer width in pixels.
    \param height Backbuffer height in pixels.
    */
    void draw(std::uint32_t width, std::uint32_t height);

private:
    DiagnosticsOverlay() = default;

    UniqueBgfxHandle<bgfx::ProgramHandle> m_color_program;

    // Rolling frame-delta history in milliseconds; one bar per sample, oldest overwritten.
    std::array<float, 240> m_frame_deltas_ms{};
    std::size_t m_next_sample{0};
    std::size_t m_sample_count{0};
};

} // namespace rock_hero::game::ui
