#include "highway/bgfx_program.h"
#include "highway/highway_atlas.h"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// The three fixed render views, executed in id order (plan 25 Phase 3 checkpoint). View 0 is
// shared with RenderDevice's backstop (its g_default_view): the device touches it so a frame
// with no scene still clears and presents, and this renderer's per-frame setViewClear wins
// whenever a scene draws — nothing else may reconfigure view 0.
constexpr bgfx::ViewId g_background_view = 0;
constexpr bgfx::ViewId g_board_view = 1;
constexpr bgfx::ViewId g_overlay_view = 2;

// Backdrop clear color behind the whole scene (0xRRGGBBAA); the reference clears to black.
constexpr std::uint32_t g_backdrop_color = 0x000000ff;

// Reference board-furniture colors (0xAARRGGBB), source-verified 2026-07-11.
constexpr ArgbColor g_beat_bar_color = 0xFF0F3B5E;        // beat and measure bars alike
constexpr ArgbColor g_fhp_lane_color = 0x402590E8;        // lit runway gap
constexpr ArgbColor g_fhp_lane_dotted_color = 0x40185C94; // darker gap on inlay-dotted frets
constexpr ArgbColor g_lane_border_color = 0x0007928F;     // per-fret runway ribbons (alpha varies)
constexpr ArgbColor g_fret_inactive_color = 0xFF202020;
constexpr ArgbColor g_fret_active_color = 0xFFC0C0C0;
constexpr ArgbColor g_fret_highlight_color = 0xFFFFA000;

// Hand-window activity horizon for the active fret state, and the fret hit-flash length.
constexpr double g_fret_active_horizon_seconds = 0.5;
constexpr double g_fret_flash_seconds = 0.1;

// Anticipation ring window before a note lands (reference: 500 ms).
constexpr double g_anticipation_seconds = 0.5;

// Board content draws painter-ordered with alpha throughout (the reference's model), so one
// blended, depth-test-only state word covers the whole board view. No cull bits on purpose:
// content is camera-facing and the lefty mirror reflects world X, which would invert winding.
constexpr std::uint64_t g_blended_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                          BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA |
                                          BGFX_STATE_MSAA;

// Overlay content is screen-space and never depth-tested.
constexpr std::uint64_t g_overlay_state =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;

// JUCE premultiplies real-alpha PNGs at decode, so textures with genuine transparency (the
// inlay skin) carry rgb*a texels and must composite with the premultiplied blend — straight
// SRC_ALPHA would apply alpha twice and darken every anti-aliased edge. The channel-scheme and
// glyph atlases are immune (opaque alpha / alpha-only sampling).
constexpr std::uint64_t g_premultiplied_state =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA) | BGFX_STATE_MSAA;

// How many fret slots the board face draws; charts cap at g_max_fret but the reference draws a
// fixed neck.
constexpr int g_face_fret_count = 24;

// Seconds a passed note takes to fade out after crossing the hit line.
constexpr double g_passed_fade_seconds = 0.15;

// Open-note bar cross-section (reference OpenNoteModel): a thin hexagonal prism spanning the
// hand window, half-thickness 0.04 at the ends bulging to 0.05 at the center station, squashed
// to a tenth of that in Z. An earlier flat slab at tail width read over 3x too tall.
constexpr double g_open_note_end_half_thickness = 0.04;
constexpr double g_open_note_middle_half_thickness = 0.05;
constexpr double g_open_note_z_squash = 0.1;
constexpr int g_open_note_segments = 6;

// Sustain tails are three-band ribbons in the reference: solid edge strips around an inner band
// at 192/255 alpha. Fretted tails split the tail width quarter/half/quarter; open tails span
// the hand window inset by a margin, with edge bands of the same width.
constexpr double g_tail_inner_alpha = 192.0 / 255.0;
constexpr double g_open_tail_margin = 0.2;

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

// Lazily built layouts. begin()'s default RendererType::Noop merely selects an attribute-size
// table shared with D3D11 (it never consults the live context), so building these is safe at
// any time; laziness just keeps the construction in one place.
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
[[nodiscard]] std::uint32_t packAbgr(const ArgbColor argb, const double alpha_scale = 1.0)
{
    const auto alpha = static_cast<std::uint32_t>(
        std::clamp(static_cast<double>((argb >> 24U) & 0xFFU) * alpha_scale, 0.0, 255.0));
    const std::uint32_t red = (argb >> 16U) & 0xFFU;
    const std::uint32_t green = (argb >> 8U) & 0xFFU;
    const std::uint32_t blue = argb & 0xFFU;
    return (alpha << 24U) | (blue << 16U) | (green << 8U) | red;
}

// Linear blend between two 0xAARRGGBB colors (the fret hit-flash mix).
[[nodiscard]] ArgbColor mixArgb(const ArgbColor from, const ArgbColor to, const double weight)
{
    const double w = std::clamp(weight, 0.0, 1.0);
    ArgbColor result = 0;
    for (const unsigned shift : {24U, 16U, 8U, 0U})
    {
        const auto a = static_cast<double>((from >> shift) & 0xFFU);
        const auto b = static_cast<double>((to >> shift) & 0xFFU);
        result |= static_cast<ArgbColor>(std::clamp(a + ((b - a) * w), 0.0, 255.0)) << shift;
    }
    return result;
}

