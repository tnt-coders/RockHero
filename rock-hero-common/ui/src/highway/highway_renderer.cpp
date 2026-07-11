#include "highway/bgfx_program.h"
#include "highway/highway_atlas.h"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <ranges>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/ui/highway/highway_renderer.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <span>
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

// Scrolling fret-number colors (reference PREVIEW_3D palette): a bright blue for a dotted fret
// inside the current hand range, the lane-border teal at half alpha elsewhere, and the FHP
// orange for hand-position arrivals and the current hand's numbers at the hit line.
constexpr ArgbColor g_fret_number_active_color = 0xFF87DDF6;
constexpr ArgbColor g_fret_number_dim_color = 0x8007928F;
constexpr ArgbColor g_fret_number_fhp_color = 0xFFFFA821;

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

// Tolerance for matching an onset to a shape-span boundary (or grouping simultaneous onsets).
// Two events at the same musical grid position resolve through the tempo map on different code
// paths (a forward cursor for note onsets, the plain resolver for shape ends), so they can land
// a rounding epsilon apart; without this tolerance a chord sitting exactly on a handshape's
// start or end would intermittently fall outside the span and lose its repeat-box treatment.
constexpr double g_onset_match_epsilon = 1.0e-4;

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

// Adaptive tail sampling for technique-modulated rails: one centerline sample per this many
// projected screen pixels, hard-capped (the reference's per-millisecond-tessellation fix).
constexpr double g_tail_pixels_per_sample = 4.0;
constexpr std::size_t g_tail_sample_cap = 256;

// Unpitched slides release pressure, so their rail dims toward this alpha across the glide.
constexpr double g_unpitched_slide_end_alpha = 0.25;

// Chord-box palette and geometry (reference values): a translucent teal panel per strummed
// chord, with corner holders, gradient frame bars, and mute-cross variants.
constexpr ArgbColor g_chord_box_color = 0xFF00D2D5;
constexpr ArgbColor g_chord_box_dark_color = 0xFF003C3D;
// The palm cross uses the reference's dark palm-mute color (its drawer reads the light
// full-mute color instead — an evident slip given the unused dark constant; the light/dark
// split is the intended reading, confirmed by the user).
constexpr ArgbColor g_chord_full_mute_cross_color = 0xFF80D8FF;
constexpr ArgbColor g_chord_palm_mute_cross_color = 0xFF005064;
constexpr ArgbColor g_chord_name_color = 0xFFE0E0E0;
constexpr double g_chord_box_frame_thickness = 0.075;

