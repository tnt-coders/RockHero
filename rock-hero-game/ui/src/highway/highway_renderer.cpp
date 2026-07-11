#include "highway/highway_renderer.h"

#include "surface/bgfx_program.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <string_view>
#include <utility>

namespace rock_hero::game::ui
{

namespace
{

// The three fixed render views, executed in id order (plan 25 Phase 3 checkpoint).
constexpr bgfx::ViewId g_background_view = 0;
constexpr bgfx::ViewId g_board_view = 1;
constexpr bgfx::ViewId g_overlay_view = 2;

// Backdrop clear color behind the whole scene (0xRRGGBBAA).
constexpr std::uint32_t g_backdrop_color = 0x14141cff;

// Board content draws painter-ordered with alpha throughout (the reference's model), so one
// blended, depth-test-only state word covers the whole board view. No cull bits on purpose:
// content is camera-facing and the lefty mirror reflects world X, which would invert winding.
constexpr std::uint64_t g_blended_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                          BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA |
                                          BGFX_STATE_MSAA;

// How many fret slots the board face draws; charts cap at g_max_fret but the reference draws a
// fixed neck.
constexpr int g_face_fret_count = 24;

// Seconds a passed note takes to fade out after crossing the hit line.
constexpr double g_passed_fade_seconds = 0.15;

// Vertex with a world position and a packed ABGR color (color / color_fade programs).
struct PosColorVertex
{
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

// Vertex with a world position, packed ABGR color, and atlas coordinates (texture_tint / glyph).
struct PosColorUvVertex
{
    float x;
    float y;
    float z;
    std::uint32_t abgr;
    float u;
    float v;
};

// Lazily built layouts (bgfx::VertexLayout::begin needs the renderer type, post-init only).
[[nodiscard]] const bgfx::VertexLayout& posColorLayout()
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

[[nodiscard]] const bgfx::VertexLayout& posColorUvLayout()
{
    static const bgfx::VertexLayout g_layout = [] {
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        return layout;
    }();
    return g_layout;
}

// Vertex makers keep designated initialization (and the double->float narrowing) in one place,
// so the drawers below stay readable per corner.
[[nodiscard]] PosColorVertex makeVertex(
    const double x, const double y, const double z, const std::uint32_t abgr)
{
    return PosColorVertex{
        .x = static_cast<float>(x),
        .y = static_cast<float>(y),
        .z = static_cast<float>(z),
        .abgr = abgr,
    };
}

[[nodiscard]] PosColorUvVertex makeUvVertex(
    const double x, const double y, const double z, const std::uint32_t abgr, const float u,
    const float v)
{
    return PosColorUvVertex{
        .x = static_cast<float>(x),
        .y = static_cast<float>(y),
        .z = static_cast<float>(z),
        .abgr = abgr,
        .u = u,
        .v = v,
    };
}

// HighwayMat4 (row-major, clip = M * world) -> the float[16] bgfx expects (row-major storage
// under a row-vector convention): a pure transpose plus narrowing. Verified against the bx
// multiply and the D3D11 no-transpose uniform upload at the Phase 3 checkpoint.
[[nodiscard]] std::array<float, 16> toBgfxMatrix(const common::core::HighwayMat4& matrix)
{
    std::array<float, 16> out{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            out.at((row * 4) + column) = static_cast<float>(matrix.m.at((column * 4) + row));
        }
    }
    return out;
}

// Packs the palette's 0xAARRGGBB into the 0xAABBGGRR vertex color bgfx consumes, scaling alpha.
[[nodiscard]] std::uint32_t packAbgr(
    const common::ui::ArgbColor argb, const double alpha_scale = 1.0)
{
    const auto alpha = static_cast<std::uint32_t>(
        std::clamp(static_cast<double>((argb >> 24U) & 0xFFU) * alpha_scale, 0.0, 255.0));
    const std::uint32_t red = (argb >> 16U) & 0xFFU;
    const std::uint32_t green = (argb >> 8U) & 0xFFU;
    const std::uint32_t blue = argb & 0xFFU;
    return (alpha << 24U) | (blue << 16U) | (green << 8U) | red;
}

// Fret window of the hand position active at a given time; the reference four-fret nut window
// when the chart has none yet.
[[nodiscard]] std::pair<int, int> activeFhpFretLines(
    const common::core::HighwayViewState& state, const double seconds)
{
    int fret = 1;
    int width = 4;
    for (const common::core::HighwayFhpView& fhp : state.fret_hand_positions)
    {
        if (fhp.seconds > seconds)
        {
            break;
        }
        fret = fhp.fret;
        width = fhp.width;
    }
    return {fret - 1, fret + width - 1};
}

// Appends one quad (two triangles) to a CPU-side batch.
template <typename Vertex>
void pushQuad(
    std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, const Vertex& v0,
    const Vertex& v1, const Vertex& v2, const Vertex& v3)
{
    const auto base = static_cast<std::uint16_t>(vertices.size());
    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);
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

// Axis-aligned quad on the floor plane (y constant, spanning x and z).
void pushFloorQuad(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y, const double z0, const double z1, const std::uint32_t abgr)
{
    pushQuad(
        vertices,
        indices,
        makeVertex(x0, y, z0, abgr),
        makeVertex(x1, y, z0, abgr),
        makeVertex(x1, y, z1, abgr),
        makeVertex(x0, y, z1, abgr));
}

// Axis-aligned quad on the board face (z constant, spanning x and y).
void pushFaceQuad(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y0, const double y1, const double z, const std::uint32_t abgr)
{
    pushQuad(
        vertices,
        indices,
        makeVertex(x0, y0, z, abgr),
        makeVertex(x1, y0, z, abgr),
        makeVertex(x1, y1, z, abgr),
        makeVertex(x0, y1, z, abgr));
}

} // namespace

std::expected<HighwayRenderer, HighwayRendererError> HighwayRenderer::create(
    const core::GameResources& resources)
{
    HighwayRenderer renderer;

    const auto load_program = [&resources](const core::GameShaderProgram program)
        -> std::expected<UniqueBgfxHandle<bgfx::ProgramHandle>, HighwayRendererError> {
        const auto vertex_bytes = resources.shaderBytes(
            program, core::ShaderStage::Vertex, core::ShaderBackend::Direct3D11);
        const auto fragment_bytes = resources.shaderBytes(
            program, core::ShaderStage::Fragment, core::ShaderBackend::Direct3D11);
        if (!vertex_bytes.has_value() || !fragment_bytes.has_value())
        {
            return std::unexpected{HighwayRendererError{
                .code = HighwayRendererErrorCode::ResourceLoadFailed,
                .message = vertex_bytes.has_value() ? fragment_bytes.error().message
                                                    : vertex_bytes.error().message
            }};
        }

        UniqueBgfxHandle<bgfx::ProgramHandle> program_handle =
            createProgramFromBytes(*vertex_bytes, *fragment_bytes);
        if (!program_handle.isValid())
        {
            return std::unexpected{HighwayRendererError{
                .code = HighwayRendererErrorCode::ProgramCreationFailed,
                .message = "bgfx rejected or failed to link a compiled highway shader program"
            }};
        }
        return program_handle;
    };

    auto color = load_program(core::GameShaderProgram::Color);
    auto color_fade = load_program(core::GameShaderProgram::ColorFade);
    auto texture_tint = load_program(core::GameShaderProgram::TextureTint);
    auto glyph = load_program(core::GameShaderProgram::Glyph);
    if (!color || !color_fade || !texture_tint || !glyph)
    {
        const auto& failed = !color          ? color.error()
                             : !color_fade   ? color_fade.error()
                             : !texture_tint ? texture_tint.error()
                                             : glyph.error();
        return std::unexpected{failed};
    }
    renderer.m_color_program = std::move(*color);
    renderer.m_color_fade_program = std::move(*color_fade);
    renderer.m_texture_tint_program = std::move(*texture_tint);
    renderer.m_glyph_program = std::move(*glyph);

    renderer.m_fade_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_fade_params", bgfx::UniformType::Vec4)};
    renderer.m_atlas_sampler = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "s_atlas", bgfx::UniformType::Sampler)};

    renderer.m_atlases = makeHighwayAtlases();

    return renderer;
}