// Reference inlay-dot pattern: fret % 12 in {0, 3, 5, 7, 9} carries a marker.
[[nodiscard]] bool isDottedFret(const int fret)
{
    const int cycle = ((fret % 12) + 12) % 12;
    return cycle == 0 || cycle == 3 || cycle == 5 || cycle == 7 || cycle == 9;
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

// One three-band sustain ribbon on the string plane: solid edge strips [x0,x1] and [x2,x3]
// around a translucent core [x1,x2] (the reference's tail cross-section).
void pushTailRibbon(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double x2, const double x3, const double lane_y, const double z0,
    const double z1, const std::uint32_t edge_abgr, const std::uint32_t inner_abgr)
{
    pushFloorQuad(vertices, indices, x0, x1, lane_y, z0, z1, edge_abgr);
    pushFloorQuad(vertices, indices, x1, x2, lane_y, z0, z1, inner_abgr);
    pushFloorQuad(vertices, indices, x2, x3, lane_y, z0, z1, edge_abgr);
}

// Floor quad with per-end colors: the reference's beat-bar gradient wings.
void pushFloorQuadGradient(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y, const double z0, const double z1,
    const std::uint32_t abgr_at_z0, const std::uint32_t abgr_at_z1)
{
    pushQuad(
        vertices,
        indices,
        makeVertex(x0, y, z0, abgr_at_z0),
        makeVertex(x1, y, z0, abgr_at_z0),
        makeVertex(x1, y, z1, abgr_at_z1),
        makeVertex(x0, y, z1, abgr_at_z1));
}

// The reference open-note bar: a hexagonal prism along X across [x0, x1], with the center
// station slightly thicker than the ends and the ring squashed nearly flat in Z. Flat-colored
// and unlit, its silhouette reads as the reference's thin rounded bar from every board-view
// angle; end-cap fans close the side-on silhouette.
void pushOpenNoteBar(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double lane_y, const double z, const std::uint32_t abgr)
{
    constexpr std::size_t g_ring_size = static_cast<std::size_t>(g_open_note_segments);
    std::array<double, g_ring_size> ring_y{};
    std::array<double, g_ring_size> ring_z{};
    for (std::size_t point = 0; point < g_ring_size; ++point)
    {
        const double angle =
            2.0 * std::numbers::pi * static_cast<double>(point) / g_open_note_segments;
        ring_y.at(point) = std::cos(angle);
        ring_z.at(point) = std::sin(angle) * g_open_note_z_squash;
    }

    // Three cross-section stations: end, bulged middle, end.
    const std::array<double, 3> station_x{x0, (x0 + x1) / 2.0, x1};
    const std::array<double, 3> station_half{
        g_open_note_end_half_thickness,
        g_open_note_middle_half_thickness,
        g_open_note_end_half_thickness,
    };

    const auto ring_vertex = [&](const std::size_t station, const std::size_t point) {
        return makeVertex(
            station_x.at(station),
            lane_y + (station_half.at(station) * ring_y.at(point)),
            z + (station_half.at(station) * ring_z.at(point)),
            abgr);
    };

    // Prism sides between adjacent stations.
    for (std::size_t station = 0; station + 1 < station_x.size(); ++station)
    {
        for (std::size_t point = 0; point < g_ring_size; ++point)
        {
            const std::size_t next_point = (point + 1) % g_ring_size;
            pushQuad(
                vertices,
                indices,
                ring_vertex(station, point),
                ring_vertex(station, next_point),
                ring_vertex(station + 1, next_point),
                ring_vertex(station + 1, point));
        }
    }

    // End caps: a triangle fan per end station (no cull state, so winding is free).
    for (const std::size_t station : {std::size_t{0}, station_x.size() - 1})
    {
        const auto base = static_cast<std::uint16_t>(vertices.size());
        for (std::size_t point = 0; point < g_ring_size; ++point)
        {
            vertices.push_back(ring_vertex(station, point));
        }
        for (std::size_t point = 1; point + 1 < g_ring_size; ++point)
        {
            indices.push_back(base);
            indices.push_back(static_cast<std::uint16_t>(base + point));
            indices.push_back(static_cast<std::uint16_t>(base + point + 1));
        }
    }
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

// Links one program from its compiled pair; the typed error names the failing program.
[[nodiscard]] std::expected<UniqueBgfxHandle<bgfx::ProgramHandle>, HighwayRendererError>
linkProgram(const HighwayShaderPair& pair, const std::string_view name)
{
    UniqueBgfxHandle<bgfx::ProgramHandle> program =
        createProgramFromBytes(pair.vertex, pair.fragment);
    if (!program.isValid())
    {
        return std::unexpected{HighwayRendererError{
            .code = HighwayRendererErrorCode::ProgramCreationFailed,
            .message = "bgfx rejected or failed to link the highway " + std::string{name} +
                       " shader program"
        }};
    }
    return program;
}

} // namespace

/*
All bgfx-facing state and drawing lives here, behind the public header's opaque pointer, so the
framework never leaks into common/ui's interface (the Tracktion isolation treatment).
*/
struct HighwayRenderer::Impl
{
    // Shader programs, one per HighwayShaderSet member.
    UniqueBgfxHandle<bgfx::ProgramHandle> color_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> color_fade_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> texture_tint_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> glyph_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> texture_program;

    // Custom uniforms (predefined ones like u_modelViewProj are never created by hand).
    UniqueBgfxHandle<bgfx::UniformHandle> fade_params;
    UniqueBgfxHandle<bgfx::UniformHandle> atlas_sampler;

    HighwayAtlases atlases;

    // Fretboard skin (one cell per fret); invalid when the asset is missing (plain board).
    UniqueBgfxHandle<bgfx::TextureHandle> inlay_texture;

    // Retained board-face geometry; rebuilt on chart load, streamed content uses transients.
    UniqueBgfxHandle<bgfx::VertexBufferHandle> face_vertices;
    UniqueBgfxHandle<bgfx::IndexBufferHandle> face_indices;
    std::uint32_t face_index_count{0};

    common::core::HighwayViewState state;
    std::vector<double> sustain_prefix_max;
    common::core::HighwayMetrics metrics;
    common::core::HighwayCamera camera;

    // Player scroll speed; a free setting later (25-Q3), the reference default until then.
    double scroll_speed{1.3};

    // One warning per process when a transient batch is dropped (budget exceeded is a bug
    // signal, not an expected runtime path).
    bool reported_transient_drop{false};

    void rebuildBoardFace();
    void draw(double now_seconds, double dt_seconds, std::uint32_t width, std::uint32_t height);
    void drawOverlayRects(
        std::span<const HighwayOverlayRect> rects, std::uint32_t width, std::uint32_t height);

    // Submits a CPU-built batch through the transient buffers; drops the batch (with one
    // process-lifetime warning) if the transient budget is ever exceeded — a bug signal, not a
    // runtime path (the defaults hold >6x headroom over the worst-case highway frame).
    template <typename Vertex>
    void submitBatch(
        const std::vector<Vertex>& vertices, const std::vector<std::uint16_t>& indices,
        const bgfx::VertexLayout& layout, const bgfx::ProgramHandle program,
        const bgfx::TextureHandle* texture, const bgfx::ViewId view = g_board_view,
        const std::uint64_t render_state = g_blended_state)
    {
        if (vertices.empty())
        {
            return;
        }
        // The batch builders index with 16-bit bases: past 65535 vertices the bases would wrap
        // and render garbage silently, so an oversized batch (malformed input; unreachable for
        // real charts) is dropped loudly instead.
        if (vertices.size() > 65535 || (texture != nullptr && !bgfx::isValid(*texture)))
        {
            if (!reported_transient_drop)
            {
                reported_transient_drop = true;
                RH_LOG_WARNING(
                    "common.highway",
                    "unsubmittable batch dropped (vertices={}, texture_valid={})",
                    vertices.size(),
                    texture == nullptr || bgfx::isValid(*texture));
            }
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
            if (!reported_transient_drop)
            {
                reported_transient_drop = true;
                RH_LOG_WARNING(
                    "common.highway",
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
            bgfx::setTexture(0, atlas_sampler.get(), *texture);
        }
        bgfx::setState(render_state);
        bgfx::submit(view, program);
    }
};

std::expected<HighwayRenderer, HighwayRendererError> HighwayRenderer::create(
    const HighwayShaderSet& shaders, const HighwayTextureSet& textures)
{
    auto impl = std::make_unique<Impl>();

    auto color = linkProgram(shaders.color, "color");
    auto color_fade = linkProgram(shaders.color_fade, "color_fade");
    auto texture_tint = linkProgram(shaders.texture_tint, "texture_tint");
    auto glyph = linkProgram(shaders.glyph, "glyph");
    auto texture = linkProgram(shaders.texture, "texture");
    if (!color || !color_fade || !texture_tint || !glyph || !texture)
    {
        const auto& failed = !color          ? color.error()
                             : !color_fade   ? color_fade.error()
                             : !texture_tint ? texture_tint.error()
                             : !glyph        ? glyph.error()
                                             : texture.error();
        return std::unexpected{failed};
    }
    impl->color_program = std::move(*color);
    impl->color_fade_program = std::move(*color_fade);
    impl->texture_tint_program = std::move(*texture_tint);
    impl->glyph_program = std::move(*glyph);
    impl->texture_program = std::move(*texture);

    impl->fade_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_fade_params", bgfx::UniformType::Vec4)};
    impl->atlas_sampler = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "s_atlas", bgfx::UniformType::Sampler)};

    impl->atlases = makeHighwayAtlases(textures.note_atlas_png);
    impl->inlay_texture = uploadPngTexture(textures.inlay_atlas_png);
    if (!impl->atlases.reference_cells || !impl->inlay_texture.isValid())
    {
        RH_LOG_WARNING(
            "common.highway",
            "reference texture assets unavailable (heads={}, inlays={}); procedural fallbacks in "
            "use",
            impl->atlases.reference_cells,
            impl->inlay_texture.isValid());
    }

    return HighwayRenderer{std::move(impl)};
}

HighwayRenderer::HighwayRenderer(std::unique_ptr<Impl> impl) noexcept
    : m_impl{std::move(impl)}
{}

HighwayRenderer::~HighwayRenderer() = default;
HighwayRenderer::HighwayRenderer(HighwayRenderer&& other) noexcept = default;
HighwayRenderer& HighwayRenderer::operator=(HighwayRenderer&& other) noexcept = default;

void HighwayRenderer::setViewState(common::core::HighwayViewState state)
{
    m_impl->state = std::move(state);
    m_impl->sustain_prefix_max = common::core::makeHighwaySustainPrefixMax(m_impl->state.notes);
    m_impl->camera.reset();
    m_impl->rebuildBoardFace();
}

void HighwayRenderer::draw(
    const double now_seconds, const double dt_seconds, const std::uint32_t width,
    const std::uint32_t height)
{
    m_impl->draw(now_seconds, dt_seconds, width, height);
}

void HighwayRenderer::drawOverlayRects(
    const std::span<const HighwayOverlayRect> rects, const std::uint32_t width,
    const std::uint32_t height)
{
    m_impl->drawOverlayRects(rects, width, height);
}

// The retained half of the board face: the per-string colored string lines on the z = 0 plane.
// Fret lines moved to the dynamic pass (they carry the reference's per-frame active and
// hit-flash states); the fretboard picture itself is the inlay skin texture.
void HighwayRenderer::Impl::rebuildBoardFace()
{
    face_vertices.reset();
    face_indices.reset();
    face_index_count = 0;
    if (state.string_count <= 0)
    {
        return;
    }

    const bool mirrored = state.options.mirrored;
    const bool invert = state.options.invert_string_order;
    const StringColorPalette& palette = charterClassicPalette();

    std::vector<PosColorVertex> vertices;
    std::vector<std::uint16_t> indices;

    const double x_start = common::core::highwayFretLineX(0, metrics, mirrored);
    const double x_end = common::core::highwayFretLineX(g_face_fret_count, metrics, mirrored);
    const auto [x_low, x_high] = std::minmax(x_start, x_end);

    // String lines: per-string colored horizontal quads, the shared palette's lane surface.
    for (int string = 1; string <= state.string_count; ++string)
    {
        const double y =
            common::core::highwayStringLaneY(string, state.string_count, metrics, invert);
        const StringLaneStyle style{stringLaneColor(string, state.string_count, palette)};
        pushFaceQuad(
            vertices, indices, x_low, x_high, y - 0.015, y + 0.015, 0.0, packAbgr(style.lane));
    }

    const bgfx::Memory* vertex_memory = bgfx::copy(
        vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(PosColorVertex)));
    const bgfx::Memory* index_memory = bgfx::copy(
        indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t)));
    face_vertices = UniqueBgfxHandle<bgfx::VertexBufferHandle>{bgfx::createVertexBuffer(
        vertex_memory, posColorLayout())};
    face_indices = UniqueBgfxHandle<bgfx::IndexBufferHandle>{bgfx::createIndexBuffer(index_memory)};
    face_index_count = static_cast<std::uint32_t>(indices.size());
}

