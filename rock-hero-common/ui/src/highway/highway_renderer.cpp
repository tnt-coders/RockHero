#include "highway/bgfx_program.h"
#include "highway/box_mute_profile.h"
#include "highway/highway_atlas.h"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <numbers>
#include <ranges>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_hit_glow.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/highway/highway_window.h>
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

// Backdrop clear color behind the whole scene (0xRRGGBBAA); cleared to black.
constexpr std::uint32_t g_backdrop_color = 0x000000ff;

// Board-furniture colors (0xAARRGGBB), source-verified 2026-07-11.
constexpr ArgbColor g_beat_bar_color = 0xFF0F3B5E; // beat and measure bars alike

// Measure downbeats (and the per-note fret-span lines, which reuse the shape) draw a sharp
// chord-box-teal attack line exactly where they occur, then a brief beat-blue fade trailing
// away down the measure — half the old symmetric-wings footprint (user direction).
constexpr double g_attack_line_half_length = 0.025;
constexpr double g_attack_fade_length = 0.2;
constexpr double g_attack_line_alpha = 0.85; // full teal read slightly too bright (user-tuned)
// The board's lit lane appearance: what each lane's surface looks like where the hand-window
// light reveals it (plain lanes the runway blue, inlay-dotted lanes distinctly darker — the
// intrinsic board pattern). The light carries these as vertex color and the per-fragment mask
// reveals them, the physically honest reading (user rule 2026-07-23): nothing is painted over
// or under the light, and away from it the board draws nothing and stays dark.
constexpr ArgbColor g_lit_lane_color = 0x402590E8;
constexpr ArgbColor g_lit_lane_dotted_color = 0x40185C94;
// The falloff band the light dissolves across, centered on each eased edge (half strength at
// the edge, gone half a band outside, full half a band inside), in world units. The centered
// placement carries the lit region's width; this constant alone sets settled-edge sharpness.
constexpr double g_window_light_falloff = 0.55;
// How strongly the light dims while it sweeps through a transition, at full steepness. The
// motion-dim model (user, 2026-07-23, replacing two edge-widening attempts that either bulged
// the silhouette or collapsed the lit core): the exterior shape keeps the settled fade
// cross-section along the entire eased contour, and the transition's fading lives in overall
// brightness instead — a light rushing across lanes cannot fully illuminate them, so the lit
// strip dips toward this fraction darker at peak sweep speed and recovers by arrival
// (tuned on sight 0.6 -> 0.85 -> 0.95, 2026-07-23; the sin-squared bell keeps the overall feel
// gentler than the old full-length plateau even at this depth).
constexpr double g_window_morph_dim = 0.95;
// Tapping-hand light envelope (right-hand-tap-lighting plan): each tap onset lights its own
// tapped fret lanes along the timeline, rising over the approach side of the tap, holding
// through sustained contact (morphing with pitched glides), and decaying after the fingers
// release, so the light dips between consecutive taps exactly as the finger lifts (user rule
// 2026-07-28 — deliberately per-onset, never merged into runs). The rise duration is each
// onset's projection-derived ramp_seconds — the fret-hand placements' own margin-based arrival
// rule (user rule 2026-07-28, replacing a fixed wall-clock rise that read inconsistently) —
// while the decay stays a short visual constant: the release is a gesture, not an arrival.
constexpr double g_tap_light_decay_seconds = 0.1;
// The lane-border ribbons release much more slowly than the floor light (user rule 2026-07-28:
// per-tap ribbon flashing read as jarring in tap sections): the light pulses with each strike
// while the brightened edges bridge the gaps of a dense run, fading only once the run ends.
constexpr double g_tap_ribbon_decay_seconds = 0.45;
// Sustain slope shading (user direction 2026-07-28): the modulated tail's centerline slope
// modulates its brightness like a surface tilting under a fixed light, so a bend's climb,
// hold, and release — and a vibrato's wobble — read from shading alone even where screen-space
// lift is foreshortened at center screen. Slope is normalized by the lane's bend-lift
// direction so a climbing PITCH always brightens regardless of which way the lane draws it.
// Gain scales world dy/dz into [-1, 1]; depth is the full brighten/darken mix at saturation.
constexpr double g_tail_slope_shade_gain = 4.8;
constexpr double g_tail_slope_shade_depth = 0.5;
// Tent-smoothing half-window for the shade, in seconds of tail time. The raw per-sample shade
// tracks the instantaneous derivative, which crosses tanh's linear region within a sample or
// two on a real bend — the shade snapped between base and saturated over a couple of segments,
// and foreshortening at screen center compressed that snap into a hard band that read as a
// sharp point on a smooth curve (user report 2026-07-29). Smoothing over a fixed TIME window
// guarantees the fade-in/out spans the same stretch of tail whatever the sample density or
// viewing angle. Kept under half the vibrato period (0.160s) so the shimmer survives.
constexpr double g_tail_slope_shade_smooth_seconds = 0.05;

// Bend chevron station, in head half-heights from the head center along the drawn bend-lift
// direction (above the note for an upward curve, below on bend-inverted lanes). Derived from
// the atlas pixels, not the quad: the head art fills only the middle ~34% of its cell and the
// glyph band is cell-centered, so this sits deliberately inside bare touch — the chevron's
// legs anchor ON the note's top edge with the apex rising clear, the bend cue's
// overlap (judged on a composite sheet, user 2026-07-29, re-tuned for the taller bolder
// glyph the same day).
constexpr double g_bend_marker_offset_heads = 0.38;
// Pre-bend target outline alpha: the hollow head silhouette parked at a pre-bent note's
// chart-truth height is an annotation, dimmed so the rising head stays the subject (first
// value judged on composite sheets 2026-07-31; expect on-sight retuning).
constexpr double g_prebend_outline_alpha = 0.5;
// The tap light leans the lit lane tint toward the FHP orange (the tap floor numbers' color)
// so the tapping hand's light reads apart from the fretting hand's window at a glance.
constexpr double g_tap_light_warm_mix = 0.3;
constexpr ArgbColor g_lane_border_color = 0x0007928F; // per-fret runway ribbons (alpha varies)
constexpr ArgbColor g_fret_inactive_color = 0xFF202020;
constexpr ArgbColor g_fret_active_color = 0xFFC0C0C0;

// Scrolling fret-number colors (Charter's PREVIEW_3D palette): a bright blue for a dotted fret
// inside the current hand range, the lane-border teal at half alpha elsewhere, and the FHP
// orange for hand-position arrivals and the current hand's numbers at the hit line.
constexpr ArgbColor g_fret_number_active_color = 0xFF87DDF6;
constexpr ArgbColor g_fret_number_dim_color = 0x8007928F;
constexpr ArgbColor g_fret_number_fhp_color = 0xFFFFA821;

// Hand-window activity horizon for the active fret state.
constexpr double g_fret_active_horizon_seconds = 0.5;

// Strike glow (the additive hit light; fret-hit-light-effect plan): the nominal release and
// dark-trough guard feeding highwayHitGlowRelease, the light's colour (a hot orange whose
// blue-channel lift lets a peak white out over already-lit content), the soft-edge falloff, the
// hot-core half-width of a fret-line strip, and the fade toward the face top that grounds the
// light at the strings' crossing. The window-light mask reaches 1.0 only falloff / 2 inside an
// edge, so a strip needs core_half >= falloff / 2 to actually peak at full intensity. The glow
// pass walks its own onset window over state.notes, so the release tunes freely — it is not
// bounded by the passed-note fade.
constexpr double g_hit_glow_release_seconds = 0.35;
constexpr double g_hit_glow_trough_guard_seconds = 0.03;
constexpr ArgbColor g_hit_glow_color = 0xFFFFB040;
constexpr double g_hit_glow_falloff = 0.2;
constexpr double g_hit_glow_core_half = g_hit_glow_falloff / 2.0;
constexpr double g_hit_glow_top_fade = 0.35;

// Anticipation ring window before a note lands (500 ms).
constexpr double g_anticipation_seconds = 0.5;

// Board content draws painter-ordered with alpha throughout (a painter's-algorithm model), so one
// blended, depth-test-only state word covers the whole board view. No cull bits on purpose:
// content is camera-facing and the lefty mirror reflects world X, which would invert winding.
constexpr std::uint64_t g_blended_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                          BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA |
                                          BGFX_STATE_MSAA;

// The strike glow adds light on top of whatever it covers instead of repainting it, so a hit
// pops identically on lit and unlit content. SRC_ALPHA -> ONE, not BGFX_STATE_BLEND_ADD
// (ONE -> ONE), which would ignore the soft mask carried in alpha and hard-edge the sprite. No
// WRITE_Z (the board writes no depth) and no WRITE_A (the light must not stomp the destination
// alpha the overlay composite sees).
constexpr std::uint64_t g_additive_state =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE) | BGFX_STATE_MSAA;

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

// How many fret slots the board face draws; charts cap at g_max_fret but the board draws a
// fixed neck. Aliased from core so the drawn board and the camera's whole-neck focus reference
// (highwayFocusWholeNeckX) can never disagree about how long the neck is.
constexpr int g_face_fret_count = common::core::g_highway_fret_count;

// Seconds a passed note takes to fade out after crossing the hit line.
constexpr double g_passed_fade_seconds = 0.15;

// Rolling-flip flat lead: single-note heads land flat this many seconds before the hit line.
// The source game flips fast and late (a 500 ms roll landing flat 100 ms out, its only
// source-verifiable timing — the flip has no documented tie to any internal
// constants); our flip instead spans the whole approach (user request 2026-07-22), and the
// slower final degrees need a longer flat stretch to read as finished before the board face
// (user-tuned).
constexpr double g_flip_flat_lead_seconds = 0.25;

// Tolerance for matching an onset to a shape-span boundary (or grouping simultaneous onsets).
// The core constant (see its rationale there) is shared so this file's chord grouping agrees
// with highwayDisplayHoldEnds, whose span-held notes drive the visible range.
constexpr double g_onset_match_epsilon = common::core::g_highway_onset_match_epsilon;

// Open-note bar cross-section (Charter's OpenNoteModel): a thin hexagonal prism spanning the
// hand window, half-thickness 0.04 at the ends bulging to 0.05 at the center station, squashed
// to a tenth of that in Z. An earlier flat slab at tail width read over 3x too tall.
constexpr double g_open_note_end_half_thickness = 0.04;
constexpr double g_open_note_middle_half_thickness = 0.05;
constexpr double g_open_note_z_squash = 0.1;
constexpr int g_open_note_segments = 6;

// The bar fades to transparent over this run at each end, so it reads as tapering almost to a
// point at the hand-window rails instead of stopping flat (user direction).
constexpr double g_open_note_end_fade_length = 0.5;

// Sustain tails are three-band ribbons in Charter: solid edge strips around an inner band
// Charter draws at 192/255 alpha. Ours is deliberately more translucent so notes stay
// readable through a tail's core (user-tuned). Fretted tails split the tail width
// quarter/half/quarter; open tails span the hand window inset by a margin, with edge bands of
// the same width.
constexpr double g_tail_inner_alpha = 96.0 / 255.0;
constexpr double g_open_tail_margin = 0.2;

// Sustain tails dissolve over this last fraction of the note duration (the glow posts' fade
// toward the note, mirrored at the tip), so a sustain ends softly instead of stopping dead.
constexpr double g_tail_tip_fade_fraction = 0.35;

// Glow posts under single notes stand the tail ribbon cross-section upright at a fraction of
// the tail width; this is the edge alpha where the post meets the floor (user-tuned to stay
// subtle). A post rises from the floor toward its note's lane center, dissolving to nothing
// partway up — the note art simply overlays the post's top (the old
// fade-exactly-at-the-head-quad-bottom invariant is removed, user 2026-07-23) — so every lane
// carries a post whose height scales naturally with the lane's height above the floor.
constexpr double g_shadow_post_floor_alpha = 0.5;

// Fraction of the lane height where the post's dissolve completes: alpha reaches zero at this
// point of the rise rather than at the lane center itself, a slightly more aggressive fade
// (user-tuned 2026-07-23).
constexpr double g_shadow_post_fade_end_fraction = 0.75;

// Open-note L posts: how far the floor foot reaches inward, measured from the bar end (the
// chord box's bottom corner holders, freestanding), fading to nothing at its tip.
constexpr double g_open_post_foot_length = 0.5;

// Adaptive tail sampling for technique-modulated rails: one centerline sample per this many
// projected screen pixels, hard-capped (Charter's per-millisecond-tessellation fix).
constexpr double g_tail_pixels_per_sample = 4.0;
constexpr std::size_t g_tail_sample_cap = 256;

// Unpitched slides release pressure, so their rail dims toward this alpha across the glide.
constexpr double g_unpitched_slide_end_alpha = 0.25;

// Chord-box palette and geometry (Charter's values): a translucent teal panel per strummed
// chord, with corner holders, gradient frame bars, and mute-cross variants.
constexpr ArgbColor g_chord_box_color = 0xFF00D2D5;
constexpr ArgbColor g_chord_box_dark_color = 0xFF003C3D;

// The frame fade's vertex modulation: the per-channel ratio dark/box (rounded), which turns
// art authored in the frame color into the frame's dark middle color when multiplied.
// Channels the box color lacks pass through, as does alpha. Derived so the dark/box
// relationship is stated only by the two constants above.
[[nodiscard]] constexpr ArgbColor frameFadeModulation()
{
    constexpr auto channel = [](const ArgbColor color, const int shift) {
        return (color >> static_cast<unsigned>(shift)) & 0xFFU;
    };
    ArgbColor modulation = 0xFF000000U;
    for (const int shift : {16, 8, 0})
    {
        const std::uint32_t box = channel(g_chord_box_color, shift);
        const std::uint32_t dark = channel(g_chord_box_dark_color, shift);
        const std::uint32_t ratio = box == 0U ? 0xFFU : (((dark * 255U) + (box / 2U)) / box);
        modulation |= ratio << static_cast<unsigned>(shift);
    }
    return modulation;
}
// Repeat-box mute marks render through the SDF shader (fs_box_mute), and chords.png is the
// single source of truth for their look: at renderer creation each mark's cross-section —
// colors, rim/core structure, halo, opacity, exactly as painted — is measured from that file
// into a two-row ramp the shader samples by exact distance from the arm centerlines (see
// box_mute_profile.h for the authoring contract). The distance field is evaluated per
// fragment because the X's arm angle changes with every box aspect — a shape no fixed bitmap
// contains — so bitmap stretching distorted line weight with the box (user findings
// 2026-07-31/08-01) while this holds the painted weights everywhere. Repainting chords.png
// is the only way to restyle the marks; there are deliberately no art constants here.
constexpr ArgbColor g_chord_name_color = 0xFFE0E0E0;

// Hand-shape span rails on the floor: arpeggio spans in Charter's purple, held shapes in
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

// Inlay-dot pattern: fret % 12 in {0, 3, 5, 7, 9} carries a marker.
[[nodiscard]] bool isDottedFret(const int fret)
{
    const int cycle = ((fret % 12) + 12) % 12;
    return cycle == 0 || cycle == 3 || cycle == 5 || cycle == 7 || cycle == 9;
}

// Continuous hand-window extent at a time, as sorted world-X edges: the core query's eased
// fractional lines mapped through the fractional fret-line overload. Fret lines stay fixed —
// these are the sliding window border's positions.
[[nodiscard]] std::pair<double, double> handWindowXAt(
    const common::core::HighwayViewState& state, const double seconds,
    const common::core::HighwayMetrics& metrics, const bool mirrored)
{
    const common::core::HighwayHandWindow window =
        common::core::highwayHandWindowAt(state.fret_hand_positions, seconds);
    const double low_x = common::core::highwayFretLineX(window.low_line, metrics, mirrored);
    const double high_x = common::core::highwayFretLineX(window.high_line, metrics, mirrored);
    return {std::min(low_x, high_x), std::max(low_x, high_x)};
}

// True when the hand window moves anywhere inside a time span (some placement's ramp overlaps
// it): geometry spanning the range must then sample the window instead of holding one extent.
[[nodiscard]] bool handWindowMovesWithin(
    const common::core::HighwayViewState& state, const double from_seconds, const double to_seconds)
{
    return std::ranges::any_of(
        state.fret_hand_positions, [&](const common::core::HighwayFhpView& fhp) {
            return fhp.ramp_seconds > 0.0 && fhp.seconds > from_seconds &&
                   fhp.seconds - fhp.ramp_seconds < to_seconds;
        });
}

// Finest time step a window ramp is sliced at, the slices per fret line of edge travel, and the
// per-ramp slice cap. Density follows the larger of duration and lateral travel: a slow glide
// needs samples in time, while a sixteenth-margin morph across several frets covers most of its
// travel in a handful of milliseconds and facets badly under time-only slicing.
constexpr double g_window_slice_seconds = 0.0125;
constexpr double g_window_slices_per_line = 16.0;
constexpr int g_window_slice_cap = 160;