void HighwayRenderer::setViewState(common::core::HighwayViewState state)
{
    m_state = std::move(state);
    m_sustain_prefix_max = common::core::makeHighwaySustainPrefixMax(m_state.notes);
    m_camera.reset();
    rebuildBoardFace();
}

// The board face is the neck picture standing at the hit line: per-string colored string lines,
// fret lines, and inlay dots, all on the z = 0 plane. Rebuilt only when the chart changes.
void HighwayRenderer::rebuildBoardFace()
{
    m_face_vertices.reset();
    m_face_indices.reset();
    m_face_index_count = 0;
    if (m_state.string_count <= 0)
    {
        return;
    }

    const bool mirrored = m_state.options.mirrored;
    const bool invert = m_state.options.invert_string_order;
    const common::ui::StringColorPalette& palette = common::ui::charterClassicPalette();

    std::vector<PosColorVertex> vertices;
    std::vector<std::uint16_t> indices;

    const double x_start = common::core::highwayFretLineX(0, m_metrics, mirrored);
    const double x_end = common::core::highwayFretLineX(g_face_fret_count, m_metrics, mirrored);
    const auto [x_low, x_high] = std::minmax(x_start, x_end);
    const double y_top = common::core::highwayStringLaneY(
                             m_state.string_count, m_state.string_count, m_metrics, false) +
                         (m_metrics.string_distance * 0.5);

    // Fret lines: thin vertical quads across the string span; the nut is slightly heavier.
    for (int fret = 0; fret <= g_face_fret_count; ++fret)
    {
        const double x = common::core::highwayFretLineX(fret, m_metrics, mirrored);
        const double half = fret == 0 ? 0.03 : 0.015;
        pushFaceQuad(
            vertices, indices, x - half, x + half, 0.0, y_top, 0.0, packAbgr(0xFF9A9A9A, 0.65));
    }

    // Inlay dots at the traditional markers, drawn as small quads mid-face.
    const double inlay_y = y_top * 0.5;
    for (const int fret : {3, 5, 7, 9, 12, 15, 17, 19, 21, 24})
    {
        const double x = common::core::highwayNoteCenterX(fret, m_metrics, mirrored);
        const double half = m_metrics.first_fret_distance * 0.11;
        const double twelfth = (fret % 12) == 0 ? m_metrics.string_distance * 0.7 : 0.0;
        pushFaceQuad(
            vertices,
            indices,
            x - half,
            x + half,
            inlay_y - half - twelfth,
            inlay_y + half + twelfth,
            0.0,
            packAbgr(0xFFFFFFFF, 0.18));
    }

    // String lines: per-string colored horizontal quads, the shared palette's lane surface.
    for (int string = 1; string <= m_state.string_count; ++string)
    {
        const double y =
            common::core::highwayStringLaneY(string, m_state.string_count, m_metrics, invert);
        const common::ui::StringLaneStyle style{common::ui::stringLaneColor(
            string, m_state.string_count, palette)};
        pushFaceQuad(
            vertices, indices, x_low, x_high, y - 0.02, y + 0.02, 0.0, packAbgr(style.lane));
    }

    const bgfx::Memory* vertex_memory = bgfx::copy(
        vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(PosColorVertex)));
    const bgfx::Memory* index_memory = bgfx::copy(
        indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t)));
    m_face_vertices = UniqueBgfxHandle<bgfx::VertexBufferHandle>{bgfx::createVertexBuffer(
        vertex_memory, posColorLayout())};
    m_face_indices =
        UniqueBgfxHandle<bgfx::IndexBufferHandle>{bgfx::createIndexBuffer(index_memory)};
    m_face_index_count = static_cast<std::uint32_t>(indices.size());
}