void HighwayRenderer::Impl::draw(
    const double now_seconds, const double dt_seconds, const std::uint32_t width,
    const std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    // Camera: instantaneous targets from the chart, smoothed frame-rate independently.
    const common::core::HighwayCameraTarget target =
        common::core::makeHighwayCameraTarget(state, now_seconds, metrics);
    camera.advance(target, dt_seconds, metrics);
    const common::core::HighwayCameraPose pose = camera.pose(metrics);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const std::array<float, 16> board_matrix = toBgfxMatrix(
        common::core::makeHighwayWorldToClip(pose, aspect, state.options.mirrored, metrics));
    const std::array<float, 16> background_matrix = toBgfxMatrix(
        common::core::makeHighwayBackgroundWorldToClip(
            pose, aspect, now_seconds, state.options.mirrored, metrics));

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

    const double scroll = scroll_speed;
    const bool mirrored = state.options.mirrored;
    const bool invert = state.options.invert_string_order;
    const double span_end_seconds = now_seconds + (metrics.visibility_window_seconds * scroll);
    const double span_start_seconds = now_seconds - g_passed_fade_seconds;
    const StringColorPalette& palette = charterClassicPalette();

    const auto time_to_z = [&](const double seconds) {
        return common::core::highwayTimeToZ(seconds - now_seconds, scroll, metrics);
    };

    // Distance fade for the floor furniture: fully faded near the hit line, opaque toward the
    // horizon (the reference's fading shader constants: 50 ms to 250 ms).
    const std::array<float, 4> fade_uniform{
        static_cast<float>(common::core::highwayTimeToZ(0.05, scroll, metrics)),
        static_cast<float>(common::core::highwayTimeToZ(0.25, scroll, metrics)),
        0.0F,
        0.0F
    };

    // Hand windows visible this frame: each placement owns the time range up to the next one
    // (the reference lights the runway per window, so the next hand position scrolls in as its
    // own lit region). A chart with no placements gets the reference nut window.
    struct HandWindow
    {
        double start_seconds;
        double end_seconds;
        int fret;
        int width;
    };
    std::vector<HandWindow> hand_windows;
    for (std::size_t index = 0; index < state.fret_hand_positions.size(); ++index)
    {
        const common::core::HighwayFhpView& fhp = state.fret_hand_positions[index];
        const double window_end = index + 1 < state.fret_hand_positions.size()
                                      ? state.fret_hand_positions[index + 1].seconds
                                      : span_end_seconds;
        if (window_end <= span_start_seconds || fhp.seconds >= span_end_seconds)
        {
            continue;
        }
        hand_windows.push_back(
            HandWindow{
                .start_seconds = std::max(fhp.seconds, span_start_seconds),
                .end_seconds = std::min(window_end, span_end_seconds),
                .fret = fhp.fret,
                .width = fhp.width,
            });
    }
    // Before the first placement (or on a chart without any) the reference nut window applies.
    if (hand_windows.empty())
    {
        hand_windows.push_back(
            HandWindow{
                .start_seconds = span_start_seconds,
                .end_seconds = span_end_seconds,
                .fret = 1,
                .width = 4,
            });
    }
    else if (state.fret_hand_positions.front().seconds > span_start_seconds)
    {
        hand_windows.insert(
            hand_windows.begin(),
            HandWindow{
                .start_seconds = span_start_seconds,
                .end_seconds = state.fret_hand_positions.front().seconds,
                .fret = 1,
                .width = 4,
            });
    }
    const auto [current_low_line, current_high_line] = activeFhpFretLines(state, now_seconds);

    // --- Lane border ribbons: one faded runway strip per fret line (the reference's floor
    // grid). Alpha tiers: bright for the current hand range, mid for any visible window's
    // range, faint elsewhere. ---
    {
        std::array<bool, g_face_fret_count + 1> in_visible_window{};
        for (const HandWindow& window : hand_windows)
        {
            for (int line = window.fret - 1; line <= window.fret + window.width - 1; ++line)
            {
                if (line >= 0 && line <= g_face_fret_count)
                {
                    in_visible_window.at(static_cast<std::size_t>(line)) = true;
                }
            }
        }

        bgfx::setUniform(fade_params.get(), fade_uniform.data());
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double z0 = time_to_z(span_start_seconds);
        const double z1 = time_to_z(span_end_seconds);
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            const double alpha = (line >= current_low_line && line <= current_high_line) ? 1.0
                                 : in_visible_window.at(static_cast<std::size_t>(line))  ? 0.375
                                                                                         : 0.125;
            const double x = common::core::highwayFretLineX(line, metrics, mirrored);
            pushFloorQuad(
                vertices,
                indices,
                x - 0.025,
                x + 0.025,
                0.004,
                z0,
                z1,
                packAbgr(g_lane_border_color | 0xFF000000U, alpha));
        }
        submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
    }

    // --- FHP lane highlight: per-window lit runway gaps, darker on inlay-dotted frets. ---
    {
        bgfx::setUniform(fade_params.get(), fade_uniform.data());
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (const HandWindow& window : hand_windows)
        {
            const double z0 = time_to_z(window.start_seconds);
            const double z1 = time_to_z(window.end_seconds);
            for (int fret = window.fret; fret <= window.fret + window.width - 1; ++fret)
            {
                if (fret < 1 || fret > g_face_fret_count)
                {
                    continue;
                }
                const double low_x = common::core::highwayFretLineX(fret - 1, metrics, mirrored);
                const double high_x = common::core::highwayFretLineX(fret, metrics, mirrored);
                const auto [x0, x1] = std::minmax(low_x, high_x);
                const ArgbColor color =
                    isDottedFret(fret) ? g_fhp_lane_dotted_color : g_fhp_lane_color;
                pushFloorQuad(vertices, indices, x0, x1, 0.008, z0, z1, packAbgr(color));
            }
        }
        submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
    }

    // --- Beat and measure bars: the reference's gradient wings in its deep blue, clipped to
    // each beat's hand window. Measures get a solid core between fade-in and fade-out wings;
    // plain beats are two wings meeting at the line. ---
    {
        bgfx::setUniform(fade_params.get(), fade_uniform.data());

        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (const common::core::HighwayBeatView& beat : state.beats)
        {
            if (beat.seconds < now_seconds - 0.2 || beat.seconds > span_end_seconds)
            {
                continue;
            }
            const auto [low_line, high_line] = activeFhpFretLines(state, beat.seconds);
            // Named operands: std::minmax over prvalues returns references into destroyed
            // temporaries (the Phase 3 CI dangling-reference finding).
            const double low_x = common::core::highwayFretLineX(low_line, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const double z = time_to_z(beat.seconds);
            const std::uint32_t solid = packAbgr(g_beat_bar_color);
            const std::uint32_t clear = packAbgr(g_beat_bar_color, 0.0);
            if (beat.measure_downbeat)
            {
                pushFloorQuadGradient(
                    vertices, indices, x0, x1, 0.015, z - 0.2, z - 0.1, clear, solid);
                pushFloorQuad(vertices, indices, x0, x1, 0.015, z - 0.1, z + 0.1, solid);
                pushFloorQuadGradient(
                    vertices, indices, x0, x1, 0.015, z + 0.1, z + 0.2, solid, clear);
            }
            else
            {
                pushFloorQuadGradient(vertices, indices, x0, x1, 0.015, z - 0.1, z, clear, solid);
                pushFloorQuadGradient(vertices, indices, x0, x1, 0.015, z, z + 0.1, solid, clear);
            }
        }
        submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
    }

    // --- Notes: shadows, sustain rails, open bars, then heads sorted far-to-near. ---
    const auto [first_note, last_note] = common::core::highwayVisibleNoteRange(
        state.notes, sustain_prefix_max, span_start_seconds, span_end_seconds);

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
        const common::core::HighwayNoteView& note = state.notes[index];
        if (note.start_seconds <= span_end_seconds && note.end_seconds >= span_start_seconds)
        {
            visible.push_back(index);
        }
    }
    // Far-to-near: later onsets draw first so nearer content alpha-composites over them.
    std::ranges::sort(visible, [this](const std::size_t lhs, const std::size_t rhs) {
        return state.notes[lhs].start_seconds > state.notes[rhs].start_seconds;
    });

    const std::array<float, 4> head_cell = atlases.head_layout.cellRect(g_head_cell_standard);
    const std::array<float, 4> anticipation_cell =
        atlases.head_layout.cellRect(g_head_cell_anticipation);
    // The reference head is a square quad (0.96 x 0.96 world units), not a lane-squashed one.
    const double head_half_w = metrics.note_half_width;
    const double head_half_h = metrics.note_half_width;

    for (const std::size_t index : visible)
    {
        const common::core::HighwayNoteView& note = state.notes[index];
        const double lane_y =
            common::core::highwayStringLaneY(note.string, state.string_count, metrics, invert);
        const ArgbColor base_color = stringLaneColor(note.string, state.string_count, palette);
        const StringLaneStyle style{base_color};
        const double fade =
            note.start_seconds >= now_seconds
                ? 1.0
                : std::max(0.0, 1.0 - ((now_seconds - note.start_seconds) / g_passed_fade_seconds));

        // Sustain tail: from the hit line (while sounding) or the onset to the sustain end, as
        // the reference's three-band ribbon (solid edges around a translucent core).
        if (note.end_seconds > note.start_seconds && note.end_seconds > now_seconds)
        {
            const double z0 = time_to_z(std::max(note.start_seconds, now_seconds));
            const double z1 = time_to_z(std::min(note.end_seconds, span_end_seconds));
            if (z1 > z0)
            {
                const std::uint32_t edge_color = packAbgr(style.tail);
                const std::uint32_t inner_color = packAbgr(style.tail, g_tail_inner_alpha);
                if (note.fret > 0)
                {
                    const double x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);
                    const double half = metrics.tail_half_width;
                    pushTailRibbon(
                        rail_vertices,
                        rail_indices,
                        x - half,
                        x - (half / 2.0),
                        x + (half / 2.0),
                        x + half,
                        lane_y,
                        z0,
                        z1,
                        edge_color,
                        inner_color);
                }
                else
                {
                    // Open sustain: the ribbon spans the hand window like the bar it trails
                    // (with the reference's inset), never the skinny fretted cross-section.
                    const auto [rail_low, rail_high] =
                        activeFhpFretLines(state, note.start_seconds);
                    const double low_x =
                        common::core::highwayFretLineX(rail_low, metrics, mirrored);
                    const double high_x =
                        common::core::highwayFretLineX(rail_high, metrics, mirrored);
                    const auto [window_x0, window_x1] = std::minmax(low_x, high_x);
                    const double xa = window_x0 + g_open_tail_margin;
                    const double xd = window_x1 - g_open_tail_margin;
                    // Degenerate-window guard: a tapered neck could shrink a far window below
                    // the margins plus edge bands; skip rather than draw crossed bands.
                    if (xd - xa > 2.0 * g_open_tail_margin)
                    {
                        pushTailRibbon(
                            rail_vertices,
                            rail_indices,
                            xa,
                            xa + g_open_tail_margin,
                            xd - g_open_tail_margin,
                            xd,
                            lane_y,
                            z0,
                            z1,
                            edge_color,
                            inner_color);
                    }
                }
            }
        }

        if (fade <= 0.0)
        {
            continue;
        }
        const double z = time_to_z(note.start_seconds);

        if (note.fret == 0)
        {
            // Open string: the reference's thin rounded bar spanning the active hand window, in
            // the full note color (the flat tail-width slab it replaces read as a plank).
            const auto [low_line, high_line] = activeFhpFretLines(state, note.start_seconds);
            const double low_x = common::core::highwayFretLineX(low_line, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            pushOpenNoteBar(
                open_vertices, open_indices, x0, x1, lane_y, z, packAbgr(base_color, fade));
            continue;
        }

        const double x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);

        // Chord membership decides both the rolling flip and the shadow (the reference skips
        // shadows for chord notes).
        const bool in_chord =
            (index > 0 &&
             std::abs(state.notes[index - 1].start_seconds - note.start_seconds) < 1.0e-4) ||
            (index + 1 < state.notes.size() &&
             std::abs(state.notes[index + 1].start_seconds - note.start_seconds) < 1.0e-4);

        // Note shadow: the reference's vertical gradient fan — a string-colored glow rising
        // from the board toward the head, load-bearing for depth perception.
        if (!in_chord)
        {
            const double base_half = head_half_w * 0.5;
            const double apex_y = std::max(lane_y - 0.3, 0.05);
            const std::uint32_t center_color = packAbgr(base_color, fade);
            const std::uint32_t edge_color = packAbgr(base_color, 0.0);
            const auto center_index = static_cast<std::uint16_t>(shadow_vertices.size());
            shadow_vertices.push_back(makeVertex(x, 0.0, z, center_color));
            shadow_vertices.push_back(makeVertex(x - base_half, 0.0, z, edge_color));
            shadow_vertices.push_back(makeVertex(x, apex_y, z, edge_color));
            shadow_vertices.push_back(makeVertex(x + base_half, 0.0, z, edge_color));
            for (const std::uint16_t offset :
                 {std::uint16_t{0},
                  std::uint16_t{1},
                  std::uint16_t{2},
                  std::uint16_t{0},
                  std::uint16_t{2},
                  std::uint16_t{3}})
            {
                shadow_indices.push_back(static_cast<std::uint16_t>(center_index + offset));
            }
        }

        // Anticipation ring: scales down onto the landing spot over the last half second
        // (reference atlas cell; chart-driven, so the editor preview shows it too — 44-Q1).
        const double seconds_out = note.start_seconds - now_seconds;
        if (atlases.reference_cells && seconds_out > 0.0 && seconds_out < g_anticipation_seconds)
        {
            double ring_scale =
                std::min(1.0, 1.0 - (0.5 * ((seconds_out - 0.25) / g_anticipation_seconds)));
            ring_scale *= ring_scale;
            const double ring_alpha =
                std::min(1.0, (g_anticipation_seconds - seconds_out) * (1000.0 / 255.0));
            const std::uint32_t ring_tint = packAbgr(base_color, ring_alpha);
            const double half = head_half_w * ring_scale;
            pushQuad(
                head_vertices,
                head_indices,
                makeUvVertex(
                    x - half,
                    lane_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[0],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    lane_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    lane_y + half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[1]),
                makeUvVertex(
                    x - half,
                    lane_y + half,
                    0.0,
                    ring_tint,
                    anticipation_cell[0],
                    anticipation_cell[1]));
        }

        // Rolling flip: single notes rotate around their travel axis during the last second;
        // chord notes land flat.
        const double rotation =
            in_chord ? 0.0
                     : std::clamp(
                           -std::numbers::pi * (note.start_seconds - now_seconds - 0.1),
                           -std::numbers::pi / 2.0,
                           0.0);
        const double cos_r = std::cos(rotation);
        const double sin_r = std::sin(rotation);
        const std::uint32_t tint = packAbgr(base_color, fade);

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

    submitBatch(shadow_vertices, shadow_indices, posColorLayout(), color_program.get(), nullptr);
    submitBatch(rail_vertices, rail_indices, posColorLayout(), color_program.get(), nullptr);
    submitBatch(open_vertices, open_indices, posColorLayout(), color_program.get(), nullptr);
    const bgfx::TextureHandle heads_texture = atlases.heads.get();
    submitBatch(
        head_vertices,
        head_indices,
        posColorUvLayout(),
        texture_tint_program.get(),
        &heads_texture);

    // --- Board face: dynamic fret lines with the reference's three states (inactive, active
    // within current and upcoming hand windows, and the sqrt-decay hit-flash that thickens up
    // to 4x — a large part of the alive feel), drawn over passing content. ---
    const double face_top_y =
        (static_cast<double>(std::max(state.string_count, 1)) * metrics.string_distance) +
        (metrics.string_distance * 0.5);
    {
        // Active fret lines: the current hand range plus every window arriving soon.
        std::array<bool, g_face_fret_count + 1> active{};
        for (int line = current_low_line; line <= current_high_line; ++line)
        {
            if (line >= 0 && line <= g_face_fret_count)
            {
                active.at(static_cast<std::size_t>(line)) = true;
            }
        }
        for (const HandWindow& window : hand_windows)
        {
            if (window.start_seconds > now_seconds + g_fret_active_horizon_seconds ||
                window.end_seconds < now_seconds)
            {
                continue;
            }
            for (int line = window.fret - 1; line <= window.fret + window.width - 1; ++line)
            {
                if (line >= 0 && line <= g_face_fret_count)
                {
                    active.at(static_cast<std::size_t>(line)) = true;
                }
            }
        }

        // Hit-flash weights: notes sounding within the last flash window light their fret lines
        // with a sqrt decay (chart-driven, like the reference preview).
        std::array<double, g_face_fret_count + 1> flash{};
        for (const std::size_t index : visible)
        {
            const common::core::HighwayNoteView& note = state.notes[index];
            const double since = now_seconds - note.start_seconds;
            if (since <= 0.0 || since >= g_fret_flash_seconds || note.fret <= 0)
            {
                continue;
            }
            const double weight = std::sqrt(1.0 - (since / g_fret_flash_seconds));
            for (const int line : {note.fret - 1, note.fret})
            {
                if (line >= 0 && line <= g_face_fret_count)
                {
                    flash.at(static_cast<std::size_t>(line)) =
                        std::max(flash.at(static_cast<std::size_t>(line)), weight);
                }
            }
        }

        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            const double x = common::core::highwayFretLineX(line, metrics, mirrored);
            const double flash_weight = flash.at(static_cast<std::size_t>(line));
            const ArgbColor state_color = active.at(static_cast<std::size_t>(line))
                                              ? g_fret_active_color
                                              : g_fret_inactive_color;
            const ArgbColor color = mixArgb(state_color, g_fret_highlight_color, flash_weight);
            const double half = (line == 0 ? 0.05 : 0.025) * (1.0 + (3.0 * flash_weight));
            pushFaceQuad(
                vertices, indices, x - half, x + half, 0.0, face_top_y, 0.0, packAbgr(color));
        }
        submitBatch(vertices, indices, posColorLayout(), color_program.get(), nullptr);
    }

    // --- String lines (retained) over the fret lines. ---
    if (face_index_count > 0 && face_vertices.isValid() && face_indices.isValid())
    {
        bgfx::setVertexBuffer(0, face_vertices.get());
        bgfx::setIndexBuffer(face_indices.get(), 0, face_index_count);
        bgfx::setState(g_blended_state);
        bgfx::submit(g_board_view, color_program.get());
    }

    // --- Fretboard skin: one textured cell per fret from the reference inlay atlas (8x4 grid),
    // drawn last on the face like the reference (the art is transparent between markers). ---
    if (inlay_texture.isValid())
    {
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        constexpr int g_inlay_columns = 8;
        constexpr int g_inlay_rows = 4;
        for (int fret = 1; fret <= g_face_fret_count; ++fret)
        {
            const int cell = fret - 1;
            // Named row/column: the integer division is the grid addressing, kept out of the
            // float expressions on purpose.
            const int cell_column = cell % g_inlay_columns;
            const int cell_row = cell / g_inlay_columns;
            const float u0 = static_cast<float>(cell_column) / g_inlay_columns;
            const float v0 = static_cast<float>(cell_row) / g_inlay_rows;
            const float u1 = u0 + (1.0F / g_inlay_columns);
            const float v1 = v0 + (1.0F / g_inlay_rows);
            const double low_x = common::core::highwayFretLineX(fret - 1, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(fret, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const std::uint32_t white = packAbgr(0xFFFFFFFF);
            pushQuad(
                vertices,
                indices,
                makeUvVertex(x0, 0.0, 0.0, white, u0, v1),
                makeUvVertex(x1, 0.0, 0.0, white, u1, v1),
                makeUvVertex(x1, face_top_y, 0.0, white, u1, v0),
                makeUvVertex(x0, face_top_y, 0.0, white, u0, v0));
        }
        const bgfx::TextureHandle inlays = inlay_texture.get();
        submitBatch(
            vertices,
            indices,
            posColorUvLayout(),
            texture_program.get(),
            &inlays,
            g_board_view,
            g_premultiplied_state);
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
                    const std::array<float, 4> rect = atlases.glyph_layout.cellRect(*cell);
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
        if (state.string_count > 0)
        {
            constexpr double g_number_height = 0.30;
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                const std::string label = std::to_string(fret);
                const double text_width =
                    g_number_height * 0.62 * static_cast<double>(label.size());
                const double x =
                    common::core::highwayNoteCenterX(fret, metrics, mirrored) - (text_width / 2.0);
                (void)push_text(label, x, -0.42, 0.0, g_number_height, packAbgr(0xFFFFFFFF, 0.55));
            }
        }

        // Section labels floating above the board at their arrival time.
        const double section_y =
            (static_cast<double>(std::max(state.string_count, 1)) * metrics.string_distance) +
            (metrics.string_distance * 2.0);
        for (const common::core::HighwaySectionView& section : state.sections)
        {
            if (section.seconds < now_seconds - 0.5 || section.seconds > span_end_seconds)
            {
                continue;
            }
            std::string label = section.type;
            std::ranges::transform(label, label.begin(), [](const char c) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            });
            const auto [low_line, high_line] = activeFhpFretLines(state, section.seconds);
            const double x = common::core::highwayFretLineX(low_line, metrics, mirrored);
            (void)push_text(
                label,
                std::min(x, common::core::highwayFretLineX(high_line, metrics, mirrored)),
                section_y,
                time_to_z(section.seconds),
                0.5,
                packAbgr(0xFFFFFFFF, 0.85));
        }

        const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
        submitBatch(
            glyph_vertices, glyph_indices, posColorUvLayout(), glyph_program.get(), &glyph_texture);
    }
}

