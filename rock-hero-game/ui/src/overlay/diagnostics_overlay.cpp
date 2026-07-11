#include "overlay/diagnostics_overlay.h"

#include "surface/bgfx_program.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace rock_hero::game::ui
{

namespace
{

// The overlay renders on the reserved screen-space view (plan 25 Phase 3's view table).
constexpr bgfx::ViewId g_overlay_view = 2;

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

// Screen-space vertex for the overlay quads.
struct OverlayVertex
{
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

[[nodiscard]] const bgfx::VertexLayout& overlayLayout()
{
    static const bgfx::VertexLayout g_layout = [] {
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        return layout;
    }();
    return g_layout;
}

// Appends one screen-space rectangle to the batch.
void pushRect(
    std::vector<OverlayVertex>& vertices, std::vector<std::uint16_t>& indices, const float x0,
    const float y0, const float x1, const float y1, const std::uint32_t abgr)
{
    const auto base = static_cast<std::uint16_t>(vertices.size());
    vertices.push_back(OverlayVertex{.x = x0, .y = y0, .z = 0.0F, .abgr = abgr});
    vertices.push_back(OverlayVertex{.x = x1, .y = y0, .z = 0.0F, .abgr = abgr});
    vertices.push_back(OverlayVertex{.x = x1, .y = y1, .z = 0.0F, .abgr = abgr});
    vertices.push_back(OverlayVertex{.x = x0, .y = y1, .z = 0.0F, .abgr = abgr});
    for (const std::uint16_t offset :
         {std::uint16_t{0},
          std::uint16_t{1},
          std::uint16_t{2},
          std::uint16_t{0},
          std::uint16_t{2},
          std::uint16_t{3}})
    {
        indices.push_back(static_cast<std::uint16_t>(base + offset));
    }
}

} // namespace

std::optional<DiagnosticsOverlay> DiagnosticsOverlay::create(
    const std::span<const std::byte> color_vertex_shader,
    const std::span<const std::byte> color_fragment_shader)
{
    DiagnosticsOverlay overlay;
    overlay.m_color_program = createProgramFromBytes(color_vertex_shader, color_fragment_shader);
    if (!overlay.m_color_program.isValid())
    {
        return std::nullopt;
    }
    return overlay;
}

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

void DiagnosticsOverlay::draw(const std::uint32_t width, const std::uint32_t height)
{
    if (width == 0 || height == 0 || m_sample_count == 0)
    {
        return;
    }

    // Pixel-space orthographic transform in bgfx's row-vector layout: x right, y down from the
    // top-left corner, matching the debug-text coordinate frame above the graph.
    const auto width_f = static_cast<float>(width);
    const auto height_f = static_cast<float>(height);
    const std::array<float, 16> ortho{
        2.0F / width_f,
        0.0F,
        0.0F,
        0.0F, //
        0.0F,
        -2.0F / height_f,
        0.0F,
        0.0F, //
        0.0F,
        0.0F,
        1.0F,
        0.0F, //
        -1.0F,
        1.0F,
        0.0F,
        1.0F, //
    };
    bgfx::setViewTransform(g_overlay_view, ortho.data(), nullptr);

    std::vector<OverlayVertex> vertices;
    std::vector<std::uint16_t> indices;

    const auto bar_count = static_cast<float>(m_frame_deltas_ms.size());
    const float panel_right = g_panel_left + (bar_count * g_bar_width) + 8.0F;
    const float panel_bottom = g_panel_top + g_panel_height;
    pushRect(
        vertices,
        indices,
        g_panel_left - 4.0F,
        g_panel_top - 4.0F,
        panel_right,
        panel_bottom + 4.0F,
        g_panel_color);

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
        pushRect(
            vertices,
            indices,
            x0,
            panel_bottom - bar_height,
            x0 + (g_bar_width - 0.5F),
            panel_bottom,
            color);
    }

    // The 60Hz budget line across the panel.
    const float reference_y = panel_bottom - ((g_reference_ms / g_full_scale_ms) * g_panel_height);
    pushRect(
        vertices,
        indices,
        g_panel_left,
        reference_y - 0.5F,
        g_panel_left + (bar_count * g_bar_width),
        reference_y + 0.5F,
        g_reference_color);

    bgfx::TransientVertexBuffer tvb{};
    bgfx::TransientIndexBuffer tib{};
    if (!bgfx::allocTransientBuffers(
            &tvb,
            overlayLayout(),
            static_cast<std::uint32_t>(vertices.size()),
            &tib,
            static_cast<std::uint32_t>(indices.size())))
    {
        return;
    }
    std::memcpy(tvb.data, vertices.data(), vertices.size() * sizeof(OverlayVertex));
    std::memcpy(tib.data, indices.data(), indices.size() * sizeof(std::uint16_t));
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(g_overlay_view, m_color_program.get());
}

} // namespace rock_hero::game::ui