void HighwayRenderer::draw(
    const double now_seconds, const double dt_seconds, const std::uint32_t width,
    const std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    // Camera: instantaneous targets from the chart, smoothed frame-rate independently.
    const common::core::HighwayCameraTarget target =
        common::core::makeHighwayCameraTarget(m_state, now_seconds, m_metrics);
    m_camera.advance(target, dt_seconds, m_metrics);
    const common::core::HighwayCameraPose pose = m_camera.pose(m_metrics);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const std::array<float, 16> board_matrix =
        toBgfxMatrix(common::core::makeHighwayWorldToClip(pose, aspect, m_metrics));
    const std::array<float, 16> background_matrix = toBgfxMatrix(
        common::core::makeHighwayBackgroundWorldToClip(pose, aspect, now_seconds, m_metrics));

    // Per-frame view setup, re-asserted from the current backbuffer size (checkpoint trap 2).
    const auto width16 = static_cast<std::uint16_t>(width);
    const auto height16 = static_cast<std::uint16_t>(height);
    for (const bgfx::ViewId view : {g_background_view, g_board_view, g_overlay_view})
    {
        bgfx::setViewRect(view, 0, 0, width16, height16);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    }
    bgfx::setViewClear(
        g_background_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, g_backdrop_color, 1.0F, 0);
    bgfx::setViewClear(g_board_view, BGFX_CLEAR_DEPTH, 0, 1.0F, 0);
    bgfx::setViewClear(g_overlay_view, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(g_background_view, background_matrix.data(), nullptr);
    bgfx::setViewTransform(g_board_view, board_matrix.data(), nullptr);
    // Clear-bearing views execute only when they have items; touch them so the clears always run.
    bgfx::touch(g_background_view);
    bgfx::touch(g_board_view);

    const double scroll = m_scroll_speed;
    const bool mirrored = m_state.options.mirrored;
    const bool invert = m_state.options.invert_string_order;
    const double span_end_seconds = now_seconds + (m_metrics.visibility_window_seconds * scroll);
    const double span_start_seconds = now_seconds - g_passed_fade_seconds;
    const common::ui::StringColorPalette& palette = common::ui::charterClassicPalette();

    const auto time_to_z = [&](const double seconds) {
        return common::core::highwayTimeToZ(seconds - now_seconds, scroll, m_metrics);
    };

    // Submits a CPU-built batch through the transient buffers; drops the batch (with one
    // process-lifetime warning) if the transient budget is ever exceeded — a bug signal, not a
    // runtime path (the defaults hold >6x headroom over the worst-case highway frame).
    const auto submit_batch = [this]<typename Vertex>(
                                  const std::vector<Vertex>& vertices,
                                  const std::vector<std::uint16_t>& indices,
                                  const bgfx::VertexLayout& layout,
                                  const bgfx::ProgramHandle program,
                                  const bgfx::TextureHandle* texture) {
        if (vertices.empty())
        {
            return;
        }
        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};
        if (!bgfx::allocTransientBuffers(
                &tvb,
                layout,
                static_cast<std::uint32_t>(vertices.size()),
                &tib,
                static_cast<std::uint32_t>(indices.size())))
        {
            if (!m_reported_transient_drop)
            {
                m_reported_transient_drop = true;
                RH_LOG_WARNING(
                    "game.highway",
                    "transient buffer budget exceeded; dropping a batch (vertices={})",
                    vertices.size());
            }
            return;
        }
        std::memcpy(tvb.data, vertices.data(), vertices.size() * sizeof(Vertex));
        std::memcpy(tib.data, indices.data(), indices.size() * sizeof(std::uint16_t));
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        if (texture != nullptr)
        {
            bgfx::setTexture(0, m_atlas_sampler.get(), *texture);
        }
        bgfx::setState(g_blended_state);
        bgfx::submit(g_board_view, program);
    };

    // --- Beat and measure bars (distance-faded floor quads, clipped to the hand window). ---
    {
        const std::array<float, 4> fade_params{
            static_cast<float>(common::core::highwayTimeToZ(0.05, scroll, m_metrics)),
            static_cast<float>(common::core::highwayTimeToZ(0.25, scroll, m_metrics)),
            0.0F,
            0.0F
        };
        bgfx::setUniform(m_fade_params.get(), fade_params.data());

        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (const common::core::HighwayBeatView& beat : m_state.beats)
        {
            if (beat.seconds < now_seconds - 0.2 || beat.seconds > span_end_seconds)
            {
                continue;
            }
            const auto [low_line, high_line] = activeFhpFretLines(m_state, beat.seconds);
            // Named operands: std::minmax over prvalues returns references into destroyed
            // temporaries (the Phase 3 CI dangling-reference finding).
            const double low_x = common::core::highwayFretLineX(low_line, m_metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, m_metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const double z = time_to_z(beat.seconds);
            const double half = beat.measure_downbeat ? 0.10 : 0.045;
            const double alpha = beat.measure_downbeat ? 0.62 : 0.38;
            pushFloorQuad(
                vertices, indices, x0, x1, 0.015, z - half, z + half, packAbgr(0xFFFFFFFF, alpha));
        }
        submit_batch(vertices, indices, posColorLayout(), m_color_fade_program.get(), nullptr);
    }

    // --- FHP lane highlight: the lit runway under the active hand window. ---
    {
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        const auto [low_line, high_line] = activeFhpFretLines(m_state, now_seconds);
        const double low_x = common::core::highwayFretLineX(low_line, m_metrics, mirrored);
        const double high_x = common::core::highwayFretLineX(high_line, m_metrics, mirrored);
        const auto [x0, x1] = std::minmax(low_x, high_x);
        pushFloorQuad(
            vertices,
            indices,
            x0,
            x1,
            0.008,
            common::core::highwayTimeToZ(-g_passed_fade_seconds, scroll, m_metrics),
            common::core::highwayTimeToZ(
                m_metrics.visibility_window_seconds * scroll, scroll, m_metrics),
            packAbgr(0xFFFFFFFF, 0.10));
        submit_batch(vertices, indices, posColorLayout(), m_color_program.get(), nullptr);
    }

    // --- Notes: shadows, sustain rails, open bars, then heads sorted far-to-near. ---
    const auto [first_note, last_note] = common::core::highwayVisibleNoteRange(
        m_state.notes, m_sustain_prefix_max, span_start_seconds, span_end_seconds);

    std::vector<PosColorVertex> shadow_vertices;
    std::vector<std::uint16_t> shadow_indices;
    std::vector<PosColorVertex> rail_vertices;
    std::vector<std::uint16_t> rail_indices;
    std::vector<PosColorVertex> open_vertices;
    std::vector<std::uint16_t> open_indices;
    std::vector<PosColorUvVertex> head_vertices;
    std::vector<std::uint16_t> head_indices;

    std::vector<std::size_t> visible;
    visible.reserve(last_note - first_note);
    for (std::size_t index = first_note; index < last_note; ++index)
    {
        const common::core::HighwayNoteView& note = m_state.notes[index];
        if (note.start_seconds <= span_end_seconds && note.end_seconds >= span_start_seconds)
        {
            visible.push_back(index);
        }
    }
    // Far-to-near: later onsets draw first so nearer content alpha-composites over them.
    std::ranges::sort(visible, [this](const std::size_t lhs, const std::size_t rhs) {
        return m_state.notes[lhs].start_seconds > m_state.notes[rhs].start_seconds;
    });

    const std::array<float, 4> head_cell = m_atlases.head_layout.cellRect(g_head_cell_standard);
    const double head_half_w = m_metrics.note_half_width;
    const double head_half_h = m_metrics.string_distance * 0.45;

    for (const std::size_t index : visible)
    {
        const common::core::HighwayNoteView& note = m_state.notes[index];
        const double lane_y =
            common::core::highwayStringLaneY(note.string, m_state.string_count, m_metrics, invert);
        const common::ui::StringLaneStyle style{common::ui::stringLaneColor(
            note.string, m_state.string_count, palette)};
        const double fade =
            note.start_seconds >= now_seconds
                ? 1.0
                : std::max(0.0, 1.0 - ((now_seconds - note.start_seconds) / g_passed_fade_seconds));

        // Sustain rail: from the hit line (while sounding) or the onset to the sustain end.
        if (note.end_seconds > note.start_seconds && note.end_seconds > now_seconds)
        {
            const double z0 = time_to_z(std::max(note.start_seconds, now_seconds));
            const double z1 = time_to_z(std::min(note.end_seconds, span_end_seconds));
            if (z1 > z0)
            {
                const double x =
                    note.fret > 0 ? common::core::highwayNoteCenterX(note.fret, m_metrics, mirrored)
                                  : (common::core::highwayFretLineX(
                                         activeFhpFretLines(m_state, note.start_seconds).first,
                                         m_metrics,
                                         mirrored) +
                                     common::core::highwayFretLineX(
                                         activeFhpFretLines(m_state, note.start_seconds).second,
                                         m_metrics,
                                         mirrored)) /
                                        2.0;
                const std::uint32_t tail_color = packAbgr(style.tail, 0.75);
                pushQuad(
                    rail_vertices,
                    rail_indices,
                    makeVertex(x - m_metrics.tail_half_width, lane_y, z0, tail_color),
                    makeVertex(x + m_metrics.tail_half_width, lane_y, z0, tail_color),
                    makeVertex(x + m_metrics.tail_half_width, lane_y, z1, tail_color),
                    makeVertex(x - m_metrics.tail_half_width, lane_y, z1, tail_color));
            }
        }

        if (fade <= 0.0)
        {
            continue;
        }
        const double z = time_to_z(note.start_seconds);

        if (note.fret == 0)
        {
            // Open string: a wide bar spanning the hand window active at the note's time.
            const auto [low_line, high_line] = activeFhpFretLines(m_state, note.start_seconds);
            const double low_x = common::core::highwayFretLineX(low_line, m_metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, m_metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const std::uint32_t bar_color = packAbgr(style.inner, fade);
            pushQuad(
                open_vertices,
                open_indices,
                makeVertex(x0, lane_y - m_metrics.tail_half_width, z, bar_color),
                makeVertex(x1, lane_y - m_metrics.tail_half_width, z, bar_color),
                makeVertex(x1, lane_y + m_metrics.tail_half_width, z, bar_color),
                makeVertex(x0, lane_y + m_metrics.tail_half_width, z, bar_color));
            continue;
        }

        const double x = common::core::highwayNoteCenterX(note.fret, m_metrics, mirrored);

        // Note shadow: a dark ellipse on the floor under the note (load-bearing for depth
        // perception), built as a small triangle fan.
        {
            constexpr int g_shadow_segments = 10;
            const double radius_x = head_half_w * 0.85;
            const double radius_z = 0.13;
            const std::uint32_t shadow_color = packAbgr(0xFF000000, 0.42 * fade);
            const auto center_index = static_cast<std::uint16_t>(shadow_vertices.size());
            shadow_vertices.push_back(makeVertex(x, 0.02, z, shadow_color));
            for (int segment = 0; segment <= g_shadow_segments; ++segment)
            {
                const double angle =
                    2.0 * std::numbers::pi * segment / static_cast<double>(g_shadow_segments);
                shadow_vertices.push_back(makeVertex(
                    x + (std::cos(angle) * radius_x),
                    0.02,
                    z + (std::sin(angle) * radius_z),
                    shadow_color));
            }
            for (int segment = 0; segment < g_shadow_segments; ++segment)
            {
                shadow_indices.push_back(center_index);
                shadow_indices.push_back(static_cast<std::uint16_t>(center_index + 1 + segment));
                shadow_indices.push_back(static_cast<std::uint16_t>(center_index + 2 + segment));
            }
        }

        // Rolling flip: single notes rotate around their travel axis during the last second;
        // chord notes (a same-onset neighbor) land flat.
        const bool in_chord =
            (index > 0 &&
             std::abs(m_state.notes[index - 1].start_seconds - note.start_seconds) < 1.0e-4) ||
            (index + 1 < m_state.notes.size() &&
             std::abs(m_state.notes[index + 1].start_seconds - note.start_seconds) < 1.0e-4);
        const double rotation =
            in_chord ? 0.0
                     : std::clamp(
                           -std::numbers::pi * (note.start_seconds - now_seconds - 0.1),
                           -std::numbers::pi / 2.0,
                           0.0);
        const double cos_r = std::cos(rotation);
        const double sin_r = std::sin(rotation);
        const std::uint32_t tint =
            packAbgr(common::ui::stringLaneColor(note.string, m_state.string_count, palette), fade);

        const auto corner = [&](const double dx, const double dy, const float u, const float v) {
            return makeUvVertex(
                x + (dx * cos_r) - (dy * sin_r),
                lane_y + (dx * sin_r) + (dy * cos_r),
                z,
                tint,
                u,
                v);
        };
        pushQuad(
            head_vertices,
            head_indices,
            corner(-head_half_w, -head_half_h, head_cell[0], head_cell[3]),
            corner(head_half_w, -head_half_h, head_cell[2], head_cell[3]),
            corner(head_half_w, head_half_h, head_cell[2], head_cell[1]),
            corner(-head_half_w, head_half_h, head_cell[0], head_cell[1]));
    }

    submit_batch(shadow_vertices, shadow_indices, posColorLayout(), m_color_program.get(), nullptr);
    submit_batch(rail_vertices, rail_indices, posColorLayout(), m_color_program.get(), nullptr);
    submit_batch(open_vertices, open_indices, posColorLayout(), m_color_program.get(), nullptr);
    const bgfx::TextureHandle heads_texture = m_atlases.heads.get();
    submit_batch(
        head_vertices,
        head_indices,
        posColorUvLayout(),
        m_texture_tint_program.get(),
        &heads_texture);

    // --- Board face (retained): strings, frets, inlays drawn over passing content. ---
    if (m_face_index_count > 0 && m_face_vertices.isValid() && m_face_indices.isValid())
    {
        bgfx::setVertexBuffer(0, m_face_vertices.get());
        bgfx::setIndexBuffer(m_face_indices.get(), 0, m_face_index_count);
        bgfx::setState(g_blended_state);
        bgfx::submit(g_board_view, m_color_program.get());
    }

    // --- Fret numbers and section labels through the glyph atlas. ---
    {
        std::vector<PosColorUvVertex> glyph_vertices;
        std::vector<std::uint16_t> glyph_indices;

        const auto push_text = [&](const std::string_view text,
                                   const double left_x,
                                   const double baseline_y,
                                   const double z,
                                   const double glyph_height,
                                   const std::uint32_t color) {
            const double advance = glyph_height * 0.62;
            double pen_x = left_x;
            for (const char character : text)
            {
                const std::optional<int> cell = highwayGlyphCellIndex(character);
                if (cell.has_value())
                {
                    const std::array<float, 4> rect = m_atlases.glyph_layout.cellRect(*cell);
                    pushQuad(
                        glyph_vertices,
                        glyph_indices,
                        makeUvVertex(pen_x, baseline_y, z, color, rect[0], rect[3]),
                        makeUvVertex(pen_x + glyph_height, baseline_y, z, color, rect[2], rect[3]),
                        makeUvVertex(
                            pen_x + glyph_height,
                            baseline_y + glyph_height,
                            z,
                            color,
                            rect[2],
                            rect[1]),
                        makeUvVertex(pen_x, baseline_y + glyph_height, z, color, rect[0], rect[1]));
                }
                pen_x += advance;
            }
            return pen_x - left_x;
        };

        // Fret numbers along the bottom of the face (defect 3 fix: atlas, not per-frame text).
        if (m_state.string_count > 0)
        {
            constexpr double g_number_height = 0.30;
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                const std::string label = std::to_string(fret);
                const double text_width =
                    g_number_height * 0.62 * static_cast<double>(label.size());
                const double x = common::core::highwayNoteCenterX(fret, m_metrics, mirrored) -
                                 (text_width / 2.0);
                (void)push_text(label, x, -0.42, 0.0, g_number_height, packAbgr(0xFFFFFFFF, 0.55));
            }
        }

        // Section labels floating above the board at their arrival time.
        const double section_y =
            (static_cast<double>(std::max(m_state.string_count, 1)) * m_metrics.string_distance) +
            (m_metrics.string_distance * 2.0);
        for (const common::core::HighwaySectionView& section : m_state.sections)
        {
            if (section.seconds < now_seconds - 0.5 || section.seconds > span_end_seconds)
            {
                continue;
            }
            std::string label = section.type;
            std::ranges::transform(label, label.begin(), [](const char c) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            });
            const auto [low_line, high_line] = activeFhpFretLines(m_state, section.seconds);
            const double x = common::core::highwayFretLineX(low_line, m_metrics, mirrored);
            (void)push_text(
                label,
                std::min(x, common::core::highwayFretLineX(high_line, m_metrics, mirrored)),
                section_y,
                time_to_z(section.seconds),
                0.5,
                packAbgr(0xFFFFFFFF, 0.85));
        }

        const bgfx::TextureHandle glyph_texture = m_atlases.glyphs.get();
        submit_batch(
            glyph_vertices,
            glyph_indices,
            posColorUvLayout(),
            m_glyph_program.get(),
            &glyph_texture);
    }
}

} // namespace rock_hero::game::ui
