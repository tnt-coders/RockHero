/*!
\file diagnostics_overlay.h
\brief Screen-space dev-diagnostics overlay content: the frame-time graph (plan 20 Phase 4).
*/

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <vector>

namespace rock_hero::game::ui
{

/*!
\brief Builds the diagnostics frame-time graph as overlay rectangles.

Pure presentation data: the shell records frame deltas here and hands the built rectangles to
the shared highway renderer's overlay pass — no GPU types on this side. The numeric readouts
beside the graph stay on bgfx debug text (printed by the shell). Reserved diagnostics panels
(detection confidence, hit-window visualization) join this builder as plans 22/24/25 land their
data feeds.
*/
class DiagnosticsOverlay
{
public:
    /*!
    \brief Records one frame delta into the rolling graph history.
    \param frame_delta Elapsed time of the frame being recorded.
    */
    void recordFrameDelta(std::chrono::nanoseconds frame_delta);

    /*!
    \brief Builds this frame's overlay rectangles (panel, bars, budget line).
    \return Rectangles in pixel coordinates, anchored below the debug-text readouts.
    */
    [[nodiscard]] std::vector<common::ui::HighwayOverlayRect> buildRects() const;

private:
    // Rolling frame-delta history in milliseconds; one bar per sample, oldest overwritten.
    std::array<float, 240> m_frame_deltas_ms{};
    std::size_t m_next_sample{0};
    std::size_t m_sample_count{0};
};

} // namespace rock_hero::game::ui