// Ascending sample times covering [from, to] for window-following geometry: the endpoints, plus
// every overlapping ramp's clamped bounds and interior slices. Settled stretches contribute no
// interior samples — the window is constant there, so segments between samples stay straight.
[[nodiscard]] std::vector<double> windowSampleTimes(
    const common::core::HighwayViewState& state, const double from_seconds, const double to_seconds)
{
    std::vector<double> times{from_seconds};
    for (std::size_t index = 0; index < state.fret_hand_positions.size(); ++index)
    {
        const common::core::HighwayFhpView& fhp = state.fret_hand_positions[index];
        if (fhp.ramp_seconds <= 0.0 || fhp.seconds <= from_seconds ||
            fhp.seconds - fhp.ramp_seconds >= to_seconds)
        {
            continue;
        }
        const double t0 = std::max(fhp.seconds - fhp.ramp_seconds, from_seconds);
        const double t1 = std::min(fhp.seconds, to_seconds);
        // The wider-moving edge's travel in fret-line units, measured from the previous settled
        // window (the nut window before the first placement).
        const double previous_low =
            index > 0 ? static_cast<double>(state.fret_hand_positions[index - 1].fret - 1) : 0.0;
        const double previous_high = index > 0 ? static_cast<double>(
                                                     state.fret_hand_positions[index - 1].fret +
                                                     state.fret_hand_positions[index - 1].width - 1)
                                               : 4.0;
        const double travel_lines = std::max(
            std::abs(static_cast<double>(fhp.fret - 1) - previous_low),
            std::abs(static_cast<double>(fhp.fret + fhp.width - 1) - previous_high));
        const int slices = std::clamp(
            static_cast<int>(std::max(
                (t1 - t0) / g_window_slice_seconds, travel_lines * g_window_slices_per_line)) +
                1,
            1,
            g_window_slice_cap);
        for (int slice = 0; slice <= slices; ++slice)
        {
            times.push_back(t0 + ((t1 - t0) * slice / slices));
        }
    }
    times.push_back(to_seconds);
    std::ranges::sort(times);
    const auto duplicates = std::ranges::unique(times);
    times.erase(duplicates.begin(), duplicates.end());
    return times;
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

// The chord-box frame's signature horizontal fade, stated once: a quad pair split at the
// horizontal middle, colored `end_abgr` at the outer ends and `middle_abgr` at the split.
// The frame bars draw it directly and the palm mute mark rides it as a vertex modulation,
// so the two can never drift apart. `make_vertex` builds each corner as (x, y, abgr).
template <typename Vertex, typename MakeVertex>
void pushMiddleFadedQuads(
    std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y0, const double y1, const std::uint32_t end_abgr,
    const std::uint32_t middle_abgr, MakeVertex&& make_vertex)
{
    const double middle_x = (x0 + x1) / 2.0;
    pushQuad(
        vertices,
        indices,
        make_vertex(x0, y0, end_abgr),
        make_vertex(middle_x, y0, middle_abgr),
        make_vertex(middle_x, y1, middle_abgr),
        make_vertex(x0, y1, end_abgr));
    pushQuad(
        vertices,
        indices,
        make_vertex(middle_x, y0, middle_abgr),
        make_vertex(x1, y0, end_abgr),
        make_vertex(x1, y1, end_abgr),
        make_vertex(middle_x, y1, middle_abgr));
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

// One endpoint of a three-band ribbon segment: a centerline offset applied to the band
// stations, the endpoint position, and the colors at that end. The outer color sits at the
// outermost stations x0/x3 — equal to the edge color for solid-edged runs, transparent for
// open tails so their edges dissolve right at the hand-window rails.
struct RibbonEnd
{
    double x_offset;
    double y;
    double z;
    std::uint32_t edge_abgr;
    std::uint32_t inner_abgr;
    std::uint32_t outer_abgr;
};

// One three-band ribbon segment between two endpoints: edge strips [x0,x1] and [x2,x3] around a
// translucent core [x1,x2] (Charter's tail cross-section), with per-end stations, offsets,
// and colors so a run can bend, fade, and change width along its length, and per-corner outer
// colors so the edge strips can fade across their width. Sustain tails chain these along the
// board; a note glow post stands a single segment upright on the face plane; an open tail's band
// follows the sliding hand window through the per-end stations.
void pushRibbonSegment(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices,
    const std::array<double, 4>& stations_a, const std::array<double, 4>& stations_b,
    const RibbonEnd& a, const RibbonEnd& b)
{
    const auto push_band = [&](const std::size_t from,
                               const std::size_t to,
                               const std::uint32_t from_a,
                               const std::uint32_t to_a,
                               const std::uint32_t from_b,
                               const std::uint32_t to_b) {
        pushQuad(
            vertices,
            indices,
            makeVertex(stations_a.at(from) + a.x_offset, a.y, a.z, from_a),
            makeVertex(stations_a.at(to) + a.x_offset, a.y, a.z, to_a),
            makeVertex(stations_b.at(to) + b.x_offset, b.y, b.z, to_b),
            makeVertex(stations_b.at(from) + b.x_offset, b.y, b.z, from_b));
    };
    push_band(0, 1, a.outer_abgr, a.edge_abgr, b.outer_abgr, b.edge_abgr);
    push_band(1, 2, a.inner_abgr, a.inner_abgr, b.inner_abgr, b.inner_abgr);
    push_band(2, 3, a.edge_abgr, a.outer_abgr, b.edge_abgr, b.outer_abgr);
}

// Constant-cross-section overload for runs whose band never changes width.
void pushRibbonSegment(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double x2, const double x3, const RibbonEnd& a, const RibbonEnd& b)
{
    const std::array<double, 4> stations{x0, x1, x2, x3};
    pushRibbonSegment(vertices, indices, stations, stations, a, b);
}

// Floor quad with per-end colors: Charter's beat-bar gradient wings.
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

// Charter's open-note bar: a hexagonal prism along X across [x0, x1], with the center
// station slightly thicker than the ends and the ring squashed nearly flat in Z. Flat-colored
// and unlit, its silhouette reads as Charter's thin rounded bar from every board-view
// angle. The end stations are fully transparent, fading in over g_open_note_end_fade_length, so
// the bar tapers visually to a point at each end (which also makes end caps pointless — the
// silhouette dissolves before it could show a flat end). The thickness scale draws the
// Charter's accent halo (the same bar at triple cross-section).
void pushOpenNoteBar(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double lane_y, const double z, const ArgbColor argb, const double alpha,
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

    // Five cross-section stations: transparent tips, full-alpha fade-in stations, bulged middle.
    const double fade_length = std::min(g_open_note_end_fade_length, (x1 - x0) / 4.0);
    const std::array<double, 5> station_x{
        x0, x0 + fade_length, (x0 + x1) / 2.0, x1 - fade_length, x1
    };
    const std::array<double, 5> station_half{
        g_open_note_end_half_thickness * thickness_scale,
        g_open_note_end_half_thickness * thickness_scale,
        g_open_note_middle_half_thickness * thickness_scale,
        g_open_note_end_half_thickness * thickness_scale,
        g_open_note_end_half_thickness * thickness_scale,
    };
    const std::array<std::uint32_t, 5> station_abgr{
        packAbgr(argb, 0.0),
        packAbgr(argb, alpha),
        packAbgr(argb, alpha),
        packAbgr(argb, alpha),
        packAbgr(argb, 0.0),
    };

    const auto ring_vertex = [&](const std::size_t station, const std::size_t point) {
        return makeVertex(
            station_x.at(station),
            lane_y + (station_half.at(station) * ring_y.at(point)),
            z + (station_half.at(station) * ring_z.at(point)),
            station_abgr.at(station));
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

// Draws one chord-box panel — corner holders, a frame variant, and the faint filling — into a
// color batch. Geometry is explicit rather than taken from a ChordGroup so both strummed chord
// boxes and arpeggio-styled hand-shape boxes reuse it. A repeat box's mute mark is not drawn
// here: the caller composites the box-scale atlas mute cell over the panel.
// \param full_height_y1 The box top for a full-height box (a repeat box is half this).
// \param box_only Half-height repeat box.
// \param with_top Full sides plus a top bar (3+ note chords); ignored under box_only/accent.
// \param any_accent Accent chevrons on the sides.
// \param frame_thickness Bar/column width of the frame; callers pass the string grid's base
//        height so the bottom bar fills the gap under the grid exactly.
void pushChordBoxPanel(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double z, const double full_height_y1, const bool box_only,
    const bool with_top, const bool any_accent, const double frame_thickness)
{
    const double middle_x = (x0 + x1) / 2.0;
    const double y0 = 0.0;
    const double y1 = box_only ? (y0 + full_height_y1) / 2.0 : full_height_y1;
    // Sets every frame dimension, not only the bottom bar: the accent chevrons and the side
    // columns below scale from it too, which is why it comes in as one value rather than being
    // read per-part.
    const double thickness = frame_thickness;

    const std::uint32_t box_solid = packAbgr(g_chord_box_color);
    const std::uint32_t box_half = packAbgr(g_chord_box_color, 128.0 / 255.0);
    const std::uint32_t dark_half = packAbgr(g_chord_box_dark_color, 128.0 / 255.0);
    const std::uint32_t box_faint = packAbgr(g_chord_box_color, 32.0 / 255.0);
    const std::uint32_t dark_faint = packAbgr(g_chord_box_dark_color, 32.0 / 255.0);
    const std::uint32_t box_clear = packAbgr(g_chord_box_color, 0.0);

    // Corner-holder fan outlines (Charter's ChordBoxHolderModel): a teal L behind a dark L, at
    // each bottom corner. Local coordinates; the right corner mirrors in X. The L legs are
    // sized for a full-height box, so a half-height repeat box scales them to half vertically
    // and a bit narrower horizontally to keep the brackets proportioned to the panel.
    constexpr std::array<std::array<double, 2>, 6> holder_background{
        {{-0.01, -0.01}, {1.01, -0.01}, {1.01, 0.11}, {0.11, 0.11}, {0.11, 1.11}, {-0.01, 1.01}}
    };
    constexpr std::array<std::array<double, 2>, 6> holder_front{
        {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.1}, {0.1, 0.1}, {0.1, 1.1}, {0.0, 1.0}}
    };
    const double holder_x_scale = box_only ? 0.75 : 1.0;
    const double holder_y_scale = box_only ? 0.5 : 1.0;
    const auto push_fan = [&](const std::span<const std::array<double, 2>> points,
                              const double origin_x,
                              const double x_sign,
                              const std::uint32_t abgr) {
        const auto base = static_cast<std::uint16_t>(vertices.size());
        for (const std::array<double, 2>& point : points)
        {
            vertices.push_back(makeVertex(
                origin_x + (x_sign * point[0] * holder_x_scale),
                point[1] * holder_y_scale,
                z,
                abgr));
        }
        for (std::size_t point = 1; point + 1 < points.size(); ++point)
        {
            indices.push_back(base);
            indices.push_back(static_cast<std::uint16_t>(base + point));
            indices.push_back(static_cast<std::uint16_t>(base + point + 1));
        }
    };
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
    // A horizontal frame bar carrying the frame's end-to-middle fade.
    const auto push_bar = [&](const double y) {
        pushMiddleFadedQuads(
            vertices,
            indices,
            x0,
            x1,
            y,
            y + thickness,
            box_half,
            dark_half,
            [&](const double vx, const double vy, const std::uint32_t abgr) {
                return makeVertex(vx, vy, z, abgr);
            });
    };

    for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
    {
        push_fan(holder_background, origin_x, x_sign, box_solid);
        push_fan(holder_front, origin_x, x_sign, packAbgr(g_chord_box_dark_color));
    }

    // Frame: bottom bar always; accent chevrons, full sides with a top bar, or short fading
    // sides (Charter's three variants).
    push_bar(y0);
    if (any_accent)
    {
        const double dx = (x1 - x0) / 3.0;
        const double y2 = y1 + (thickness * 2.0);
        for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
        {
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
            push_face(origin_x, y0, box_half, origin_x + (x_sign * thickness), y1, box_half);
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
    UniqueBgfxHandle<bgfx::ProgramHandle> window_light_program;
    UniqueBgfxHandle<bgfx::ProgramHandle> box_mute_program;

    // Custom uniforms (predefined ones like u_modelViewProj are never created by hand).
    UniqueBgfxHandle<bgfx::UniformHandle> fade_params;
    UniqueBgfxHandle<bgfx::UniformHandle> atlas_sampler;
    UniqueBgfxHandle<bgfx::UniformHandle> window_light_params;
    UniqueBgfxHandle<bgfx::UniformHandle> box_mute_params;
    UniqueBgfxHandle<bgfx::UniformHandle> box_mute_arms;

    // The box-mute marks' measured cross-sections (from chords.png) and the two-row ramp
    // texture the SDF shader samples them through.
    BoxMuteProfiles box_mute_profiles{};
    UniqueBgfxHandle<bgfx::TextureHandle> box_mute_ramp;

    HighwayAtlases atlases;

    // Fretboard skin (one cell per fret); invalid when the asset is missing (plain board).
    UniqueBgfxHandle<bgfx::TextureHandle> inlay_texture;

    // Decoded inlay dimensions, for the half-texel UV inset that keeps interior fret cells from
    // bleeding into each other under minification.
    int inlay_texture_width{0};
    int inlay_texture_height{0};

    // Fingering panel texture (barre shapes + finger names); invalid skips the panel.
    UniqueBgfxHandle<bgfx::TextureHandle> fingering_texture;

    // Retained board-face geometry; rebuilt on chart load, streamed content uses transients.
    UniqueBgfxHandle<bgfx::VertexBufferHandle> face_vertices;
    UniqueBgfxHandle<bgfx::IndexBufferHandle> face_indices;
    std::uint32_t face_index_count{0};

    common::core::HighwayViewState state;
    // Per-note display hold end (the sustain end, span-extended for sustainless strums): feeds
    // the visibility prefix max and pins chord heads at the hit line through their span.
    std::vector<double> display_hold_ends;
    std::vector<double> sustain_prefix_max;
    common::core::HighwayMetrics metrics;
    common::core::HighwayCamera camera;

    // Player scroll speed; a free setting later (25-Q3), the default until then.
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
    auto window_light = linkProgram(shaders.window_light, "window_light");
    auto box_mute = linkProgram(shaders.box_mute, "box_mute");
    if (!color || !color_fade || !texture_tint || !glyph || !texture || !window_light || !box_mute)
    {
        const auto& failed = !color          ? color.error()
                             : !color_fade   ? color_fade.error()
                             : !texture_tint ? texture_tint.error()
                             : !glyph        ? glyph.error()
                             : !texture      ? texture.error()
                             : !window_light ? window_light.error()
                                             : box_mute.error();
        return std::unexpected{failed};
    }
    impl->color_program = std::move(*color);
    impl->color_fade_program = std::move(*color_fade);
    impl->texture_tint_program = std::move(*texture_tint);
    impl->glyph_program = std::move(*glyph);
    impl->texture_program = std::move(*texture);
    impl->window_light_program = std::move(*window_light);
    impl->box_mute_program = std::move(*box_mute);

    impl->fade_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_fade_params", bgfx::UniformType::Vec4)};
    impl->atlas_sampler = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "s_atlas", bgfx::UniformType::Sampler)};
    impl->window_light_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_window_light_params", bgfx::UniformType::Vec4)};
    impl->box_mute_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_box_mute_params", bgfx::UniformType::Vec4)};
    impl->box_mute_arms = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_box_mute_arms", bgfx::UniformType::Vec4)};

    impl->atlases = makeHighwayAtlases(textures.note_atlas_png);
    UploadedTexture inlay = uploadPngTexture(textures.inlay_atlas_png);
    impl->inlay_texture_width = inlay.width;
    impl->inlay_texture_height = inlay.height;
    impl->inlay_texture = std::move(inlay.handle);
    UploadedTexture fingering = uploadPngTexture(textures.fingering_png);
    impl->fingering_texture = std::move(fingering.handle);

    // Box-mute marks: chords.png is the single source of truth for the marks' look. Measure
    // both marks' cross-sections from its pixels and upload them as the two-row ramp the SDF
    // shader samples by distance; what is painted there is exactly what the boxes render.
    if (const std::optional<BoxMuteProfiles> profiles =
            measureBoxMuteProfiles(textures.chord_marks_png);
        profiles.has_value())
    {
        impl->box_mute_profiles = *profiles;
        const auto width = static_cast<std::uint32_t>(g_box_mute_ramp_samples);
        const bgfx::Memory* memory = bgfx::alloc(width * 2U * 4U);
        std::memcpy(memory->data, profiles->palm.ramp.data(), width * 4U);
        std::memcpy(memory->data + (width * 4U), profiles->full.ramp.data(), width * 4U);
        impl->box_mute_ramp = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
            static_cast<std::uint16_t>(width),
            2,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory)};
    }

    // Texture assets are required product content: a missing, undecodable, or wrong-shape
    // asset is a broken install, not a degradable state (user decision 2026-08-01 — the
    // procedural fallbacks this check replaces silently masked exactly such failures).
    if (!impl->atlases.heads.isValid() ||
        impl->atlases.head_layout.capacity() < g_head_cell_count ||
        !impl->inlay_texture.isValid() || !impl->fingering_texture.isValid() ||
        !impl->box_mute_ramp.isValid())
    {
        return std::unexpected{HighwayRendererError{
            .code = HighwayRendererErrorCode::TextureAssetInvalid,
            .message = std::format(
                "highway texture assets missing or invalid (note atlas loaded={} with {} of "
                "{} required cells, inlays loaded={}, fingering loaded={}, chord mute marks "
                "measured={}); the install or resource deployment is broken",
                impl->atlases.heads.isValid(),
                impl->atlases.head_layout.capacity(),
                g_head_cell_count,
                impl->inlay_texture.isValid(),
                impl->fingering_texture.isValid(),
                impl->box_mute_ramp.isValid())
        }};
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
    m_impl->display_hold_ends =
        common::core::highwayDisplayHoldEnds(m_impl->state.notes, m_impl->state.shapes);
    m_impl->sustain_prefix_max =
        common::core::makeHighwaySustainPrefixMax(m_impl->display_hold_ends);
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
// Fret lines moved to the dynamic pass (they carry Charter's per-frame active and
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

    // Camera: stepped targets from the upcoming chart, turned into motion by the spring.
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
    // horizon (Charter's fading shader constants: 50 ms to 250 ms).
    const std::array<float, 4> fade_uniform{
        static_cast<float>(common::core::highwayTimeToZ(0.05, scroll, metrics)),
        static_cast<float>(common::core::highwayTimeToZ(0.25, scroll, metrics)),
        0.0F,
        0.0F
    };

    // Settled hand windows visible this frame: each placement owns the time range from its
    // arrival up to the next placement's ramp start (the transition itself is drawn as a
    // sampled sweep below, so settled spans shrink by the following ramp). The first
    // placement's window already holds before its arrival (the pre-held opening, matching
    // highwayHandWindowAt), so its span extends back to the span start even when the arrival
    // itself is far past the horizon. A chart with no placements gets the reference nut
    // window.
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
        const double window_start = index == 0 ? span_start_seconds : fhp.seconds;
        const double window_end = index + 1 < state.fret_hand_positions.size()
                                      ? state.fret_hand_positions[index + 1].seconds -
                                            state.fret_hand_positions[index + 1].ramp_seconds
                                      : span_end_seconds;
        if (window_end <= span_start_seconds || window_start >= span_end_seconds ||
            window_end <= window_start)
        {
            continue;
        }
        hand_windows.push_back(
            HandWindow{
                .start_seconds = std::max(window_start, span_start_seconds),
                .end_seconds = std::min(window_end, span_end_seconds),
                .fret = fhp.fret,
                .width = fhp.width,
            });
    }
    if (state.fret_hand_positions.empty())
    {
        hand_windows.push_back(
            HandWindow{
                .start_seconds = span_start_seconds,
                .end_seconds = span_end_seconds,
                .fret = 1,
                .width = 4,
            });
    }
    // The window at the current instant, fractional mid-transition: the shared coverage signal
    // for the hit-line presentation (lane brightness, pinned numbers).
    const common::core::HighwayHandWindow current_window =
        common::core::highwayHandWindowAt(state.fret_hand_positions, now_seconds);

    // --- Lane border ribbons: one faded runway strip per fret line (Charter's floor
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
        // Right-hand windows join the visible tier (user rule 2026-07-28): lines the tapping
        // hand's light path crosses on screen brighten like any visible window's. The tier is
        // a per-line array, so overlap with the fretting hand's windows deduplicates itself,
        // and the path union already carries any tapped-slide morph — the eased-coverage
        // machinery stays exclusive to the current fretting-hand window's hit-line crossfade.
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            if (tap.path.front().seconds > span_end_seconds)
            {
                break; // onsets ascend, so nothing later is on screen
            }
            if (tap.path.back().seconds + g_tap_light_decay_seconds < span_start_seconds)
            {
                continue;
            }
            double low = tap.path.front().fret_low;
            double high = tap.path.front().fret_high;
            for (const common::core::HighwayTapLightStation& station : tap.path)
            {
                low = std::min(low, station.fret_low);
                high = std::max(high, station.fret_high);
            }
            const int first_line = std::max(0, static_cast<int>(std::floor(low)) - 1);
            const int last_line = std::min(g_face_fret_count, static_cast<int>(std::ceil(high)));
            for (int line = first_line; line <= last_line; ++line)
            {
                in_visible_window.at(static_cast<std::size_t>(line)) = true;
            }
        }

        // The tapping hand's contribution to the bright tier (user catch 2026-07-28: the
        // left window's coverage brightened its ribbons to full while the tap lanes stayed at
        // the mid tier): each tap active at NOW brightens its lines by its own light envelope
        // — rising on approach, full through the hold, fading over the decay — over the path
        // extent at now (eased mid-glide with the same curve the light travels). Coverage
        // softens across one fret past the extent's bounding lines, and lines take the max
        // over taps, so hand overlap deduplicates itself exactly like the other tiers.
        std::array<double, g_face_fret_count + 1> tap_coverage{};
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            const common::core::HighwayTapLightStation& front = tap.path.front();
            const common::core::HighwayTapLightStation& back = tap.path.back();
            // Ramps vary per onset, so this cannot early-break on ascending onsets; the
            // per-tap skip is a cheap POD compare. The ribbons use their own slower decay.
            if (front.seconds - tap.ramp_seconds > now_seconds ||
                back.seconds + g_tap_ribbon_decay_seconds < now_seconds)
            {
                continue;
            }
            double envelope = 1.0;
            double low = front.fret_low;
            double high = front.fret_high;
            if (now_seconds < front.seconds)
            {
                envelope = (now_seconds - (front.seconds - tap.ramp_seconds)) / tap.ramp_seconds;
            }
            else if (now_seconds > back.seconds)
            {
                envelope = 1.0 - ((now_seconds - back.seconds) / g_tap_ribbon_decay_seconds);
                low = back.fret_low;
                high = back.fret_high;
            }
            else
            {
                for (std::size_t station = 0; station + 1 < tap.path.size(); ++station)
                {
                    const common::core::HighwayTapLightStation& a = tap.path[station];
                    const common::core::HighwayTapLightStation& b = tap.path[station + 1];
                    if (now_seconds > b.seconds)
                    {
                        continue;
                    }
                    const double span = b.seconds - a.seconds;
                    const double progress =
                        span > 0.0 ? std::clamp((now_seconds - a.seconds) / span, 0.0, 1.0) : 1.0;
                    const double weight = common::core::highwaySlideEaseWeight(progress, false);
                    low = a.fret_low + ((b.fret_low - a.fret_low) * weight);
                    high = a.fret_high + ((b.fret_high - a.fret_high) * weight);
                    break;
                }
            }
            for (int line = 0; line <= g_face_fret_count; ++line)
            {
                const double inside =
                    std::min(static_cast<double>(line) - (low - 1.0), high - line);
                const double line_coverage = std::clamp(1.0 + inside, 0.0, 1.0) * envelope;
                double& slot = tap_coverage.at(static_cast<std::size_t>(line));
                slot = std::max(slot, line_coverage);
            }
        }

        bgfx::setUniform(fade_params.get(), fade_uniform.data());
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double z0 = time_to_z(span_start_seconds);
        const double z1 = time_to_z(span_end_seconds);
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            // Coverage crossfade (signed 2026-07-23): each full-length strip lerps from its
            // non-current tier to full brightness by how deeply the current eased window
            // contains it, so the brightened band hands off line-by-line in lockstep with the
            // border crossing the hit line. Lines stay straight and fixed; only alpha animates.
            // Whichever hand covers a line more deeply wins it (max-combine).
            const double base_alpha =
                in_visible_window.at(static_cast<std::size_t>(line)) ? 0.375 : 0.125;
            const double coverage = std::max(
                common::core::highwayHandWindowLineCoverage(
                    current_window, static_cast<double>(line)),
                tap_coverage.at(static_cast<std::size_t>(line)));
            const double alpha = base_alpha + ((1.0 - base_alpha) * coverage);
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

    // --- Hand-window light: one continuous brightness calculation across the window width,
    // evaluated per fragment (fhp-window-motion plan, lighting redesign). Each slab vertex
    // carries the fragment's distances inside the window's eased edges (linear within a slice,
    // so interpolation is exact); the fragment shader dissolves the light softly across the
    // falloff band straddling each edge. No geometric clipping, no border line — soft edges at
    // any zoom, and transitions cannot facet or misalign. The slab is emitted per board lane
    // so each quad carries its lane's intrinsic lit tint (plain vs inlay-dotted) as vertex
    // color: the light reveals the board's own coloring rather than painting over it, and the
    // shared per-fragment mask is identical across lanes (the edge distances are one linear
    // field in x), so the lit region still reads as one continuous light. ---
    {
        const std::array<float, 4> light_params{
            static_cast<float>(g_window_light_falloff), 0.0F, 0.0F, 0.0F
        };
        bgfx::setUniform(window_light_params.get(), light_params.data());
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        const std::vector<double> times =
            windowSampleTimes(state, span_start_seconds, span_end_seconds);
        std::vector<double> zs(times.size());
        std::vector<double> lows(times.size());
        std::vector<double> highs(times.size());
        for (std::size_t sample = 0; sample < times.size(); ++sample)
        {
            const auto [low_x, high_x] = handWindowXAt(state, times[sample], metrics, mirrored);
            zs[sample] = time_to_z(times[sample]);
            lows[sample] = low_x;
            highs[sample] = high_x;
        }
        // Motion dim (user model 2026-07-23): the silhouette keeps the settled cross-section
        // everywhere — the same x-measured fade band along the whole eased contour — and the
        // transition's fading lives in the light's brightness instead. Each ramp dims along a
        // sin-squared bell over its own progress: full brightness at the ramp's start and end,
        // deepest exactly mid-transition, so the light reads as gradually fading out of the
        // lit span, through the dark middle of the sweep, and back into the arriving span
        // (user shaping 2026-07-23 — a per-sample speed dim plateaued at maximum across most
        // of a fast morph instead). The bell's depth scales with the ramp's overall sweep
        // steepness, so slow glides keep most of their glow.
        std::vector<double> dims(times.size(), 1.0);
        for (const common::core::HighwayFhpView& fhp : state.fret_hand_positions)
        {
            if (fhp.ramp_seconds <= 0.0 || fhp.seconds <= span_start_seconds ||
                fhp.seconds - fhp.ramp_seconds >= span_end_seconds)
            {
                continue;
            }
            const double ramp_start = fhp.seconds - fhp.ramp_seconds;
            const auto [low_from, high_from] = handWindowXAt(state, ramp_start, metrics, mirrored);
            const auto [low_to, high_to] = handWindowXAt(state, fhp.seconds, metrics, mirrored);
            const double dz = time_to_z(fhp.seconds) - time_to_z(ramp_start);
            if (dz <= 0.0)
            {
                continue;
            }
            const double slope =
                std::max(std::abs(low_to - low_from), std::abs(high_to - high_from)) / dz;
            const double depth = g_window_morph_dim * (slope / std::sqrt(1.0 + (slope * slope)));
            for (std::size_t sample = 0; sample < times.size(); ++sample)
            {
                if (times[sample] < ramp_start || times[sample] > fhp.seconds)
                {
                    continue;
                }
                const double progress = (times[sample] - ramp_start) / fhp.ramp_seconds;
                const double bell = std::sin(std::numbers::pi * progress);
                dims[sample] = std::min(dims[sample], 1.0 - (depth * bell * bell));
            }
        }
        const double spill = g_window_light_falloff / 2.0;
        for (std::size_t sample = 1; sample < times.size(); ++sample)
        {
            const double za = zs[sample - 1];
            const double zb = zs[sample];
            const double low_a = lows[sample - 1];
            const double low_b = lows[sample];
            const double high_a = highs[sample - 1];
            const double high_b = highs[sample];
            // Every fragment past the half-band spill outside both slice-end windows is fully
            // dark, so lanes entirely beyond it draw nothing.
            const double lit_x0 = std::min(low_a, low_b) - spill;
            const double lit_x1 = std::max(high_a, high_b) + spill;
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                const double lane_low = common::core::highwayFretLineX(fret - 1, metrics, mirrored);
                const double lane_high = common::core::highwayFretLineX(fret, metrics, mirrored);
                const auto [lane_x0, lane_x1] = std::minmax(lane_low, lane_high);
                if (lane_x1 < lit_x0 || lane_x0 > lit_x1)
                {
                    continue;
                }
                const ArgbColor lane_color =
                    isDottedFret(fret) ? g_lit_lane_dotted_color : g_lit_lane_color;
                const std::uint32_t tint_a = packAbgr(lane_color, dims[sample - 1]);
                const std::uint32_t tint_b = packAbgr(lane_color, dims[sample]);
                const auto vertex = [&](const double x,
                                        const double z,
                                        const double low,
                                        const double high,
                                        const std::uint32_t tint) {
                    return makeUvVertex(
                        x,
                        0.008,
                        z,
                        tint,
                        static_cast<float>((x - low) + spill),
                        static_cast<float>((high - x) + spill));
                };
                pushQuad(
                    vertices,
                    indices,
                    vertex(lane_x0, za, low_a, high_a, tint_a),
                    vertex(lane_x1, za, low_a, high_a, tint_a),
                    vertex(lane_x1, zb, low_b, high_b, tint_b),
                    vertex(lane_x0, zb, low_b, high_b, tint_b));
            }
        }
        submitBatch(vertices, indices, posColorUvLayout(), window_light_program.get(), nullptr);
    }

    // --- Tapping-hand light: one patch per tap onset over its own tapped fret lanes, alpha
    // rising toward the tap along the approach side, holding through the derived path (which
    // morphs with pitched glides and sustained contact), and decaying after the fingers
    // release (the right-hand-tap-lighting plan). Patches are deliberately per-onset, never
    // merged into runs: the dip between consecutive taps mirrors the finger lifting (user rule
    // 2026-07-28). Consecutive same-lane patches simply alpha-compose where their envelopes
    // overlap, which shallows the dip toward continuous light only at densities where the
    // hand genuinely never leaves the board. Reuses the window light's per-fragment soft x
    // edges; the tint leans toward the FHP orange so the two hands' lights read apart. ---
    {
        const std::array<float, 4> light_params{
            static_cast<float>(g_window_light_falloff), 0.0F, 0.0F, 0.0F
        };
        bgfx::setUniform(window_light_params.get(), light_params.data());
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double spill = g_window_light_falloff / 2.0;
        // One strip segment between two instants, each end carrying its own alpha and
        // fractional fret extent (the soft x edges interpolate across the quad exactly like
        // the window light's slices), clipped to the visible span with endpoint values
        // re-interpolated so a patch never floats past a board edge.
        const auto emit_segment = [&](double time_a,
                                      double alpha_a,
                                      double low_a,
                                      double high_a,
                                      double time_b,
                                      double alpha_b,
                                      double low_b,
                                      double high_b) {
            if (time_b <= time_a || time_b <= span_start_seconds || time_a >= span_end_seconds)
            {
                return;
            }
            const auto lerp = [](const double a, const double b, const double w) {
                return a + ((b - a) * w);
            };
            if (time_a < span_start_seconds)
            {
                const double w = (span_start_seconds - time_a) / (time_b - time_a);
                alpha_a = lerp(alpha_a, alpha_b, w);
                low_a = lerp(low_a, low_b, w);
                high_a = lerp(high_a, high_b, w);
                time_a = span_start_seconds;
            }
            if (time_b > span_end_seconds)
            {
                const double w = (span_end_seconds - time_a) / (time_b - time_a);
                alpha_b = lerp(alpha_a, alpha_b, w);
                low_b = lerp(low_a, low_b, w);
                high_b = lerp(high_a, high_b, w);
                time_b = span_end_seconds;
            }
            const double za = time_to_z(time_a);
            const double zb = time_to_z(time_b);
            const auto patch_edges = [&](const double low, const double high) {
                const double edge0 = common::core::highwayFretLineX(low - 1.0, metrics, mirrored);
                const double edge1 = common::core::highwayFretLineX(high, metrics, mirrored);
                return std::pair{std::min(edge0, edge1), std::max(edge0, edge1)};
            };
            const auto [low_x_a, high_x_a] = patch_edges(low_a, high_a);
            const auto [low_x_b, high_x_b] = patch_edges(low_b, high_b);
            const int lane_first =
                std::max(1, static_cast<int>(std::floor(std::min(low_a, low_b))));
            const int lane_last =
                std::min(g_face_fret_count, static_cast<int>(std::ceil(std::max(high_a, high_b))));
            for (int fret = lane_first; fret <= lane_last; ++fret)
            {
                const double lane_low = common::core::highwayFretLineX(fret - 1, metrics, mirrored);
                const double lane_high = common::core::highwayFretLineX(fret, metrics, mirrored);
                const auto [lane_x0, lane_x1] = std::minmax(lane_low, lane_high);
                // Warm the hue only: the mix target takes the lane's own alpha so the tap
                // light keeps the window light's base opacity (mixArgb blends all four
                // channels, and the FHP orange is opaque).
                const ArgbColor base =
                    isDottedFret(fret) ? g_lit_lane_dotted_color : g_lit_lane_color;
                const ArgbColor lane_color = mixArgb(
                    base,
                    (base & 0xFF000000U) | (g_fret_number_fhp_color & 0x00FFFFFFU),
                    g_tap_light_warm_mix);
                const auto vertex = [&](const double x,
                                        const double z,
                                        const std::uint32_t tint,
                                        const double low_x,
                                        const double high_x) {
                    return makeUvVertex(
                        x,
                        0.008,
                        z,
                        tint,
                        static_cast<float>((x - low_x) + spill),
                        static_cast<float>((high_x - x) + spill));
                };
                const std::uint32_t tint_a = packAbgr(lane_color, alpha_a);
                const std::uint32_t tint_b = packAbgr(lane_color, alpha_b);
                pushQuad(
                    vertices,
                    indices,
                    vertex(lane_x0, za, tint_a, low_x_a, high_x_a),
                    vertex(lane_x1, za, tint_a, low_x_a, high_x_a),
                    vertex(lane_x1, zb, tint_b, low_x_b, high_x_b),
                    vertex(lane_x0, zb, tint_b, low_x_b, high_x_b));
            }
        };
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            const common::core::HighwayTapLightStation& front = tap.path.front();
            const common::core::HighwayTapLightStation& back = tap.path.back();
            // Ramps vary per onset, so ascending onsets give no early break: each tap skips
            // with two cheap POD compares instead.
            if (back.seconds + g_tap_light_decay_seconds < span_start_seconds ||
                front.seconds - tap.ramp_seconds > span_end_seconds)
            {
                continue;
            }
            emit_segment(
                front.seconds - tap.ramp_seconds,
                0.0,
                front.fret_low,
                front.fret_high,
                front.seconds,
                1.0,
                front.fret_low,
                front.fret_high);
            // The hold morphs along the path; gliding segments subdivide with the notes' own
            // slide ease so the light travels with the tapped glide instead of cutting straight
            // across it.
            for (std::size_t station = 0; station + 1 < tap.path.size(); ++station)
            {
                const common::core::HighwayTapLightStation& a = tap.path[station];
                const common::core::HighwayTapLightStation& b = tap.path[station + 1];
                const bool gliding = std::is_neq(a.fret_low <=> b.fret_low) ||
                                     std::is_neq(a.fret_high <=> b.fret_high);
                if (!gliding)
                {
                    emit_segment(
                        a.seconds,
                        1.0,
                        a.fret_low,
                        a.fret_high,
                        b.seconds,
                        1.0,
                        b.fret_low,
                        b.fret_high);
                    continue;
                }
                constexpr int slices = 6;
                double previous_seconds = a.seconds;
                double previous_low = a.fret_low;
                double previous_high = a.fret_high;
                for (int slice = 1; slice <= slices; ++slice)
                {
                    const double progress = static_cast<double>(slice) / slices;
                    const double seconds = a.seconds + ((b.seconds - a.seconds) * progress);
                    const double weight = common::core::highwaySlideEaseWeight(progress, false);
                    const double low = a.fret_low + ((b.fret_low - a.fret_low) * weight);
                    const double high = a.fret_high + ((b.fret_high - a.fret_high) * weight);
                    emit_segment(
                        previous_seconds,
                        1.0,
                        previous_low,
                        previous_high,
                        seconds,
                        1.0,
                        low,
                        high);
                    previous_seconds = seconds;
                    previous_low = low;
                    previous_high = high;
                }
            }
            emit_segment(
                back.seconds,
                1.0,
                back.fret_low,
                back.fret_high,
                back.seconds + g_tap_light_decay_seconds,
                0.0,
                back.fret_low,
                back.fret_high);
        }
        submitBatch(vertices, indices, posColorUvLayout(), window_light_program.get(), nullptr);
    }

    // --- Beat and measure bars: Charter's gradient wings in its deep blue, clipped to
    // each beat's hand window. Measures get a sharp teal attack line on the downbeat with a
    // brief blue fade trailing into the measure; plain beats are two wings meeting at the
    // line. ---
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
            const auto [x0, x1] = handWindowXAt(state, beat.seconds, metrics, mirrored);
            const double z = time_to_z(beat.seconds);
            const std::uint32_t solid = packAbgr(g_beat_bar_color);
            const std::uint32_t clear = packAbgr(g_beat_bar_color, 0.0);
            if (beat.measure_downbeat)
            {
                pushFloorQuad(
                    vertices,
                    indices,
                    x0,
                    x1,
                    0.015,
                    z - g_attack_line_half_length,
                    z + g_attack_line_half_length,
                    packAbgr(g_chord_box_color, g_attack_line_alpha));
                pushFloorQuadGradient(
                    vertices,
                    indices,
                    x0,
                    x1,
                    0.015,
                    z + g_attack_line_half_length,
                    z + g_attack_line_half_length + g_attack_fade_length,
                    solid,
                    clear);
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
            const ArgbColor color =
                shape.arpeggio ? g_arpeggio_color : (g_lane_border_color | 0xFF000000U);
            const std::uint32_t solid = packAbgr(color);
            const std::uint32_t clear = packAbgr(color, 0.0);
            // Rails follow the hand window's edges, sampled so a mid-span window move (a chord
            // slide under a held shape) sweeps them along with everything else; in settled
            // stretches consecutive samples share one extent and the trapezoids stay straight.
            const double rail_from = std::max(now_seconds, shape.start_seconds);
            const double rail_to = std::min(shape.end_seconds, span_end_seconds);
            const std::vector<double> times = windowSampleTimes(state, rail_from, rail_to);
            for (std::size_t sample = 1; sample < times.size(); ++sample)
            {
                const auto [a_x0, a_x1] =
                    handWindowXAt(state, times[sample - 1], metrics, mirrored);
                const auto [b_x0, b_x1] = handWindowXAt(state, times[sample], metrics, mirrored);
                const double za = std::max(0.0, time_to_z(times[sample - 1]));
                const double zb = std::max(0.0, time_to_z(times[sample]));
                // Solid core between fade-out wings, per edge (Charter's cross-section).
                const auto push_band = [&](const double xa_from,
                                           const double xa_to,
                                           const double xb_from,
                                           const double xb_to,
                                           const std::uint32_t color_from,
                                           const std::uint32_t color_to) {
                    pushQuad(
                        vertices,
                        indices,
                        makeVertex(xa_from, 0.01, za, color_from),
                        makeVertex(xa_to, 0.01, za, color_to),
                        makeVertex(xb_to, 0.01, zb, color_to),
                        makeVertex(xb_from, 0.01, zb, color_from));
                };
                for (const auto& [xa, xb] : {std::pair{a_x0, b_x0}, std::pair{a_x1, b_x1}})
                {
                    push_band(
                        xa - g_shape_rail_fade_half_width,
                        xa - g_shape_rail_core_half_width,
                        xb - g_shape_rail_fade_half_width,
                        xb - g_shape_rail_core_half_width,
                        clear,
                        solid);
                    push_band(
                        xa - g_shape_rail_core_half_width,
                        xa + g_shape_rail_core_half_width,
                        xb - g_shape_rail_core_half_width,
                        xb + g_shape_rail_core_half_width,
                        solid,
                        solid);
                    push_band(
                        xa + g_shape_rail_core_half_width,
                        xa + g_shape_rail_fade_half_width,
                        xb + g_shape_rail_core_half_width,
                        xb + g_shape_rail_fade_half_width,
                        solid,
                        clear);
                }
            }
        }
        submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
    }

    // --- Notes: per-note geometry batched per onset group and flushed far-to-near (see
    // flush_note_batches below). ---
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
        // The hold end, not the sustain end: a span-held strum stays drawable while its head
        // pins at the hit line long after its sustainless onset has passed.
        if (note.start_seconds <= span_end_seconds &&
            display_hold_ends[index] >= span_start_seconds)
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
        // Members that are not tapped: only these decide the PLAIN chord box (user rule
        // 2026-07-28 — taps are the other hand; a fretted note under a simultaneous tap is a
        // single note, and a tapped dyad gets the tapped box from the tap onsets instead).
        std::size_t non_tap_count;
        bool any_accent;
        common::core::NoteMute common_mute;
        // True when every note is fully muted (a dead chug restating the preceding chord hides
        // behind the repeat box, and muted runs never break another chord's repeat chain).
        bool all_full_muted;
        // Repeat-chord treatment (Charter's visibility rules): the strum renders as a
        // half-height box with its mute cross and NO notes.
        bool box_only;
        // Onset of the next note-showing strum, capping this group's span-hold display (see
        // the take-over pass below); infinity when none follows in the visible range.
        double hold_cap_seconds;
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
            .non_tap_count = 0,
            .any_accent = false,
            .common_mute = state.notes[index].mute,
            .all_full_muted = true,
            .box_only = false,
            .hold_cap_seconds = std::numeric_limits<double>::infinity(),
            .frets = {},
        };
        group.frets.reserve(group.count);
        for (std::size_t member = index; member < group_end; ++member)
        {
            const common::core::HighwayNoteView& note = state.notes[member];
            if (note.attack != common::core::NoteAttack::Tap)
            {
                ++group.non_tap_count;
            }
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

    // Repeat classification (Charter's chord visibility rules): a strum shows only the
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
    for (ChordGroup& group : chord_groups)
    {
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
            // A dead chug earns the X repeat box only when it restates the nearest preceding
            // chord's posture (muted or not); with fresh frets it displays its notes and their
            // mute crosses like any chord (user rule 2026-07-21 — Charter blanks every
            // dead chug).
            std::size_t cursor = group.first;
            while (cursor > 0)
            {
                const double onset = state.notes[cursor - 1].start_seconds;
                std::size_t run_begin = cursor - 1;
                while (run_begin > 0 && std::abs(state.notes[run_begin - 1].start_seconds - onset) <
                                            g_onset_match_epsilon)
                {
                    --run_begin;
                }
                const std::size_t run_count = cursor - run_begin;
                if (run_count >= 2)
                {
                    std::vector<std::pair<int, int>> run_frets;
                    run_frets.reserve(run_count);
                    for (std::size_t member = run_begin; member < cursor; ++member)
                    {
                        run_frets.emplace_back(
                            state.notes[member].string, state.notes[member].fret);
                    }
                    std::ranges::sort(run_frets);
                    group.box_only = run_frets == group.frets;
                    break;
                }
                cursor = run_begin;
            }
            continue;
        }
        // Marked chords always show their notes — unless every note is palm muted, where
        // Charter's mute short-circuit applies the repeat rule anyway.
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

    // Span-hold take-over: a span-held strum's heads stay pinned at the hit line until the
    // next strum that shows its notes arrives to re-pin the identical heads there, so the
    // newcomer owns the hold display from its onset and the two never stack. Box-only
    // repeats, dead chugs, and single notes continue the hold rather than taking it over,
    // exactly as they never break a repeat chain.
    {
        double next_shown_onset = std::numeric_limits<double>::infinity();
        for (ChordGroup& group : chord_groups | std::views::reverse)
        {
            group.hold_cap_seconds = next_shown_onset;
            if (group.count >= 2 && !group.box_only && !group.all_full_muted)
            {
                next_shown_onset = group.start_seconds;
            }
        }
    }

    // Vertical extent of the board face's fret lines: the string grid's base (the floor stays
    // y = 0; the chord box's bottom bar fills the gap below the grid) up to an equal
    // half-string margin above the top lane (highwayLaneToY centers lanes on half-string
    // offsets above the base). Shared by the fret-line pass below and everything that must not
    // rise past the fret grid.
    const double face_bottom_y = metrics.string_grid_base_y;
    const double face_top_y =
        metrics.string_grid_base_y +
        (static_cast<double>(std::max(state.string_count, 1)) * metrics.string_distance);

    // Arpeggio bracket geometry accumulates per box PER STRING in the box pass below and
    // submits lane-dominantly inside the note pass (user rule 2026-07-24, refining two earlier
    // orderings): an upright bracket against a flat lane ribbon is occluded by lane height, not
    // time — a camera ray from above reaches the higher surface first regardless of z — so each
    // bracket glyph draws over everything on lower lanes (any onset) and yields only to groups
    // containing notes on lanes above its own. The box panels stay under all notes as before.
    struct BracketBatch
    {
        int lane{0};
        double span_start_seconds{0.0};
        double span_end_seconds{0.0};
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        bool submitted{false};
    };
    std::vector<BracketBatch> bracket_batches;

    // --- Chord and arpeggio boxes: Charter's translucent panels at chord onsets, plus an
    // arpeggio-styled box (the same panel with the fretboard bracket notation overlaid) at each
    // arpeggio shape's start. Drawn far-to-near BEFORE the notes so nearer content composites
    // over them (the board view is painter-ordered, no depth buffer). An arpeggio start draws
    // exactly one box — if a chord group lands there it is drawn arpeggio-style rather than a
    // second plain box — and note heads are never suppressed. Repeated/dead strums render the
    // half-height repeat box with its mute mark. ---
    {
        std::vector<PosColorVertex> box_vertices;
        std::vector<std::uint16_t> box_indices;
        // Repeat-box mute marks render through the SDF program (see the g_chord_mute_*
        // constants). They ride a different program than the panels, and painter order must
        // hold ACROSS boxes — dense chug chains overlap heavily on screen in perspective, and
        // a far box's mark must never composite over a nearer box's panel — so the panel
        // batch flushes before each mark and the mark submits immediately. Draw-call cost is
        // bounded by the visible marked repeat boxes: tens at worst, noise for bgfx.
        std::vector<PosColorUvVertex> box_marker_vertices;
        std::vector<std::uint16_t> box_marker_indices;
        const auto flush_box_panels = [&] {
            if (box_vertices.empty())
            {
                return;
            }
            submitBatch(box_vertices, box_indices, posColorLayout(), color_program.get(), nullptr);
            box_vertices.clear();
            box_indices.clear();
        };
        // Boxes rise exactly to the fret-line top: any higher and the panel visibly pokes past
        // the fret grid (user-flagged bug; the old top added half a string distance).
        const double full_height_y1 = face_top_y;

        // Overlays one arpeggio shape's posture brackets (the fretboard notation) at a box's z:
        // a bracket per fretted string, or the window-end brackets for an open string. Window
        // edges arrive fractional mid-transition, so the open brackets center on the edge lanes
        // through the fractional fret-line map.
        const auto push_arpeggio_brackets = [&](const common::core::HighwayShapeView& shape,
                                                const double z,
                                                const double low_line,
                                                const double high_line) {
            const double half = metrics.note_half_width;
            const auto push_bracket = [&](const int cell,
                                          const double center_x,
                                          const double center_y,
                                          const std::uint32_t tint,
                                          const bool mirror_u) {
                const std::array<float, 4> rect = atlases.head_layout.cellRect(cell);
                const float u0 = mirror_u ? rect[2] : rect[0];
                const float u1 = mirror_u ? rect[0] : rect[2];
                pushQuad(
                    bracket_batches.back().vertices,
                    bracket_batches.back().indices,
                    makeUvVertex(center_x - half, center_y - half, z, tint, u0, rect[3]),
                    makeUvVertex(center_x + half, center_y - half, z, tint, u1, rect[3]),
                    makeUvVertex(center_x + half, center_y + half, z, tint, u1, rect[1]),
                    makeUvVertex(center_x - half, center_y + half, z, tint, u0, rect[1]));
            };
            for (const common::core::HighwayShapeStringView& entry : shape.strings)
            {
                const double y = common::core::highwayStringLaneY(
                    entry.string, state.string_count, metrics, invert);
                const std::uint32_t tint =
                    packAbgr(stringLaneColor(entry.string, state.string_count, palette));
                // One lane-tagged batch per posture string, so the note pass can order each
                // glyph against note content by lane height; the span window scopes which
                // notes can force the glyph underneath them.
                bracket_batches.push_back(
                    BracketBatch{
                        .lane = invert ? (state.string_count + 1 - entry.string) : entry.string,
                        .span_start_seconds = shape.start_seconds,
                        .span_end_seconds = shape.end_seconds,
                        .vertices = {},
                        .indices = {},
                    });
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
                        common::core::highwayFretLineX(low_line + 0.5, metrics, mirrored),
                        y,
                        tint,
                        false);
                    push_bracket(
                        g_head_cell_arpeggio_open_bracket,
                        common::core::highwayFretLineX(high_line - 0.5, metrics, mirrored),
                        y,
                        tint,
                        true);
                }
            }
        };

        // The far-to-near draw list. Each arpeggio shape gets one box (styled with brackets) for
        // as long as it is on screen; each chord group gets a plain box unless an arpeggio shape
        // starts at the same position, in which case the arpeggio box covers it (the chord's
        // note heads still render — nothing is suppressed).
        struct BoxDraw
        {
            double start_seconds;
            bool box_only;
            bool with_top;
            bool any_accent;
            common::core::NoteMute mute;
            const common::core::HighwayShapeView* arpeggio_shape;
            // A tapped chord box spans the taps' own fret extent instead of the fretting
            // hand's window (right-hand-tap-lighting plan); null for left-hand boxes.
            const common::core::HighwayTapOnsetView* tap;
        };
        std::vector<BoxDraw> boxes;
        for (const common::core::HighwayShapeView& shape : state.shapes)
        {
            if (!shape.arpeggio || shape.end_seconds < now_seconds ||
                shape.start_seconds > span_end_seconds)
            {
                continue;
            }
            boxes.push_back(
                BoxDraw{
                    .start_seconds = shape.start_seconds,
                    .box_only = false,
                    // Charter's chord-box rule (3+ sounding strings get the top bar),
                    // counted from the arpeggio's posture strings.
                    .with_top = shape.strings.size() > 2,
                    .any_accent = false,
                    .mute = common::core::NoteMute::None,
                    .arpeggio_shape = &shape,
                    .tap = nullptr,
                });
        }
        for (const ChordGroup& group : chord_groups)
        {
            // Only non-tap members earn the plain (fretting-hand) box: a fretted note under a
            // simultaneous tap is a single note, and tapped chords get their own box below.
            if (group.non_tap_count < 2 || group.start_seconds < now_seconds ||
                group.start_seconds > span_end_seconds)
            {
                continue;
            }
            const bool coincides_with_arpeggio =
                std::ranges::any_of(state.shapes, [&](const common::core::HighwayShapeView& shape) {
                    return shape.arpeggio && std::abs(shape.start_seconds - group.start_seconds) <
                                                 g_onset_match_epsilon;
                });
            if (coincides_with_arpeggio)
            {
                continue;
            }
            boxes.push_back(
                BoxDraw{
                    .start_seconds = group.start_seconds,
                    .box_only = group.box_only,
                    .with_top = group.non_tap_count > 2,
                    .any_accent = group.any_accent,
                    .mute = group.common_mute,
                    .arpeggio_shape = nullptr,
                    .tap = nullptr,
                });
        }
        // Tapped chord boxes (right-hand-tap-lighting plan): two or more taps struck together
        // get their own box on the taps' fret extent — the tapping hand's counterpart of the
        // strummed box. Derived per onset; no repeat-box chain (taps are percussive).
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            if (tap.count < 2 || tap.seconds < now_seconds || tap.seconds > span_end_seconds)
            {
                continue;
            }
            boxes.push_back(
                BoxDraw{
                    .start_seconds = tap.seconds,
                    .box_only = false,
                    .with_top = tap.count > 2,
                    .any_accent = false,
                    .mute = common::core::NoteMute::None,
                    .arpeggio_shape = nullptr,
                    .tap = &tap,
                });
        }
        std::ranges::sort(boxes, [](const BoxDraw& lhs, const BoxDraw& rhs) {
            return lhs.start_seconds > rhs.start_seconds;
        });

        // One SDF-rendered mute mark over its repeat panel's interior — the rect between the
        // frame's inner edges, so the mark stops exactly at the borders instead of covering
        // them. The quad covers that interior exactly, texcoord carries interior-local
        // world-unit offsets, and the fragment shader samples chords.png's measured
        // cross-section by exact distance from the arm centerlines — the texture defines the
        // look, the shader only lays the arms out. Submits immediately so painter order holds
        // across boxes — see the batch comment above.
        const auto push_box_mute_marker = [&](const double x0,
                                              const double x1,
                                              const double y0,
                                              const double y1,
                                              const double z,
                                              const common::core::NoteMute mute) {
            const double half_x = (x1 - x0) / 2.0;
            const double half_y = (y1 - y0) / 2.0;
            const double middle_x = (x0 + x1) / 2.0;
            const double middle_y = (y0 + y1) / 2.0;
            const bool full = mute == common::core::NoteMute::Full;
            const BoxMuteProfile& profile = full ? box_mute_profiles.full : box_mute_profiles.palm;
            // Both marks span the full interior height (sighted layout policy). The palm X
            // runs border-less edge to edge: arms corner-to-corner of the interior, with the
            // clip rect and the arms' horizontal cut pushed past the quad by the ramp extent
            // so the quad slices the arms mid-stroke at the frame's inner edges. The full X
            // keeps a square footprint the height of the interior, tips wrapped by the note
            // art's squared corners, its top and bottom stroke edges meeting the frame
            // exactly (the sub-pixel antialiasing tail past them is cut by the quad).
            const double glyph_height = 2.0 * half_y;
            const double arm_half_x = full ? half_y : half_x;
            const double overshoot = full ? 0.0 : profile.extent_fraction * glyph_height;
            const double arm_length = std::sqrt((arm_half_x * arm_half_x) + (half_y * half_y));
            const auto params = std::array<float, 4>{
                static_cast<float>(arm_half_x + overshoot),
                static_cast<float>(half_y + overshoot),
                static_cast<float>(profile.stroke_half_fraction * glyph_height),
                static_cast<float>(profile.extent_fraction * glyph_height),
            };
            // The ramp rows sit at v = 0.25 (palm) and 0.75 (full) of the two-row texture;
            // a full-mute arm tip lands as an axis-aligned corner — the note art's squared
            // tip — cut vertically at the arms' horizontal extent and horizontally by the
            // rect clip; a palm-mute tip is the quad's raw cut instead.
            const auto arms = std::array<float, 4>{
                static_cast<float>(arm_half_x / arm_length),
                static_cast<float>(half_y / arm_length),
                static_cast<float>(arm_half_x + overshoot),
                full ? 0.75F : 0.25F,
            };
            bgfx::setUniform(box_mute_params.get(), params.data());
            bgfx::setUniform(box_mute_arms.get(), arms.data());
            // The quad covers the interior exactly with no margin: the full mark dissolves
            // inside it and the palm mark is sliced by it. The palm rim is authored in the
            // frame color, so its quads ride the frame's own fade (pushMiddleFadedQuads,
            // shared with the bars) as a vertex modulation — full brightness at the ends,
            // the derived dark/box ratio at the middle — keeping the rim equal to the
            // border wherever they meet. The full mark's rim is its own color and stays
            // unshaded.
            const std::uint32_t white = packAbgr(0xFFFFFFFF);
            const auto make_corner =
                [&](const double vx, const double vy, const std::uint32_t abgr) {
                    return makeUvVertex(
                        vx,
                        vy,
                        z,
                        abgr,
                        static_cast<float>(vx - middle_x),
                        static_cast<float>(vy - middle_y));
                };
            if (full)
            {
                pushQuad(
                    box_marker_vertices,
                    box_marker_indices,
                    make_corner(x0, y0, white),
                    make_corner(x1, y0, white),
                    make_corner(x1, y1, white),
                    make_corner(x0, y1, white));
            }
            else
            {
                pushMiddleFadedQuads(
                    box_marker_vertices,
                    box_marker_indices,
                    x0,
                    x1,
                    y0,
                    y1,
                    white,
                    packAbgr(frameFadeModulation()),
                    make_corner);
            }
            const bgfx::TextureHandle ramp = box_mute_ramp.get();
            submitBatch(
                box_marker_vertices,
                box_marker_indices,
                posColorUvLayout(),
                box_mute_program.get(),
                &ramp);
            box_marker_vertices.clear();
            box_marker_indices.clear();
        };

        for (const BoxDraw& box : boxes)
        {
            const double z = std::max(0.0, time_to_z(box.start_seconds));
            if (box.tap != nullptr)
            {
                // A tapped box spans the taps' own fret slots — their derived right-hand
                // window — not the fretting hand's.
                const double tap_low =
                    common::core::highwayFretLineX(box.tap->fret_low - 1, metrics, mirrored);
                const double tap_high =
                    common::core::highwayFretLineX(box.tap->fret_high, metrics, mirrored);
                pushChordBoxPanel(
                    box_vertices,
                    box_indices,
                    std::min(tap_low, tap_high),
                    std::max(tap_low, tap_high),
                    z,
                    full_height_y1,
                    box.box_only,
                    box.with_top,
                    box.any_accent,
                    metrics.string_grid_base_y);
                continue;
            }
            // Display-time window (user catch 2026-07-23): an approaching box takes the window
            // at its own onset instant, and a box riding the hit line re-evaluates per frame, so
            // a held arpeggio slides along with the chord sliding under it instead of staying
            // frozen at its onset window.
            const double window_seconds = std::max(box.start_seconds, now_seconds);
            const common::core::HighwayHandWindow window =
                common::core::highwayHandWindowAt(state.fret_hand_positions, window_seconds);
            const auto [x0, x1] = handWindowXAt(state, window_seconds, metrics, mirrored);
            pushChordBoxPanel(
                box_vertices,
                box_indices,
                x0,
                x1,
                z,
                full_height_y1,
                box.box_only,
                box.with_top,
                box.any_accent,
                metrics.string_grid_base_y);
            if (box.box_only && box.mute != common::core::NoteMute::None)
            {
                flush_box_panels();
                // The frame's inner edges bound the mark (pushChordBoxPanel geometry): the
                // bottom bar tops out one frame thickness up, the side columns end one
                // thickness inside, and the top-bar variant's bar sits above the half-height
                // fill — so the fill top is the interior ceiling for every variant.
                const double thickness = metrics.string_grid_base_y;
                push_box_mute_marker(
                    x0 + thickness, x1 - thickness, thickness, full_height_y1 / 2.0, z, box.mute);
            }
            if (box.arpeggio_shape != nullptr)
            {
                push_arpeggio_brackets(*box.arpeggio_shape, z, window.low_line, window.high_line);
            }
        }

        flush_box_panels();
        // Brackets submit interleaved with the note batches (see the declaration above).
    }

    const std::array<float, 4> head_cell = atlases.head_layout.cellRect(g_head_cell_standard);
    const std::array<float, 4> anticipation_cell =
        atlases.head_layout.cellRect(g_head_cell_anticipation);
    // Charter's head is a square quad (0.96 x 0.96 world units), not a lane-squashed one.
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

    // The four per-note batches flush per onset group, far-to-near: the board view is
    // painter-ordered with no depth writes, so one global submit per category would let a
    // distant head or open bar composite over a nearer note's sustain tail (the depth-order
    // bug this replaces). Within a group the categories keep Charter's layering: shadows
    // under rails under open bars under heads.
    const bgfx::TextureHandle heads_texture = atlases.heads.get();
    const auto flush_note_batches = [&] {
        // The shadow batch is floor furniture (span lines, glow posts, open-bar corner Ls), so
        // it takes the floor's distance fade near the board face like every other floor
        // element; heads, rails, and open bars are gameplay content and stay opaque.
        bgfx::setUniform(fade_params.get(), fade_uniform.data());
        submitBatch(
            shadow_vertices, shadow_indices, posColorLayout(), color_fade_program.get(), nullptr);
        submitBatch(rail_vertices, rail_indices, posColorLayout(), color_program.get(), nullptr);
        submitBatch(open_vertices, open_indices, posColorLayout(), color_program.get(), nullptr);
        submitBatch(
            head_vertices,
            head_indices,
            posColorUvLayout(),
            texture_tint_program.get(),
            &heads_texture);
        shadow_vertices.clear();
        shadow_indices.clear();
        rail_vertices.clear();
        rail_indices.clear();
        open_vertices.clear();
        open_indices.clear();
        head_vertices.clear();
        head_indices.clear();
    };
    std::size_t batched_group = chord_groups.size();

    // Bend chevrons layer above EVERY head of their onset group (user rule 2026-07-29): a
    // chord's notes push lane-ascending into one batch, so an inline marker from a lower lane
    // would be overdrawn by a higher groupmate's head. Chevrons therefore collect here during
    // the group and append to the head batch at the group boundary — last in the group's
    // painter order, still under nearer groups' flushes.
    struct PendingBendMarker
    {
        double x{0.0};
        double y{0.0};
        double z{0.0};
        double direction{1.0};
        double half_w{0.0};
        double half_h{0.0};
        std::uint32_t tint{0};
    };
    std::vector<PendingBendMarker> pending_bend_markers;
    const auto emit_pending_bend_markers = [&] {
        const std::array<float, 4> rect = atlases.head_layout.cellRect(g_head_cell_bend);
        for (const PendingBendMarker& marker : pending_bend_markers)
        {
            const auto corner =
                [&](const double dx, const double dy, const float u, const float v) {
                    return makeUvVertex(
                        marker.x + (dx * marker.direction),
                        marker.y + (dy * marker.direction),
                        marker.z,
                        marker.tint,
                        u,
                        v);
                };
            pushQuad(
                head_vertices,
                head_indices,
                corner(-marker.half_w, -marker.half_h, rect[0], rect[3]),
                corner(marker.half_w, -marker.half_h, rect[2], rect[3]),
                corner(marker.half_w, marker.half_h, rect[2], rect[1]),
                corner(-marker.half_w, marker.half_h, rect[0], rect[1]));
        }
        pending_bend_markers.clear();
    };
    // Lane-dominant bracket submission at NOTE granularity (user rule 2026-07-24, twice
    // refined: groups can hold lanes on both sides of a glyph, so group-level slotting let a
    // low open tail ride its higher groupmate over the notation). A bracket glyph stays
    // pending — compositing over every lower lane's tails and heads, whatever their onsets —
    // until the note about to draw sits on a lane ABOVE the glyph AND overlaps its span; the
    // batches flush and the glyph submits underneath that note, mid-group when that is where
    // the lane boundary falls (in-group notes iterate lane-ascending, so lower lanes are
    // already batched). Never-triggered glyphs drain after the last group.
    const auto submit_brackets_below = [&](const common::core::HighwayNoteView& note) {
        const int note_lane = invert ? (state.string_count + 1 - note.string) : note.string;
        bool flushed = false;
        for (BracketBatch& batch : bracket_batches)
        {
            if (batch.submitted || batch.lane >= note_lane ||
                note.start_seconds > batch.span_end_seconds ||
                note.end_seconds < batch.span_start_seconds)
            {
                continue;
            }
            if (!flushed)
            {
                flush_note_batches();
                flushed = true;
            }
            submitBatch(
                batch.vertices,
                batch.indices,
                posColorUvLayout(),
                texture_tint_program.get(),
                &heads_texture);
            batch.submitted = true;
        }
    };

    for (const std::size_t index : visible)
    {
        const common::core::HighwayNoteView& note = state.notes[index];
        const std::size_t group_index = note_group[index - first_note];
        const ChordGroup& group = chord_groups[group_index];
        if (group.box_only)
        {
            // Repeated and dead strums render as their repeat box alone:
            // no heads, shadows, tails, or anticipation for the group's notes.
            continue;
        }
        if (group_index != batched_group)
        {
            // Group boundary: the finished group's chevrons append last, over all its heads.
            // (The mid-group bracket flush deliberately skips this — chevrons keep pending and
            // ride a later batch, which still draws above the earlier one.)
            emit_pending_bend_markers();
            flush_note_batches();
            batched_group = group_index;
        }
        submit_brackets_below(note);
        const double lane_y =
            common::core::highwayStringLaneY(note.string, state.string_count, metrics, invert);
        const ArgbColor base_color = stringLaneColor(note.string, state.string_count, palette);
        const StringLaneStyle style{base_color};

        // Head anchor: an approaching head rides its onset toward the board; a sounding head
        // pins at the hit line (anchor = now, the arpeggio boxes' display-time treatment) and
        // travels with its slide, bend, and hand window in sync with the consumed tail; a
        // finished head fades out in place at the hit line — consumed, never passing through
        // the board — over the passed fade that runs from the hold end. The hold
        // end is the sustain end — or, for a sustainless strum under a hand-shape span, the
        // span end (the span reads as the chord's hold even though no tail is drawn, and the
        // pin persists while repeat boxes restate the chord underneath), released early when
        // a later strum re-shows the chord and takes over the pinned display. For a plain
        // sustainless note it is the onset, the original behavior.
        const double hold_end_seconds =
            std::max(note.end_seconds, std::min(display_hold_ends[index], group.hold_cap_seconds));
        const double head_seconds = std::clamp(now_seconds, note.start_seconds, hold_end_seconds);
        const double fade =
            hold_end_seconds >= now_seconds
                ? 1.0
                : std::max(0.0, 1.0 - ((now_seconds - hold_end_seconds) / g_passed_fade_seconds));
        // Strike transients (the fret-span attack line) keep the onset anchor and its own
        // fade: they mark the landing moment and scroll past like the measure lines they
        // mirror, rather than holding with the pinned head.
        const double attack_fade =
            note.start_seconds >= now_seconds
                ? 1.0
                : std::max(0.0, 1.0 - ((now_seconds - note.start_seconds) / g_passed_fade_seconds));
        // Slide state at the anchor: a sounding head glides with its slide, and an unpitched
        // release dims the head and its post in step with the tail.
        const SlideState head_slide = slide_state_at(
            note,
            note.fret > 0 ? common::core::highwayNoteCenterX(note.fret, metrics, mirrored) : 0.0,
            head_seconds);

        // Bend geometry: lift per semitone, inverted on the upper displayed half so curves stay
        // inside the board. The chart-truth station is the curve's anchor-time value (a pinned
        // sounding head rides the curve with the tail centerline); an approaching pre-bent head
        // reveals that station progressively — see the reveal below.
        const int displayed_lane = invert ? (state.string_count + 1 - note.string) : note.string;
        const double bend_direction =
            common::core::highwayBendInverted(displayed_lane, state.string_count) ? -1.0 : 1.0;
        const auto note_y_at = [&](const double seconds, const double taper) {
            double semitones =
                common::core::highwayBendSemitonesAt(note.bend, note.start_seconds, seconds);
            if (note.vibrato)
            {
                semitones += taper * common::core::g_highway_vibrato_depth_semitones *
                             common::core::highwayVibratoWobble(seconds - note.start_seconds);
            }
            return lane_y + (bend_direction * common::core::highwayBendLiftY(semitones, metrics));
        };
        // The pinned head samples the tail centerline's exact taper so its wobbles stay glued
        // to the tail's hit-line end while sounding (zero at both true tail ends).
        const double head_taper =
            note.end_seconds > note.start_seconds
                ? common::core::highwayTailTaper(
                      (head_seconds - note.start_seconds) / (note.end_seconds - note.start_seconds),
                      common::core::g_highway_tail_taper_fraction)
                : 0.0;
        // Chart-truth head station: the curve's value at the anchor time. A pre-bent curve is
        // already lifted at the onset, so this sits off the lane for the entire approach.
        const double chart_head_y = note_y_at(head_seconds, head_taper);
        // Rolling-flip clock, hoisted from the head-art roll below because the pre-bend reveal
        // shares it: 0 once the art lies flat (g_flip_flat_lead_seconds before the hit line),
        // 1 at the visibility edge.
        const double roll_span_seconds = std::max(
            span_end_seconds - now_seconds - g_flip_flat_lead_seconds, g_flip_flat_lead_seconds);
        const double flip_remaining = std::clamp(
            (note.start_seconds - now_seconds - g_flip_flat_lead_seconds) / roll_span_seconds,
            0.0,
            1.0);
        // Pre-bend reveal (user design 2026-07-31): an approaching pre-bent head spawns on its
        // own lane and rises toward the outlined chart-truth station in step with the rolling
        // flip, lining up exactly when the art lands flat; the outline, chevron, tail, and
        // anticipation ring hold the chart truth throughout, so the rising head is the only
        // moving element. Exact identity for every non-pre-bent note: the curve is 0.0 at the
        // onset, so chart_head_y == lane_y and the mix collapses. Chords skip the roll but keep
        // this clock, so chord pre-bends still land with their groupmates.
        const double head_y = note.start_seconds > now_seconds
                                  ? lane_y + ((chart_head_y - lane_y) * (1.0 - flip_remaining))
                                  : chart_head_y;

        // Sustain tail: from the hit line (while sounding) or the onset to the sustain end, as
        // Charter's three-band ribbon (solid edges around a translucent core). Technique
        // notes modulate the centerline, sampled adaptively in screen space.
        if (note.end_seconds > note.start_seconds && note.end_seconds > now_seconds)
        {
            const double tail_from = std::max(note.start_seconds, now_seconds);
            const double tail_to = std::min(note.end_seconds, span_end_seconds);

            // Tip fade: alpha dissolves over the sustain's last fraction (the glow posts' fade,
            // mirrored), anchored to the full note duration so the fading tip stays put while
            // the hit line consumes the body.
            const double duration = note.end_seconds - note.start_seconds;
            const auto tip_alpha = [&](const double seconds) {
                return std::clamp(
                    (note.end_seconds - seconds) / (duration * g_tail_tip_fade_fraction), 0.0, 1.0);
            };

            // Band X stations: fretted tails straddle the note center, open tails span the hand
            // window with Charter's inset (with a degenerate-window guard for tapered
            // necks). An open band's stations follow the sliding window per time, collapsing
            // gracefully when a mid-transition extent gets too narrow for the insets.
            const auto open_band_stations = [&](const double seconds) {
                const auto [window_x0, window_x1] =
                    handWindowXAt(state, seconds, metrics, mirrored);
                std::array<double, 4> stations{
                    window_x0 + g_open_tail_margin,
                    window_x0 + (2.0 * g_open_tail_margin),
                    window_x1 - (2.0 * g_open_tail_margin),
                    window_x1 - g_open_tail_margin,
                };
                if (stations[2] < stations[1])
                {
                    const double center = (stations[1] + stations[2]) / 2.0;
                    stations[1] = center;
                    stations[2] = center;
                    stations[0] = std::min(stations[0], center);
                    stations[3] = std::max(stations[3], center);
                }
                return stations;
            };
            std::array<double, 4> band{};
            bool band_valid = tail_to > tail_from;
            double base_x = 0.0;
            if (note.fret > 0)
            {
                base_x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);
                const double half = common::core::highwayTailHalfWidth(metrics);
                band = {base_x - half, base_x - (half / 2.0), base_x + (half / 2.0), base_x + half};
            }
            else
            {
                // Sampled at the VISIBLE tail start, not the onset: tail_from advances with
                // playback, and once a window move has scrolled fully behind the hit line the
                // remaining tail must hold the settled post-move window — the onset-time window
                // is the pre-move one, and using it snapped a ringing open tail back to the old
                // hand position the instant the ramp left the visible span (user finding
                // 2026-07-23). With no ramp inside [tail_from, tail_to] the window is constant
                // across the whole visible tail, so tail_from is exact.
                band = open_band_stations(tail_from);
                band_valid = band_valid && (band[3] - band[0] > 2.0 * g_open_tail_margin);
                base_x = (band[0] + band[3]) / 2.0;
            }

            const bool modulated =
                note.vibrato || note.tremolo || !note.bend.empty() || !note.slides.empty();
            // An open band whose window moves under it must sample its stations along the tail
            // (the tail travels with the hand — fhp-window-motion plan).
            const bool open_band_moves =
                note.fret == 0 && handWindowMovesWithin(state, tail_from, tail_to);
            if (band_valid && !modulated && !open_band_moves)
            {
                const auto ribbon_end = [&](const double seconds) {
                    const double alpha = tip_alpha(seconds);
                    const std::uint32_t edge = packAbgr(style.tail, alpha);
                    return RibbonEnd{
                        .x_offset = 0.0,
                        .y = lane_y,
                        .z = time_to_z(seconds),
                        .edge_abgr = edge,
                        .inner_abgr = packAbgr(style.tail, g_tail_inner_alpha * alpha),
                        .outer_abgr = note.fret > 0 ? edge : packAbgr(style.tail, 0.0),
                    };
                };
                // Split where the tip fade begins: alpha is linear on each side of the split,
                // so two segments render the envelope exactly.
                const double fade_begin_seconds =
                    note.end_seconds - (duration * g_tail_tip_fade_fraction);
                const double body_end = std::clamp(fade_begin_seconds, tail_from, tail_to);
                if (body_end > tail_from)
                {
                    pushRibbonSegment(
                        rail_vertices,
                        rail_indices,
                        band[0],
                        band[1],
                        band[2],
                        band[3],
                        ribbon_end(tail_from),
                        ribbon_end(body_end));
                }
                if (tail_to > body_end)
                {
                    pushRibbonSegment(
                        rail_vertices,
                        rail_indices,
                        band[0],
                        band[1],
                        band[2],
                        band[3],
                        ribbon_end(body_end),
                        ribbon_end(tail_to));
                }
            }
            else if (band_valid)
            {
                // Sample density comes from the projected arc length of the modulated
                // centerline (a coarse probe polyline), not the straight lane span: a bend's
                // vertical lift or a slide's lateral travel can dominate a tail's on-screen
                // length, and the flat measure starved exactly those tails of samples, so
                // their smooth curves rendered as chunky polylines with visible corners.
                constexpr std::size_t g_arc_probe_segments = 16;
                double pixels = 0.0;
                double probe_x = 0.0;
                double probe_y = 0.0;
                double probe_z = 0.0;
                for (std::size_t probe = 0; probe <= g_arc_probe_segments; ++probe)
                {
                    const double mix =
                        static_cast<double>(probe) / static_cast<double>(g_arc_probe_segments);
                    const double seconds = tail_from + ((tail_to - tail_from) * mix);
                    const double taper = common::core::highwayTailTaper(
                        (seconds - note.start_seconds) / duration,
                        common::core::g_highway_tail_taper_fraction);
                    const double arc_x = base_x + slide_state_at(note, base_x, seconds).x_offset;
                    const double arc_y = note_y_at(seconds, taper);
                    const double arc_z = time_to_z(seconds);
                    if (probe > 0)
                    {
                        pixels += projected_pixels(probe_x, probe_y, probe_z, arc_x, arc_y, arc_z);
                    }
                    probe_x = arc_x;
                    probe_y = arc_y;
                    probe_z = arc_z;
                }
                const std::size_t uniform_count = common::core::highwayTailSampleCount(
                    pixels, g_tail_pixels_per_sample, g_tail_sample_cap);
                std::vector<double> sample_times = common::core::makeHighwayTailSampleTimes(
                    note, tail_from, tail_to, uniform_count);
                if (open_band_moves)
                {
                    // Fold in the window's own ramp samples so the band tracks the eased border
                    // exactly instead of aliasing across it.
                    const std::vector<double> window_times =
                        windowSampleTimes(state, tail_from, tail_to);
                    sample_times.insert(
                        sample_times.end(), window_times.begin(), window_times.end());
                    std::ranges::sort(sample_times);
                    const auto duplicates = std::ranges::unique(sample_times);
                    sample_times.erase(duplicates.begin(), duplicates.end());
                }

                struct TailSample
                {
                    std::array<double, 4> stations;
                    double x_offset;
                    double y;
                    double z;
                    double alpha;
                };
                std::vector<TailSample> samples;
                samples.reserve(sample_times.size());
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
                            common::core::highwayTailHalfWidth(metrics) * taper *
                            common::core::highwayTremoloWobble(seconds - note.start_seconds);
                    }
                    samples.push_back(
                        TailSample{
                            .stations = open_band_moves ? open_band_stations(seconds) : band,
                            .x_offset = x_offset,
                            .y = note_y_at(seconds, taper),
                            .z = time_to_z(seconds),
                            .alpha = slide.alpha * tip_alpha(seconds),
                        });
                }
                // Per-sample slope shading (central differences over the centerline): a
                // climbing pitch brightens toward white, a release darkens, flat holds stay
                // at the base tint — cheap per-vertex lighting through the existing color
                // pipeline, no shader involved. tanh saturation, not a hard clamp: the clamp's
                // knee drew a visible hard-edged brightness band where a steep climb maxed
                // out, while tanh rolls off smoothly at the same sensitivity.
                std::vector<double> lifts(samples.size(), 0.0);
                for (std::size_t sample = 0; sample < samples.size(); ++sample)
                {
                    const std::size_t before = sample > 0 ? sample - 1 : sample;
                    const std::size_t after = sample + 1 < samples.size() ? sample + 1 : sample;
                    const double dz = samples[after].z - samples[before].z;
                    if (dz <= 0.0)
                    {
                        continue;
                    }
                    const double pitch_slope =
                        bend_direction * (samples[after].y - samples[before].y) / dz;
                    lifts[sample] =
                        g_tail_slope_shade_depth * std::tanh(pitch_slope * g_tail_slope_shade_gain);
                }
                // Tent-smooth the shade over a fixed time window (z is linear in time, so the
                // window converts once): the brightness fades in and out across the same
                // stretch of tail regardless of sample density or foreshortening, instead of
                // snapping where the derivative crosses tanh's knee.
                const double shade_window_z = std::abs(
                    time_to_z(now_seconds + g_tail_slope_shade_smooth_seconds) -
                    time_to_z(now_seconds));
                std::vector<ArgbColor> shaded(samples.size(), style.tail);
                for (std::size_t sample = 0; sample < samples.size(); ++sample)
                {
                    double lift = lifts[sample];
                    if (shade_window_z > 0.0)
                    {
                        // Samples are time-ordered and z is monotone in time, so the window
                        // walk outward from the sample stops at the first miss on each side.
                        double total = lifts[sample];
                        double total_weight = 1.0;
                        const auto accumulate = [&](const std::size_t other) {
                            const double distance = std::abs(samples[other].z - samples[sample].z);
                            if (distance >= shade_window_z)
                            {
                                return false;
                            }
                            const double weight = 1.0 - (distance / shade_window_z);
                            total += lifts[other] * weight;
                            total_weight += weight;
                            return true;
                        };
                        // The decrement stays out of the condition so the index is not modified
                        // and read in one expression (bugprone-inc-dec-in-conditions).
                        for (std::size_t other = sample; other > 0;)
                        {
                            --other;
                            if (!accumulate(other))
                            {
                                break;
                            }
                        }
                        for (std::size_t other = sample + 1;
                             other < samples.size() && accumulate(other);
                             ++other)
                        {
                        }
                        lift = total / total_weight;
                    }
                    shaded[sample] =
                        lift >= 0.0
                            ? mixArgb(style.tail, (style.tail & 0xFF000000U) | 0x00FFFFFFU, lift)
                            : mixArgb(style.tail, style.tail & 0xFF000000U, -lift);
                }
                for (std::size_t sample = 1; sample < samples.size(); ++sample)
                {
                    const TailSample& a = samples[sample - 1];
                    const TailSample& b = samples[sample];
                    const ArgbColor tail_a = shaded[sample - 1];
                    const ArgbColor tail_b = shaded[sample];
                    pushRibbonSegment(
                        rail_vertices,
                        rail_indices,
                        a.stations,
                        b.stations,
                        RibbonEnd{
                            .x_offset = a.x_offset,
                            .y = a.y,
                            .z = a.z,
                            .edge_abgr = packAbgr(tail_a, a.alpha),
                            .inner_abgr = packAbgr(tail_a, g_tail_inner_alpha * a.alpha),
                            .outer_abgr = packAbgr(tail_a, note.fret > 0 ? a.alpha : 0.0),
                        },
                        RibbonEnd{
                            .x_offset = b.x_offset,
                            .y = b.y,
                            .z = b.z,
                            .edge_abgr = packAbgr(tail_b, b.alpha),
                            .inner_abgr = packAbgr(tail_b, g_tail_inner_alpha * b.alpha),
                            .outer_abgr = packAbgr(tail_b, note.fret > 0 ? b.alpha : 0.0),
                        });
                }
            }
        }

        if (fade <= 0.0)
        {
            continue;
        }
        // Consumed at the line: a passed head keeps the hit-line station while its fade runs.
        // head_seconds itself stays clamped at the hold end so slide, bend, taper, and
        // hand-window sampling hold the note's final state instead of extrapolating past it.
        const double z = time_to_z(std::max(head_seconds, now_seconds));

        // Marker quads composite over the head base exactly like Charter's CPU-composited
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

        // Chord membership decides the rolling flip, the shadow, and the chord box (Charter
        // skips shadows for chord notes).
        const bool in_chord = group.count >= 2;

        // Glow post: the sustain tails' three-band ribbon stood upright at a user-tuned
        // fraction of the tail width, rising from the board toward the note's lane center and
        // dissolving to nothing at the fade-end fraction of that height. One geometry serves
        // both users — the note shadow at the onset (the note art overlays the post's top, so
        // every lane down to the bottom one carries a post scaled to its own height) and the
        // pitched slide-waypoint markers at their own slots and times — so a shape or banding
        // tweak can never desync them.
        const double post_half_width = common::core::highwayTailHalfWidth(metrics) * 0.375;
        const double post_top_y = head_y * g_shadow_post_fade_end_fraction;
        const double post_floor_alpha = fade * head_slide.alpha * g_shadow_post_floor_alpha;
        const auto push_glow_post =
            [&](const double center_x, const double post_z, const double floor_alpha) {
                const std::uint32_t floor_edge = packAbgr(base_color, floor_alpha);
                const std::uint32_t clear = packAbgr(base_color, 0.0);
                const RibbonEnd floor_end{
                    .x_offset = 0.0,
                    .y = 0.0,
                    .z = post_z,
                    .edge_abgr = floor_edge,
                    .inner_abgr = packAbgr(base_color, g_tail_inner_alpha * floor_alpha),
                    .outer_abgr = floor_edge,
                };
                const RibbonEnd head_end{
                    .x_offset = 0.0,
                    .y = post_top_y,
                    .z = post_z,
                    .edge_abgr = clear,
                    .inner_abgr = clear,
                    .outer_abgr = clear,
                };
                pushRibbonSegment(
                    shadow_vertices,
                    shadow_indices,
                    center_x - post_half_width,
                    center_x - (post_half_width / 2.0),
                    center_x + (post_half_width / 2.0),
                    center_x + post_half_width,
                    floor_end,
                    head_end);
            };

        // Fret-span line: a floor line under this single note, spanning the fret slots it
        // occupies — the measure lines' exact treatment (sharp teal attack on the landing z,
        // brief blue fade trailing toward the horizon), just clipped to the note's frets.
        // Drawn into the shadow batch so all other note geometry composites over it. As a
        // strike transient it keeps the onset anchor and fade while the head pins.
        const auto push_span_line = [&](const double span_x0, const double span_x1) {
            const double onset_z = time_to_z(note.start_seconds);
            pushFloorQuad(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                onset_z - g_attack_line_half_length,
                onset_z + g_attack_line_half_length,
                packAbgr(g_chord_box_color, g_attack_line_alpha * attack_fade));
            pushFloorQuadGradient(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                onset_z + g_attack_line_half_length,
                onset_z + g_attack_line_half_length + g_attack_fade_length,
                packAbgr(g_beat_bar_color, attack_fade),
                packAbgr(g_beat_bar_color, 0.0));
        };

        // A pitched slide waypoint's board furniture: a glow post and fret-span line at its own
        // slot and time — the intermediate targets the hand glides through. No note head: the
        // slide is one sounded note, so only its picked head is drawn (user rule 2026-07-28).
        // Waypoints stay on the note's string, so they share its lane and color; the post skips
        // the head's slide-dim (a waypoint has no sliding head above it). The fret number rides
        // the board floor with the scrolling numbers, pushed in that pass below.
        const auto push_waypoint_marker = [&](const int wp_fret, const double wp_seconds) {
            const double wp_x = common::core::highwayNoteCenterX(wp_fret, metrics, mirrored);
            const double wp_z = time_to_z(wp_seconds);
            push_glow_post(wp_x, wp_z, fade * g_shadow_post_floor_alpha);
            const double slot_low = common::core::highwayFretLineX(wp_fret - 1, metrics, mirrored);
            const double slot_high = common::core::highwayFretLineX(wp_fret, metrics, mirrored);
            const auto [span_x0, span_x1] = std::minmax(slot_low, slot_high);
            pushFloorQuad(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                wp_z - g_attack_line_half_length,
                wp_z + g_attack_line_half_length,
                packAbgr(g_chord_box_color, g_attack_line_alpha * fade));
        };

        if (note.fret == 0)
        {
            // Open string: Charter's thin rounded bar spanning the active hand window, in
            // the full note color (the flat tail-width slab it replaces read as a plank). A bar
            // landing mid-transition takes the eased window at its own anchor instant, so a
            // pinned sounding bar follows the sliding window like its ringing tail does.
            const auto [x0, x1] = handWindowXAt(state, head_seconds, metrics, mirrored);
            // L posts pointing inward from the bar ends (the chord box's bottom corner
            // holders, freestanding — user direction): one continuous two-leg ribbon per
            // corner, its cross-section turning 45 degrees at the corner station so the
            // bands wrap the L outline unbroken (an upright post plus a separate foot read
            // as a cross at the corner, not an L). Cross-section colors are the open-tail
            // treatment — transparent boundaries around edge strips around the translucent
            // core — and both leg ends fade to nothing: the upright at the shared
            // post_top_y (the glow posts' fade end below the bar), skipped entirely when that
            // top leaves no room above the corner miter.
            if (!in_chord)
            {
                push_span_line(x0, x1);
            }
            const double leg_thickness = 2.0 * post_half_width;
            if (!in_chord && post_top_y > leg_thickness)
            {
                for (const auto& [corner_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
                {
                    // A station holds the four cross-section points at fractions 0, 1/4, 3/4, 1
                    // across the leg thickness, outer L boundary first, plus the alpha envelope
                    // along the run.
                    struct LStation
                    {
                        std::array<double, 4> x;
                        std::array<double, 4> y;
                        double alpha_scale;
                    };
                    const auto leg_x = [&](const double fraction) {
                        return corner_x + (x_sign * leg_thickness * fraction);
                    };
                    const double tip_x = corner_x + (x_sign * g_open_post_foot_length);
                    const std::array<LStation, 3> stations{
                        // Top of the upright leg: fully faded (the glow posts' dissolve; the
                        // boosted floor alpha carries the per-lane visibility).
                        LStation{
                            .x = {leg_x(0.0), leg_x(0.25), leg_x(0.75), leg_x(1.0)},
                            .y = {post_top_y, post_top_y, post_top_y, post_top_y},
                            .alpha_scale = 0.0,
                        },
                        // Corner miter: the diagonal from the outer corner on the floor to the
                        // inner corner one thickness up and in.
                        LStation{
                            .x = {leg_x(0.0), leg_x(0.25), leg_x(0.75), leg_x(1.0)},
                            .y = {0.0, 0.25 * leg_thickness, 0.75 * leg_thickness, leg_thickness},
                            .alpha_scale = 1.0,
                        },
                        // Foot tip: fully faded.
                        LStation{
                            .x = {tip_x, tip_x, tip_x, tip_x},
                            .y = {0.0, 0.25 * leg_thickness, 0.75 * leg_thickness, leg_thickness},
                            .alpha_scale = 0.0,
                        },
                    };
                    // Per-station outer/edge/inner colors; bands run outer->edge, core,
                    // edge->outer like an open tail's cross-section.
                    const auto station_colors = [&](const LStation& station) {
                        return std::array<std::uint32_t, 3>{
                            packAbgr(base_color, 0.0),
                            packAbgr(base_color, post_floor_alpha * station.alpha_scale),
                            packAbgr(
                                base_color,
                                g_tail_inner_alpha * post_floor_alpha * station.alpha_scale),
                        };
                    };
                    constexpr std::array<std::array<std::size_t, 2>, 3> g_band_colors{
                        {{0, 1}, {2, 2}, {1, 0}}
                    };
                    for (std::size_t segment = 0; segment + 1 < stations.size(); ++segment)
                    {
                        const LStation& a = stations.at(segment);
                        const LStation& b = stations.at(segment + 1);
                        const std::array<std::uint32_t, 3> colors_a = station_colors(a);
                        const std::array<std::uint32_t, 3> colors_b = station_colors(b);
                        for (std::size_t band = 0; band < g_band_colors.size(); ++band)
                        {
                            const auto [from_color, to_color] = g_band_colors.at(band);
                            pushQuad(
                                shadow_vertices,
                                shadow_indices,
                                makeVertex(a.x.at(band), a.y.at(band), z, colors_a.at(from_color)),
                                makeVertex(
                                    a.x.at(band + 1), a.y.at(band + 1), z, colors_a.at(to_color)),
                                makeVertex(
                                    b.x.at(band + 1), b.y.at(band + 1), z, colors_b.at(to_color)),
                                makeVertex(b.x.at(band), b.y.at(band), z, colors_b.at(from_color)));
                        }
                    }
                }
            }
            pushOpenNoteBar(open_vertices, open_indices, x0, x1, head_y, z, base_color, fade, 1.0);
            if (note.accent)
            {
                // Charter's accent halo: the same bar at triple thickness, faint.
                pushOpenNoteBar(
                    open_vertices,
                    open_indices,
                    x0,
                    x1,
                    head_y,
                    z,
                    base_color,
                    fade * (96.0 / 255.0),
                    3.0);
            }
            // Technique markers at the window center (Charter's open-note overlay set).
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

        // Fretted head anchor: the fret-slot middle, or — for a NATURAL harmonic sounding
        // between frets — the chart's fractional node point, which is where the fret hand
        // touches. A pinch harmonic's touch is the PICKING hand's node: the fret hand stays on
        // note.fret, so the head keeps the fret anchor (imported pinches drew at the node fret,
        // user catch 2026-07-31) and the node waits for a dedicated right-hand cue (25-Q5).
        double x = common::core::highwayNoteCenterX(note.fret, metrics, mirrored);
        if (note.harmonic == common::core::NoteHarmonic::Natural && note.touch.has_value())
        {
            const double touch = *note.touch;
            const double touch_floor = std::floor(touch);
            const auto touch_fret = static_cast<int>(touch_floor);
            const double left_x = common::core::highwayFretLineX(touch_fret, metrics, mirrored);
            const double right_x =
                common::core::highwayFretLineX(touch_fret + 1, metrics, mirrored);
            x = left_x + ((right_x - left_x) * (touch - touch_floor));
        }
        // A sounding head travels with its glide and shakes with tremolo exactly like the tail
        // centerline's anchor-time sample, so head and tail stay one gesture at the hit line
        // (both offsets are zero before the onset).
        x += head_slide.x_offset;
        if (note.tremolo)
        {
            x += common::core::highwayTailHalfWidth(metrics) * head_taper *
                 common::core::highwayTremoloWobble(head_seconds - note.start_seconds);
        }

        if (!in_chord)
        {
            // Span from the note's own fret slot, not the (possibly harmonic-shifted) head x.
            const double slot_low_x =
                common::core::highwayFretLineX(note.fret - 1, metrics, mirrored);
            const double slot_high_x = common::core::highwayFretLineX(note.fret, metrics, mirrored);
            const auto [span_x0, span_x1] = std::minmax(slot_low_x, slot_high_x);
            push_span_line(span_x0, span_x1);
            push_glow_post(x, z, post_floor_alpha);
        }

        // Anticipation ring: scales down onto the landing spot over the last half second
        // (reference atlas cell; chart-driven, so the editor preview shows it too — 44-Q1).
        // The landing spot is the chart-truth station: a pre-bend's ring shrinks onto the
        // target outline, not onto the still-rising head.
        const double seconds_out = note.start_seconds - now_seconds;
        if (seconds_out > 0.0 && seconds_out < g_anticipation_seconds)
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
                    chart_head_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[0],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    chart_head_y - half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + half,
                    chart_head_y + half,
                    0.0,
                    ring_tint,
                    anticipation_cell[2],
                    anticipation_cell[1]),
                makeUvVertex(
                    x - half,
                    chart_head_y + half,
                    0.0,
                    ring_tint,
                    anticipation_cell[0],
                    anticipation_cell[1]));
        }

        // Pre-bend target outline (user design 2026-07-31): the anticipation cell — already a
        // hollow copy of the head's rim in the reference atlas — parks at the chart-truth
        // station for the whole approach, so a pre-bent note reads as a slot the head rises
        // into instead of passing for a plainly fretted note on the lane it occupies. It rides
        // the note's own z (unlike the ring's hit-line landing preview), stays axis-aligned
        // like the upright technique markers, and stops at the onset, where the landed head
        // covers it. chart_head_y - lane_y is exactly the onset bend lift (the vibrato taper is
        // zero at the onset) and exactly 0.0 for a non-pre-bent curve, so the > 0.0 test is a
        // precise pre-bend gate, not a tolerance.
        if (note.start_seconds > now_seconds && std::abs(chart_head_y - lane_y) > 0.0)
        {
            const std::uint32_t outline_tint =
                packAbgr(base_color, g_prebend_outline_alpha * fade * head_slide.alpha);
            pushQuad(
                head_vertices,
                head_indices,
                makeUvVertex(
                    x - head_half_w,
                    chart_head_y - head_half_h,
                    z,
                    outline_tint,
                    anticipation_cell[0],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + head_half_w,
                    chart_head_y - head_half_h,
                    z,
                    outline_tint,
                    anticipation_cell[2],
                    anticipation_cell[3]),
                makeUvVertex(
                    x + head_half_w,
                    chart_head_y + head_half_h,
                    z,
                    outline_tint,
                    anticipation_cell[2],
                    anticipation_cell[1]),
                makeUvVertex(
                    x - head_half_w,
                    chart_head_y + head_half_h,
                    z,
                    outline_tint,
                    anticipation_cell[0],
                    anticipation_cell[1]));
        }

        // Rolling flip: single notes stand vertical as they enter the visibility window and
        // roll flat around their travel axis across the whole approach, landing flat
        // g_flip_flat_lead_seconds before the hit line; chord notes stay flat throughout. The
        // clock (flip_remaining) is computed beside the head station, where the pre-bend
        // reveal shares it.
        const double rotation = in_chord ? 0.0 : (-std::numbers::pi / 2.0) * flip_remaining;
        const double cos_r = std::cos(rotation);
        const double sin_r = std::sin(rotation);
        const std::uint32_t tint = packAbgr(base_color, fade * head_slide.alpha);

        // Head base: the technique variant under left-hand technique markers, else the standard
        // head (Charter's base-cell selection).
        const bool tech_head = note.mute == common::core::NoteMute::Full ||
                               note.harmonic == common::core::NoteHarmonic::Natural ||
                               note.attack == common::core::NoteAttack::Hammer ||
                               note.attack == common::core::NoteAttack::Pull;
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

        {
            // Rotating markers ride the rolling flip (Charter bakes these into the head
            // texture), in Charter's composite order.
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
            // Upright markers stay flat through the flip (Charter overlays these after
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

        // Bend notation (bend-head-indicators plan, fourth pass on sight 2026-07-28: chevron
        // stacks read as clutter, amount figures did not read at speed, and target rails were
        // redundant furniture once the tail itself carried the amount): the head carries ONE
        // chevron marker — a caret-shaped bend cue —
        // announcing only that a bend is coming. It rides the bend-lift side of the head
        // (above the note for an upward curve, below on bend-inverted lanes) and its 180-degree
        // flip keeps it pointing where the drawn curve goes. Amount and stages are the tail's
        // own geometry: physical lift height plus slope shading. The station is the
        // chart-truth height: on an approaching pre-bend the chevron rides the target outline,
        // not the rising head (user rule 2026-07-31); everywhere else chart and head coincide.
        if (!note.bend.empty())
        {
            pending_bend_markers.push_back(
                PendingBendMarker{
                    .x = x,
                    .y = chart_head_y + (bend_direction * g_bend_marker_offset_heads * head_half_h),
                    .z = z,
                    .direction = bend_direction,
                    .half_w = head_half_w,
                    .half_h = head_half_h,
                    .tint = tint,
                });
        }

        // Each pitched slide waypoint gets its own post and line; an unpitched slide-out
        // is a pressure release with no target to mark, so it gets no board furniture — only the
        // rail's own dimming trail. A waypoint carries its own time, which can sit well past its
        // note's onset, so each marker culls to the same upcoming window as its floor fret
        // number below: past span_end it would float beyond the board's far edge, and behind the
        // hit line it would stand at full alpha after its number vanished. Waypoint and
        // tapped-note fret numbers ride the board floor with the scrolling numbers, pushed in
        // that pass below.
        // Stacked chord slides dedup (user rule 2026-07-30): members sliding together land
        // waypoints on the same fret at the same instant, and their markers would pile up in
        // one slot — only the member on the lowest displayed lane (nearest the floor, so its
        // post overlaps nothing above it) draws the shared marker.
        const auto stacked_below = [&](const common::core::HighwaySlideView& waypoint) {
            for (std::size_t member = group.first; member < group.first + group.count; ++member)
            {
                const common::core::HighwayNoteView& other = state.notes[member];
                if (member == index ||
                    common::core::highwayStringLaneY(
                        other.string, state.string_count, metrics, invert) >= lane_y)
                {
                    continue;
                }
                for (const common::core::HighwaySlideView& other_waypoint : other.slides)
                {
                    if (!other_waypoint.unpitched && other_waypoint.fret == waypoint.fret &&
                        std::abs(other_waypoint.seconds - waypoint.seconds) < g_onset_match_epsilon)
                    {
                        return true;
                    }
                }
            }
            return false;
        };
        for (const common::core::HighwaySlideView& waypoint : note.slides)
        {
            if (!waypoint.unpitched && waypoint.fret > 0 && waypoint.seconds > now_seconds &&
                waypoint.seconds <= span_end_seconds && !stacked_below(waypoint))
            {
                push_waypoint_marker(waypoint.fret, waypoint.seconds);
            }
        }
    }

    emit_pending_bend_markers();
    flush_note_batches();
    // Drain the never-triggered bracket glyphs: nothing above their lanes overlapped them, so
    // they read over everything below.
    for (BracketBatch& batch : bracket_batches)
    {
        if (!batch.submitted)
        {
            submitBatch(
                batch.vertices,
                batch.indices,
                posColorUvLayout(),
                texture_tint_program.get(),
                &heads_texture);
            batch.submitted = true;
        }
    }

    // --- Scrolling fret numbers: Charter's readability aid. Numbers ride the board floor
    // at each dotted fret on every measure downbeat (bright inside the current hand range, dim
    // elsewhere), mark each upcoming hand-position arrival in orange, and pin the current hand's
    // numbers at the hit line; all fade in as they approach. Drawn after the notes so numbers
    // read over every note, tail, and chord box, but before the board face: the face is the
    // wall nearest the camera, so its fret lines and skin keep occluding numbers scrolling in
    // behind it (numbers popping through the fretboard would read as a depth violation). ---
    {
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double z_faded = common::core::highwayTimeToZ(0.05, scroll, metrics);
        const double z_close = common::core::highwayTimeToZ(0.25, scroll, metrics);

        // One billboarded number at a fret's slot on the board floor; alpha fades in between the
        // hit line and z_close when fade is requested (Charter bakes the fade into the color,
        // since the glyph program has no fade uniform), scaled by the caller's own alpha (the
        // window-coverage fades).
        const auto push_number = [&](const int fret,
                                     const double z,
                                     const ArgbColor base,
                                     const bool fade,
                                     const double alpha) {
            const double glyph_height = z > 0.0 ? 0.70 : 0.40;
            const std::string label = std::to_string(fret);
            const double text_width = glyph_height * 0.62 * static_cast<double>(label.size());
            const double left_x =
                common::core::highwayNoteCenterX(fret, metrics, mirrored) - (text_width / 2.0);
            double alpha_scale = alpha;
            if (fade && z < z_close)
            {
                alpha_scale *= std::clamp((z - z_faded) / (z_close - z_faded), 0.0, 1.0);
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
        // How deeply a fret's whole lane sits inside a window: the min of its two lines'
        // coverages — the shared signal the number fades and color blends follow.
        const auto fret_coverage = [](const common::core::HighwayHandWindow& window,
                                      const int fret) {
            return std::min(
                common::core::highwayHandWindowLineCoverage(window, static_cast<double>(fret - 1)),
                common::core::highwayHandWindowLineCoverage(window, static_cast<double>(fret)));
        };

        // Dotted-fret numbers on each visible measure downbeat, lit within the hand range (a
        // downbeat mid-transition blends the dim and active colors by its coverage).
        for (const common::core::HighwayBeatView& beat : state.beats)
        {
            if (!beat.measure_downbeat || beat.seconds < now_seconds - 0.2 ||
                beat.seconds > span_end_seconds)
            {
                continue;
            }
            const common::core::HighwayHandWindow beat_window =
                common::core::highwayHandWindowAt(state.fret_hand_positions, beat.seconds);
            const double z = time_to_z(beat.seconds);
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                if (!isDottedFret(fret))
                {
                    continue;
                }
                const double coverage = fret_coverage(beat_window, fret);
                push_number(
                    fret,
                    z,
                    mixArgb(g_fret_number_dim_color, g_fret_number_active_color, coverage),
                    true,
                    1.0);
            }
        }

        // An upcoming floor target — a hand-position arrival, a tapped note, or a pitched slide
        // waypoint — gets the same orange number at its fret slot, fading in on approach. One
        // push owns the window gate and the argument bundle so the borrowed treatments can
        // never drift apart.
        const auto push_target_number = [&](const int fret, const double seconds) {
            if (seconds <= now_seconds || seconds > span_end_seconds)
            {
                return;
            }
            push_number(fret, time_to_z(seconds), g_fret_number_fhp_color, true, 1.0);
        };

        // Upcoming hand-position arrivals, in the FHP orange.
        for (const common::core::HighwayFhpView& fhp : state.fret_hand_positions)
        {
            push_target_number(fhp.fret, fhp.seconds);
        }

        // Tap numbers label POSITIONS, not notes (user rule 2026-07-28 — per-note numbers
        // over-labeled dense runs): one number per tap onset, at its low fret like a
        // hand-position arrival, and only when it tells the player something new — the first
        // tap after the lighting lapsed, or a move away from the position the previous tap's
        // path LANDED in. A repeat inside a lit run (the previous release plus the ribbon
        // decay still bridging this rise — the same bridge the lane edges show) is already
        // established and stays unlabeled, as are chord upper members. A tapped glide then
        // establishes each landing as its own new position (user rule 2026-07-28, matching the
        // placements a fretting-hand glide carries at its targets): every path station that
        // changes the extent gets an arrival number of its own.
        const common::core::HighwayTapOnsetView* previous_tap = nullptr;
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            const bool repeat_in_lit_run =
                previous_tap != nullptr &&
                std::lround(previous_tap->path.back().fret_low) == tap.fret_low &&
                std::lround(previous_tap->path.back().fret_high) == tap.fret_high &&
                previous_tap->path.back().seconds + g_tap_ribbon_decay_seconds >=
                    tap.seconds - tap.ramp_seconds;
            if (!repeat_in_lit_run)
            {
                push_target_number(tap.fret_low, tap.seconds);
            }
            for (std::size_t station = 1; station < tap.path.size(); ++station)
            {
                const common::core::HighwayTapLightStation& a = tap.path[station - 1];
                const common::core::HighwayTapLightStation& b = tap.path[station];
                if (std::is_neq(a.fret_low <=> b.fret_low) ||
                    std::is_neq(a.fret_high <=> b.fret_high))
                {
                    push_target_number(static_cast<int>(std::lround(b.fret_low)), b.seconds);
                }
            }
            previous_tap = &tap;
        }

        // Slide waypoints deliberately push no numbers of their own (user rule 2026-07-28,
        // completing the one-rule model: an orange number marks a hand position being
        // established, nothing else). A fretting-hand glide that moves the window carries a
        // hand-position placement at its target (normalization rule 9), and a tapped glide's
        // landings are labeled through the path-station loop above — both hands' glides earn
        // their numbers as POSITIONS, never as waypoints. The waypoint glow posts and
        // fret-span lines remain — they are target furniture, not labels.

        // The current hand's numbers pinned at the hit line. Coverage fade (signed 2026-07-23):
        // every glyph stays at its own lane's fixed position and animates opacity only, fading
        // out as the sweeping border leaves its lane and in as the border reaches it.
        for (int fret = 1; fret <= g_face_fret_count; ++fret)
        {
            const double coverage = fret_coverage(current_window, fret);
            if (coverage > 0.0)
            {
                push_number(fret, 0.0, g_fret_number_fhp_color, false, coverage);
            }
        }

        const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
        submitBatch(vertices, indices, posColorUvLayout(), glyph_program.get(), &glyph_texture);
    }

    // --- String lines (retained), under the fret lines and nut, on the z = 0 plane. The board
    // paints in submission order (sequential view, depth test only), so the strings go down first
    // and the fret lines and nut below draw over them. ---
    if (face_index_count > 0 && face_vertices.isValid() && face_indices.isValid())
    {
        bgfx::setVertexBuffer(0, face_vertices.get());
        bgfx::setIndexBuffer(face_indices.get(), 0, face_index_count);
        bgfx::setState(g_blended_state);
        bgfx::submit(g_board_view, color_program.get());
    }

    // --- Board face: dynamic fret lines with Charter's three states (inactive, active
    // within current and upcoming hand windows, and the sqrt-decay hit-flash that thickens up
    // to 4x — a large part of the alive feel), drawn over the string lines and passing content.
    // Fret lines run from
    // face_bottom_y to face_top_y (the string grid alone — the gap below the grid base belongs
    // to the chord boxes' bottom bars), both defined above the chord-box pass. ---
    {
        // Active fret lines: the current hand window's coverage (fractional mid-transition, so
        // the face lines' active state crossfades in lockstep with the sweeping border) plus
        // every window arriving soon at full weight.
        std::array<double, g_face_fret_count + 1> active{};
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            active.at(static_cast<std::size_t>(line)) = common::core::highwayHandWindowLineCoverage(
                current_window, static_cast<double>(line));
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
                    active.at(static_cast<std::size_t>(line)) = 1.0;
                }
            }
        }
        // Right-hand windows activate their lines under the same horizon (user rule
        // 2026-07-28): the lines the tapping hand's light path crosses light up while the tap
        // is held or arriving soon. Per-line array, so overlap with the fretting hand's
        // windows deduplicates itself; the path union carries any tapped-slide morph.
        for (const common::core::HighwayTapOnsetView& tap : state.tap_onsets)
        {
            if (tap.path.front().seconds > now_seconds + g_fret_active_horizon_seconds)
            {
                break; // onsets ascend, so nothing later reaches the horizon
            }
            if (tap.path.back().seconds + g_tap_light_decay_seconds < now_seconds)
            {
                continue;
            }
            double low = tap.path.front().fret_low;
            double high = tap.path.front().fret_high;
            for (const common::core::HighwayTapLightStation& station : tap.path)
            {
                low = std::min(low, station.fret_low);
                high = std::max(high, station.fret_high);
            }
            const int first_line = std::max(0, static_cast<int>(std::floor(low)) - 1);
            const int last_line = std::min(g_face_fret_count, static_cast<int>(std::ceil(high)));
            for (int line = first_line; line <= last_line; ++line)
            {
                active.at(static_cast<std::size_t>(line)) = 1.0;
            }
        }

        // Strike brightening lives wholly in the additive glow pass at the end of the frame;
        // the lines themselves carry only the inactive/active hand-window state.
        std::vector<PosColorVertex> vertices;
        std::vector<std::uint16_t> indices;
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            const double x = common::core::highwayFretLineX(line, metrics, mirrored);
            const ArgbColor color = mixArgb(
                g_fret_inactive_color,
                g_fret_active_color,
                active.at(static_cast<std::size_t>(line)));
            const double half = line == 0 ? 0.05 : 0.025;
            pushFaceQuad(
                vertices,
                indices,
                x - half,
                x + half,
                face_bottom_y,
                face_top_y,
                0.0,
                packAbgr(color));
        }
        submitBatch(vertices, indices, posColorLayout(), color_program.get(), nullptr);
    }

    // --- Fretboard skin: one textured cell per fret from the reference inlay atlas (8x4 grid),
    // drawn last on the face like Charter (the art is transparent between markers). ---
    if (inlay_texture.isValid())
    {
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        constexpr int g_inlay_columns = 8;
        constexpr int g_inlay_rows = 4;
        // Half-texel inset so a cell samples strictly inside its own texels; the inlay PNG is not
        // square, so u and v inset by different amounts. Zero dimensions (decode failed) fall back
        // to no inset.
        const float half_texel_u =
            inlay_texture_width > 0 ? 0.5F / static_cast<float>(inlay_texture_width) : 0.0F;
        const float half_texel_v =
            inlay_texture_height > 0 ? 0.5F / static_cast<float>(inlay_texture_height) : 0.0F;
        const float cell_u = 1.0F / g_inlay_columns;
        const float cell_v = 1.0F / g_inlay_rows;
        for (int fret = 1; fret <= g_face_fret_count; ++fret)
        {
            const int cell = fret - 1;
            // Named row/column: the integer division is the grid addressing, kept out of the
            // float expressions on purpose.
            const int cell_column = cell % g_inlay_columns;
            const int cell_row = cell / g_inlay_columns;
            const float u0 = (static_cast<float>(cell_column) * cell_u) + half_texel_u;
            const float v0 = (static_cast<float>(cell_row) * cell_v) + half_texel_v;
            const float u1 = (static_cast<float>(cell_column + 1) * cell_u) - half_texel_u;
            const float v1 = (static_cast<float>(cell_row + 1) * cell_v) - half_texel_v;
            const double low_x = common::core::highwayFretLineX(fret - 1, metrics, mirrored);
            const double high_x = common::core::highwayFretLineX(fret, metrics, mirrored);
            const auto [x0, x1] = std::minmax(low_x, high_x);
            const std::uint32_t white = packAbgr(0xFFFFFFFF);
            pushQuad(
                vertices,
                indices,
                makeUvVertex(x0, face_bottom_y, 0.0, white, u0, v1),
                makeUvVertex(x1, face_bottom_y, 0.0, white, u1, v1),
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
    // after the skin (Charter's pass order). Suppressed while the current chord is fully
    // muted — dead chugs show no fingering. ---
    {
        // The active shape: the last one starting within Charter's 20 ms lookahead that
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
            // (Arpeggio brackets now ride the arpeggio box in the chord/arpeggio-box pass, which
            // scrolls them in and parks them at the hit line — folded in from the old
            // active-shape-only pass that used to draw here.)

            // Fingering spots: barre-aware shape cells plus finger-name cells from the
            // fingering texture (a real-alpha PNG, so the premultiplied blend applies).
            if (fingering_texture.isValid())
            {
                std::vector<PosColorUvVertex> vertices;
                std::vector<std::uint16_t> indices;
                const double spot_half = metrics.string_distance / 2.0;
                const std::uint32_t white = packAbgr(0xFFFFFFFF);
                // Quarter-grid UV cells with Charter's inset.
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
            std::string label = section.name;
            std::ranges::transform(label, label.begin(), [](const char c) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            });
            (void)push_text(
                label,
                handWindowXAt(state, section.seconds, metrics, mirrored).first,
                section_y,
                time_to_z(section.seconds),
                0.5,
                packAbgr(0xFFFFFFFF, 0.85));
        }

        // Chord names ride the hit line while their shape is active (Charter's placement: left
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
            // Display-time window: a name riding the hit line follows the window per frame, so
            // it travels with a chord sliding under its shape span.
            const double window_seconds = std::max(shape.start_seconds, now_seconds);
            (void)push_text(
                shape.name,
                handWindowXAt(state, window_seconds, metrics, mirrored).first - 1.75,
                chord_name_y,
                std::max(0.0, time_to_z(shape.start_seconds)),
                0.7,
                packAbgr(g_chord_name_color));
        }

        const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
        submitBatch(
            glyph_vertices, glyph_indices, posColorUvLayout(), glyph_program.get(), &glyph_texture);
    }

    // --- Strike glow: an additive light that pops the instant a note crosses the fretboard and
    // reads as a 100%-perfect strike (fret-hit-light-effect plan; deterministic note-arrival
    // trigger — an input-gated game version swaps only the trigger source). Reuses the window
    // light's soft-x-edge sprite under the additive blend, so a strike strictly ADDS luminance
    // and pops identically on lit and unlit content. Deliberately the LAST board-view
    // submission: the premultiplied inlay skin would punch dark dot silhouettes through a glow
    // drawn earlier, and the fingering panel and hit-line text would dim it. The envelope is a
    // stateless function of now - onset, per-onset with an inter-onset release clamp, so fast
    // sections keep a discrete pop per strike instead of fusing into a shimmer. ---
    {
        std::vector<PosColorUvVertex> vertices;
        std::vector<std::uint16_t> indices;
        const double spill = g_hit_glow_falloff / 2.0;
        const double clamp_horizon = g_hit_glow_release_seconds + g_hit_glow_trough_guard_seconds;

        // One soft vertical strip on the face: hot core g_hit_glow_core_half wide, the window
        // light's soft x edges, and the envelope in vertex alpha fading toward the face top so
        // the light reads grounded at the strings' crossing (the mask itself is horizontal-only).
        const auto push_strip = [&](const double center_x, const double envelope) {
            const std::uint32_t bottom = packAbgr(g_hit_glow_color, envelope);
            const std::uint32_t top = packAbgr(g_hit_glow_color, envelope * g_hit_glow_top_fade);
            const auto vertex = [&](const double x, const double y, const std::uint32_t tint) {
                return makeUvVertex(
                    x,
                    y,
                    0.0,
                    tint,
                    static_cast<float>((x - (center_x - g_hit_glow_core_half)) + spill),
                    static_cast<float>(((center_x + g_hit_glow_core_half) - x) + spill));
            };
            const double x0 = center_x - g_hit_glow_core_half - spill;
            const double x1 = center_x + g_hit_glow_core_half + spill;
            pushQuad(
                vertices,
                indices,
                vertex(x0, face_bottom_y, bottom),
                vertex(x1, face_bottom_y, bottom),
                vertex(x1, face_top_y, top),
                vertex(x0, face_top_y, top));
        };
        // Per-fret-line max envelopes: fretted singles, single taps, and tapped-box edge lines
        // share these slots, so overlapping strikes on a shared line resolve by max, never
        // additive stacking.
        std::array<double, g_face_fret_count + 1> line_glow{};
        const auto light_line = [&](const int line, const double envelope) {
            if (line >= 0 && line <= g_face_fret_count)
            {
                line_glow.at(static_cast<std::size_t>(line)) =
                    std::max(line_glow.at(static_cast<std::size_t>(line)), envelope);
            }
        };
        // Strikes that light the two live window-edge frets: lone opens, and strummed chords —
        // a strum glows only the chord box's left and right frets, the box interior stays
        // deliberately dark (user direction 2026-07-30; what the interior does instead is an
        // open decision). Both kinds share the same two strips, so their onsets collect here in
        // ascending order and each clamps against the next window-edge strike of either kind.
        std::vector<double> window_edge_onsets;

        // Fretting-hand onset clusters, walked over the glow's own onset window: glow tails
        // outlive the passed-note fade, so the pass binary-searches state.notes directly
        // instead of reusing the visible range (which drops a sustainless note
        // g_passed_fade_seconds after it crosses and would cap every tunable release). The walk
        // extends one clamp horizon past now so strikes at the hit line clamp against strikes
        // still approaching. Clusters use the chord boxes' own grouping rule: notes within the
        // onset epsilon strike together, and only non-tap members count toward the box. Tap
        // onsets are the other hand and glow from state.tap_onsets below.
        const auto glow_begin = std::ranges::lower_bound(
            state.notes,
            now_seconds - g_hit_glow_release_seconds,
            std::ranges::less{},
            [](const common::core::HighwayNoteView& note) { return note.start_seconds; });
        for (auto index = static_cast<std::size_t>(glow_begin - state.notes.begin());
             index < state.notes.size();)
        {
            const double cluster_start = state.notes[index].start_seconds;
            if (cluster_start > now_seconds + clamp_horizon)
            {
                break;
            }
            std::size_t cluster_end = index + 1;
            while (cluster_end < state.notes.size() &&
                   std::abs(state.notes[cluster_end].start_seconds - cluster_start) <
                       g_onset_match_epsilon)
            {
                ++cluster_end;
            }
            std::size_t non_tap_count = 0;
            bool any_open = false;
            for (std::size_t member = index; member < cluster_end; ++member)
            {
                const common::core::HighwayNoteView& note = state.notes[member];
                if (note.attack != common::core::NoteAttack::Tap)
                {
                    ++non_tap_count;
                    any_open = any_open || note.fret == 0;
                }
            }
            const bool boxed = non_tap_count >= 2;
            if (boxed || any_open)
            {
                window_edge_onsets.push_back(cluster_start);
            }
            if (!boxed && cluster_start <= now_seconds)
            {
                // Fretted singles (including a fretted note under a simultaneous tap): each
                // lights its own fret lines. A later strike on the same fret clamps the tail
                // even when it folds into a chord box whose edge frets miss these lines — the
                // error is a slightly shorter tail, erring toward discreteness.
                for (std::size_t member = index; member < cluster_end; ++member)
                {
                    const common::core::HighwayNoteView& note = state.notes[member];
                    if (note.attack == common::core::NoteAttack::Tap || note.fret <= 0)
                    {
                        continue;
                    }
                    double spacing = std::numeric_limits<double>::infinity();
                    for (std::size_t next = cluster_end; next < state.notes.size(); ++next)
                    {
                        const common::core::HighwayNoteView& later = state.notes[next];
                        if (later.start_seconds - note.start_seconds > clamp_horizon)
                        {
                            break;
                        }
                        if (later.fret == note.fret)
                        {
                            spacing = later.start_seconds - note.start_seconds;
                            break;
                        }
                    }
                    const double envelope = common::core::highwayHitGlowIntensity(
                        now_seconds - note.start_seconds,
                        common::core::highwayHitGlowRelease(
                            g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing));
                    if (envelope > 0.0)
                    {
                        light_line(note.fret - 1, envelope);
                        light_line(note.fret, envelope);
                    }
                }
            }
            index = cluster_end;
        }

        // Window-edge envelope: ascending onsets, each clamped against its successor (open or
        // strum alike — they relight the same two strips), resolved by max into one shared
        // intensity.
        double window_edge_glow = 0.0;
        for (std::size_t onset = 0; onset < window_edge_onsets.size(); ++onset)
        {
            if (window_edge_onsets[onset] > now_seconds)
            {
                break;
            }
            const double spacing = onset + 1 < window_edge_onsets.size()
                                       ? window_edge_onsets[onset + 1] - window_edge_onsets[onset]
                                       : std::numeric_limits<double>::infinity();
            window_edge_glow = std::max(
                window_edge_glow,
                common::core::highwayHitGlowIntensity(
                    now_seconds - window_edge_onsets[onset],
                    common::core::highwayHitGlowRelease(
                        g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing)));
        }

        // Slide landings and bend targets: every scored arrival pops the glow at its geometry
        // (user frame 2026-07-30 — the game registers these as hit-or-miss, and the editor
        // previews 100%-perfect play, so each one shows its success feedback). A pitched slide
        // waypoint is a fret arrival — the finger lands on a new fret, the tail kinks there,
        // the FHP window ramps there — and pops the landing's lines, whichever hand slides;
        // unpitched trail-offs are pressure already releasing and contribute nothing (the tap
        // light's rule). A bend target is a pitch arrival on the fret the finger stays planted
        // on, so it pops that same line pair: each curve point ending a sloped segment (bend
        // reached, release completed) is an arrival, while flat holds and the onset point are
        // not — the strike already covers the onset. No inter-onset clamp: these are sparse,
        // never the machine-gun case the clamp exists for, and the per-line max absorbs
        // overlap. The sustain-aware range query covers a long sustain sliding or bending at
        // its very end, whose onset left the cluster walk's window long ago.
        const auto [waypoint_first, waypoint_last] = common::core::highwayVisibleNoteRange(
            state.notes, sustain_prefix_max, now_seconds - g_hit_glow_release_seconds, now_seconds);
        for (std::size_t index = waypoint_first; index < waypoint_last; ++index)
        {
            const common::core::HighwayNoteView& note = state.notes[index];
            for (const common::core::HighwaySlideView& waypoint : note.slides)
            {
                if (waypoint.unpitched || waypoint.fret <= 0)
                {
                    continue;
                }
                const double envelope = common::core::highwayHitGlowIntensity(
                    now_seconds - waypoint.seconds, g_hit_glow_release_seconds);
                if (envelope > 0.0)
                {
                    light_line(waypoint.fret - 1, envelope);
                    light_line(waypoint.fret, envelope);
                }
            }
            for (std::size_t point = 1; note.fret > 0 && point < note.bend.size(); ++point)
            {
                const common::core::HighwayBendPointView& segment_from = note.bend[point - 1];
                const common::core::HighwayBendPointView& arrival = note.bend[point];
                if (std::is_eq(arrival.semitones <=> segment_from.semitones))
                {
                    continue; // a flat hold segment ends in no arrival
                }
                const double envelope = common::core::highwayHitGlowIntensity(
                    now_seconds - arrival.seconds, g_hit_glow_release_seconds);
                if (envelope > 0.0)
                {
                    light_line(note.fret - 1, envelope);
                    light_line(note.fret, envelope);
                }
            }
        }

        // Tapping-hand onsets: a tapped chord pops the two fret lines at its box's edges (the
        // interior stays dark like the strummed boxes), a single tap pops its fret lines like a
        // fretted single. Same-geometry means the same fret extent; partially overlapping
        // extents are separate lights that max-resolve on any shared line.
        for (std::size_t tap_index = 0; tap_index < state.tap_onsets.size(); ++tap_index)
        {
            const common::core::HighwayTapOnsetView& tap = state.tap_onsets[tap_index];
            if (tap.seconds > now_seconds)
            {
                break; // onsets ascend
            }
            const double since = now_seconds - tap.seconds;
            if (since >= g_hit_glow_release_seconds)
            {
                continue;
            }
            double spacing = std::numeric_limits<double>::infinity();
            for (std::size_t next = tap_index + 1; next < state.tap_onsets.size(); ++next)
            {
                const common::core::HighwayTapOnsetView& later = state.tap_onsets[next];
                if (later.seconds - tap.seconds > clamp_horizon)
                {
                    break;
                }
                if (later.fret_low == tap.fret_low && later.fret_high == tap.fret_high &&
                    (later.count >= 2) == (tap.count >= 2))
                {
                    spacing = later.seconds - tap.seconds;
                    break;
                }
            }
            const double envelope = common::core::highwayHitGlowIntensity(
                since,
                common::core::highwayHitGlowRelease(
                    g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing));
            if (envelope <= 0.0)
            {
                continue;
            }
            if (tap.count >= 2)
            {
                light_line(tap.fret_low - 1, envelope);
                light_line(tap.fret_high, envelope);
            }
            else
            {
                light_line(tap.fret_low - 1, envelope);
                light_line(tap.fret_low, envelope);
            }
        }

        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            const double envelope = line_glow.at(static_cast<std::size_t>(line));
            if (envelope > 0.0)
            {
                push_strip(common::core::highwayFretLineX(line, metrics, mirrored), envelope);
            }
        }
        if (window_edge_glow > 0.0)
        {
            // Chord-box edges and open strikes follow the live (possibly sliding) window, so a
            // decay tail travels with the hand exactly like the window light it brightens.
            const auto [low_x, high_x] = handWindowXAt(state, now_seconds, metrics, mirrored);
            push_strip(low_x, window_edge_glow);
            push_strip(high_x, window_edge_glow);
        }

        const std::array<float, 4> light_params{
            static_cast<float>(g_hit_glow_falloff), 0.0F, 0.0F, 0.0F
        };
        bgfx::setUniform(window_light_params.get(), light_params.data());
        submitBatch(
            vertices,
            indices,
            posColorUvLayout(),
            window_light_program.get(),
            nullptr,
            g_board_view,
            g_additive_state);
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
