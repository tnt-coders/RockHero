#include "overlay/diagnostics_overlay.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rock_hero::game::ui
{

namespace
{

// Graph panel geometry in pixels. The panel sits below the debug-text readouts (16px rows).
constexpr float g_panel_left = 16.0F;
constexpr float g_panel_top = 88.0F;
constexpr float g_panel_height = 96.0F;
constexpr float g_bar_width = 2.0F;

// Frame delta that maps to the full panel height; 25ms keeps a 60Hz frame at ~2/3 height.
constexpr float g_full_scale_ms = 25.0F;

// Reference line at the 60Hz frame budget.
constexpr float g_reference_ms = 1000.0F / 60.0F;

// Packed ABGR colors for the graph elements.
constexpr std::uint32_t g_panel_color = 0x99000000;     // translucent black backdrop
constexpr std::uint32_t g_bar_ok_color = 0xFF40C040;    // green: comfortably paced
constexpr std::uint32_t g_bar_warn_color = 0xFF30C0E0;  // amber: above 12ms
constexpr std::uint32_t g_bar_over_color = 0xFF4040E0;  // red: above 20ms (visible hitch)
constexpr std::uint32_t g_reference_color = 0x59FFFFFF; // faint 60Hz budget line

} // namespace

void DiagnosticsOverlay::recordFrameDelta(const std::chrono::nanoseconds frame_delta)
{
    if (frame_delta <= std::chrono::nanoseconds{0})
    {
        return;
    }
    m_frame_deltas_ms.at(m_next_sample) =
        static_cast<float>(static_cast<double>(frame_delta.count()) / 1.0e6);
    m_next_sample = (m_next_sample + 1) % m_frame_deltas_ms.size();
    m_sample_count = std::min(m_sample_count + 1, m_frame_deltas_ms.size());
}

std::vector<common::ui::HighwayOverlayRect> DiagnosticsOverlay::buildRects() const
{
    std::vector<common::ui::HighwayOverlayRect> rects;
    if (m_sample_count == 0)
    {
        return rects;
    }
    rects.reserve(m_sample_count + 2);

    const auto bar_count = static_cast<float>(m_frame_deltas_ms.size());
    const float panel_right = g_panel_left + (bar_count * g_bar_width) + 8.0F;
    const float panel_bottom = g_panel_top + g_panel_height;
    rects.push_back(
        common::ui::HighwayOverlayRect{
            .left = g_panel_left - 4.0F,
            .top = g_panel_top - 4.0F,
            .right = panel_right,
            .bottom = panel_bottom + 4.0F,
            .abgr = g_panel_color,
        });

    // Bars, oldest to newest left to right; the write cursor points at the oldest sample.
    for (std::size_t offset = 0; offset < m_sample_count; ++offset)
    {
        const std::size_t index =
            (m_next_sample + (m_frame_deltas_ms.size() - m_sample_count) + offset) %
            m_frame_deltas_ms.size();
        const float delta_ms = m_frame_deltas_ms.at(index);
        const float clamped = std::clamp(delta_ms, 0.0F, g_full_scale_ms);
        const float bar_height = (clamped / g_full_scale_ms) * g_panel_height;
        const float x0 = g_panel_left + (static_cast<float>(offset) * g_bar_width);
        const std::uint32_t color = delta_ms >= 20.0F   ? g_bar_over_color
                                    : delta_ms >= 12.0F ? g_bar_warn_color
                                                        : g_bar_ok_color;
        rects.push_back(
            common::ui::HighwayOverlayRect{
                .left = x0,
                .top = panel_bottom - bar_height,
                .right = x0 + (g_bar_width - 0.5F),
                .bottom = panel_bottom,
                .abgr = color,
            });
    }

    // The 60Hz budget line across the panel.
    const float reference_y = panel_bottom - ((g_reference_ms / g_full_scale_ms) * g_panel_height);
    rects.push_back(
        common::ui::HighwayOverlayRect{
            .left = g_panel_left,
            .top = reference_y - 0.5F,
            .right = g_panel_left + (bar_count * g_bar_width),
            .bottom = reference_y + 0.5F,
            .abgr = g_reference_color,
        });

    return rects;
}

} // namespace rock_hero::game::ui