// Hand-shape span rails on the floor: arpeggio spans in the reference purple, held shapes in
// the lane-border teal; a solid core with fade-out wings (fret thickness x3 and x9).
constexpr ArgbColor g_arpeggio_color = 0xFFC040FF;
constexpr double g_shape_rail_core_half_width = 0.075;
constexpr double g_shape_rail_fade_half_width = 0.225;

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
// angle; end-cap fans close the side-on silhouette. The thickness scale draws the reference's
// accent halo (the same bar at triple cross-section).
void pushOpenNoteBar(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double lane_y, const double z, const std::uint32_t abgr,
    const double thickness_scale)
{
    constexpr auto g_ring_size = static_cast<std::size_t>(g_open_note_segments);
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
        g_open_note_end_half_thickness * thickness_scale,
        g_open_note_middle_half_thickness * thickness_scale,
        g_open_note_end_half_thickness * thickness_scale,
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

// Appends billboarded (constant-z) glyph quads for a text string to a glyph batch, left-anchored
// at (left_x, baseline_y) and growing right; returns the advanced pen width. Shared by every text
// pass (fret numbers, section labels, chord names).
[[nodiscard]] double pushGlyphText(
    std::vector<PosColorUvVertex>& vertices, std::vector<std::uint16_t>& indices,
    const HighwayAtlasLayout& glyph_layout, const std::string_view text, const double left_x,
    const double baseline_y, const double z, const double glyph_height, const std::uint32_t color)
{
    const double advance = glyph_height * 0.62;
    double pen_x = left_x;
    for (const char character : text)
    {
        const std::optional<int> cell = highwayGlyphCellIndex(character);
        if (cell.has_value())
        {
            const std::array<float, 4> rect = glyph_layout.cellRect(*cell);
            pushQuad(
                vertices,
                indices,
                makeUvVertex(pen_x, baseline_y, z, color, rect[0], rect[3]),
                makeUvVertex(pen_x + glyph_height, baseline_y, z, color, rect[2], rect[3]),
                makeUvVertex(
                    pen_x + glyph_height, baseline_y + glyph_height, z, color, rect[2], rect[1]),
                makeUvVertex(pen_x, baseline_y + glyph_height, z, color, rect[0], rect[1]));
        }
        pen_x += advance;
    }
    return pen_x - left_x;
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

    // Fingering panel texture (barre shapes + finger names); invalid skips the panel.
    UniqueBgfxHandle<bgfx::TextureHandle> fingering_texture;

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
    impl->fingering_texture = uploadPngTexture(textures.fingering_png);
    if (!impl->atlases.reference_cells || !impl->inlay_texture.isValid() ||
        !impl->fingering_texture.isValid())
    {
        RH_LOG_WARNING(
            "common.highway",
            "reference texture assets unavailable (heads={}, inlays={}, fingering={}); "
            "procedural fallbacks in use",
            impl->atlases.reference_cells,
            impl->inlay_texture.isValid(),
            impl->fingering_texture.isValid());
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
    const common::core::HighwayMat4 world_to_clip =
        common::core::makeHighwayWorldToClip(pose, aspect, state.options.mirrored, metrics);
    const std::array<float, 16> board_matrix = toBgfxMatrix(world_to_clip);
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

    // --- Hand-shape span rails: thick fading edge lines along each shape span at its hand
    // window's fret lines, riding the hit line while active (purple marks arpeggio spans). ---
    {
        bgfx::setUniform(fade_params.get(), fade_uniform.data());
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (const common::core::HighwayShapeView& shape : state.shapes)
        {
            if (shape.end_seconds < now_seconds || shape.start_seconds > span_end_seconds)
            {
                continue;
            }
            const double z0 = std::max(0.0, time_to_z(shape.start_seconds));
            const double z1 = time_to_z(std::min(shape.end_seconds, span_end_seconds));
            if (z1 <= z0)
            {
                continue;
            }
            const ArgbColor color =
                shape.arpeggio ? g_arpeggio_color : (g_lane_border_color | 0xFF000000U);
            const std::uint32_t solid = packAbgr(color);
            const std::uint32_t clear = packAbgr(color, 0.0);
            const auto [low_line, high_line] = activeFhpFretLines(state, shape.start_seconds);
            for (const int line : {low_line, high_line})
            {
                const double x = common::core::highwayFretLineX(line, metrics, mirrored);
                // Solid core between fade-out wings, per line (the reference's cross-section).
                const auto push_band = [&](const double xa,
                                           const double xb,
                                           const std::uint32_t color_a,
                                           const std::uint32_t color_b) {
                    pushQuad(
                        vertices,
                        indices,
                        makeVertex(xa, 0.01, z0, color_a),
                        makeVertex(xb, 0.01, z0, color_b),
                        makeVertex(xb, 0.01, z1, color_b),
                        makeVertex(xa, 0.01, z1, color_a));
                };
                push_band(
                    x - g_shape_rail_fade_half_width,
                    x - g_shape_rail_core_half_width,
                    clear,
                    solid);
                push_band(
                    x - g_shape_rail_core_half_width,
                    x + g_shape_rail_core_half_width,
                    solid,
                    solid);
                push_band(
                    x + g_shape_rail_core_half_width,
                    x + g_shape_rail_fade_half_width,
                    solid,
                    clear);
            }
        }
        submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
    }

    // --- Scrolling fret numbers: the reference's readability aid. Numbers ride the board floor
    // at each dotted fret on every measure downbeat (bright inside the current hand range, dim
    // elsewhere), mark each upcoming hand-position arrival in orange, and pin the current hand's
    // numbers at the hit line; all fade in as they approach. Drawn before the notes so heads
    // occlude the floor numbers. ---
    {
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double z_faded = common::core::highwayTimeToZ(0.05, scroll, metrics);
        const double z_close = common::core::highwayTimeToZ(0.25, scroll, metrics);

        // One billboarded number at a fret's slot on the board floor; alpha fades in between the
        // hit line and z_close when fade is requested (Charter bakes the fade into the color,
        // since the glyph program has no fade uniform).
        const auto push_number =
            [&](const int fret, const double z, const ArgbColor base, const bool fade) {
                const double glyph_height = z > 0.0 ? 0.70 : 0.40;
                const std::string label = std::to_string(fret);
                const double text_width = glyph_height * 0.62 * static_cast<double>(label.size());
                const double left_x =
                    common::core::highwayNoteCenterX(fret, metrics, mirrored) - (text_width / 2.0);
                double alpha_scale = 1.0;
                if (fade && z < z_close)
                {
                    alpha_scale = std::clamp((z - z_faded) / (z_close - z_faded), 0.0, 1.0);
                }
                (void)pushGlyphText(
                    vertices,
                    indices,
                    atlases.glyph_layout,
                    label,
                    left_x,
                    -glyph_height / 2.0,
                    z,
                    glyph_height,
                    packAbgr(base, alpha_scale));
            };

        // Dotted-fret numbers on each visible measure downbeat, lit within the hand range.
        for (const common::core::HighwayBeatView& beat : state.beats)
        {
            if (!beat.measure_downbeat || beat.seconds < now_seconds - 0.2 ||
                beat.seconds > span_end_seconds)
            {
                continue;
            }
            const auto [low_line, high_line] = activeFhpFretLines(state, beat.seconds);
            const double z = time_to_z(beat.seconds);
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                if (!isDottedFret(fret))
                {
                    continue;
                }
                const bool active = fret >= low_line + 1 && fret <= high_line;
                push_number(
                    fret, z, active ? g_fret_number_active_color : g_fret_number_dim_color, true);
            }
        }

        // Upcoming hand-position arrivals, in the FHP orange.
        for (const common::core::HighwayFhpView& fhp : state.fret_hand_positions)
        {
            if (fhp.seconds <= now_seconds || fhp.seconds > span_end_seconds)
            {
                continue;
            }
            push_number(fhp.fret, time_to_z(fhp.seconds), g_fret_number_fhp_color, true);
        }

        // The current hand range pinned at the hit line, always solid.
        const auto [current_low, current_high] = activeFhpFretLines(state, now_seconds);
        for (int fret = current_low + 1; fret <= current_high; ++fret)
        {
            if (fret >= 1 && fret <= g_face_fret_count)
            {
                push_number(fret, 0.0, g_fret_number_fhp_color, false);
            }
        }

        const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
        submitBatch(vertices, indices, posColorUvLayout(), glyph_program.get(), &glyph_texture);
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
    // Draw order for every note batch (this single vector orders shadows, rails, opens, and
    // heads alike). A total order on three keys keeps the paint order deterministic frame to
    // frame — a single time key leaves same-onset chord notes equivalent, and the non-stable
    // sort then orders their overlapping heads arbitrarily, which flickers as notes enter and
    // leave the window. Keys:
    //   1. onset descending (far-to-near, so nearer-in-time content composites over farther);
    //   2. base string-lane Y ascending, so a higher-on-screen note paints over a lower one at
    //      the same onset (the static lane Y, never the bend-animated head Y);
    //   3. note index, a unique tiebreak that makes the order total (and thus stable).
    std::vector<double> lane_key(last_note - first_note, 0.0);
    for (const std::size_t index : visible)
    {
        const common::core::HighwayNoteView& note = state.notes[index];
        lane_key[index - first_note] =
            common::core::highwayStringLaneY(note.string, state.string_count, metrics, invert);
    }
    // Compared with < / > only (no float equality) so the strict-weak-ordering stays clean
    // under -Wfloat-equal; ties on both real keys fall through to the unique index.
    std::ranges::sort(visible, [&](const std::size_t lhs, const std::size_t rhs) {
        const double lhs_onset = state.notes[lhs].start_seconds;
        const double rhs_onset = state.notes[rhs].start_seconds;
        if (lhs_onset > rhs_onset)
        {
            return true;
        }
        if (lhs_onset < rhs_onset)
        {
            return false;
        }
        const double lhs_lane = lane_key[lhs - first_note];
        const double rhs_lane = lane_key[rhs - first_note];
        if (lhs_lane < rhs_lane)
        {
            return true;
        }
        if (lhs_lane > rhs_lane)
        {
            return false;
        }
        return lhs < rhs;
    });

    // Chord groups: notes sharing an onset (contiguous in the sorted note stream). Membership
    // decides the rolling flip and the shadow; groups of two or more get a chord box.
    struct ChordGroup
    {
        double start_seconds;
        std::size_t first;
        std::size_t count;
        bool any_accent;
        common::core::NoteMute common_mute;
        // True when every note is fully muted (dead chugs never show their notes, and they do
        // not break a repeat chain).
        bool all_full_muted;
        // Repeat-chord treatment (the reference's visibility rules): the strum renders as a
        // half-height box with its mute cross and NO notes.
        bool box_only;
        // Sorted (string, fret) pairs for matching strums against the shape's posture.
        std::vector<std::pair<int, int>> frets;
    };
    std::vector<ChordGroup> chord_groups;
    std::vector<std::size_t> note_group(last_note - first_note, 0);
    for (std::size_t index = first_note; index < last_note;)
    {
        std::size_t group_end = index + 1;
        while (group_end < last_note &&
               std::abs(state.notes[group_end].start_seconds - state.notes[index].start_seconds) <
                   g_onset_match_epsilon)
        {
            ++group_end;
        }
        ChordGroup group{
            .start_seconds = state.notes[index].start_seconds,
            .first = index,
            .count = group_end - index,
            .any_accent = false,
            .common_mute = state.notes[index].mute,
            .all_full_muted = true,
            .box_only = false,
            .frets = {},
        };
        group.frets.reserve(group.count);
        for (std::size_t member = index; member < group_end; ++member)
        {
            const common::core::HighwayNoteView& note = state.notes[member];
            group.any_accent = group.any_accent || note.accent;
            if (note.mute != group.common_mute)
            {
                group.common_mute = common::core::NoteMute::None;
            }
            group.all_full_muted =
                group.all_full_muted && note.mute == common::core::NoteMute::Full;
            group.frets.emplace_back(note.string, note.fret);
            note_group[member - first_note] = chord_groups.size();
        }
        std::ranges::sort(group.frets);
        chord_groups.push_back(std::move(group));
        index = group_end;
    }

    // Repeat classification (the reference's chord visibility rules): a strum shows only the
    // half-height repeat box when it repeats the hand shape's own posture within the shape span
    // — single notes and dead chugs between strums do not break the chain. Fully-muted strums
    // never show notes; sustained or technique-bearing strums always do.
    const auto posture_matches = [](const common::core::HighwayShapeView& shape,
                                    const std::vector<std::pair<int, int>>& frets) {
        if (shape.strings.empty() || shape.strings.size() != frets.size())
        {
            return false;
        }
        for (std::size_t entry = 0; entry < frets.size(); ++entry)
        {
            // Posture entries ascend by string (projection order), like the sorted pairs.
            if (shape.strings[entry].string != frets[entry].first ||
                shape.strings[entry].fret != frets[entry].second)
            {
                return false;
            }
        }
        return true;
    };
    for (std::size_t group_index = 0; group_index < chord_groups.size(); ++group_index)
    {
        ChordGroup& group = chord_groups[group_index];
        if (group.count < 2)
        {
            continue;
        }
        bool has_tails = false;
        bool all_palm_muted = true;
        bool any_marks = false;
        for (std::size_t member = group.first; member < group.first + group.count; ++member)
        {
            const common::core::HighwayNoteView& note = state.notes[member];
            has_tails = has_tails || note.end_seconds > note.start_seconds || note.vibrato ||
                        note.tremolo || !note.bend.empty() || !note.slides.empty();
            all_palm_muted = all_palm_muted && note.mute == common::core::NoteMute::Palm;
            any_marks = any_marks || note.harmonic != common::core::NoteHarmonic::None ||
                        note.attack != common::core::NoteAttack::Pick ||
                        note.mute != common::core::NoteMute::None;
        }
        if (has_tails)
        {
            continue;
        }
        if (group.all_full_muted)
        {
            group.box_only = true;
            continue;
        }
        // Marked chords always show their notes — unless every note is palm muted, where the
        // reference's mute short-circuit applies the repeat rule anyway.
        if (any_marks && !all_palm_muted)
        {
            continue;
        }
        const common::core::HighwayShapeView* shape = nullptr;
        for (const common::core::HighwayShapeView& candidate : state.shapes)
        {
            // Tolerance so a shape starting on the same grid position as the chord (resolved a
            // rounding epsilon later) is still selected rather than skipped.
            if (candidate.start_seconds > group.start_seconds + g_onset_match_epsilon)
            {
                break;
            }
            shape = &candidate;
        }
        // A chord onset at (or within rounding of) the shape's end is still under the span — the
        // strict >= here used to drop the handshape's last strum from repeat-box treatment.
        if (shape == nullptr || group.start_seconds > shape->end_seconds + g_onset_match_epsilon ||
            !posture_matches(*shape, group.frets))
        {
            continue;
        }
        // Walk the raw note stream backward (not the visible-range groups: a predecessor that
        // scrolled out behind the hit line must still anchor the repeat chain, or repeat boxes
        // would pop back into full notes as they approach the player).
        std::size_t cursor = group.first;
        while (cursor > 0)
        {
            const double onset = state.notes[cursor - 1].start_seconds;
            // Tolerance at the span start: the first strum of a repeat chain usually sits exactly
            // on the shape start, and a rounding epsilon below it would break the walk before it
            // finds the anchoring run — the common cause of a repeat chord flickering to notes.
            if (onset < shape->start_seconds - g_onset_match_epsilon)
            {
                break;
            }
            std::size_t run_begin = cursor - 1;
            while (run_begin > 0 && std::abs(state.notes[run_begin - 1].start_seconds - onset) <
                                        g_onset_match_epsilon)
            {
                --run_begin;
            }
            const std::size_t run_count = cursor - run_begin;
            if (run_count >= 2)
            {
                bool run_all_full_muted = true;
                std::vector<std::pair<int, int>> run_frets;
                run_frets.reserve(run_count);
                for (std::size_t member = run_begin; member < cursor; ++member)
                {
                    run_all_full_muted = run_all_full_muted &&
                                         state.notes[member].mute == common::core::NoteMute::Full;
                    run_frets.emplace_back(state.notes[member].string, state.notes[member].fret);
                }
                if (!run_all_full_muted)
                {
                    std::ranges::sort(run_frets);
                    group.box_only = posture_matches(*shape, run_frets);
                    break;
                }
            }
            cursor = run_begin;
        }
    }

    // --- Chord boxes: the reference's translucent panels at chord onsets, drawn far-to-near
    // BEFORE the notes so nearer content composites over them (the reference draws them after
    // the notes under a depth buffer; this board view is painter-ordered, so boxes go first —
    // the only cost is the faint filling no longer tinting its own chord's heads). Repeated and
    // dead strums render the half-height repeat box with the mute cross. ---
    {
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;

        // Corner-holder fan outlines (reference ChordBoxHolderModel): a teal L bracket with a
        // dark inner L, at each bottom corner. Local coordinates; the right corner mirrors in X.
        constexpr std::array<std::array<double, 2>, 6> g_holder_background{
            {{-0.01, -0.01}, {1.01, -0.01}, {1.01, 0.11}, {0.11, 0.11}, {0.11, 1.11}, {-0.01, 1.01}}
        };
        constexpr std::array<std::array<double, 2>, 6> g_holder_front{
            {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.1}, {0.1, 0.1}, {0.1, 1.1}, {0.0, 1.0}}
        };
        const auto push_fan = [&](const std::span<const std::array<double, 2>> points,
                                  const double origin_x,
                                  const double x_sign,
                                  const double z,
                                  const std::uint32_t abgr) {
            const auto base = static_cast<std::uint16_t>(vertices.size());
            for (const std::array<double, 2>& point : points)
            {
                vertices.push_back(makeVertex(origin_x + (x_sign * point[0]), point[1], z, abgr));
            }
            for (std::size_t point = 1; point + 1 < points.size(); ++point)
            {
                indices.push_back(base);
                indices.push_back(static_cast<std::uint16_t>(base + point));
                indices.push_back(static_cast<std::uint16_t>(base + point + 1));
            }
        };

        for (const ChordGroup& group : std::views::reverse(chord_groups))
        {
            if (group.count < 2 || group.start_seconds < now_seconds ||
                group.start_seconds > span_end_seconds)
            {
                continue;
            }
            const auto [low_line, high_line] = activeFhpFretLines(state, group.start_seconds);
            const double low_x = common::core::highwayFretLineX(low_line, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const double middle_x = (x0 + x1) / 2.0;
            const double z = std::max(0.0, time_to_z(group.start_seconds));
            const double y0 = 0.0;
            // Half a string above the top lane (which now sits at (count - 0.5) * string_distance),
            // matching the reference box top of one string above the top string.
            const double full_height_y1 =
                (static_cast<double>(state.string_count) + 0.5) * metrics.string_distance;
            // The repeat box is half height (the reference's onlyBox treatment).
            const double y1 = group.box_only ? (y0 + full_height_y1) / 2.0 : full_height_y1;
            const bool with_top = group.count > 2;
            const double thickness = g_chord_box_frame_thickness;

            const std::uint32_t box_solid = packAbgr(g_chord_box_color);
            const std::uint32_t box_half = packAbgr(g_chord_box_color, 128.0 / 255.0);
            const std::uint32_t dark_half = packAbgr(g_chord_box_dark_color, 128.0 / 255.0);
            const std::uint32_t box_faint = packAbgr(g_chord_box_color, 32.0 / 255.0);
            const std::uint32_t dark_faint = packAbgr(g_chord_box_dark_color, 32.0 / 255.0);
            const std::uint32_t box_clear = packAbgr(g_chord_box_color, 0.0);

            // A vertical face quad with per-corner colors (the frame's gradient pieces).
            const auto push_face = [&](const double xa,
                                       const double ya,
                                       const std::uint32_t ca,
                                       const double xb,
                                       const double yb,
                                       const std::uint32_t cb) {
                pushQuad(
                    vertices,
                    indices,
                    makeVertex(xa, ya, z, ca),
                    makeVertex(xb, ya, z, cb),
                    makeVertex(xb, yb, z, cb),
                    makeVertex(xa, yb, z, ca));
            };
            // A horizontal frame bar fading toward the middle from both ends.
            const auto push_bar = [&](const double y) {
                push_face(x0, y, box_half, middle_x, y + thickness, dark_half);
                push_face(middle_x, y, dark_half, x1, y + thickness, box_half);
            };

            // Corner holders (background L behind the dark L).
            for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
            {
                push_fan(g_holder_background, origin_x, x_sign, z, box_solid);
                push_fan(g_holder_front, origin_x, x_sign, z, packAbgr(g_chord_box_dark_color));
            }

            // Frame: bottom bar always; accent chevrons, full sides with a top bar, or short
            // fading sides (the reference's three variants).
            push_bar(y0);
            if (group.any_accent)
            {
                const double dx = (x1 - x0) / 3.0;
                const double y2 = y1 + (thickness * 2.0);
                for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
                {
                    // The reference's chevron strip, unrolled to triangles.
                    const std::array<std::array<double, 2>, 6> strip{
                        {{0.0, y0},
                         {thickness * 2.0, y0},
                         {0.0, y2},
                         {thickness * 2.0, y1},
                         {dx, y2},
                         {dx + thickness, y1}}
                    };
                    const std::array<std::uint32_t, 6> strip_colors{
                        box_solid, box_solid, box_solid, box_solid, box_half, box_half
                    };
                    const auto base = static_cast<std::uint16_t>(vertices.size());
                    for (std::size_t point = 0; point < strip.size(); ++point)
                    {
                        vertices.push_back(makeVertex(
                            origin_x + (x_sign * strip.at(point)[0]),
                            strip.at(point)[1],
                            z,
                            strip_colors.at(point)));
                    }
                    for (std::size_t point = 0; point + 2 < strip.size(); ++point)
                    {
                        indices.push_back(static_cast<std::uint16_t>(base + point));
                        indices.push_back(static_cast<std::uint16_t>(base + point + 1));
                        indices.push_back(static_cast<std::uint16_t>(base + point + 2));
                    }
                }
            }
            else if (with_top)
            {
                for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
                {
                    push_face(
                        origin_x, y0, box_half, origin_x + (x_sign * thickness), y1, box_half);
                }
                push_bar(y1);
            }
            else
            {
                const double fade_start_y = (y0 + y1) / 2.0;
                for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
                {
                    const double column_x1 = origin_x + (x_sign * thickness);
                    push_face(origin_x, y0, box_half, column_x1, fade_start_y, box_half);
                    pushQuad(
                        vertices,
                        indices,
                        makeVertex(origin_x, fade_start_y, z, box_half),
                        makeVertex(column_x1, fade_start_y, z, box_half),
                        makeVertex(column_x1, y1, z, box_clear),
                        makeVertex(origin_x, y1, z, box_clear));
                }
            }

            // Filling: the faint panel, darker toward the middle.
            push_face(x0, y0, box_faint, middle_x, y1, dark_faint);
            push_face(middle_x, y0, dark_faint, x1, y1, box_faint);

            // Mute cross: thin X strokes on the repeat box alone (a full box's notes carry
            // their own mute markers), light for full mutes and dark for palm mutes.
            if (group.box_only && group.common_mute != common::core::NoteMute::None)
            {
                const double center_y = (y0 + y1) / 2.0;
                const bool full = group.common_mute == common::core::NoteMute::Full;
                const double d0y = 0.8 * (y1 - center_y);
                const double d1y = (full ? 0.95 : 0.9) * (y1 - center_y);
                const double d0x = full ? d0y : 0.8 * (x1 - middle_x);
                const double d1x = full ? d1y : 0.9 * (x1 - middle_x);
                const std::uint32_t cross =
                    packAbgr(full ? g_chord_full_mute_cross_color : g_chord_palm_mute_cross_color);
                pushQuad(
                    vertices,
                    indices,
                    makeVertex(middle_x - d1x, center_y + d0y, z, cross),
                    makeVertex(middle_x - d0x, center_y + d1y, z, cross),
                    makeVertex(middle_x + d1x, center_y - d0y, z, cross),
                    makeVertex(middle_x + d0x, center_y - d1y, z, cross));
                pushQuad(
                    vertices,
                    indices,
                    makeVertex(middle_x + d1x, center_y + d0y, z, cross),
                    makeVertex(middle_x - d0x, center_y - d1y, z, cross),
                    makeVertex(middle_x - d1x, center_y - d0y, z, cross),
                    makeVertex(middle_x + d0x, center_y + d1y, z, cross));
            }
        }
        submitBatch(vertices, indices, posColorLayout(), color_program.get(), nullptr);
    }

    const std::array<float, 4> head_cell = atlases.head_layout.cellRect(g_head_cell_standard);
    const std::array<float, 4> anticipation_cell =
        atlases.head_layout.cellRect(g_head_cell_anticipation);
    // The reference head is a square quad (0.96 x 0.96 world units), not a lane-squashed one.
    const double head_half_w = metrics.note_half_width;
    const double head_half_h = metrics.note_half_width;

    // Projected on-screen length between two world points, for adaptive tail sampling.
    const auto projected_pixels = [&](const double x0,
                                      const double y0,
                                      const double z0,
                                      const double x1,
                                      const double y1,
                                      const double z1) {
        const std::array<double, 3> a = world_to_clip.projectPoint(x0, y0, z0);
        const std::array<double, 3> b = world_to_clip.projectPoint(x1, y1, z1);
        const double dx = (b[0] - a[0]) * 0.5 * static_cast<double>(width);
        const double dy = (b[1] - a[1]) * 0.5 * static_cast<double>(height);
        return std::sqrt((dx * dx) + (dy * dy));
    };

    // Slide state at a time: the eased X offset from the note's base fret plus the alpha dim of
    // unpitched (pressure-release) glides.
    struct SlideState
    {
        double x_offset;
        double alpha;
    };
    const auto slide_state_at =
        [&](const common::core::HighwayNoteView& note, const double base_x, const double seconds) {
            if (note.slides.empty() || note.fret <= 0)
            {
                return SlideState{.x_offset = 0.0, .alpha = 1.0};
            }
            double segment_start_seconds = note.start_seconds;
            double segment_start_x = base_x;
            for (const common::core::HighwaySlideView& waypoint : note.slides)
            {
                const double waypoint_x =
                    common::core::highwayNoteCenterX(waypoint.fret, metrics, mirrored);
                if (seconds <= waypoint.seconds)
                {
                    const double span = waypoint.seconds - segment_start_seconds;
                    const double progress =
                        span > 0.0 ? std::clamp((seconds - segment_start_seconds) / span, 0.0, 1.0)
                                   : 1.0;
                    const double weight =
                        common::core::highwaySlideEaseWeight(progress, waypoint.unpitched);
                    const double alpha =
                        waypoint.unpitched ? 1.0 + ((g_unpitched_slide_end_alpha - 1.0) * progress)
                                           : 1.0;
                    return SlideState{
                        .x_offset =
                            segment_start_x + ((waypoint_x - segment_start_x) * weight) - base_x,
                        .alpha = alpha,
                    };
                }
                segment_start_seconds = waypoint.seconds;
                segment_start_x = waypoint_x;
            }
            // Past the last waypoint the glide holds its target (and any unpitched dimming).
            const common::core::HighwaySlideView& last = note.slides.back();
            return SlideState{
                .x_offset = common::core::highwayNoteCenterX(last.fret, metrics, mirrored) - base_x,
                .alpha = last.unpitched ? g_unpitched_slide_end_alpha : 1.0,
            };
        };

    for (const std::size_t index : visible)
    {
        const common::core::HighwayNoteView& note = state.notes[index];
        const ChordGroup& group = chord_groups[note_group[index - first_note]];
        if (group.box_only)
        {
            // Repeated and dead strums render as their repeat box alone (reference visibility):
            // no heads, shadows, tails, or anticipation for the group's notes.
            continue;
        }
        const double lane_y =
            common::core::highwayStringLaneY(note.string, state.string_count, metrics, invert);
        const ArgbColor base_color = stringLaneColor(note.string, state.string_count, palette);
        const StringLaneStyle style{base_color};
        const double fade =
            note.start_seconds >= now_seconds
                ? 1.0
                : std::max(0.0, 1.0 - ((now_seconds - note.start_seconds) / g_passed_fade_seconds));

        // Bend geometry: lift per semitone, inverted on the upper displayed half so curves stay
        // inside the board; the head anchors at the onset's bend value (a prebend shows).
        const int displayed_lane = invert ? (state.string_count + 1 - note.string) : note.string;
        const double bend_direction =
            common::core::highwayBendInverted(displayed_lane, state.string_count) ? -1.0 : 1.0;
        const auto note_y_at = [&](const double seconds, const double taper) {
            double semitones =
                common::core::highwayBendSemitonesAt(note.bend, note.start_seconds, seconds);
            if (note.vibrato)
            {
                semitones +=
                    taper * common::core::highwayVibratoWobble(seconds - note.start_seconds);
            }
            return lane_y + (bend_direction * metrics.bend_lift_per_half_step * semitones);
        };
        const double head_y = note_y_at(note.start_seconds, 0.0);

        // Sustain tail: from the hit line (while sounding) or the onset to the sustain end, as
        // the reference's three-band ribbon (solid edges around a translucent core). Technique
        // notes modulate the centerline, sampled adaptively in screen space.
        if (note.end_seconds > note.start_seconds && note.end_seconds > now_seconds)
        {
            const double tail_from = std::max(note.start_seconds, now_seconds);
            const double tail_to = std::min(note.end_seconds, span_end_seconds);

            // Band X stations: fretted tails straddle the note center, open tails span the hand
            // window with the reference's inset (with a degenerate-window guard for tapered
            // necks).
            double band_x0 = 0.0;
            double band_x1 = 0.0;
            double band_x2 = 0.0;
            double band_x3 = 0.0;
            bool band_valid = tail_to > tail_from;
            double base_x = 0.0;
            if (note.fret > 0)
            {
                base_x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);
                const double half = metrics.tail_half_width;
                band_x0 = base_x - half;
                band_x1 = base_x - (half / 2.0);
                band_x2 = base_x + (half / 2.0);
                band_x3 = base_x + half;
            }
            else
            {
                const auto [rail_low, rail_high] = activeFhpFretLines(state, note.start_seconds);
                const double low_x = common::core::highwayFretLineX(rail_low, metrics, mirrored);
                const double high_x = common::core::highwayFretLineX(rail_high, metrics, mirrored);
                const auto [window_x0, window_x1] = std::minmax(low_x, high_x);
                band_x0 = window_x0 + g_open_tail_margin;
                band_x3 = window_x1 - g_open_tail_margin;
                band_x1 = band_x0 + g_open_tail_margin;
                band_x2 = band_x3 - g_open_tail_margin;
                band_valid = band_valid && (band_x3 - band_x0 > 2.0 * g_open_tail_margin);
                base_x = (band_x0 + band_x3) / 2.0;
            }

            const bool modulated =
                note.vibrato || note.tremolo || !note.bend.empty() || !note.slides.empty();
            if (band_valid && !modulated)
            {
                pushTailRibbon(
                    rail_vertices,
                    rail_indices,
                    band_x0,
                    band_x1,
                    band_x2,
                    band_x3,
                    lane_y,
                    time_to_z(tail_from),
                    time_to_z(tail_to),
                    packAbgr(style.tail),
                    packAbgr(style.tail, g_tail_inner_alpha));
            }
            else if (band_valid)
            {
                const double pixels = projected_pixels(
                    base_x, lane_y, time_to_z(tail_from), base_x, lane_y, time_to_z(tail_to));
                const std::size_t uniform_count = common::core::highwayTailSampleCount(
                    pixels, g_tail_pixels_per_sample, g_tail_sample_cap);
                const std::vector<double> sample_times = common::core::makeHighwayTailSampleTimes(
                    note, tail_from, tail_to, uniform_count);

                struct TailSample
                {
                    double x_offset;
                    double y;
                    double z;
                    double alpha;
                };
                std::vector<TailSample> samples;
                samples.reserve(sample_times.size());
                const double duration = note.end_seconds - note.start_seconds;
                for (const double seconds : sample_times)
                {
                    // Taper progresses over the full note duration, so wobbles anchor on the
                    // string line at the true tail ends even when the hit line clips the view.
                    const double taper = common::core::highwayTailTaper(
                        (seconds - note.start_seconds) / duration,
                        common::core::g_highway_tail_taper_fraction);
                    const SlideState slide = slide_state_at(note, base_x, seconds);
                    double x_offset = slide.x_offset;
                    if (note.tremolo)
                    {
                        x_offset +=
                            metrics.tail_half_width * taper *
                            common::core::highwayTremoloWobble(seconds - note.start_seconds);
                    }
                    samples.push_back(
                        TailSample{
                            .x_offset = x_offset,
                            .y = note_y_at(seconds, taper),
                            .z = time_to_z(seconds),
                            .alpha = slide.alpha,
                        });
                }
                for (std::size_t sample = 1; sample < samples.size(); ++sample)
                {
                    const TailSample& a = samples[sample - 1];
                    const TailSample& b = samples[sample];
                    const std::uint32_t edge_a = packAbgr(style.tail, a.alpha);
                    const std::uint32_t edge_b = packAbgr(style.tail, b.alpha);
                    const std::uint32_t inner_a =
                        packAbgr(style.tail, g_tail_inner_alpha * a.alpha);
                    const std::uint32_t inner_b =
                        packAbgr(style.tail, g_tail_inner_alpha * b.alpha);
                    const auto push_segment = [&](const double x_from,
                                                  const double x_to,
                                                  const std::uint32_t color_a,
                                                  const std::uint32_t color_b) {
                        pushQuad(
                            rail_vertices,
                            rail_indices,
                            makeVertex(x_from + a.x_offset, a.y, a.z, color_a),
                            makeVertex(x_to + a.x_offset, a.y, a.z, color_a),
                            makeVertex(x_to + b.x_offset, b.y, b.z, color_b),
                            makeVertex(x_from + b.x_offset, b.y, b.z, color_b));
                    };
                    push_segment(band_x0, band_x1, edge_a, edge_b);
                    push_segment(band_x1, band_x2, inner_a, inner_b);
                    push_segment(band_x2, band_x3, edge_a, edge_b);
                }
            }
        }

        if (fade <= 0.0)
        {
            continue;
        }
        const double z = time_to_z(note.start_seconds);

        // Marker quads composite over the head base exactly like the reference's CPU-composited
        // per-status textures (alpha "over" is associative), so the atlas cells draw directly.
        const auto push_marker = [&](const double center_x,
                                     const double center_y,
                                     const double marker_z,
                                     const double cos_r,
                                     const double sin_r,
                                     const int cell,
                                     const std::uint32_t marker_tint) {
            const std::array<float, 4> rect = atlases.head_layout.cellRect(cell);
            const auto corner =
                [&](const double dx, const double dy, const float u, const float v) {
                    return makeUvVertex(
                        center_x + (dx * cos_r) - (dy * sin_r),
                        center_y + (dx * sin_r) + (dy * cos_r),
                        marker_z,
                        marker_tint,
                        u,
                        v);
                };
            pushQuad(
                head_vertices,
                head_indices,
                corner(-head_half_w, -head_half_h, rect[0], rect[3]),
                corner(head_half_w, -head_half_h, rect[2], rect[3]),
                corner(head_half_w, head_half_h, rect[2], rect[1]),
                corner(-head_half_w, head_half_h, rect[0], rect[1]));
        };

        if (note.fret == 0)
        {
            // Open string: the reference's thin rounded bar spanning the active hand window, in
            // the full note color (the flat tail-width slab it replaces read as a plank).
            const auto [low_line, high_line] = activeFhpFretLines(state, note.start_seconds);
            const double low_x = common::core::highwayFretLineX(low_line, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(high_line, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            pushOpenNoteBar(
                open_vertices, open_indices, x0, x1, head_y, z, packAbgr(base_color, fade), 1.0);
            if (note.accent)
            {
                // The reference's accent halo: the same bar at triple thickness, faint.
                pushOpenNoteBar(
                    open_vertices,
                    open_indices,
                    x0,
                    x1,
                    head_y,
                    z,
                    packAbgr(base_color, fade * (96.0 / 255.0)),
                    3.0);
            }
            // Technique markers at the window center (the reference's open-note overlay set).
            if (atlases.reference_cells)
            {
                const double center_x = (x0 + x1) / 2.0;
                const std::uint32_t marker_tint = packAbgr(base_color, fade);
                if (note.attack == common::core::NoteAttack::Pull)
                {
                    push_marker(center_x, head_y, z, 1.0, 0.0, g_head_cell_pull_off, marker_tint);
                }
                if (note.mute == common::core::NoteMute::Palm)
                {
                    push_marker(center_x, head_y, z, 1.0, 0.0, g_head_cell_palm_mute, marker_tint);
                }
                else if (note.mute == common::core::NoteMute::Full)
                {
                    push_marker(center_x, head_y, z, 1.0, 0.0, g_head_cell_full_mute, marker_tint);
                }
                if (note.attack == common::core::NoteAttack::Slap)
                {
                    push_marker(center_x, head_y, z, 1.0, 0.0, g_head_cell_slap, marker_tint);
                }
                else if (note.attack == common::core::NoteAttack::Pop)
                {
                    push_marker(center_x, head_y, z, 1.0, 0.0, g_head_cell_pop, marker_tint);
                }
            }
            continue;
        }

        // Fretted head anchor: the fret-slot middle, or the true touch position for a harmonic
        // sounding between frets (the chart's fractional node point).
        double x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);
        if (note.harmonic != common::core::NoteHarmonic::None && note.touch.has_value())
        {
            const double touch = *note.touch;
            const double touch_floor = std::floor(touch);
            const auto touch_fret = static_cast<int>(touch_floor);
            const double left_x = common::core::highwayFretLineX(touch_fret, metrics, mirrored);
            const double right_x =
                common::core::highwayFretLineX(touch_fret + 1, metrics, mirrored);
            x = left_x + ((right_x - left_x) * (touch - touch_floor));
        }

        // Chord membership decides the rolling flip, the shadow, and the chord box (the
        // reference skips shadows for chord notes).
        const bool in_chord = group.count >= 2;

        // Note shadow: the reference's vertical gradient fan — a string-colored glow rising
        // from the board toward the head, load-bearing for depth perception.
        if (!in_chord)
        {
            const double base_half = head_half_w * 0.5;
            const double apex_y = std::max(head_y - 0.3, 0.05);
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
                    head_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[0],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    head_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    head_y + half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[1]),
                makeUvVertex(
                    x - half,
                    head_y + half,
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

        // Head base: the technique variant under left-hand technique markers, else the standard
        // head (the reference's base-cell selection).
        const bool tech_head =
            atlases.reference_cells && (note.mute == common::core::NoteMute::Full ||
                                        note.harmonic == common::core::NoteHarmonic::Natural ||
                                        note.attack == common::core::NoteAttack::Hammer ||
                                        note.attack == common::core::NoteAttack::Pull);
        const std::array<float, 4> base_cell =
            tech_head ? atlases.head_layout.cellRect(g_head_cell_tech) : head_cell;
        const auto corner = [&](const double dx, const double dy, const float u, const float v) {
            return makeUvVertex(
                x + (dx * cos_r) - (dy * sin_r),
                head_y + (dx * sin_r) + (dy * cos_r),
                z,
                tint,
                u,
                v);
        };
        pushQuad(
            head_vertices,
            head_indices,
            corner(-head_half_w, -head_half_h, base_cell[0], base_cell[3]),
            corner(head_half_w, -head_half_h, base_cell[2], base_cell[3]),
            corner(head_half_w, head_half_h, base_cell[2], base_cell[1]),
            corner(-head_half_w, head_half_h, base_cell[0], base_cell[1]));

        if (atlases.reference_cells)
        {
            // Rotating markers ride the rolling flip (the reference bakes these into the head
            // texture), in the reference's composite order.
            if (note.harmonic == common::core::NoteHarmonic::Natural)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_harmonic, tint);
            }
            else if (note.harmonic == common::core::NoteHarmonic::Pinch)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_pinch_harmonic, tint);
            }
            if (note.mute == common::core::NoteMute::Palm)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_palm_mute, tint);
            }
            if (note.attack == common::core::NoteAttack::Tap)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_tap, tint);
            }
            else if (note.attack == common::core::NoteAttack::Slap)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_slap, tint);
            }
            else if (note.attack == common::core::NoteAttack::Pop)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_pop, tint);
            }
            if (note.accent)
            {
                push_marker(x, head_y, z, cos_r, sin_r, g_head_cell_accent, tint);
            }
            // Upright markers stay flat through the flip (the reference overlays these after
            // the rotated head).
            if (note.mute == common::core::NoteMute::Full)
            {
                push_marker(x, head_y, z, 1.0, 0.0, g_head_cell_full_mute, tint);
            }
            if (note.attack == common::core::NoteAttack::Hammer)
            {
                push_marker(x, head_y, z, 1.0, 0.0, g_head_cell_hammer_on, tint);
            }
            else if (note.attack == common::core::NoteAttack::Pull)
            {
                push_marker(x, head_y, z, 1.0, 0.0, g_head_cell_pull_off, tint);
            }
        }
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
    // Fret lines run from the board (y = 0) to here; with lanes centered on half-string offsets
    // (highwayLaneToY) this leaves an equal half-string margin above the top lane and below the
    // bottom one.
    const double face_top_y =
        static_cast<double>(std::max(state.string_count, 1)) * metrics.string_distance;
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

    // --- Fingering panel and arpeggio brackets for the active hand shape, on the board face
    // after the skin (the reference's pass order). Suppressed while the current chord is fully
    // muted — dead chugs show no fingering. ---
    {
        // The active shape: the last one starting within the reference's 20 ms lookahead that
        // is still running.
        const common::core::HighwayShapeView* active_shape = nullptr;
        for (const common::core::HighwayShapeView& shape : state.shapes)
        {
            if (shape.start_seconds > now_seconds + 0.02)
            {
                break;
            }
            active_shape = &shape;
        }
        if (active_shape != nullptr && active_shape->end_seconds < now_seconds)
        {
            active_shape = nullptr;
        }
        if (active_shape != nullptr && !active_shape->arpeggio)
        {
            // Fully-muted current chord: find the chord group at or before the lookahead.
            const ChordGroup* current_group = nullptr;
            for (const ChordGroup& group : chord_groups)
            {
                if (group.start_seconds > now_seconds + 0.02)
                {
                    break;
                }
                current_group = &group;
            }
            if (current_group != nullptr &&
                current_group->start_seconds >= active_shape->start_seconds &&
                current_group->count >= 2 &&
                current_group->common_mute == common::core::NoteMute::Full)
            {
                active_shape = nullptr;
            }
        }

        if (active_shape != nullptr)
        {
            // Arpeggio brackets: one bracket per posture string in the string color (open
            // strings bracket the hand window's ends instead).
            if (active_shape->arpeggio && atlases.reference_cells)
            {
                std::vector<PosColorUvVertex> vertices;
                std::vector<std::uint16_t> indices;
                const auto [low_line, high_line] =
                    activeFhpFretLines(state, active_shape->start_seconds);
                const auto push_bracket = [&](const int cell,
                                              const double center_x,
                                              const double center_y,
                                              const std::uint32_t tint,
                                              const bool mirror_u) {
                    const std::array<float, 4> rect = atlases.head_layout.cellRect(cell);
                    const float u0 = mirror_u ? rect[2] : rect[0];
                    const float u1 = mirror_u ? rect[0] : rect[2];
                    pushQuad(
                        vertices,
                        indices,
                        makeUvVertex(
                            center_x - head_half_w, center_y - head_half_h, 0.0, tint, u0, rect[3]),
                        makeUvVertex(
                            center_x + head_half_w, center_y - head_half_h, 0.0, tint, u1, rect[3]),
                        makeUvVertex(
                            center_x + head_half_w, center_y + head_half_h, 0.0, tint, u1, rect[1]),
                        makeUvVertex(
                            center_x - head_half_w,
                            center_y + head_half_h,
                            0.0,
                            tint,
                            u0,
                            rect[1]));
                };
                for (const common::core::HighwayShapeStringView& entry : active_shape->strings)
                {
                    const double y = common::core::highwayStringLaneY(
                        entry.string, state.string_count, metrics, invert);
                    const std::uint32_t tint =
                        packAbgr(stringLaneColor(entry.string, state.string_count, palette));
                    if (entry.fret > 0)
                    {
                        push_bracket(
                            g_head_cell_arpeggio_fret_bracket,
                            common::core::highwayNoteCenterX(entry.fret, metrics, mirrored),
                            y,
                            tint,
                            false);
                    }
                    else
                    {
                        push_bracket(
                            g_head_cell_arpeggio_open_bracket,
                            common::core::highwayNoteCenterX(low_line + 1, metrics, mirrored),
                            y,
                            tint,
                            false);
                        push_bracket(
                            g_head_cell_arpeggio_open_bracket,
                            common::core::highwayNoteCenterX(high_line, metrics, mirrored),
                            y,
                            tint,
                            true);
                    }
                }
                submitBatch(
                    vertices,
                    indices,
                    posColorUvLayout(),
                    texture_tint_program.get(),
                    &heads_texture);
            }

            // Fingering spots: barre-aware shape cells plus finger-name cells from the
            // fingering texture (a real-alpha PNG, so the premultiplied blend applies).
            if (fingering_texture.isValid())
            {
                std::vector<PosColorUvVertex> vertices;
                std::vector<std::uint16_t> indices;
                const double spot_half = metrics.string_distance / 2.0;
                const std::uint32_t white = packAbgr(0xFFFFFFFF);
                // Quarter-grid UV cells with the reference's inset.
                const auto cell_uv = [](const int column, const int row) {
                    return std::array<float, 4>{
                        static_cast<float>((column * 0.25) + 0.001),
                        static_cast<float>((row * 0.25) + 0.001),
                        static_cast<float>((column * 0.25) + 0.249),
                        static_cast<float>((row * 0.25) + 0.249),
                    };
                };
                const std::array<std::array<float, 4>, 5> finger_name_cells{
                    cell_uv(3, 0), cell_uv(0, 1), cell_uv(1, 1), cell_uv(2, 1), cell_uv(3, 1)
                };
                const auto push_spot = [&](const int fret,
                                           const double lane_y,
                                           const std::array<float, 4>& uv,
                                           const bool flip_v) {
                    const double x = common::core::highwayNoteCenterX(fret, metrics, mirrored);
                    const float v0 = flip_v ? uv[3] : uv[1];
                    const float v1 = flip_v ? uv[1] : uv[3];
                    pushQuad(
                        vertices,
                        indices,
                        makeUvVertex(x - spot_half, lane_y - spot_half, 0.0, white, uv[0], v1),
                        makeUvVertex(x + spot_half, lane_y - spot_half, 0.0, white, uv[2], v1),
                        makeUvVertex(x + spot_half, lane_y + spot_half, 0.0, white, uv[2], v0),
                        makeUvVertex(x - spot_half, lane_y + spot_half, 0.0, white, uv[0], v0));
                };

                // Collect each finger's displayed-lane range and fret (a barre when it spans).
                struct FingerSpan
                {
                    int low_lane{0};
                    int high_lane{0};
                    int fret{0};
                    bool used{false};
                };
                std::array<FingerSpan, 5> fingers{};
                for (const common::core::HighwayShapeStringView& entry : active_shape->strings)
                {
                    if (!entry.finger.has_value() || *entry.finger < 0 || *entry.finger > 4 ||
                        entry.fret <= 0)
                    {
                        continue;
                    }
                    const int lane =
                        invert ? (state.string_count + 1 - entry.string) : entry.string;
                    FingerSpan& span = fingers.at(static_cast<std::size_t>(*entry.finger));
                    if (!span.used)
                    {
                        span = FingerSpan{
                            .low_lane = lane, .high_lane = lane, .fret = entry.fret, .used = true
                        };
                    }
                    else
                    {
                        span.low_lane = std::min(span.low_lane, lane);
                        span.high_lane = std::max(span.high_lane, lane);
                        span.fret = entry.fret;
                    }
                }
                const auto lane_center_y = [&](const int lane) {
                    return common::core::highwayLaneToY(lane, metrics);
                };
                for (std::size_t finger = 0; finger < fingers.size(); ++finger)
                {
                    const FingerSpan& span = fingers.at(finger);
                    if (!span.used)
                    {
                        continue;
                    }
                    if (span.low_lane == span.high_lane)
                    {
                        push_spot(span.fret, lane_center_y(span.low_lane), cell_uv(0, 0), false);
                        push_spot(
                            span.fret,
                            lane_center_y(span.low_lane),
                            finger_name_cells.at(finger),
                            false);
                        continue;
                    }
                    // Barre: an upright end at the top lane (with the finger name), middles
                    // between, and a flipped end at the bottom lane.
                    push_spot(span.fret, lane_center_y(span.high_lane), cell_uv(1, 0), false);
                    push_spot(
                        span.fret,
                        lane_center_y(span.high_lane),
                        finger_name_cells.at(finger),
                        false);
                    for (int lane = span.low_lane + 1; lane < span.high_lane; ++lane)
                    {
                        push_spot(span.fret, lane_center_y(lane), cell_uv(2, 0), false);
                    }
                    push_spot(span.fret, lane_center_y(span.low_lane), cell_uv(1, 0), true);
                }
                const bgfx::TextureHandle fingering = fingering_texture.get();
                submitBatch(
                    vertices,
                    indices,
                    posColorUvLayout(),
                    texture_program.get(),
                    &fingering,
                    g_board_view,
                    g_premultiplied_state);
            }
        }
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
            return pushGlyphText(
                glyph_vertices,
                glyph_indices,
                atlases.glyph_layout,
                text,
                left_x,
                baseline_y,
                z,
                glyph_height,
                color);
        };

        // (Fret numbers now scroll down the board with the beats — see the earlier fret-number
        // pass — replacing the static row that used to sit along the bottom of the face here.)

        // Section labels floating above the board at their arrival time.
        const double section_y = face_top_y + (metrics.string_distance * 1.5);
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

        // Chord names ride the hit line while their shape is active (reference placement: left
        // of the hand window, above the top lane), skipped once the shape is about to end.
        const double chord_name_y = face_top_y - (metrics.string_distance * 0.5) + 0.5;
        for (const common::core::HighwayShapeView& shape : state.shapes)
        {
            if (shape.name.empty() || shape.end_seconds < now_seconds ||
                shape.start_seconds > span_end_seconds)
            {
                continue;
            }
            if (shape.start_seconds <= now_seconds && shape.end_seconds <= now_seconds + 0.15)
            {
                continue;
            }
            const auto [low_line, high_line] = activeFhpFretLines(state, shape.start_seconds);
            const double low_x = std::min(
                common::core::highwayFretLineX(low_line, metrics, mirrored),
                common::core::highwayFretLineX(high_line, metrics, mirrored));
            (void)push_text(
                shape.name,
                low_x - 1.75,
                chord_name_y,
                std::max(0.0, time_to_z(shape.start_seconds)),
                0.7,
                packAbgr(g_chord_name_color));
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