// Overlay rectangles ride the same transient path as the scene, on the overlay view with a
// pixel-space orthographic transform (x right, y down from the top-left corner).
void HighwayRenderer::Impl::drawOverlayRects(
    const std::span<const HighwayOverlayRect> rects, const std::uint32_t width,
    const std::uint32_t height)
{
    if (rects.empty() || width == 0 || height == 0)
    {
        return;
    }

    const auto width_f = static_cast<float>(width);
    const auto height_f = static_cast<float>(height);
    const std::array<float, 16> ortho{
        2.0F / width_f,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        -2.0F / height_f,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        1.0F,
        0.0F,
        -1.0F,
        1.0F,
        0.0F,
        1.0F,
    };
    bgfx::setViewTransform(g_overlay_view, ortho.data(), nullptr);

    std::vector<PosColorVertex> vertices;
    std::vector<std::uint16_t> indices;
    vertices.reserve(rects.size() * 4);
    indices.reserve(rects.size() * 6);
    for (const HighwayOverlayRect& rect : rects)
    {
        pushQuad(
            vertices,
            indices,
            makeVertex(rect.left, rect.top, 0.0, rect.abgr),
            makeVertex(rect.right, rect.top, 0.0, rect.abgr),
            makeVertex(rect.right, rect.bottom, 0.0, rect.abgr),
            makeVertex(rect.left, rect.bottom, 0.0, rect.abgr));
    }
    submitBatch(
        vertices,
        indices,
        posColorLayout(),
        color_program.get(),
        nullptr,
        g_overlay_view,
        g_overlay_state);
}

} // namespace rock_hero::common::ui
