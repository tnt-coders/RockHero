#include "highway/bgfx_program.h"
#include "highway/box_mute_profile.h"
#include "highway/head_art_profile.h"
#include "highway/highway_atlas.h"
#include "highway/highway_emphasis_styles.h"
#include "highway/highway_floor_geometry.h"
#include "highway/highway_head_marks.h"
#include "highway/highway_slide_path.h"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cassert>
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
#include <rock_hero/common/core/shared/displayed_strings.h>
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

// The shared resource table this renderer is built against. Pulled in unqualified because the draw
// and creation code names a program or texture asset by enumerator many times over.
using common::core::g_highway_shader_programs;
using common::core::g_highway_textures;
using common::core::highwayShaderProgramName;
using common::core::indexOf;
using Program = common::core::HighwayShaderProgram;
using TextureAsset = common::core::HighwayTexture;

// The three fixed render views, executed in id order (plan 25 Phase 3 checkpoint). View 0 is
// shared with RenderDevice's backstop (its g_default_view): the device touches it so a frame
// with no scene still clears and presents, and this renderer's per-frame setViewClear wins
// whenever a scene draws — nothing else may reconfigure view 0.
constexpr bgfx::ViewId g_background_view = 0;
constexpr bgfx::ViewId g_board_view = 1;
constexpr bgfx::ViewId g_overlay_view = 2;

// Backdrop clear color behind the whole scene (0xRRGGBBAA); cleared to black.
constexpr std::uint32_t g_backdrop_color = 0x000000ff;

// Board-furniture colors (0xAARRGGBB).
constexpr ArgbColor g_beat_bar_color = 0xFF0F3B5E; // beat and measure bars alike

// Measure downbeats (and the per-note fret-span lines, which reuse the shape) draw a sharp
// chord-box-teal attack line exactly where they occur, then a brief beat-blue fade trailing
// away down the measure — half the old symmetric-wings footprint.
constexpr double g_attack_line_half_length = 0.025;
constexpr double g_attack_fade_length = 0.2;
constexpr double g_attack_line_alpha = 0.85; // full teal read slightly too bright
// The lighting plane every floor light shares: the fretting hand's window and the tapping hand's.
// Between the lane ribbons (0.004) and the beat bars (0.015) — the floor itself stays at y = 0 and
// content is raised off it (the floor law), so a new floor mark takes a height in that stack rather
// than moving the plane. Named once so a third light cannot land on a third literal.
constexpr double g_floor_light_y = 0.008;

// The board's lit lane appearance: what each lane's surface looks like where the hand-window
// light reveals it (plain lanes the runway blue, inlay-dotted lanes distinctly darker — the
// intrinsic board pattern). The light carries these as vertex color and the per-fragment mask
// reveals them, the physically honest reading: nothing is painted over or under the light, and
// away from it the board draws nothing and stays dark.
constexpr ArgbColor g_lit_lane_color = 0x402590E8;
constexpr ArgbColor g_lit_lane_dotted_color = 0x40185C94;
// The falloff band the light dissolves across, centered on each eased edge (half strength at
// the edge, gone half a band outside, full half a band inside), in world units. The centered
// placement carries the lit region's width; this constant alone sets settled-edge sharpness.
constexpr double g_window_light_falloff = 0.55;
// How strongly the light dims while it sweeps through a transition, at full steepness. The
// motion-dim model (replacing two edge-widening attempts that either bulged the silhouette or
// collapsed the lit core): the exterior shape keeps the settled fade cross-section along the
// entire eased contour, and the transition's fading lives in overall brightness instead — a
// light rushing across lanes cannot fully illuminate them, so the lit strip dips toward this
// fraction darker at peak sweep speed and recovers by arrival (the sin-squared bell keeps the
// overall feel gentler than the old full-length plateau even at this depth).
constexpr double g_window_morph_dim = 0.95;
// Tapping-hand light envelope (right-hand-tap-lighting plan): each tap onset lights its own
// tapped fret lanes along the timeline, rising over the approach side of the tap, holding
// through sustained contact (morphing with pitched glides), and decaying after the fingers
// release, so the light dips between consecutive taps exactly as the finger lifts
// (deliberately per-onset, never merged into runs). The rise duration is each onset's
// projection-derived ramp_seconds — the fret-hand placements' own margin-based arrival rule
// (replacing a fixed wall-clock rise that read inconsistently) — while the release below stays a
// short visual constant: a release is a gesture, not an arrival.
//
// Named for the FLOOR PLANE rather than for the tapping hand: it is the release every light on
// that plane fades over, and the lane-border ribbons' own constant below is stated as a contrast
// against it.
constexpr double g_floor_light_release_seconds = 0.1;
// The lane-border ribbons release much more slowly than the floor light (per-tap ribbon
// flashing read as jarring in tap sections): the light pulses with each strike while the
// brightened edges bridge the gaps of a dense run, fading only once the run ends.
constexpr double g_tap_ribbon_decay_seconds = 0.45;
// Sustain slope shading: the modulated tail's centerline slope modulates its brightness like a
// surface tilting under a fixed light, so a bend's climb, hold, and release — and a vibrato's
// wobble — read from shading alone even where screen-space lift is foreshortened at center
// screen. Slope is normalized by the lane's bend-lift direction so a climbing PITCH always
// brightens regardless of which way the lane draws it. Gain scales world dy/dz into [-1, 1];
// depth is the full brighten/darken mix at saturation.
constexpr double g_tail_slope_shade_gain = 4.8;
constexpr double g_tail_slope_shade_depth = 0.5;
// Tent-smoothing half-window for the shade, in seconds of tail time. The raw per-sample shade
// tracks the instantaneous derivative, which crosses tanh's linear region within a sample or
// two on a real bend — the shade snapped between base and saturated over a couple of segments,
// and foreshortening at screen center compressed that snap into a hard band that read as a
// sharp point on a smooth curve. Smoothing over a fixed TIME window guarantees the fade-in/out
// spans the same stretch of tail whatever the sample density or viewing angle, and at 0.05 s it
// stays well under half the fixed vibrato period (one sixth of a second for every song), so
// the wobble's shimmer is never dulled toward its average.
constexpr double g_tail_slope_shade_smooth_seconds = 0.05;

// Bend chevron clearance past the head ART's top edge, in head half-heights along the drawn
// bend-lift direction. The chevron's authored relationship is to the note's VISIBLE top edge —
// legs anchoring on it with the apex rising clear, the bend cue's overlap — so the station is
// derived per draw from the load-measured silhouette (edge = center + half height) plus this
// clearance, and can never drift when the art is rebaked. The value is the remainder of the
// previously hand-kept 0.38 station after subtracting the art's top edge as it measured when
// that station was authored ((10.822 - 0.526) / 31.5), preserving the authored look exactly.
constexpr double g_bend_marker_edge_clearance_heads = 0.0532;

// Pre-bend target outline alpha: the hollow head silhouette parked at a pre-bent note's
// chart-truth height is an annotation, dimmed so the rising head stays the subject.
constexpr double g_prebend_outline_alpha = 0.5;

/*
The head ART's own silhouette is MEASURED FROM THE PNG AT LOAD — head_art_profile.h, the
box_mute_profile pattern — and lives in Impl::head_art, in atlas texels of the drawn quad's
index space. The head cell fills only the middle of its quad, so a light sized against the QUAD
starts far outside the note; the accent glow's distance field is sized against the measured
silhouette instead. Measuring at load is what makes a rebaked atlas unable to leave the light
tracing a shape the art no longer has — exactly how the hand-kept constants this replaced would
have failed, silently, on the next rebake that forgot to refit them.

TEXELS, not world units, converted through headArtTexelWidth()/headArtTexelHeight() below —
because the world size of a drawn texel is NOT the cell size, and stating silhouettes in world
once meant carrying that error into every consumer.
*/

/*
World size of one drawn texel of a head cell, per axis.

Divided by cell_size - 1, not cell_size, and that is the whole point of routing this through a
function. The head quad spans `2 * note_half_width` of world, but HighwayAtlasLayout::cellRect
insets each cell's UV rect by half a texel on every side (to stop neighbouring cells bleeding
under minification), so the quad's corners sample texel CENTRES 0.5 and cell_size - 0.5 — one
texel less than the cell's own span. The divisor reads the LOADED layout rather than a 64
literal, so a rebake at a different cell size rescales the light with the art instead of
silently mis-sizing every glow, and the head's world size still lives in exactly one place.

Two functions because the axes can differ: the width metric is its own quantity — the reference's
head is narrower for its fret slot than ours while matching our height (highway_metrics.h) — so
a future width change squashes the rectangle head's drawn texels horizontally while their height
holds. Every x-extent converts through the width texel and every y-extent through the height
texel, so the accent light keeps tracing the art the quad actually draws.
*/
[[nodiscard]] double headArtTexelWidth(
    const common::core::HighwayMetrics& metrics, const HighwayAtlasLayout& layout)
{
    return (2.0 * metrics.note_half_width) / (static_cast<double>(layout.cell_size) - 1.0);
}

[[nodiscard]] double headArtTexelHeight(
    const common::core::HighwayMetrics& metrics, const HighwayAtlasLayout& layout)
{
    return (2.0 * metrics.note_half_height) / (static_cast<double>(layout.cell_size) - 1.0);
}

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

// Capo display (crude first treatment, roadmap 25-Q6): the dead zone the capo silences between
// the nut and its fret line, dimmed over the face; and the clamp itself, a steel bar with a
// dark rim laid across the strings just behind its fret, overhanging the string grid so it
// reads as hardware rather than another fret line.
constexpr ArgbColor g_capo_dead_zone_color = 0xB4000000;
constexpr ArgbColor g_capo_bar_color = 0xFFB9BEC6;
constexpr ArgbColor g_capo_bar_rim_color = 0xFF14161A;
// The clamp's span in fret-line coordinates behind the capo's line, and its overhang past the
// string grid in string distances.
constexpr double g_capo_bar_near_fraction = 0.08;
constexpr double g_capo_bar_far_fraction = 0.45;
constexpr double g_capo_bar_overhang_strings = 0.3;
// The rim's outset around the steel body, in fret widths.
constexpr double g_capo_bar_rim_fraction = 0.05;

// Strike glow (the additive hit light; fret-hit-light-effect plan): the nominal release and
// dark-trough guard feeding highwayHitGlowRelease, the light's colour (a hot orange whose
// blue-channel lift lets a peak white out over already-lit content), the soft-edge falloff, the
// hot-core half-width of a fret-line strip, and the fade toward the face top that grounds the
// light at the strings' crossing. The window-light mask reaches 1.0 only falloff / 2 inside an
// edge, so a strip needs core_half >= falloff / 2 to actually peak at full intensity. The glow
// pass walks its own onset window over state.chart.notes, so the release tunes freely — it is not
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

// The accent glow's blend, SIGNED additive 2026-08-18 from the sighted operator ladder (screen
// and lighten measured structurally identical over this near-black board and were deleted with
// the sampler). It consumes PREMULTIPLIED source, which is why it is ONE -> ONE rather than the
// SRC_ALPHA -> ONE above: the glow shader has already scaled its colour by its own alpha, and
// letting the blender scale it again would apply the falloff twice and square the light.
constexpr std::uint64_t g_glow_add_state =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE);

// Emitter depth for a SOLID subject — a note head, an open string's bar — as opposed to a frame.
// Any value past the shape's own inradius makes the WHOLE interior emit, which is exactly what
// turns the field from a rim into a back light. One world unit is several times the deepest note
// silhouette on the board (a node head's is 0.17), so nothing in the note batch approaches it.
constexpr double g_glow_solid_emitter_depth = 1.0;

// The accent light, SIGNED 2026-08-18 as the sighted "medium flat" candidate — reach 0.12 world
// (about eight texels), falloff exponent 2.0 — and its radiance gain raised to 1.5 on 2026-08-20
// after the user found the neutral gain too subtle in play. The gain lever is now SPENT
// (docs/tracking/watch-items.md): past roughly 2.0 the extra radiance mostly grows the white-hot
// core rather than adding width, so if accents still fail to read the knob left is REACH. The
// tried alternatives are recorded in docs/plans/in-progress/highway-note-art-state.md. Above a
// gain of about 1.08 (255/237, the palette's brightest channel) the glow shader's per-channel
// clip is live, which is what shapes the white-hot core the shader describes.
constexpr double g_accent_reach = 0.12;
constexpr double g_accent_exponent = 2.0;

// A chord box draws its accent at NEUTRAL radiance, where a note takes the axis's
// step up (g_accent_gain, highway_emphasis_styles.h). Sighted 2026-08-20: "chord
// boxes are already plenty accented as shipped".
//
// This is a size compensation, NOT a second opinion about the light. Outside a
// silhouette the field is provably identical for both subjects: the shader's
// inward term is max(-d - depth, 0), which is zero wherever d > 0, so the
// emitter depth that separates a solid from a frame never touches the outward
// falloff. A note's halo and a box's halo are the same brightness at the same
// distance from their edge. What differs is how MUCH of it there is - a box
// frame is a long band where a note's halo is a ring a couple of pixels wide on
// approach - so equal radiance does not read as equal accent. Holding the read
// equal is what the player actually sees, and it costs one number.
//
// If an accented chord ever shows its box and its heads disagreeing when read in
// one glance, that is the signal this trade was the wrong way round.
constexpr double g_accent_gain_boxes = 1.0;

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

// How many fret slots the board face draws — the chart's own fret cap, derived rather than
// restated, so the drawn board, the model's highest fret, and the camera's whole-neck focus
// reference (highwayFocusWholeNeckX) can never disagree about how long the neck is.
constexpr int g_face_fret_count = common::core::g_highway_fret_count;

// Seconds a passed note takes to fade out after crossing the hit line.
constexpr double g_passed_fade_seconds = 0.15;

// Rolling-flip flat lead: single-note heads land flat this many seconds before the hit line.
// The reference flip is fast and late (a 500 ms roll landing flat 100 ms out, the only timing
// verifiable from reference material — the flip has no documented tie to any internal
// constants); our flip instead spans the whole approach, and the slower final degrees need a
// longer flat stretch to read as finished before the board face.
constexpr double g_flip_flat_lead_seconds = 0.25;

// Tolerance for matching an onset to a shape-span boundary (or grouping simultaneous onsets).
// The core constant (see its rationale there) is shared so this file's chord grouping agrees
// with HighwayViewState::display_hold_ends, whose span-held notes drive the visible range.
constexpr double g_onset_match_epsilon = common::core::g_onset_match_epsilon;

// Open-note bar cross-section (Charter's OpenNoteModel): a thin hexagonal prism spanning the
// hand window, half-thickness 0.04 at the ends bulging to 0.05 at the center station, squashed
// to a tenth of that in Z. An earlier flat slab at tail width read over 3x too tall.
constexpr double g_open_note_end_half_thickness = 0.04;
constexpr double g_open_note_middle_half_thickness = 0.05;
constexpr double g_open_note_z_squash = 0.1;
constexpr int g_open_note_segments = 6;

// The bar fades to transparent over this run at each end, so it reads as tapering almost to a
// point at the hand-window rails instead of stopping flat.
constexpr double g_open_note_end_fade_length = 0.5;

// Sustain tails are three-band ribbons in Charter: solid edge strips around an inner band
// Charter draws at 192/255 alpha. Ours is deliberately more translucent so notes stay
// readable through a tail's core. Fretted tails split the tail width quarter/half/quarter;
// open tails span the hand window inset by g_open_tail_margin (highway_floor_geometry.h, where
// every floor mark under an open note reads it), with edge bands of the same width.
constexpr double g_tail_inner_alpha = 96.0 / 255.0;

// Sustain tails dissolve over this last fraction of the note duration (the glow posts' fade
// toward the note, mirrored at the tip), so a sustain ends softly instead of stopping dead.
constexpr double g_tail_tip_fade_fraction = 0.35;

// Seconds of tail over which a sustain rises from nothing at its onset. A FIXED span rather
// than a fraction, for the reason the tremolo ramp is counted in teeth: the rise then occupies
// the same stretch of board on every note instead of a dozen frames on a long sustain and none
// on a short one. Two beats of a ribbon at 120 BPM is a second, so this is a few percent of a
// typical tail — long enough to read as emerging, short enough that no sustain looks late.
constexpr double g_tail_onset_fade_seconds = 0.05;

// Glow posts under single notes stand the tail ribbon cross-section upright at a fraction of
// the tail width; this is the edge alpha where the post meets the floor, kept low enough that
// the post stays subtle. A post rises from the floor toward its note's lane center,
// dissolving to nothing partway up — the note art simply overlays the post's top (the old
// fade-exactly-at-the-head-quad-bottom invariant is removed) — so every lane carries a post
// whose height scales naturally with the lane's height above the floor.
constexpr double g_shadow_post_floor_alpha = 0.5;

// Fraction of the lane height where the post's dissolve completes: alpha reaches zero at this
// point of the rise rather than at the lane center itself, a slightly more aggressive fade.
constexpr double g_shadow_post_fade_end_fraction = 0.75;

// Open-note L posts: how far the floor foot reaches inward, measured from the bar end (the
// chord box's bottom corner holders, freestanding), fading to nothing at its tip.
constexpr double g_open_post_foot_length = 0.5;

// Adaptive tail sampling for technique-modulated rails: one centerline sample per this many
// projected screen pixels, hard-capped (Charter's per-millisecond-tessellation fix).
constexpr double g_tail_pixels_per_sample = 4.0;
constexpr std::size_t g_tail_sample_cap = 256;

// Chord-box palette and geometry (Charter's values): a translucent teal panel per strummed
// chord, with corner holders, gradient frame bars, and mute-cross variants.
constexpr ArgbColor g_chord_box_color = 0xFF00D2D5;
constexpr ArgbColor g_chord_box_dark_color = 0xFF003C3D;

// The frame's own opacity, shared by the gradient frame bars and by the mute marks drawn against
// them. The palm mark's rim has to composite identically to the border it stops against, so the
// value gets one home rather than two places that agree by coincidence.
constexpr double g_chord_box_frame_alpha = 128.0 / 255.0;

// Repeat-box mute marks render through the SDF shader (fs_box_mute). chords.png carries their
// STRUCTURE only — tint weighting, rim/core structure, and coverage, in the same channel scheme
// the note atlas uses — which the renderer measures at creation into a two-row ramp the shader
// samples by exact distance from the arm centerlines (see box_mute_profile.h for the authoring
// contract). Hue and opacity both arrive as vertex color from the constants here, never from the
// art: the palm mark wears the frame's own color and alpha so the two stay equal wherever they
// meet, and a hue baked into the pixels could not follow a retune of it. The distance field is
// evaluated per fragment because the X's arm angle changes with every box aspect — a shape no
// fixed bitmap contains — so bitmap stretching distorted line weight with the box while this
// holds the measured weights everywhere.
constexpr ArgbColor g_full_mute_mark_color = 0xFF52798A;

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

// Vertex for the accent glow: the same position and color, plus the fragment's offset from its
// subject's center and the subject's own shape. The SHAPE rides the vertex rather than a uniform
// so every accented head, open bar and box still batches into one draw despite each having its
// own extents — which is what makes a per-fragment field cost no more draw calls than the stacked
// quads it replaces.
struct PosColorGlowVertex
{
    float x;
    float y;
    float z;
    std::uint32_t abgr;
    // Offset from the subject's center, in world units, pre-rotation for a head that flips.
    float local_x;
    float local_y;
    // Half extents of the silhouette this light surrounds.
    float half_w;
    float half_h;
    // Corner radius, and which distance field to evaluate (0 = rounded box, 1 = rhombus).
    float corner;
    float shape;
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

[[nodiscard]] const bgfx::VertexLayout& posColorGlowLayout()
{
    static const bgfx::VertexLayout g_layout = [] {
        bgfx::VertexLayout layout;
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
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

/*
The silhouette an accent glow surrounds, in the subject's own local frame.

The ONE description the glow shader understands, so a fretted head, an open string's bar and a
chord box frame all reach the same program with the same three numbers rather than each carrying
its own light. Extents are half sizes in world units from the subject's centre.
*/
struct GlowShape
{
    double half_w{};
    double half_h{};

    // Corner radius; equal to the smaller half extent makes a capsule.
    double corner{};

    // True selects the rhombus distance field (node heads) over the rounded box.
    bool rhombus{};
};

[[nodiscard]] PosColorGlowVertex makeGlowVertex(
    const double x, const double y, const double z, const std::uint32_t abgr, const double local_x,
    const double local_y, const GlowShape& shape)
{
    return PosColorGlowVertex{
        .x = static_cast<float>(x),
        .y = static_cast<float>(y),
        .z = static_cast<float>(z),
        .abgr = abgr,
        .local_x = static_cast<float>(local_x),
        .local_y = static_cast<float>(local_y),
        .half_w = static_cast<float>(shape.half_w),
        .half_h = static_cast<float>(shape.half_h),
        .corner = static_cast<float>(shape.corner),
        .shape = shape.rhombus ? 1.0F : 0.0F,
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

/*
Gives an accent light's colour the broadband pedestal every real emitter has.

Mixes a string colour toward the achromatic grey AT ITS OWN PEAK, so the hue and the brightest
channel are untouched and only the darker channels lift. Without it a pure hue can never whiten:
the palette's red is literally `(237, 0, 0)` and its teal `(0, 181, 160)`, so multiplying by a gain
clips the one live channel and simply stops — brighter is impossible and desaturation never
happens, which is the "it is the string's colour but it does not read as light" complaint exactly.

The physical warrant is not stylistic. No emitter is spectrally pure, and more to the point the
scatter that produces a glow AT ALL — in a lens, in the atmosphere, in the eye's own optics — is
broadband. That is why every photograph of a red taillight or a neon sign has a white centre inside
a coloured halo. This constant is how wide that pedestal is; the gain then decides how far up it
the core climbs, and the falloff decides where each fragment sits along the ramp.
*/
constexpr double g_glow_spectral_floor = 0.18;

[[nodiscard]] ArgbColor emitterSpectrum(const ArgbColor argb)
{
    const ArgbColor peak = std::max({(argb >> 16U) & 0xFFU, (argb >> 8U) & 0xFFU, argb & 0xFFU});
    const ArgbColor achromatic = (argb & 0xFF000000U) | (peak << 16U) | (peak << 8U) | peak;
    return mixArgb(argb, achromatic, g_glow_spectral_floor);
}

// Inlay-dot pattern: fret % 12 in {0, 3, 5, 7, 9} carries a marker.
[[nodiscard]] bool isDottedFret(const int fret)
{
    const int cycle = ((fret % 12) + 12) % 12;
    return cycle == 0 || cycle == 3 || cycle == 5 || cycle == 7 || cycle == 9;
}

// The double marker's (frets 12, 24) dot separation as a fraction of the string grid's height,
// measured from the shipped sheet's 12th-fret pair: 341 of 512 cell texels, exactly symmetric
// about the grid's vertical middle.
constexpr double g_inlay_double_separation_fraction = 341.0 / 512.0;

// Continuous hand-window extent at a time, as sorted world-X edges: the core query's eased
// fractional lines mapped through the fractional fret-line overload. Fret lines stay fixed —
// these are the sliding window border's positions.
[[nodiscard]] std::pair<double, double> handWindowXAt(
    const common::core::HighwayViewState& state, const double seconds,
    const common::core::HighwayMetrics& metrics, const bool mirrored)
{
    const common::core::HighwayHandWindow window =
        common::core::highwayHandWindowAt(state.chart.fret_hand_positions, seconds);
    const double low_x = common::core::highwayFretLineX(window.low_line, metrics, mirrored);
    const double high_x = common::core::highwayFretLineX(window.high_line, metrics, mirrored);
    return {std::min(low_x, high_x), std::max(low_x, high_x)};
}

// One note's floor footprint (highwayFloorFootprint) at a time, for geometry that follows the
// hand: the window is read at that instant, and where it has narrowed past an open string's
// insets the footprint collapses onto the window's centre at zero width rather than inverting the
// mark. The open tail's band stations walk this per sample, which is why the collapse is stated
// here once instead of at each walk. Only an open string's result varies with time — a fretted
// note's footprint never reads the window, so the binary search behind it is spent for nothing
// there, which is cheaper than restating the open/fretted split at every caller.
[[nodiscard]] HighwayFloorFootprint floorFootprintAt(
    const common::core::HighwayViewState& state, const common::core::NoteViewState& note,
    const double fretted_half_width, const double seconds,
    const common::core::HighwayMetrics& metrics, const bool mirrored)
{
    const std::pair<double, double> window_x = handWindowXAt(state, seconds, metrics, mirrored);
    return highwayFloorFootprint(note, fretted_half_width, window_x, metrics, mirrored)
        .value_or(
            HighwayFloorFootprint{
                .center_x = (window_x.first + window_x.second) / 2.0,
                .half_width = 0.0,
            });
}

// The footprint a HARMONIC's floor marks occupy: centred on the drawn node — the note's own
// fretboard anchor, which for a harmonic IS the node (highwayNoteFretboardX) — and slightly wider
// than the head above it, by half the floor lights' edge band on each side.
//
// The CENTRE is the decided part (user sighting 2026-08-30): the fret-span line sat wire-to-wire
// in a fret slot while the touch that makes the figure a harmonic is at the node, so the mark
// pointed a wire away from the hand. The WIDTH is the value that sighting was taken at — it was
// derived from the harmonic node light's lit core, spill included, and that light has since been
// tabled; the number stays because it is the one that was looked at, not because a light still
// needs matching. A node-centred line one fret slot wide is the obvious alternative if this is
// ever re-sighted. Stated once here so the mark has one authority rather than a width per drawer.
//
// This is highwayFloorFootprint's FRETTED answer spelled here rather than routed through it,
// because a harmonic never takes the open-string branch — openString is false wherever a node is
// carried — so the shared entry would contribute a dead branch and an optional that cannot be
// empty. The part that must not be restated, the position, is the same authority either way.
[[nodiscard]] HighwayFloorFootprint harmonicMarkFootprint(
    const common::core::NoteViewState& note, const common::core::HighwayMetrics& metrics,
    const bool mirrored)
{
    return HighwayFloorFootprint{
        .center_x = highwayNoteFretboardX(note, note.fret, metrics, mirrored),
        .half_width = metrics.note_half_width + (g_window_light_falloff / 2.0),
    };
}

// True when the hand window moves anywhere inside a time span (some placement's ramp overlaps
// it): geometry spanning the range must then sample the window instead of holding one extent.
// Visits only the placements that can overlap the span, on the bounds windowSampleTimes states
// below: arrivals ascend, so the walk starts past `from_seconds`, and once an arrival sits
// `max_ramp_seconds` past `to_seconds` neither its own ramp nor any later one reaches back in.
[[nodiscard]] bool handWindowMovesWithin(
    const common::core::HighwayViewState& state, const double from_seconds, const double to_seconds,
    const double max_ramp_seconds)
{
    const std::vector<common::core::FhpViewState>& fhps = state.chart.fret_hand_positions;
    for (const common::core::FhpViewState& fhp : std::ranges::subrange(
             std::ranges::upper_bound(
                 fhps, from_seconds, std::ranges::less{}, &common::core::FhpViewState::seconds),
             fhps.end()))
    {
        if (fhp.seconds - max_ramp_seconds >= to_seconds)
        {
            break;
        }
        if (fhp.ramp_seconds > 0.0 && fhp.seconds - fhp.ramp_seconds < to_seconds)
        {
            return true;
        }
    }
    return false;
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
// Fills the caller's buffer rather than allocating (draw() calls this per shape rail and per
// moving open tail), and visits only the placements that can overlap the span: arrivals ascend,
// so the walk starts past `from`, and once an arrival sits `max_ramp_seconds` past `to` neither
// it nor anything later can reach back into the window.
void windowSampleTimes(
    const common::core::HighwayViewState& state, const double from_seconds, const double to_seconds,
    const double max_ramp_seconds, std::vector<double>& times)
{
    times.clear();
    times.push_back(from_seconds);
    const std::vector<common::core::FhpViewState>& fhps = state.chart.fret_hand_positions;
    const auto start = static_cast<std::size_t>(
        std::ranges::upper_bound(
            fhps, from_seconds, std::ranges::less{}, &common::core::FhpViewState::seconds) -
        fhps.begin());
    for (std::size_t index = start; index < fhps.size(); ++index)
    {
        const common::core::FhpViewState& fhp = fhps[index];
        if (fhp.seconds - max_ramp_seconds >= to_seconds)
        {
            break;
        }
        if (fhp.ramp_seconds <= 0.0 || fhp.seconds - fhp.ramp_seconds >= to_seconds)
        {
            continue;
        }
        const double t0 = std::max(fhp.seconds - fhp.ramp_seconds, from_seconds);
        const double t1 = std::min(fhp.seconds, to_seconds);
        // The wider-moving edge's travel in fret-line units, measured from the previous settled
        // window (the nut window before the first placement).
        const double previous_low =
            index > 0 ? static_cast<double>(state.chart.fret_hand_positions[index - 1].fret - 1)
                      : 0.0;
        const double previous_high = index > 0
                                         ? static_cast<double>(
                                               state.chart.fret_hand_positions[index - 1].fret +
                                               state.chart.fret_hand_positions[index - 1].width - 1)
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
}

// Appends one quad (two triangles) to a CPU-side batch, choosing the split diagonal.
template <typename Vertex>
void pushQuadWithSplit(
    std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, const Vertex& v0,
    const Vertex& v1, const Vertex& v2, const Vertex& v3, const bool split_from_v0_to_v2)
{
    const auto base = static_cast<std::uint16_t>(vertices.size());
    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);
    const std::array<std::uint16_t, 6> offsets =
        split_from_v0_to_v2
            ? std::array<std::uint16_t, 6>{
                  std::uint16_t{0},
                  std::uint16_t{1},
                  std::uint16_t{2},
                  std::uint16_t{0},
                  std::uint16_t{2},
                  std::uint16_t{3},
              }
            : std::array<std::uint16_t, 6>{
                  std::uint16_t{0},
                  std::uint16_t{1},
                  std::uint16_t{3},
                  std::uint16_t{1},
                  std::uint16_t{2},
                  std::uint16_t{3},
              };
    for (const std::uint16_t offset : offsets)
    {
        indices.push_back(static_cast<std::uint16_t>(base + offset));
    }
}

// Appends one quad through the default split used by ordinary axis-aligned batches.
template <typename Vertex>
void pushQuad(
    std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, const Vertex& v0,
    const Vertex& v1, const Vertex& v2, const Vertex& v3)
{
    pushQuadWithSplit(vertices, indices, v0, v1, v2, v3, true);
}

/*
Lays the one quad an accent glow needs around a silhouette.

The single authority for accent-glow geometry: the quad is the silhouette grown by the reach on
every side, and each corner carries its own pre-rotation offset from the centre so the fragment
stage can evaluate an exact distance. Callers differ only in the shape they hand in and in where
they place the result, which is what keeps one light across three unrelated subjects from being
three lights that would have to be kept in agreement by hand.

cos_r,sin_r: Rotation of the subject in the board plane (a head's rolling flip). The offsets
       handed to the shader stay UNROTATED, so the field turns with the subject rather than the
       subject sliding through a fixed field.
low_y: Floor for the quad's lower edge in world Y, in the subject's local frame. A chord box
       stands ON the board, so growing its glow downward would tuck light under the floor; every
       other subject passes the default and gets a symmetric quad.
*/
void pushAccentGlow(
    std::vector<PosColorGlowVertex>& vertices, std::vector<std::uint16_t>& indices,
    const double center_x, const double center_y, const double z, const GlowShape& shape,
    const double reach, const std::uint32_t abgr, const double cos_r = 1.0,
    const double sin_r = 0.0, const double low_y = -std::numeric_limits<double>::infinity())
{
    const double out_w = shape.half_w + reach;
    const double out_h = shape.half_h + reach;
    const double low = std::max(-out_h, low_y);
    if (!(out_w > 0.0) || !(out_h > low))
    {
        return;
    }
    const auto corner = [&](const double dx, const double dy) {
        return makeGlowVertex(
            center_x + (dx * cos_r) - (dy * sin_r),
            center_y + (dx * sin_r) + (dy * cos_r),
            z,
            abgr,
            dx,
            dy,
            shape);
    };
    pushQuad(
        vertices,
        indices,
        corner(-out_w, low),
        corner(out_w, low),
        corner(out_w, out_h),
        corner(-out_w, out_h));
}

/*
The unit ramp `max(0, s)` convolved with the signed light's quadratic falloff kernel (1-|s|)^2,
in kernel radii — the closed form of "spread a straight ramp by the light". The profile below is
built from differences of it, which is what turns the bar's hard-cornered alpha ramp into the
rounded one a light actually casts.
*/
[[nodiscard]] double glowRampIntegral(const double sigma)
{
    if (sigma <= -1.0)
    {
        return 0.0;
    }
    if (sigma >= 1.0)
    {
        return sigma;
    }
    // The kernel is (1-|s|)^2, so its ramp integral is quartic.
    if (sigma <= 0.0)
    {
        const double t = 1.0 + sigma;
        return (t * t * t * t) / 8.0;
    }
    return 0.125 + (sigma / 2.0) + (0.75 * sigma * sigma) - (0.5 * sigma * sigma * sigma) +
           ((sigma * sigma * sigma * sigma) / 8.0);
}

/*
How strongly an open string's bar EMITS at one point along its length, from 0 to 1.

The bar is not a uniform emitter: its own alpha ramps from nothing to full over
g_open_note_end_fade_length at each end, so it tapers to a point rather than stopping flat.
A light behind it has to follow that or it lights the two stretches where the bar is not there —
which is precisely what the user saw ("adding the light seems to undo the effect of the faded
edges").

But it must not merely COPY the ramp either. A back light's brightness at a point is the
contribution of the emitter NEAR that point, not only the emitter directly behind it, so the light
carries a little past where the bar itself has vanished. Copying the bar would kill it exactly at
the tip, and the round before this one already showed what falling short looks like.

So the profile is the bar's alpha ramp CONVOLVED with the light's own falloff kernel — derived
rather than fitted. Measured against the alternatives (2026-08-16), it is the only candidate that
scores near ideal on both failure axes at once: the light's visible edge lands 0.98 px past the
bar's own at the near end where a flat profile overshot by 3.17 px, and the mark's brightness at
the dead tip drops from 1.40x the plateau (a flat glow's brightest point is the tip, which for the
blue string went nearly white) to 1.00x.

Note what this is NOT: shortening the silhouette. Pulling the capsule in by a quarter of the fade
lands the overshoot at zero too, but still measures 0.21x on taper, because a uniform-alpha field
ends in a hard cap wherever you put it — the cap only moves, and it reads as a bulb on a stick.

s_from_end: distance from the nearer end of the bar; negative outside it.
fade: length of the bar's own alpha ramp.
Returns the emission weight in [0, 1]; exactly 1 across the bar's whole middle.
*/
[[nodiscard]] double openBarEmission(const double s_from_end, const double fade)
{
    if (!(fade > 0.0))
    {
        return 1.0;
    }
    const double spread =
        (g_accent_reach / fade) * (glowRampIntegral(s_from_end / g_accent_reach) -
                                   glowRampIntegral((s_from_end - fade) / g_accent_reach));
    return std::clamp(spread, 0.0, 1.0);
}

// One authority for the open bar's end-fade length: the bar's own alpha ramp and the light's
// axial profile both read it, and they must agree exactly or the light lands on stretches
// where the bar is not.
[[nodiscard]] double openBarFadeLength(const double x0, const double x1)
{
    return std::min(g_open_note_end_fade_length, (x1 - x0) / 4.0);
}

// The chord-box frame's signature horizontal fade, stated once: a quad pair split at the
// horizontal middle, colored `end_abgr` at the outer ends and `middle_abgr` at the split.
// The frame bars draw it directly and the palm mute mark rides it as a vertex modulation,
// so the two can never drift apart. `make_vertex` builds each corner as (x, y, abgr).
template <typename Vertex, typename MakeVertex>
void pushMiddleFadedQuads(
    std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y0, const double y1, const std::uint32_t end_abgr,
    const std::uint32_t middle_abgr, const MakeVertex& make_vertex)
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

/*
A chord box frame's vertical shape, derived once from the three flags that decide it.

The frame is not one rectangle: a repeat box is half height, and a box with fewer than three notes
has NO top bar — its side columns simply fade out from the midpoint instead. Both the panel and the
accent light around it need those facts, and they must agree exactly or the light draws a top edge
where the shape has none. Deriving them in one place is what makes that agreement structural
rather than a rule restated in two files' worth of arithmetic.
*/
struct ChordBoxFrame
{
    // Top of the side columns (and of the interior fill).
    double side_y1{};

    // Outermost lit Y: above the top bar when there is one, else the columns' own top.
    double outer_top{};

    /*
    Where the columns begin fading out.

    Equal to outer_top when a top bar closes the shape (nothing fades), else the columns'
    midpoint.
    */
    double fade_start_y{};

    // Whether a top bar closes the frame. False means the light must draw no top edge.
    bool closed_top{};
};

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

    /*
    True when the outer stations dissolve to transparent instead of ending on the edge
           color — an open tail's band tapering into the hand-window rails, against a fretted
           tail's hard edge.

    A SHAPE fact, and it is stored rather than derived because the alternative was derived from
    the wrong thing. This used to be an `outer_abgr` color that callers built as "the edge color,
    or the same color at zero alpha", and the accent glow recovered the shape by asking whether
    the two colors differed. The tail's own envelope destroys that: at the onset and at the end of
    the tip fade every alpha reaches zero, so the transparent outer and the faded edge pack to the
    SAME value and an open tail read as hard-edged exactly where it dissolves — the glow's inboard
    clip collapsing onto the silhouette across the whole tip fade.

    Stating the shape once also deletes the field it replaces: a transparent outer is exactly the
    edge color with its alpha cleared, so the color was never independent information.
    */
    bool outer_transparent;
};

// The outer stations' color, derived from the shape rather than stored beside it: dissolving
// means the edge color at zero alpha, so a band fades out without shifting hue.
[[nodiscard]] std::uint32_t ribbonOuterAbgr(const RibbonEnd& end)
{
    return end.outer_transparent ? (end.edge_abgr & 0x00FFFFFFU) : end.edge_abgr;
}

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
    const std::uint32_t outer_a = ribbonOuterAbgr(a);
    const std::uint32_t outer_b = ribbonOuterAbgr(b);
    push_band(0, 1, outer_a, a.edge_abgr, outer_b, b.edge_abgr);
    push_band(1, 2, a.inner_abgr, a.inner_abgr, b.inner_abgr, b.inner_abgr);
    push_band(2, 3, a.edge_abgr, outer_a, b.edge_abgr, outer_b);
}

/*
Alpha error budget for one product-faded quad, in the units alpha is authored in.

A tapered mark whose span envelope also fades has alpha that is a PRODUCT: the envelope along the
span times the taper across it. A quad drawn as two triangles cannot carry that product. Each
triangle interpolates linearly, so the one on the far side of the split diagonal holds the span's
STARTING envelope across its whole share of the taper, and the deviation lands unevenly on the two
sides.

Measured on an open tail before this bound existed: the two edges drew with taper widths
differing by more than four times (0.031 against 0.130 world at one instant, against an authored
0.100), the narrow side stopped up to 0.11 world inside its authored width, and which side was
which SWAPPED between the onset ramp and the tip fade, because the envelope's slope flips sign.
The art authors a symmetric taper; the renderer was drawing an asymmetric one.

The peak deviation is a quarter of the envelope's change WITHIN ONE QUAD, so bounding that change
bounds the error — four counts of 255 here, which is below the step the 8-bit vertex colors can
represent anyway once it is spread across a strip's width.
*/
constexpr double g_product_alpha_span_tolerance = 4.0 / 255.0;

/*
Sub-segments a product-faded span needs to hold its interpolation error inside the budget above.

alpha_from: Envelope at the span's near end.
alpha_to: Envelope at the span's far end.
Returns at least one; more only where the envelope actually moves.

A span whose envelope holds still carries no product at all — the taper is then the same at both
ends and two triangles reproduce it exactly — so the whole plateau of every sustain still draws
as a single quad and pays nothing.
*/
[[nodiscard]] int productAlphaSpanSteps(const double alpha_from, const double alpha_to)
{
    const double change = std::abs(alpha_to - alpha_from);
    const double steps = std::ceil(change / (4.0 * g_product_alpha_span_tolerance));
    return std::max(1, static_cast<int>(steps));
}

// Constant-cross-section overload for runs whose band never changes width.
void pushRibbonSegment(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double x2, const double x3, const RibbonEnd& a, const RibbonEnd& b)
{
    const std::array<double, 4> stations{x0, x1, x2, x3};
    pushRibbonSegment(vertices, indices, stations, stations, a, b);
}

/*
Lights one segment of a sustain ribbon, to the same accent light its head wears.

The loud end of the emphasis axis has to reach the tail because the quiet end already does — a
ghost dims head, markers, open bar AND ribbon, so an accent that stopped at the head left the axis
saying different things at its two ends. Both are statements about how hard the string was struck,
and a sustain rings on from that strike either way.

Takes the ribbon's OWN stations and ends, so the light follows every modulation the tail follows —
a bend's lift, a slide's travel, vibrato's wobble, an open band tracking a moving hand window —
without restating any of it. The alpha comes in from the ribbon's envelope for the same reason: the
light has to fade in at the onset and dissolve at the tip exactly where the ribbon does, or it
would glow around a stretch of ribbon that has already gone.

The emitter is the ribbon's FULLY OPAQUE cross-section, so the light is drawn outward from the
last station at which the authored alpha is still full — never inward of it. The glow composites
BEHIND the ribbon: light emitted behind the opaque edge strips is invisible, and light emitted
behind the translucent core shows through it, so a quad across the whole band lit the ribbon
ONLY through the part authored to stay quiet — the core brightened past its own edges while the
strips hid the light entirely, and the defect grew as the tip fade thinned the ribbon. Clipping
at the last fully-opaque station deletes that whole failure mode: a fretted tail's own pixels
are identical lit or unlit, and its accent is purely the halo.

Past that station the strength follows the authored alpha's ramp to zero, spread by the light's
own falloff kernel — openBarEmission, the same authority the open bar's glow strip slices into
corner-clustered columns, reused verbatim here on the width axis. A fretted end has no ramp (its
outer station color IS its edge color), so its emission is flat and each side is a single quad;
an open end's outer strip ramps to transparent across the tail margin, and the columns carry
that taper so the light dissolves at the hand-window rails exactly where the ribbon does. Which
case applies is read off the end's own packed colors, never off the note's kind.

A BAND, not a box. Every vertex pins its local Y to zero, so only the X boundary is ever evaluated
and the rounded-box field reduces exactly to `|local_x| - half_w`. `half_h` is set equal to
`half_w` for that reason: any value at least `half_w` yields the identical field, and matching them
is the one choice that needs no constant of its own.

`columns` is the caller's working buffer for that column set, cleared here and left holding this
segment's columns. It is a parameter because this runs once per RIBBON SEGMENT — hundreds of times
per note per frame on a long modulated tail — and a fresh vector each time was that many
allocations on the deadline path.
*/
void pushTailGlowSegment(
    std::vector<PosColorGlowVertex>& vertices, std::vector<std::uint16_t>& indices,
    const std::array<double, 4>& stations_a, const std::array<double, 4>& stations_b,
    const RibbonEnd& a, const RibbonEnd& b, const ArgbColor color, const double alpha_a,
    const double alpha_b, std::vector<double>& columns)
{
    struct TailGlowEnd
    {
        double half;
        double center;
        double fade;
        double y;
        double z;
        double alpha;
    };
    const auto make_end =
        [](const std::array<double, 4>& stations, const RibbonEnd& end, const double alpha) {
            return TailGlowEnd{
                .half = (stations[3] - stations[0]) / 2.0,
                .center = ((stations[0] + stations[3]) / 2.0) + end.x_offset,
                .fade = end.outer_transparent ? stations[3] - stations[2] : 0.0,
                .y = end.y,
                .z = end.z,
                .alpha = alpha,
            };
        };
    const TailGlowEnd end_a = make_end(stations_a, a, alpha_a);
    const TailGlowEnd end_b = make_end(stations_b, b, alpha_b);

    // Column parameter: signed distance inboard from the outer silhouette, zero on it,
    // positive toward the core, negative into the halo. A hard-edged segment carries a flat
    // emission, so one column pair spans silhouette to halo edge; a ramped segment gets the
    // bar strip's corner-clustered set.
    columns.clear();
    if (end_a.fade > 0.0 || end_b.fade > 0.0)
    {
        columns.reserve(15);
        const double inner_limit = std::max(end_a.fade, end_b.fade);
        for (const double corner_s : {0.0, end_a.fade, end_b.fade})
        {
            for (const double offset : {-1.0, -0.5, 0.0, 0.5, 1.0})
            {
                columns.push_back(
                    std::clamp(corner_s + (offset * g_accent_reach), -g_accent_reach, inner_limit));
            }
        }
        std::ranges::sort(columns);
        const auto duplicates = std::ranges::unique(columns);
        columns.erase(duplicates.begin(), duplicates.end());
    }
    else
    {
        columns.assign({-g_accent_reach, 0.0});
    }

    const auto vertex_at = [&](const TailGlowEnd& end, const double s, const double side) {
        // Never inward of this end's own last fully-opaque station: columns past a shorter
        // fade collapse onto its clip line, and the zero-width quads draw nothing.
        const double s_end = std::min(s, end.fade);
        const double local_x = side * (end.half - s_end);
        const double weight = openBarEmission(s_end, end.fade);
        return makeGlowVertex(
            end.center + local_x,
            end.y,
            end.z,
            packAbgr(color, end.alpha * weight),
            local_x,
            0.0,
            GlowShape{.half_w = end.half, .half_h = end.half, .corner = 0.0, .rhombus = false});
    };
    for (const double side : {-1.0, 1.0})
    {
        for (std::size_t column = 0; column + 1 < columns.size(); ++column)
        {
            pushQuad(
                vertices,
                indices,
                vertex_at(end_a, columns[column], side),
                vertex_at(end_a, columns[column + 1], side),
                vertex_at(end_b, columns[column + 1], side),
                vertex_at(end_b, columns[column], side));
        }
    }
}

// EVERY horizontal line the board lays on the floor — the fret-span line under a note, the one
// under a slide keyframe, and the beat and measure bars — draws through here, and every one of
// them dissolves at its ends. That is the whole rule (user ruling 2026-08-30: the taper is
// consistent everywhere), so there is no un-tapered option to pick wrong: the fade is the
// open-string bar's own end-fade (openBarFadeLength) derived from the quad's OWN span rather than
// stated by the caller, which is what keeps one dissolve rule instead of a value each site spells.
//
// It began as the harmonic line's alone: that line is node-centred, so it no longer stops on the
// fret wires that gave a slot line its flat ends. Sighting it beside the flat-ended lines settled
// the general case the other way — a hard end reads as an edge belonging to nothing wherever it
// falls, wires included.
//
// Three columns, because one quad carries one linear gradient and this needs a ramp at each end.
// When z alpha also moves, the mark carries the same x-taper times z-envelope product as a ribbon
// band. One two-triangle quad cannot carry that product: a fixed split diagonal biases the fade
// sideways, which is obvious when a harmonic mark is node-centred inside a fret slot. The same
// product-error budget used by sustain ribbons bounds the z slices here, and the falling x-taper
// flips its split so the residual one-slice error mirrors instead of steering the fading trail.
void pushTaperedFloorQuad(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double y, const double z0, const double z1, const ArgbColor argb,
    const double alpha_at_z0, const double alpha_at_z1)
{
    // Four stations across x: the two ends at nothing, the two inboard shoulders at full.
    const double fade_length = openBarFadeLength(x0, x1);
    const std::array<double, 4> station_x{x0, x0 + fade_length, x1 - fade_length, x1};
    const std::array<double, 4> station_scale{0.0, 1.0, 1.0, 0.0};
    const int z_steps = productAlphaSpanSteps(alpha_at_z0, alpha_at_z1);
    const double z_span = z1 - z0;
    const double alpha_span = alpha_at_z1 - alpha_at_z0;
    for (int z_step = 0; z_step < z_steps; ++z_step)
    {
        const double from_fraction = static_cast<double>(z_step) / static_cast<double>(z_steps);
        const double to_fraction = static_cast<double>(z_step + 1) / static_cast<double>(z_steps);
        const double from_z = z0 + (z_span * from_fraction);
        const double to_z = z0 + (z_span * to_fraction);
        const double from_alpha = alpha_at_z0 + (alpha_span * from_fraction);
        const double to_alpha = alpha_at_z0 + (alpha_span * to_fraction);
        for (std::size_t column = 0; column + 1 < station_x.size(); ++column)
        {
            const double left_scale = station_scale.at(column);
            const double right_scale = station_scale.at(column + 1);
            const double left_x = station_x.at(column);
            const double right_x = station_x.at(column + 1);
            const bool split_from_left_start_to_right_end = right_scale >= left_scale;
            pushQuadWithSplit(
                vertices,
                indices,
                makeVertex(left_x, y, from_z, packAbgr(argb, from_alpha * left_scale)),
                makeVertex(right_x, y, from_z, packAbgr(argb, from_alpha * right_scale)),
                makeVertex(right_x, y, to_z, packAbgr(argb, to_alpha * right_scale)),
                makeVertex(left_x, y, to_z, packAbgr(argb, to_alpha * left_scale)),
                split_from_left_start_to_right_end);
        }
    }
}

// Charter's open-note bar: a hexagonal prism along X across [x0, x1], with the center
// station slightly thicker than the ends and the ring squashed nearly flat in Z. Flat-colored
// and unlit, its silhouette reads as Charter's thin rounded bar from every board-view
// angle. The end stations are fully transparent, fading in over g_open_note_end_fade_length, so
// the bar tapers visually to a point at each end (which also makes end caps pointless — the
// silhouette dissolves before it could show a flat end). The thickness scale carries the emphasis
// axis on a bar that has no head to wear it: below one it thins a ghost, above one it redraws the
// bar as the accent's rim.
void pushOpenNoteBar(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double lane_y, const double z, const ArgbColor argb, const double alpha,
    const double thickness_scale)
{
    constexpr auto ring_size = static_cast<std::size_t>(g_open_note_segments);
    std::array<double, ring_size> ring_y{};
    std::array<double, ring_size> ring_z{};
    for (std::size_t point = 0; point < ring_size; ++point)
    {
        const double angle =
            2.0 * std::numbers::pi * static_cast<double>(point) / g_open_note_segments;
        ring_y.at(point) = std::cos(angle);
        ring_z.at(point) = std::sin(angle) * g_open_note_z_squash;
    }

    // Five cross-section stations: transparent tips, full-alpha fade-in stations, bulged middle.
    const double fade_length = openBarFadeLength(x0, x1);
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
        for (std::size_t point = 0; point < ring_size; ++point)
        {
            const std::size_t next_point = (point + 1) % ring_size;
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

// One glyph's pen advance. The atlas is monospace-ish — digit and letter ink fills ~0.6 of the
// cell — but a period's ink is a sliver, and advancing it a full slot floats the dot mid-gap
// ("2 . 3" where the label means "2.3"), so the narrow punctuation advances narrow.
[[nodiscard]] double glyphAdvance(const char character, const double glyph_height)
{
    return glyph_height * (character == '.' ? 0.28 : 0.62);
}

// The pen width a text string will advance — the width authority every caller centring a label
// must use, so measurement and drawing cannot disagree about where the ink lands.
[[nodiscard]] double glyphTextWidth(const std::string_view text, const double glyph_height)
{
    double width = 0.0;
    for (const char character : text)
    {
        width += glyphAdvance(character, glyph_height);
    }
    return width;
}

// Appends billboarded (constant-z) glyph quads for a text string to a glyph batch, left-anchored
// at (left_x, baseline_y) and growing right; returns the advanced pen width. Shared by every text
// pass (fret numbers, section labels, chord names). Each quad centres on its own advance — the
// glyph ink is cell-centred, so this is what makes a narrow advance pull the INK in rather than
// just the pen.
[[nodiscard]] double pushGlyphText(
    std::vector<PosColorUvVertex>& vertices, std::vector<std::uint16_t>& indices,
    const HighwayAtlasLayout& glyph_layout, const std::string_view text, const double left_x,
    const double baseline_y, const double z, const double glyph_height, const std::uint32_t color)
{
    double pen_x = left_x;
    for (const char character : text)
    {
        const double advance = glyphAdvance(character, glyph_height);
        const std::optional<int> cell = highwayGlyphCellIndex(character);
        if (cell.has_value())
        {
            const std::array<float, 4> rect = glyph_layout.cellRect(*cell);
            const double quad_left = pen_x + (advance / 2.0) - (glyph_height / 2.0);
            pushQuad(
                vertices,
                indices,
                makeUvVertex(quad_left, baseline_y, z, color, rect[0], rect[3]),
                makeUvVertex(quad_left + glyph_height, baseline_y, z, color, rect[2], rect[3]),
                makeUvVertex(
                    quad_left + glyph_height,
                    baseline_y + glyph_height,
                    z,
                    color,
                    rect[2],
                    rect[1]),
                makeUvVertex(quad_left, baseline_y + glyph_height, z, color, rect[0], rect[1]));
        }
        pen_x += advance;
    }
    return pen_x - left_x;
}

// Draws one chord-box panel — corner holders, a frame variant, and the faint filling — into a
// color batch. Geometry is explicit rather than taken from a chord group so both strummed chord
// boxes and arpeggio-styled hand-shape boxes reuse it. A repeat box's mute mark is not drawn
// here: the caller submits the SDF-program mark over the panel's frame interior.
//
// This is the ONE definition of a box's shape, and an accented box's light is drawn by calling it
// again rather than by laying rectangles that would have to agree with it. That matters because
// the shape is not one shape: `box_only` halves it, `with_top` decides between a top bar and two
// columns that fade out at the midpoint, and the corner holders are their own silhouette. A
// hand-rolled outline drew a top bar where a two-note chord has none and full-height columns
// beside fading ones — the same defect the open string's light had, for the same reason.
// full_height_y1: the box top for a full-height box (a repeat box is half this).
// box_only: half-height repeat box.
// with_top: full sides plus a top bar (3+ note chords); ignored under box_only.
// alpha_scale: multiplies every part's alpha — the quiet end of the emphasis axis, which
//        takes the box's presence down the way a ghost takes a head's.
// frame_thickness: bar/column width of the frame; callers pass the string grid's base
//        height so the bottom bar fills the gap under the grid exactly.
ChordBoxFrame chordBoxFrame(
    const double full_height_y1, const bool box_only, const bool with_top,
    const double frame_thickness)
{
    const double y1 = box_only ? full_height_y1 / 2.0 : full_height_y1;
    return ChordBoxFrame{
        .side_y1 = y1,
        .outer_top = with_top ? y1 + frame_thickness : y1,
        .fade_start_y = with_top ? y1 + frame_thickness : y1 / 2.0,
        .closed_top = with_top,
    };
}

void pushChordBoxPanel(
    std::vector<PosColorVertex>& vertices, std::vector<std::uint16_t>& indices, const double x0,
    const double x1, const double z, const double full_height_y1, const bool box_only,
    const bool with_top, const double alpha_scale, const double frame_thickness)
{
    const double y0 = 0.0;
    const ChordBoxFrame frame = chordBoxFrame(full_height_y1, box_only, with_top, frame_thickness);
    const double y1 = frame.side_y1;
    // Sets every frame dimension, not only the bottom bar: the top bar and the side columns below
    // scale from it too, which is why it comes in as one value rather than being read per-part.
    const double thickness = frame_thickness;

    // Every part scales by the one emphasis alpha, so a quiet box keeps its whole construction
    // — holders, fades and all — and only its presence changes.
    const std::uint32_t box_solid = packAbgr(g_chord_box_color, alpha_scale);
    const std::uint32_t box_half =
        packAbgr(g_chord_box_color, g_chord_box_frame_alpha * alpha_scale);
    const std::uint32_t dark_half =
        packAbgr(g_chord_box_dark_color, g_chord_box_frame_alpha * alpha_scale);
    const std::uint32_t box_faint = packAbgr(g_chord_box_color, (32.0 / 255.0) * alpha_scale);
    const std::uint32_t dark_faint = packAbgr(g_chord_box_dark_color, (32.0 / 255.0) * alpha_scale);
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
        push_fan(holder_front, origin_x, x_sign, packAbgr(g_chord_box_dark_color, alpha_scale));
    }

    // Frame: bottom bar always, then full sides with a top bar or short fading sides. The accent
    // chevrons that used to be a third variant here are gone: emphasis is a rendered light now,
    // so a box states it the same way a note does rather than by changing shape into a mark that
    // meant "loud" only by convention.
    push_bar(y0);
    if (with_top)
    {
        for (const auto& [origin_x, x_sign] : {std::pair{x0, 1.0}, std::pair{x1, -1.0}})
        {
            push_face(origin_x, y0, box_half, origin_x + (x_sign * thickness), y1, box_half);
        }
        push_bar(y1);
    }
    else
    {
        const double fade_start_y = frame.fade_start_y;
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

    // Filling: the faint panel, carrying the frame's end-to-middle fade.
    pushMiddleFadedQuads(
        vertices,
        indices,
        x0,
        x1,
        y0,
        y1,
        box_faint,
        dark_faint,
        [&](const double vx, const double vy, const std::uint32_t abgr) {
            return makeVertex(vx, vy, z, abgr);
        });
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

// Names why a structural-art measurement failed, for the asset-invalid diagnostic. Deliberately
// switches without a default so a new StructuralArtError has to be named here rather than being
// reported as one of the existing reasons.
[[nodiscard]] constexpr std::string_view describeStructuralArtError(const StructuralArtError error)
{
    switch (error)
    {
        case StructuralArtError::UndecodableImage:
        {
            return "undecodable";
        }
        case StructuralArtError::UnanalyzableArt:
        {
            return "unanalyzable";
        }
        case StructuralArtError::AlphaBearingImage:
        {
            return "alpha-bearing (the art must have no alpha channel — coverage lives in its blue "
                   "channel and opacity is applied per draw)";
        }
    }
    return "unrecognized";
}

} // namespace

/*
All bgfx-facing state and drawing lives here, behind the public header's opaque pointer, so the
framework never leaks into common/ui's interface (the Tracktion isolation treatment).
*/
// One deferred technique-marker quad; the marker-deferral rationale (why markers collect per
// onset group instead of drawing inline) lives at the pending_markers site in draw().
struct PendingMarker
{
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double cos_r{1.0};
    double sin_r{0.0};
    double half_w{0.0};
    double half_h{0.0};
    int cell{0};
    std::uint32_t tint{0};
    bool flip_v{false};
};

// One scrolling floor label (fret number or node decimal); the ordering and draining rules live
// at the floor-number banner in draw().
struct FloorNumber
{
    double seconds{0.0};
    double z{0.0};
    int fret{0};
    // A natural harmonic's node: the label prints the DECIMAL position and centers on the
    // node's exact spot rather than the fret slot's middle, because the decimal is the
    // information — nothing else on the board states where between the wires the touch lands.
    std::optional<double> node{};
    ArgbColor base{0};
    bool fade{false};
    double alpha{1.0};
    // Position in build order, the sort's tiebreak; stamped in one pass before the sort.
    std::size_t build_index{0};
};

// One settled fret-hand window visible this frame; the span rules live at the hand_windows site
// in draw().
struct HandWindow
{
    double start_seconds;
    double end_seconds;
    int fret;
    int width;
};

// One sampled slice of the hand-window light: its z and the two eased window edges there, plus
// the motion dim the ramps write. One record rather than four parallel arrays, so a slice cannot
// be half-written.
struct WindowLightSlice
{
    double z;
    double low_x;
    double high_x;
    double dim;
};

// One arpeggio bracket glyph awaiting its lane-dominant submission; the ordering rule lives at
// the bracket_batches site in draw().
struct BracketBatch
{
    int lane{0};
    double span_start_seconds{0.0};
    double span_end_seconds{0.0};
    std::vector<PosColorUvVertex> vertices;
    std::vector<std::uint16_t> indices;
    bool submitted{false};
};

// One chord, arpeggio, or tapped box in the far-to-near draw list; the classification rules live
// at the boxes site in draw().
struct BoxDraw
{
    double start_seconds;
    bool box_only;
    bool with_top;
    // The box's own dynamics. An arpeggio box never carries one: the bracket is a
    // POSTURE — the hand holding a shape, not a strike — so emphasis belongs to the
    // notes inside it, which state their own.
    common::core::NoteEmphasis emphasis;
    // The strum's two mute unanimities, straight off the group: a box speaks for the
    // whole strum, so only a mute every member shares reaches it — one dead string in a
    // palm-muted chord leaves this box palm muted. Each flag draws its own mark below,
    // and a box unanimous in both wears both.
    bool palm_mute;
    bool dead;
    const common::core::ShapeViewState* arpeggio_shape;
    // A tapped chord box spans the taps' own fret extent instead of the fretting
    // hand's window (right-hand-tap-lighting plan); null for left-hand boxes.
    const common::core::HighwayTapOnsetViewState* tap;
    // Build position, the sort's tiebreak: onset alone is not a total order (a
    // tap-and-strum instant emits two boxes), and the deterministic build order is what
    // keeps their overlap from flickering frame to frame.
    std::size_t build_index;
};

// One sampled station of a modulated tail's centerline.
struct TailSample
{
    std::array<double, 4> stations;
    double x_offset;
    double y;
    double z;
    double alpha;
};

// Running totals of a modulated tail's slope shades, indexed so entry i holds the sums over
// samples [0, i). The tent weight is linear in a sample's z, so each of the three sums is one
// term of the smoothed value — see the shade-smoothing site in draw().
struct TailShadeSums
{
    double lift;
    double lift_z;
    double z;
};

// The two CPU-side buffers one board pass builds its batch in. Handed out already cleared (see
// FrameScratch::colorBatch), so a pass that shares a buffer with an earlier pass cannot inherit
// its geometry.
template <typename Vertex> struct FrameBatch
{
    std::vector<Vertex>& vertices;
    std::vector<std::uint16_t>& indices;
};

// Geometry and label scratch reused across frames, so a steady scene stops allocating on the
// per-frame deadline path; within a frame the batches are still cleared per onset group exactly
// as before. INVARIANT: every member is cleared before it is read. draw() clears all of them up
// front through clearForFrame(); the shared pass buffers below are cleared a second time on
// handout, which is what also covers drawOverlayRects — the other public entry point into this
// scratch, which never calls clearForFrame(). A missed clear draws last frame's content into
// this one.
struct FrameScratch
{
    std::vector<PosColorVertex> shadow_vertices;
    std::vector<std::uint16_t> shadow_indices;
    std::vector<PosColorVertex> rail_vertices;
    std::vector<std::uint16_t> rail_indices;
    std::vector<PosColorVertex> open_vertices;
    std::vector<std::uint16_t> open_indices;
    std::vector<PosColorGlowVertex> accent_glow_vertices;
    std::vector<std::uint16_t> accent_glow_indices;
    std::vector<PosColorUvVertex> head_vertices;
    std::vector<std::uint16_t> head_indices;
    std::vector<PosColorVertex> box_vertices;
    std::vector<std::uint16_t> box_indices;
    std::vector<PosColorUvVertex> box_marker_vertices;
    std::vector<std::uint16_t> box_marker_indices;
    std::vector<PosColorGlowVertex> box_glow_vertices;
    std::vector<std::uint16_t> box_glow_indices;
    std::vector<PosColorUvVertex> number_vertices;
    std::vector<std::uint16_t> number_indices;
    std::vector<std::size_t> visible;
    std::vector<double> lane_key;
    std::vector<double> window_times;
    std::vector<PendingMarker> pending_markers;
    std::vector<FloorNumber> floor_numbers;

    // The board's furniture passes — lane ribbons, both hand lights, beat bars, shape rails,
    // fret lines, inlays, capo, glyph text, strike glow — each build one batch, submit it, and
    // are done, so they never hold geometry across one another and one buffer pair per vertex
    // layout serves them all. Handed out through colorBatch()/texturedBatch(), which clear on
    // handout: a shared buffer that a pass forgot to clear would draw the previous pass's
    // geometry under this pass's program, so the clear is not left to the pass to remember.
    std::vector<PosColorVertex> pass_color_vertices;
    std::vector<std::uint16_t> pass_color_indices;
    std::vector<PosColorUvVertex> pass_textured_vertices;
    std::vector<std::uint16_t> pass_textured_indices;

    std::vector<HandWindow> hand_windows;
    std::vector<WindowLightSlice> window_light_slices;
    std::vector<BracketBatch> bracket_batches;
    std::vector<BoxDraw> boxes;
    std::vector<double> window_edge_onsets;
    // The accent light's column parameters, shared by the tail-glow segments and the open bar's
    // glow strip: the two build the same corner-clustered set through openBarEmission, and never
    // at the same time (a note's tail ribbon finishes before its head draws).
    std::vector<double> glow_columns;
    // The modulated tail's per-note working set, in build order.
    std::vector<double> tail_wobble_times;
    std::vector<TailSample> tail_samples;
    std::vector<double> tail_lifts;
    std::vector<TailShadeSums> tail_shade_sums;
    std::vector<ArgbColor> tail_shaded;

    [[nodiscard]] FrameBatch<PosColorVertex> colorBatch()
    {
        pass_color_vertices.clear();
        pass_color_indices.clear();
        return FrameBatch<PosColorVertex>{
            .vertices = pass_color_vertices,
            .indices = pass_color_indices,
        };
    }

    [[nodiscard]] FrameBatch<PosColorUvVertex> texturedBatch()
    {
        pass_textured_vertices.clear();
        pass_textured_indices.clear();
        return FrameBatch<PosColorUvVertex>{
            .vertices = pass_textured_vertices,
            .indices = pass_textured_indices,
        };
    }

    void clearForFrame()
    {
        shadow_vertices.clear();
        shadow_indices.clear();
        rail_vertices.clear();
        rail_indices.clear();
        open_vertices.clear();
        open_indices.clear();
        accent_glow_vertices.clear();
        accent_glow_indices.clear();
        head_vertices.clear();
        head_indices.clear();
        box_vertices.clear();
        box_indices.clear();
        box_marker_vertices.clear();
        box_marker_indices.clear();
        box_glow_vertices.clear();
        box_glow_indices.clear();
        number_vertices.clear();
        number_indices.clear();
        visible.clear();
        lane_key.clear();
        window_times.clear();
        pending_markers.clear();
        floor_numbers.clear();
        pass_color_vertices.clear();
        pass_color_indices.clear();
        pass_textured_vertices.clear();
        pass_textured_indices.clear();
        hand_windows.clear();
        window_light_slices.clear();
        bracket_batches.clear();
        boxes.clear();
        window_edge_onsets.clear();
        glow_columns.clear();
        tail_wobble_times.clear();
        tail_samples.clear();
        tail_lifts.clear();
        tail_shade_sums.clear();
        tail_shaded.clear();
    }
};

// The frame-scope facts the board passes read: the instant being drawn, the span of song time
// on screen, and the per-frame derivations (the settled hand windows, the shape spans reaching
// the board) that every pass would otherwise re-derive. draw() builds one at the top of the
// frame and hands it to each pass, so a pass declares what it reads instead of capturing
// draw()'s locals.
//
// Only what changes per FRAME belongs here. The passes are Impl members, so every per-STATE
// fact — the metrics, the view state, the scroll speed, the derivations taken beside
// displayed_count — is read from the renderer itself, never copied through this record.
//
// The spans view storage that is complete before this record is built and untouched for the
// rest of the frame: the frame's hand windows in the scratch, the chart's own shape list.
struct FrameContext
{
    // Playback song time for this frame; the origin every time-to-z conversion measures from.
    double now_seconds{0.0};

    // The drawn span of song time: from the passed-note fade behind the hit line out to the
    // visibility horizon, which is where the visible-range searches clamp.
    double span_start_seconds{0.0};
    double span_end_seconds{0.0};

    // The settled fret-hand windows visible this frame, in arrival order.
    std::span<const HandWindow> hand_windows;

    // The window at the current instant, fractional mid-transition.
    common::core::HighwayHandWindow current_window;

    // The hand-posture spans reaching the board this frame.
    std::span<const common::core::ShapeViewState> visible_shapes;
};

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
    UniqueBgfxHandle<bgfx::ProgramHandle> accent_glow_program;

    // Custom uniforms (predefined ones like u_modelViewProj are never created by hand).
    UniqueBgfxHandle<bgfx::UniformHandle> fade_params;
    UniqueBgfxHandle<bgfx::UniformHandle> atlas_sampler;
    UniqueBgfxHandle<bgfx::UniformHandle> window_light_params;
    UniqueBgfxHandle<bgfx::UniformHandle> accent_glow_params;
    UniqueBgfxHandle<bgfx::UniformHandle> box_mute_params;
    UniqueBgfxHandle<bgfx::UniformHandle> box_mute_arms;

    // The box-mute marks' measured layout fractions (from chords.png) and the two-row ramp
    // texture the SDF shader samples the marks' cross-sections through; the ramps themselves
    // live only in that texture.
    struct BoxMuteLayout
    {
        double stroke_half_fraction{0.0};
        double extent_fraction{0.0};
    };
    struct BoxMuteLayouts
    {
        BoxMuteLayout palm;
        BoxMuteLayout full;
    };
    BoxMuteLayouts box_mute_layouts{};
    UniqueBgfxHandle<bgfx::TextureHandle> box_mute_ramp;

    // The head art's silhouette, measured from notes.png at create (see the block above the
    // texel-conversion functions); the accent glow sizes its distance field from this.
    HeadArtProfile head_art{};

    HighwayAtlases atlases;

    // Fretboard skin (one cell per fret); invalid when the asset is missing (plain board).
    UniqueBgfxHandle<bgfx::TextureHandle> inlay_texture;

    // Decoded inlay dimensions, for the half-texel UV inset that keeps interior fret cells from
    // bleeding into each other under minification.
    int inlay_texture_width{0};
    int inlay_texture_height{0};

    // Retained board-face geometry; rebuilt on chart load, streamed content uses transients.
    UniqueBgfxHandle<bgfx::VertexBufferHandle> face_vertices;
    UniqueBgfxHandle<bgfx::IndexBufferHandle> face_indices;
    std::uint32_t face_index_count{0};

    common::core::HighwayViewState state;
    // The displayed lane count and the padding below the chart's strings, resolved once per state
    // from the chart's own count and the display minimum (displayedStringCount / displayedLane):
    // the scene keeps chart strings, and every lane the board draws goes through laneOf.
    int displayed_count{0};
    int extra_lanes{0};
    std::vector<double> sustain_prefix_max;
    // The same companion table for the hand-posture spans, which overlap freely like the notes'
    // sustains: the shape passes bound their visible range through visibleEventRange with this.
    std::vector<double> shape_prefix_max;
    // Natural-harmonic node series, derived once per chart revision like sustain_prefix_max:
    // the draw path labels and suppresses from this table instead of re-walking every note.
    std::vector<common::core::HighwayNodeSeries> node_series;
    // Longest FHP approach ramp, for windowSampleTimes' exact early-out: an arrival this far
    // past a window's end cannot reach back into it, and neither can any later arrival.
    double max_fhp_ramp_seconds{0.0};
    // The tapping hand's counterpart of sustain_prefix_max and max_fhp_ramp_seconds, derived once
    // per chart revision: the running maximum of the light paths' end times, and the longest tap
    // rise. Tap light envelopes run from an onset's rise start to its path end plus a decay, and
    // the paths overlap freely, so the prefix maximum is what bounds a span's first candidate
    // (visibleEventRange's argument, applied to the hand that has no `end_seconds` field) and the
    // longest rise is what bounds its last.
    std::vector<double> tap_end_prefix_max;
    double max_tap_ramp_seconds{0.0};
    FrameScratch scratch;
    common::core::HighwayMetrics metrics;
    common::core::HighwayCamera camera;

    // Player scroll speed; a free setting later (25-Q3), the default until then.
    double scroll_speed{1.3};

    // One warning per process when a transient batch is dropped (budget exceeded is a bug
    // signal, not an expected runtime path).
    bool reported_transient_drop{false};
    bool reported_oversized_drop{false};

    // The displayed lane a chart string occupies, counted from the lowest lane.
    [[nodiscard]] int laneOf(const int chart_string) const noexcept
    {
        return common::core::displayedLane(chart_string, extra_lanes);
    }

    /*
    The tap onsets whose light can reach [from_seconds, to_seconds], as a half-open index range —
    visibleEventRange's answer for the tapping hand, which carries its extent in a path rather
    than in an `end_seconds` field. A tap's light rises over `ramp_seconds` before its onset and
    releases `decay_seconds` after its path ends, so:

      - every onset before the first whose prefix maximum of path ends reaches back into the span
        has released before it (the ends overlap freely, which is exactly why the bound is the
        prefix maximum rather than the ends themselves), and
      - every onset from the first whose rise cannot start by `to_seconds` — even at the longest
        rise in the chart — begins after it, as do all later onsets.

    Both tests are spelled with the same expressions the per-tap skips use, so the range is a
    tight superset of what those skips keep and callers still run them: the range never drops a
    tap the caller's own test would have kept, and a rounding tie only costs one skipped tap.
    */
    [[nodiscard]] std::pair<std::size_t, std::size_t> litTapOnsetRange(
        const double from_seconds, const double to_seconds,
        const double decay_seconds) const noexcept
    {
        const auto first = static_cast<std::size_t>(
            std::ranges::partition_point(
                tap_end_prefix_max,
                [&](const double path_end) { return path_end + decay_seconds < from_seconds; }) -
            tap_end_prefix_max.begin());
        const auto last = static_cast<std::size_t>(
            std::ranges::partition_point(
                state.tap_onsets,
                [&](const common::core::HighwayTapOnsetViewState& tap) {
                    return tap.path.front().seconds - max_tap_ramp_seconds <= to_seconds;
                }) -
            state.tap_onsets.begin());
        return {std::min(first, last), last};
    }

    // Board z for a song time under the current scroll speed: the one conversion the passes and
    // the content scheduler share, so the mapping is stated once.
    [[nodiscard]] double timeToZ(const FrameContext& frame, const double seconds) const noexcept
    {
        return common::core::highwayTimeToZ(seconds - frame.now_seconds, scroll_speed, metrics);
    }

    // The floor furniture's distance fade, as the two board z values it runs between: fully
    // faded near the hit line, opaque toward the horizon (Charter's fading shader constants,
    // 50 ms to 250 ms out). The color-fade program takes the band as a uniform; the scrolling
    // floor numbers bake it into vertex color instead, because the glyph program has no fade
    // uniform. Both read the band from here rather than restating the two constants.
    [[nodiscard]] std::pair<double, double> fadeBandZ() const noexcept
    {
        return {
            common::core::highwayTimeToZ(0.05, scroll_speed, metrics),
            common::core::highwayTimeToZ(0.25, scroll_speed, metrics)
        };
    }

    // Arms the distance fade for the next color-fade submit. A bgfx uniform is ambient state
    // applied at the submit that follows, so every pass drawing through that program calls this
    // itself instead of inheriting whatever a neighbour happened to leave behind.
    void setFadeUniform() const
    {
        const auto [faded_z, close_z] = fadeBandZ();
        const std::array<float, 4> fade_uniform{
            static_cast<float>(faded_z), static_cast<float>(close_z), 0.0F, 0.0F
        };
        bgfx::setUniform(fade_params.get(), fade_uniform.data());
    }

    // Vertical extent of the board face's fret lines: the string grid's base (the floor stays
    // y = 0; the chord box's bottom bar fills the gap below the grid) up to an equal half-string
    // margin above the top lane. Shared by the fret-line pass and everything that must not rise
    // past the fret grid — including the bend saturation, which is why the top edge is derived
    // in highway_metrics.h rather than here. Both are per-STATE (the displayed lane count and
    // the metrics), which is why they are derived here rather than carried on the frame.
    [[nodiscard]] double faceBottomY() const noexcept
    {
        return metrics.string_grid_base_y;
    }
    [[nodiscard]] double faceTopY() const noexcept
    {
        return common::core::highwayStringGridTopY(displayed_count, metrics);
    }

    // The tap onsets whose light can reach a time span, as the taps themselves; litTapOnsetRange
    // holds the bound and its proof. Every pass keeps its own per-tap skip — this only spares
    // each one the onsets that provably fail it, the way visibleEventRange spares the note sweep.
    [[nodiscard]] std::span<const common::core::HighwayTapOnsetViewState> litTaps(
        const double from_seconds, const double to_seconds,
        const double decay_seconds) const noexcept
    {
        const auto [first, last] = litTapOnsetRange(from_seconds, to_seconds, decay_seconds);
        return std::span<const common::core::HighwayTapOnsetViewState>(state.tap_onsets)
            .subspan(first, last - first);
    }

    void rebuildBoardFace();
    void draw(double now_seconds, double dt_seconds, std::uint32_t width, std::uint32_t height);

    // One board pass each, in the order draw() calls them. Painter order IS submission order
    // (the board view is Sequential and writes no depth), so a pass paints exactly where its
    // call sits and the order here is the layering. Each sets every uniform and texture bind it
    // draws with: bgfx state is ambient until the next submit, so nothing inherits a neighbour's.
    // The frame's own facts arrive as the context; everything per-state (the scratch buffers,
    // the metrics, the view state) each pass reads from the renderer, like any other member.
    void drawLaneBorderRibbons(const FrameContext& frame);
    void drawHandWindowLight(const FrameContext& frame);
    void drawTappingHandLight(const FrameContext& frame);
    void drawBeatBars(const FrameContext& frame);
    void drawHandShapeRails(const FrameContext& frame);
    void drawStringLines();
    void drawFretLines(const FrameContext& frame);
    void drawFretboardMarkers();
    void drawCapo();
    void drawSectionLabels(const FrameContext& frame);
    void drawStrikeGlow(const FrameContext& frame);

    void drawOverlayRects(
        std::span<const HighwayOverlayRect> rects, std::uint32_t width, std::uint32_t height);

    // Submits a CPU-built batch through the transient buffers; drops the batch (with one
    // process-lifetime warning per failure class) if it cannot be submitted — a bug signal, not a
    // runtime path. Nothing measures the frame's emission against bgfx's default transient pool,
    // so no headroom figure is claimed here; the tail sampler's one budget is what bounds the
    // largest batch.
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
        // and render garbage silently, so an oversized batch is dropped and reported instead. Not
        // unreachable for real charts — the accent glow of one onset group of long teethed open
        // tails once reached it, which is why the tail sampler now holds one budget — so the
        // report has its own flag: a pool-exhaustion report must not silence this one, nor this
        // one it.
        if (vertices.size() > 65535 || (texture != nullptr && !bgfx::isValid(*texture)))
        {
            if (!reported_oversized_drop)
            {
                reported_oversized_drop = true;
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

    // Link every program in the shared table, so adding one is a table row rather than another
    // link site here. The first bad binary fails creation: a broken install is broken whichever
    // stage bgfx rejected, and the error names the program.
    std::array<UniqueBgfxHandle<bgfx::ProgramHandle>, g_highway_shader_programs.size()> linked;
    for (const Program program : g_highway_shader_programs)
    {
        auto result = linkProgram(shaders.at(indexOf(program)), highwayShaderProgramName(program));
        if (!result.has_value())
        {
            return std::unexpected{result.error()};
        }
        linked.at(indexOf(program)) = std::move(*result);
    }

    // The renderer's own named handles: the one place a program in the shared table is bound to the
    // slot the draw code reaches for.
    impl->color_program = std::move(linked.at(indexOf(Program::Color)));
    impl->color_fade_program = std::move(linked.at(indexOf(Program::ColorFade)));
    impl->texture_tint_program = std::move(linked.at(indexOf(Program::TextureTint)));
    impl->glyph_program = std::move(linked.at(indexOf(Program::Glyph)));
    impl->texture_program = std::move(linked.at(indexOf(Program::Texture)));
    impl->window_light_program = std::move(linked.at(indexOf(Program::WindowLight)));
    impl->box_mute_program = std::move(linked.at(indexOf(Program::BoxMute)));
    impl->accent_glow_program = std::move(linked.at(indexOf(Program::AccentGlow)));

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
    impl->accent_glow_params = UniqueBgfxHandle<bgfx::UniformHandle>{bgfx::createUniform(
        "u_accent_glow_params", bgfx::UniformType::Vec4)};

    impl->atlases = makeHighwayAtlases(textures.at(indexOf(TextureAsset::Notes)));
    UploadedTexture inlay = uploadPngTexture(textures.at(indexOf(TextureAsset::Inlays)));
    impl->inlay_texture_width = inlay.width;
    impl->inlay_texture_height = inlay.height;
    impl->inlay_texture = std::move(inlay.handle);

    // Box-mute marks: chords.png is the single source of truth for the marks' STRUCTURE. Measure
    // both marks' cross-sections from its pixels and upload them as the two-row ramp the SDF
    // shader samples by distance; the measured weighting and coverage are exactly what the boxes
    // render, with hue and opacity applied per draw as vertex color. Draw needs only the layout
    // fractions afterwards — the ramps live in the GPU texture.
    const std::expected<BoxMuteProfiles, StructuralArtError> profiles =
        measureBoxMuteProfiles(textures.at(indexOf(TextureAsset::ChordMarks)));
    if (profiles.has_value())
    {
        impl->box_mute_layouts = {
            .palm =
                {
                    .stroke_half_fraction = profiles->palm.stroke_half_fraction,
                    .extent_fraction = profiles->palm.extent_fraction,
                },
            .full = {
                .stroke_half_fraction = profiles->full.stroke_half_fraction,
                .extent_fraction = profiles->full.extent_fraction,
            },
        };
        const auto width = static_cast<std::uint32_t>(g_box_mute_ramp_samples);
        // One RGBA row per mark. Size the rows in std::size_t so neither the memcpy lengths nor
        // the destination offset comes from a multiplication performed in the narrower type.
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
        const bgfx::Memory* memory = bgfx::alloc(static_cast<std::uint32_t>(row_bytes * 2U));
        std::memcpy(memory->data, profiles->palm.ramp.data(), row_bytes);
        std::memcpy(memory->data + row_bytes, profiles->full.ramp.data(), row_bytes);
        impl->box_mute_ramp = UniqueBgfxHandle<bgfx::TextureHandle>{bgfx::createTexture2D(
            static_cast<std::uint16_t>(width),
            2,
            false,
            1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            memory)};
    }

    // The head art's silhouette, measured from the same notes.png bytes the atlas uploads: the
    // accent glow sizes its distance field from these numbers, so they must describe the pixels
    // actually shipped rather than the pixels some earlier fit remembered.
    const std::expected<HeadArtProfile, StructuralArtError> measured_head_art =
        measureHeadArtProfile(textures.at(indexOf(TextureAsset::Notes)));
    if (measured_head_art.has_value())
    {
        impl->head_art = *measured_head_art;
    }

    // Texture assets are required product content: a missing, undecodable, or wrong-shape
    // asset is a broken install, not a degradable state (the procedural fallbacks this check
    // replaces silently masked exactly such failures).
    if (!impl->atlases.heads.isValid() ||
        impl->atlases.head_layout.capacity() < g_head_cell_count ||
        !impl->inlay_texture.isValid() || !impl->box_mute_ramp.isValid() ||
        !measured_head_art.has_value())
    {
        const std::string_view chord_marks_state =
            profiles.has_value() ? "measured" : describeStructuralArtError(profiles.error());
        const std::string_view head_art_state =
            measured_head_art.has_value() ? "measured"
                                          : describeStructuralArtError(measured_head_art.error());
        return std::unexpected{HighwayRendererError{
            .code = HighwayRendererErrorCode::TextureAssetInvalid,
            .message = std::format(
                "highway texture assets missing or invalid (note atlas loaded={} with {} of "
                "{} required cells, inlays loaded={}, chord mute marks {}, head silhouette {}); "
                "the install or resource deployment is broken",
                impl->atlases.heads.isValid(),
                impl->atlases.head_layout.capacity(),
                g_head_cell_count,
                impl->inlay_texture.isValid(),
                chord_marks_state,
                head_art_state)
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
    // The draw path indexes display_hold_ends and note_group by note index with no per-frame
    // check, so the one-per-note contracts (highway_view_state.h) are asserted here, at the only
    // place a state enters — a hand-built state that breaks them should fail loudly instead of
    // reading past a vector inside the frame loop.
    assert(m_impl->state.chart.display_hold_ends.size() == m_impl->state.chart.notes.size());
    assert(m_impl->state.note_group.size() == m_impl->state.chart.notes.size());
    m_impl->displayed_count = common::core::displayedStringCount(
        m_impl->state.chart.stringCount(), m_impl->state.options.minimum_string_count);
    m_impl->extra_lanes = m_impl->displayed_count - m_impl->state.chart.stringCount();
    m_impl->sustain_prefix_max =
        common::core::makeSustainPrefixMax(m_impl->state.chart.display_hold_ends);
    // The DRAWN extents, named rather than taken off the spans: the board draws no reveal, so the
    // trimmed end is the furthest any rail or bracket of its reaches.
    m_impl->shape_prefix_max = common::core::makeSustainPrefixMax(
        m_impl->state.chart.shapes |
        std::views::transform(&common::core::ShapeViewState::drawn_end_seconds));
    m_impl->node_series = common::core::makeHighwayNodeSeries(m_impl->state.chart.notes);
    m_impl->max_fhp_ramp_seconds = 0.0;
    for (const common::core::FhpViewState& fhp : m_impl->state.chart.fret_hand_positions)
    {
        m_impl->max_fhp_ramp_seconds = std::max(m_impl->max_fhp_ramp_seconds, fhp.ramp_seconds);
    }
    m_impl->tap_end_prefix_max = common::core::makeSustainPrefixMax(
        m_impl->state.tap_onsets |
        std::views::transform([](const common::core::HighwayTapOnsetViewState& tap) {
            return tap.path.back().seconds;
        }));
    m_impl->max_tap_ramp_seconds = 0.0;
    for (const common::core::HighwayTapOnsetViewState& tap : m_impl->state.tap_onsets)
    {
        m_impl->max_tap_ramp_seconds = std::max(m_impl->max_tap_ramp_seconds, tap.ramp_seconds);
    }
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
    if (displayed_count <= 0)
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
    for (int string = 1; string <= displayed_count; ++string)
    {
        const double y = common::core::highwayStringLaneY(string, displayed_count, metrics, invert);
        const StringLaneStyle style{stringLaneColor(string, displayed_count, palette)};
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
    bgfx::setViewTransform(g_board_view, board_matrix.data(), nullptr);
    // Clear-bearing views execute only when they have items; touch them so the clears always run.
    bgfx::touch(g_background_view);
    bgfx::touch(g_board_view);

    scratch.clearForFrame();
    const bool mirrored = state.options.mirrored;
    const bool invert = state.options.invert_string_order;
    const double span_end_seconds =
        now_seconds + (metrics.visibility_window_seconds * scroll_speed);
    const double span_start_seconds = now_seconds - g_passed_fade_seconds;
    const StringColorPalette& palette = charterClassicPalette();

    // One FALLOFF is shared by every lit subject — fretted heads, open strings, and chord box
    // frames — and exactly two numbers differ per subject. The EMITTER DEPTH separates a solid
    // object from a frame: a note is lit from behind, so its whole interior emits; a chord box is a
    // frame the player reads notes THROUGH, so only the band one frame-thickness deep emits and the
    // interior stays dark. The RADIANCE compensates for how much of each subject ends up lit, which
    // the depth deliberately does not do (see g_accent_gain_boxes). One field, two dials.
    const auto glow_uniform_at = [](const double emitter_depth, const double gain) {
        return std::array<float, 4>{
            static_cast<float>(g_accent_reach),
            static_cast<float>(g_accent_exponent),
            static_cast<float>(emitter_depth),
            static_cast<float>(gain),
        };
    };
    // Gain sits beside depth because both are per-SUBJECT: one shared falloff, asked for the two
    // numbers that differ. See g_accent_gain_boxes for why the radiances differ at all.
    const std::array<float, 4> note_glow_uniform =
        glow_uniform_at(g_glow_solid_emitter_depth, g_accent_gain);
    const std::array<float, 4> box_glow_uniform =
        glow_uniform_at(metrics.string_grid_base_y, g_accent_gain_boxes);

    // Settled hand windows visible this frame: each placement owns the time range from its
    // arrival up to the next placement's ramp start (the transition itself is drawn as a
    // sampled sweep below, so settled spans shrink by the following ramp). The first
    // placement's window already holds before its arrival (the pre-held opening, matching
    // highwayHandWindowAt), so its span extends back to the span start even when the arrival
    // itself is far past the horizon. A chart with no placements gets the reference nut
    // window.
    // Arrivals ascend and a window ends at the NEXT arrival's ramp start — never later than that
    // arrival itself — so a placement whose successor arrives at or before the span start closes
    // before the span and cannot show: the walk begins one placement before the first arrival
    // past the span start. It ends at the first arrival at or past the span end, whose window
    // opens past the span, as does every later one. Index 0 is the exception at both ends: its
    // window is pre-held from the span start, so it is always a candidate when it is reached.
    const std::vector<common::core::FhpViewState>& fhps = state.chart.fret_hand_positions;
    const auto after_span_start = static_cast<std::size_t>(
        std::ranges::upper_bound(
            fhps, span_start_seconds, std::ranges::less{}, &common::core::FhpViewState::seconds) -
        fhps.begin());
    const auto at_span_end = static_cast<std::size_t>(
        std::ranges::lower_bound(
            fhps, span_end_seconds, std::ranges::less{}, &common::core::FhpViewState::seconds) -
        fhps.begin());
    const std::size_t first_window = after_span_start > 0 ? after_span_start - 1 : 0;
    const std::size_t last_window = std::min(fhps.size(), std::max(at_span_end, std::size_t{1}));
    std::vector<HandWindow>& hand_windows = scratch.hand_windows;
    for (std::size_t index = first_window; index < last_window; ++index)
    {
        const common::core::FhpViewState& fhp = fhps[index];
        const double window_start = index == 0 ? span_start_seconds : fhp.seconds;
        const double window_end = index + 1 < fhps.size()
                                      ? fhps[index + 1].seconds - fhps[index + 1].ramp_seconds
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
    if (state.chart.fret_hand_positions.empty())
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
        common::core::highwayHandWindowAt(state.chart.fret_hand_positions, now_seconds);

    // Hand-posture spans reaching the board this frame. Spans ascend by start but overlap
    // freely, so the range is visibleEventRange over the prefix maximum of their ends — the note
    // sweep's own search, on the other list that carries a span. Both shape passes (the rails and
    // the arpeggio boxes below) clamp to the same [now, span end] and each still tests its own
    // shapes, so the search is made once for both.
    const auto [first_shape, last_shape] = common::core::visibleEventRange(
        state.chart.shapes, shape_prefix_max, now_seconds, span_end_seconds);
    const auto visible_shapes = std::span<const common::core::ShapeViewState>(state.chart.shapes)
                                    .subspan(first_shape, last_shape - first_shape);

    // Every frame-scope fact the passes below read, gathered once. The fields take the locals
    // above rather than deriving anything a second time; the content scheduler that still lives
    // in this function keeps reading those locals until it is sliced out too.
    const FrameContext frame{
        .now_seconds = now_seconds,
        .span_start_seconds = span_start_seconds,
        .span_end_seconds = span_end_seconds,
        .hand_windows = hand_windows,
        .current_window = current_window,
        .visible_shapes = visible_shapes,
    };
    // The z conversion the content scheduler below still reaches for by name; it delegates to
    // the passes' own mapping rather than restating it.
    const auto time_to_z = [&](const double seconds) { return timeToZ(frame, seconds); };

    // The board's furniture, under the content. Each pass carries its own banner; the order of
    // the calls is the layering, because the board view paints in submission order.
    drawLaneBorderRibbons(frame);
    drawHandWindowLight(frame);
    drawTappingHandLight(frame);
    drawBeatBars(frame);
    drawHandShapeRails(frame);

    // --- Notes: per-note geometry batched per onset group and flushed far-to-near (see
    // flush_note_batches below). ---
    const auto [first_note, last_note] = common::core::visibleEventRange(
        state.chart.notes, sustain_prefix_max, span_start_seconds, span_end_seconds);

    std::vector<PosColorVertex>& shadow_vertices = scratch.shadow_vertices;
    std::vector<std::uint16_t>& shadow_indices = scratch.shadow_indices;
    std::vector<PosColorVertex>& rail_vertices = scratch.rail_vertices;
    std::vector<std::uint16_t>& rail_indices = scratch.rail_indices;
    std::vector<PosColorVertex>& open_vertices = scratch.open_vertices;
    std::vector<std::uint16_t>& open_indices = scratch.open_indices;
    // ONE accent-light batch for fretted heads and open strings alike, submitted before both so
    // the light sits UNDER whatever it surrounds: drawn over, it repaints the note's own pixels,
    // which was the retired atlas ring's whole problem. The two used to need separate batches
    // because a head's light was head ART and a bar's was bar GEOMETRY, so each had to slot in
    // just above its own subject. A distance field is neither — it is the same quad and the same
    // program for both — so the layering constraint collapses to "under the notes" and the second
    // batch with it.
    std::vector<PosColorGlowVertex>& accent_glow_vertices = scratch.accent_glow_vertices;
    std::vector<std::uint16_t>& accent_glow_indices = scratch.accent_glow_indices;
    std::vector<PosColorUvVertex>& head_vertices = scratch.head_vertices;
    std::vector<std::uint16_t>& head_indices = scratch.head_indices;

    std::vector<std::size_t>& visible = scratch.visible;
    visible.reserve(last_note - first_note);
    for (std::size_t index = first_note; index < last_note; ++index)
    {
        const common::core::NoteViewState& note = state.chart.notes[index];
        // A silently-held stop is not a note the board draws: it makes no sound, so it has no
        // head, no shadow, no rail and no tail. Filtered HERE, once, because every note batch
        // below iterates this one index list — the board shows what a silent hold MEANS through
        // the posture rails instead.
        //
        // The hold end, not the sustain end: a span-held strum stays drawable while its head
        // pins at the hit line long after its sustainless onset has passed.
        if (!common::core::silentHold(note.attack) && note.start_seconds <= span_end_seconds &&
            state.chart.display_hold_ends[index] >= span_start_seconds)
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
    std::vector<double>& lane_key = scratch.lane_key;
    lane_key.assign(last_note - first_note, 0.0);
    for (const std::size_t index : visible)
    {
        const common::core::NoteViewState& note = state.chart.notes[index];
        lane_key[index - first_note] =
            common::core::highwayStringLaneY(laneOf(note.string), displayed_count, metrics, invert);
    }
    // Compared with < / > only (no float equality) so the strict-weak-ordering stays clean
    // under -Wfloat-equal; ties on both real keys fall through to the unique index.
    std::ranges::sort(visible, [&](const std::size_t lhs, const std::size_t rhs) {
        const double lhs_onset = state.chart.notes[lhs].start_seconds;
        const double rhs_onset = state.chart.notes[rhs].start_seconds;
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

    // Onset groups arrive classified on the state (makeHighwayChordGroups): the repeat and
    // take-over rules look backward through the whole note stream, so the projection derives
    // them once per chart revision and this path only clamps them to its window.

    // Arpeggio bracket geometry accumulates per box PER STRING in the box pass below and
    // submits lane-dominantly inside the note pass: an upright bracket against a flat lane
    // ribbon is occluded by lane height, not time — a camera ray from above reaches the higher
    // surface first regardless of z — so each bracket glyph draws over everything on lower
    // lanes (any onset) and yields only to groups containing notes on lanes above its own. The
    // box panels stay under all notes as before.
    std::vector<BracketBatch>& bracket_batches = scratch.bracket_batches;

    // --- Chord and arpeggio boxes: Charter's translucent panels at chord onsets, plus an
    // arpeggio-styled box (the same panel with the fretboard bracket notation overlaid) at each
    // arpeggio shape's start. Drawn far-to-near BEFORE the notes so nearer content composites
    // over them (the board view is painter-ordered, no depth buffer). An arpeggio start draws
    // exactly one box — if a chord group lands there it is drawn arpeggio-style rather than a
    // second plain box — and note heads are never suppressed. Repeated/dead strums render the
    // half-height repeat box with its mute mark. ---
    {
        std::vector<PosColorVertex>& box_vertices = scratch.box_vertices;
        std::vector<std::uint16_t>& box_indices = scratch.box_indices;
        // Repeat-box mute marks render through the SDF program (see box_mute_profile.h for
        // the measured-art model). They ride a different program than the panels, and painter
        // order must hold ACROSS boxes — dense chug chains overlap heavily on screen, and
        // a far box's mark must never composite over a nearer box's panel — so the panel
        // batch flushes before each mark and the mark submits immediately. Draw-call cost is
        // bounded by the visible marked repeat boxes: tens at worst, noise for bgfx.
        std::vector<PosColorUvVertex>& box_marker_vertices = scratch.box_marker_vertices;
        std::vector<std::uint16_t>& box_marker_indices = scratch.box_marker_indices;
        // An accented box's light rides the SAME flush as the panel it lights, which puts it
        // after its own panel (so it lands ON the frame rather than behind it). Getting the other
        // half of painter order — a far box's light never washing over a NEARER box — takes an
        // extra flush at each accented box, because boxes otherwise batch across the whole loop.
        // The extra draw calls are bounded by the accented boxes on screen, which is the same
        // bound the muted repeat boxes already pay for the same reason.
        std::vector<PosColorGlowVertex>& box_glow_vertices = scratch.box_glow_vertices;
        std::vector<std::uint16_t>& box_glow_indices = scratch.box_glow_indices;
        const auto flush_box_panels = [&] {
            submitBatch(box_vertices, box_indices, posColorLayout(), color_program.get(), nullptr);
            box_vertices.clear();
            box_indices.clear();
            // The same program, uniform and blend operator a note's accent uses — a box and a
            // note are one look, not two that happen to be on at once.
            //
            // OVER the panel, where a note's light goes under its head. The two are not
            // inconsistent: a head is opaque, so light beneath it is the only light that shows,
            // while a frame bar is the thing that has to EMIT — put the light under it and the
            // bar's own paint covers exactly the pixels the light was for. Boxes draw behind the
            // notes either way, so this never puts box light on top of a head.
            bgfx::setUniform(accent_glow_params.get(), box_glow_uniform.data());
            submitBatch(
                box_glow_vertices,
                box_glow_indices,
                posColorGlowLayout(),
                accent_glow_program.get(),
                nullptr,
                g_board_view,
                g_glow_add_state);
            box_glow_vertices.clear();
            box_glow_indices.clear();
        };
        // Boxes rise exactly to the fret-line top: any higher and the panel visibly pokes past
        // the fret grid (the old top added half a string distance).
        const double full_height_y1 = faceTopY();

        // Overlays one arpeggio shape's posture brackets (the fretboard notation) at a box's z:
        // a bracket per fretted string, or the window-end brackets for an open string. Window
        // edges arrive fractional mid-transition, so the open brackets center on the edge lanes
        // through the fractional fret-line map.
        const auto push_arpeggio_brackets = [&](const common::core::ShapeViewState& shape,
                                                const double z,
                                                const double low_line,
                                                const double high_line) {
            // Square glyph art at the family size: brackets hold their shape from the height
            // metric rather than squashing with a head narrower than tall.
            const double half = metrics.note_half_height;
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
            for (const common::core::ShapeStringViewState& entry : shape.strings)
            {
                const int lane = laneOf(entry.string);
                const double y =
                    common::core::highwayStringLaneY(lane, displayed_count, metrics, invert);
                const std::uint32_t tint =
                    packAbgr(stringLaneColor(lane, displayed_count, palette));
                // One lane-tagged batch per posture string, so the note pass can order each
                // glyph against note content by lane height; the span window scopes which
                // notes can force the glyph underneath them.
                bracket_batches.push_back(
                    BracketBatch{
                        .lane = invert ? (displayed_count + 1 - lane) : lane,
                        .span_start_seconds = shape.start_seconds,
                        .span_end_seconds = shape.drawn_end_seconds,
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
        std::vector<BoxDraw>& boxes = scratch.boxes;
        for (const common::core::ShapeViewState& shape : visible_shapes)
        {
            // WHERE the posture mark draws, from the projection rather than from the span's start
            // ([D2] amendment 2): a landing-opened span states nothing at its landing, so it draws
            // no mark there and defers to its first interior sounding — and one that never sounds
            // interiorly draws none at all. Bound to a local so the presence test and every read
            // below are provably the same object.
            const std::optional<double>& mark = shape.bracket_seconds;
            if (!shape.arpeggio || !mark.has_value() || shape.drawn_end_seconds < now_seconds)
            {
                continue;
            }
            const double bracket_seconds = *mark;
            if (bracket_seconds > span_end_seconds)
            {
                continue;
            }
            // An arpeggio box carries emphasis only by INHERITANCE, never on its own account: it
            // is a posture, and a hand holding a shape is not struck. But where a strummed chord
            // lands where this mark draws, this box replaces that chord's box (the coincidence
            // test below skips the chord), so it must say what the box it replaced would have
            // said. A single accented note inside the arpeggio is not a strum and does not glow
            // the box — its own head already states it.
            // Groups ascend by onset: binary-search the epsilon neighbourhood and keep the
            // strict test as the authority inside it.
            const auto group_candidates = std::ranges::subrange(
                std::ranges::lower_bound(
                    state.chord_groups,
                    bracket_seconds - g_onset_match_epsilon,
                    std::ranges::less{},
                    &common::core::HighwayChordGroupViewState::start_seconds),
                std::ranges::upper_bound(
                    state.chord_groups,
                    bracket_seconds + g_onset_match_epsilon,
                    std::ranges::less{},
                    &common::core::HighwayChordGroupViewState::start_seconds));
            const auto struck_group = std::ranges::find_if(
                group_candidates, [&](const common::core::HighwayChordGroupViewState& group) {
                    return group.box_treatment != common::core::HighwayChordBoxTreatment::None &&
                           std::abs(group.start_seconds - bracket_seconds) < g_onset_match_epsilon;
                });
            boxes.push_back(
                BoxDraw{
                    .start_seconds = bracket_seconds,
                    .box_only = false,
                    // Charter's chord-box rule (3+ sounding strings get the top bar),
                    // counted from the arpeggio's posture strings.
                    .with_top = shape.strings.size() > 2,
                    .emphasis = struck_group != group_candidates.end()
                                    ? struck_group->emphasis
                                    : common::core::NoteEmphasis::Normal,
                    .palm_mute = false,
                    .dead = false,
                    .arpeggio_shape = &shape,
                    .tap = nullptr,
                    .build_index = boxes.size(),
                });
        }
        // Every posture mark drawn this frame is now in `boxes`, and only those: the plain-box
        // loop below reads this prefix to know which onsets an arpeggio mark already speaks for.
        const std::ptrdiff_t arpeggio_boxes = static_cast<std::ptrdiff_t>(boxes.size());
        // Groups ascend by onset, so the window clamp is a binary search over the state's
        // whole-song list rather than a per-group test.
        const auto boxed_groups = std::ranges::subrange(
            std::ranges::lower_bound(
                state.chord_groups,
                now_seconds,
                std::ranges::less{},
                &common::core::HighwayChordGroupViewState::start_seconds),
            std::ranges::upper_bound(
                state.chord_groups,
                span_end_seconds,
                std::ranges::less{},
                &common::core::HighwayChordGroupViewState::start_seconds));
        for (const common::core::HighwayChordGroupViewState& group : boxed_groups)
        {
            // Which box this strum draws — or that it draws none — is the projection's one answer
            // (common::core::HighwayChordBoxTreatment), never a count re-read here. A box marks
            // SIMULTANEITY (LAW IV, amended 2026-08-29), so only a group with fewer than two
            // fretting-hand members arrives here as None.
            if (group.box_treatment == common::core::HighwayChordBoxTreatment::None)
            {
                continue;
            }
            // Asked of the arpeggio boxes THIS FRAME ALREADY BUILT rather than of the shape list
            // again, and that is not an optimization: a posture mark no longer draws at its span's
            // start ([D2] amendment 2), so a search keyed on span starts would look in the wrong
            // place for a deferred one. The boxes pushed above are exactly the marks that draw, at
            // exactly the instants they draw at, so comparing against them cannot go stale. It is a
            // scan rather than a search because the arpeggio marks visible at once are a handful,
            // and the old form searched the whole song's shape list per group.
            //
            // Not the same question as the group's own \ref arpeggio_mark, which the strike glow
            // reads: that says a mark STANDS at this onset, whole-song and window-free, while this
            // says one DRAWS here in this frame. Suppression owes the stricter answer — a box
            // suppressed for a mark the frame did not build would leave the onset with nothing
            // drawn at all — so the two stay separate on purpose rather than by oversight.
            const bool coincides_with_arpeggio = std::ranges::any_of(
                std::ranges::subrange(boxes.begin(), boxes.begin() + arpeggio_boxes),
                [&](const BoxDraw& box) {
                    return std::abs(box.start_seconds - group.start_seconds) <
                           g_onset_match_epsilon;
                });
            if (coincides_with_arpeggio)
            {
                continue;
            }
            boxes.push_back(
                BoxDraw{
                    .start_seconds = group.start_seconds,
                    .box_only =
                        group.box_treatment == common::core::HighwayChordBoxTreatment::Repeat,
                    .with_top = group.fretting_hand_count > 2,
                    .emphasis = group.emphasis,
                    .palm_mute = group.all_palm_muted,
                    .dead = group.all_dead,
                    .arpeggio_shape = nullptr,
                    .tap = nullptr,
                    .build_index = boxes.size(),
                });
        }
        // Tapped chord boxes (right-hand-tap-lighting plan): two or more taps struck together
        // get their own box on the taps' fret extent — the tapping hand's counterpart of the
        // strummed box. Derived per onset; no repeat-box chain (taps are percussive). A box sits
        // AT its onset with no envelope around it, so onsets ascending makes the two time skips
        // the ends of a binary-searched range, exactly like the strummed groups' clamp above.
        for (const common::core::HighwayTapOnsetViewState& tap : std::ranges::subrange(
                 std::ranges::lower_bound(
                     state.tap_onsets,
                     now_seconds,
                     std::ranges::less{},
                     &common::core::HighwayTapOnsetViewState::seconds),
                 std::ranges::upper_bound(
                     state.tap_onsets,
                     span_end_seconds,
                     std::ranges::less{},
                     &common::core::HighwayTapOnsetViewState::seconds)))
        {
            if (tap.count < 2)
            {
                continue;
            }
            boxes.push_back(
                BoxDraw{
                    .start_seconds = tap.seconds,
                    .box_only = false,
                    .with_top = tap.count > 2,
                    // A tapped box has no strummed group behind it to read an emphasis from; the
                    // taps state their own on their heads.
                    .emphasis = common::core::NoteEmphasis::Normal,
                    .palm_mute = false,
                    .dead = false,
                    .arpeggio_shape = nullptr,
                    .tap = &tap,
                    .build_index = boxes.size(),
                });
        }
        // The build-index tiebreak makes the order total, the same answer the note sweep reached:
        // two boxes at one onset (a tap-and-strum instant emits a plain box and a tapped box)
        // would otherwise order their overlapping panels by the pivot sequence, which flickers as
        // other boxes scroll in and out. A tiebreak beats stable_sort here because stable_sort
        // allocates its merge buffer inside the per-frame draw path.
        std::ranges::sort(boxes, [](const BoxDraw& lhs, const BoxDraw& rhs) {
            if (std::is_neq(lhs.start_seconds <=> rhs.start_seconds))
            {
                return lhs.start_seconds > rhs.start_seconds;
            }
            return lhs.build_index < rhs.build_index;
        });

        // ONE of the two SDF-rendered mute marks over its repeat panel's interior — the rect
        // between the frame's inner edges, so the mark stops exactly at the borders instead of
        // covering them. `dead_mark` selects WHICH mark this call lays down, not what the strum
        // is: the caller draws one per flag its strum is unanimous in, so a box that is both
        // wears both, stacked exactly as the note heads stack theirs. The quad covers that
        // interior exactly, texcoord carries interior-local world-unit offsets, and the fragment
        // shader samples chords.png's measured cross-section by exact distance from the arm
        // centerlines — the texture defines the structure, the shader lays the arms out, and the
        // vertex color supplies hue and opacity. Submits immediately so painter order holds
        // across boxes AND across a stacked pair — see the batch comment above.
        const auto push_box_mute_marker = [&](const double x0,
                                              const double x1,
                                              const double y0,
                                              const double y1,
                                              const double z,
                                              const bool dead_mark) {
            const double half_x = (x1 - x0) / 2.0;
            const double half_y = (y1 - y0) / 2.0;
            const double middle_x = (x0 + x1) / 2.0;
            const double middle_y = (y0 + y1) / 2.0;
            const BoxMuteLayout& profile =
                dead_mark ? box_mute_layouts.full : box_mute_layouts.palm;
            // Both marks span the full interior height. The palm X runs border-less edge to
            // edge: arms corner-to-corner of the interior, with the clip rect pushed past the
            // quad by the ramp extent so the quad slices the arms mid-stroke at the frame's
            // inner edges. The full X keeps a square footprint the height of the interior, tips
            // wrapped by the note art's squared corners, its top and bottom stroke edges
            // meeting the frame exactly (the sub-pixel antialiasing tail past them is cut by
            // the quad).
            const double glyph_height = 2.0 * half_y;
            const double arm_half_x = dead_mark ? half_y : half_x;
            const double overshoot = dead_mark ? 0.0 : profile.extent_fraction * glyph_height;
            const double arm_length = std::sqrt((arm_half_x * arm_half_x) + (half_y * half_y));
            const auto params = std::array<float, 4>{
                static_cast<float>(arm_half_x + overshoot),
                static_cast<float>(half_y + overshoot),
                static_cast<float>(profile.stroke_half_fraction * glyph_height),
                static_cast<float>(profile.extent_fraction * glyph_height),
            };
            // The ramp rows sit at v = 0.25 (palm) and 0.75 (full) of the two-row texture;
            // the shader's rect clip alone ends the arms, so a full-mute tip lands as the
            // note art's squared corner and a palm-mute tip is the quad's raw cut.
            const auto arms = std::array<float, 4>{
                static_cast<float>(arm_half_x / arm_length),
                static_cast<float>(half_y / arm_length),
                0.0F,
                dead_mark ? 0.75F : 0.25F,
            };
            bgfx::setUniform(box_mute_params.get(), params.data());
            bgfx::setUniform(box_mute_arms.get(), arms.data());
            // The quad covers the interior exactly with no margin: the full mark dissolves
            // inside it and the palm mark is sliced by it. The art carries structure only, so the
            // vertex color passed here supplies BOTH the mark's hue and its opacity (the shader
            // weights the tint by the art's R and scales this alpha by the art's coverage). The
            // palm mark wears the frame's own colors at the frame's own alpha and rides the
            // frame's end-to-middle fade (pushMiddleFadedQuads, shared with the bars), so its rim
            // is the same expression as the border it meets; the full mark is a flat opaque tint.
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
            if (dead_mark)
            {
                const std::uint32_t tint = packAbgr(g_full_mute_mark_color);
                pushQuad(
                    box_marker_vertices,
                    box_marker_indices,
                    make_corner(x0, y0, tint),
                    make_corner(x1, y0, tint),
                    make_corner(x1, y1, tint),
                    make_corner(x0, y1, tint));
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
                    packAbgr(g_chord_box_color, g_chord_box_frame_alpha),
                    packAbgr(g_chord_box_dark_color, g_chord_box_frame_alpha),
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
            // The box carries the strum's dynamics the same way its notes carry theirs — quiet
            // takes its presence down, loud adds light around it — so a repeat box, which draws
            // NO heads, still states the axis, and a plain box states it in parity with the
            // repeat it may become.
            const double box_alpha = emphasisAlpha(box.emphasis);
            // An accented box takes the SAME light a note takes: one glow around the frame's
            // outer rectangle, spending the light's reach outward onto the dark board and inward
            // across the bars themselves. Nothing here is the box's own number, so the box moves
            // with any accent retune by construction — it once carried constants of its own and
            // visibly failed to follow.
            //
            // Why ONE field rather than a piece per side, which is what this replaces: four
            // independent ramps have no radial term, so out past a corner the top cap carried its
            // full alpha where the sides had already decayed to zero. That was the reported hard
            // cut, and it is not a tuning error — it is what per-side ramps DO. A distance field
            // has one boundary and one falloff, so there is no corner for two pieces to disagree
            // at.
            //
            // The measurement that still governs the size: the frame bar is 0.075 world thick,
            // which projects to 0.7 px at the horizon and 2.3 px a third of a second out (half
            // that again in the editor preview). Lighting the bar can only make a hairline
            // brighter, so the light has to have somewhere to go — which is why the reach is
            // absolute world units shared with the notes rather than a fraction of the box.
            const auto push_box_accent_light = [&](const double light_x0, const double light_x1) {
                if (!common::core::isAccented(box.emphasis))
                {
                    return;
                }
                const ChordBoxFrame box_frame = chordBoxFrame(
                    full_height_y1, box.box_only, box.with_top, metrics.string_grid_base_y);
                // The box's own teal, given the same broadband pedestal a string's light gets.
                // It needed a hand-tuned white lift of its own before the shared spectrum
                // existed, because teal light laid on a teal frame is the least perceptible
                // change available; the box now shares the note light's spectrum outright and
                // carries no number of its own.
                const std::uint32_t lit = packAbgr(emitterSpectrum(g_chord_box_color), 1.0);
                const double half_w = (light_x1 - light_x0) / 2.0;
                const double center_x = (light_x0 + light_x1) / 2.0;

                if (box_frame.closed_top)
                {
                    // Closed rectangle: one quad, one field, bottom edge on the floor.
                    pushAccentGlow(
                        box_glow_vertices,
                        box_glow_indices,
                        center_x,
                        box_frame.outer_top / 2.0,
                        z,
                        GlowShape{
                            .half_w = half_w,
                            .half_h = box_frame.outer_top / 2.0,
                            .corner = 0.0,
                            .rhombus = false,
                        },
                        g_accent_reach,
                        lit,
                        1.0,
                        0.0,
                        -box_frame.outer_top / 2.0);
                    return;
                }

                // No top bar: the columns fade out from their midpoint, so the light must have no
                // top edge either. The rectangle is sized so its BOTTOM edge sits on the floor —
                // the bottom bar is real and has to glow — while its TOP edge lands one reach
                // above the drawn quad. A frame-depth emitter reaches depth + reach inward, so a
                // sliver of the phantom top face's light does graze the quad's upper band; the
                // vertex fade below is what kills it (peak weight under one count of 255), which
                // is why the sizing stays this simple. Left, right and bottom register; the top
                // never reads. The vertical fade is then carried in VERTEX alpha across the same
                // span the columns fade over, read from the same derivation they read it from.
                const double open_half_h = (box_frame.side_y1 + g_accent_reach) / 2.0;
                const GlowShape open_shape{
                    .half_w = half_w,
                    .half_h = open_half_h,
                    .corner = 0.0,
                    .rhombus = false,
                };
                const std::uint32_t clear = packAbgr(g_chord_box_color, 0.0);
                const double out_w = half_w + g_accent_reach;
                const auto push_span = [&](const double y_low,
                                           const double y_high,
                                           const std::uint32_t a_low,
                                           const std::uint32_t a_high) {
                    const auto at =
                        [&](const double side_x, const double y, const std::uint32_t abgr) {
                            return makeGlowVertex(
                                center_x + side_x, y, z, abgr, side_x, y - open_half_h, open_shape);
                        };
                    pushQuad(
                        box_glow_vertices,
                        box_glow_indices,
                        at(-out_w, y_low, a_low),
                        at(out_w, y_low, a_low),
                        at(out_w, y_high, a_high),
                        at(-out_w, y_high, a_high));
                };
                push_span(0.0, box_frame.fade_start_y, lit, lit);
                push_span(box_frame.fade_start_y, box_frame.side_y1, lit, clear);
            };
            if (box.tap != nullptr)
            {
                // A tapped box spans the taps' own fret slots — their derived right-hand
                // window — not the fretting hand's.
                const double tap_low =
                    common::core::highwayFretLineX(box.tap->fret_low - 1, metrics, mirrored);
                const double tap_high =
                    common::core::highwayFretLineX(box.tap->fret_high, metrics, mirrored);
                push_box_accent_light(std::min(tap_low, tap_high), std::max(tap_low, tap_high));
                pushChordBoxPanel(
                    box_vertices,
                    box_indices,
                    std::min(tap_low, tap_high),
                    std::max(tap_low, tap_high),
                    z,
                    full_height_y1,
                    box.box_only,
                    box.with_top,
                    box_alpha,
                    metrics.string_grid_base_y);
                if (common::core::isAccented(box.emphasis))
                {
                    flush_box_panels();
                }
                continue;
            }
            // Display-time window: an approaching box takes the window at its own onset instant,
            // and a box riding the hit line re-evaluates per frame, so a held arpeggio slides
            // along with the chord sliding under it instead of staying frozen at its onset
            // window.
            const double window_seconds = std::max(box.start_seconds, now_seconds);
            const common::core::HighwayHandWindow window =
                common::core::highwayHandWindowAt(state.chart.fret_hand_positions, window_seconds);
            const auto [x0, x1] = handWindowXAt(state, window_seconds, metrics, mirrored);
            push_box_accent_light(x0, x1);
            pushChordBoxPanel(
                box_vertices,
                box_indices,
                x0,
                x1,
                z,
                full_height_y1,
                box.box_only,
                box.with_top,
                box_alpha,
                metrics.string_grid_base_y);
            if (common::core::isAccented(box.emphasis))
            {
                flush_box_panels();
            }
            if (box.box_only && common::core::isMuted(box.palm_mute, box.dead))
            {
                flush_box_panels();
                // The frame's inner edges bound the mark (pushChordBoxPanel geometry): the
                // bottom bar tops out one frame thickness up, the side columns end one
                // thickness inside, and the interior fill rises to the frame's side_y1 — asked
                // from chordBoxFrame so the mark can never disagree with the panel it rides.
                const double thickness = metrics.string_grid_base_y;
                const ChordBoxFrame mark_frame =
                    chordBoxFrame(full_height_y1, box.box_only, box.with_top, thickness);
                // Two flags, two marks, each drawn from its own flag with no precedence between
                // them: a strum unanimous in both wears the palm mark with the dead mark over it,
                // the same stack and the same order the note heads draw (palm rotating with the
                // head, the dead X upright over it). Each marker submits its own batch, so the
                // order here IS the paint order; the board writes no depth, so the second mark is
                // not depth-rejected at the first one's z.
                if (box.palm_mute)
                {
                    push_box_mute_marker(
                        x0 + thickness, x1 - thickness, thickness, mark_frame.side_y1, z, false);
                }
                if (box.dead)
                {
                    push_box_mute_marker(
                        x0 + thickness, x1 - thickness, thickness, mark_frame.side_y1, z, true);
                }
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
    // Charter's head is a square quad (0.96 x 0.96 world units), not a lane-squashed one.
    const double head_half_w = metrics.note_half_width;
    const double head_half_h = metrics.note_half_height;

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

    // The six per-note batches flush per onset group, far-to-near: the board view is
    // painter-ordered with no depth writes, so one global submit per category would let a
    // distant head or open bar composite over a nearer note's sustain tail (the depth-order
    // bug this replaces). Within a group the categories keep Charter's layering: shadows
    // under rails under open bars under heads.
    const bgfx::TextureHandle heads_texture = atlases.heads.get();
    const auto flush_note_batches = [&] {
        // The shadow batch is floor furniture (span lines, glow posts, open-bar corner Ls), so
        // it takes the floor's distance fade near the board face like every other floor
        // element; heads, rails, and open bars are gameplay content and stay opaque.
        setFadeUniform();
        submitBatch(
            shadow_vertices, shadow_indices, posColorLayout(), color_fade_program.get(), nullptr);
        // The accent light, under every note of the group — and now under the RAILS too, which is
        // why it submits before them: once the light reaches a sustain ribbon it has to sit behind
        // the ribbon like it sits behind a head, or it washes out the very thing it is lighting.
        bgfx::setUniform(accent_glow_params.get(), note_glow_uniform.data());
        submitBatch(
            accent_glow_vertices,
            accent_glow_indices,
            posColorGlowLayout(),
            accent_glow_program.get(),
            nullptr,
            g_board_view,
            g_glow_add_state);
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
        accent_glow_vertices.clear();
        accent_glow_indices.clear();
        head_vertices.clear();
        head_indices.clear();
    };
    std::size_t batched_group = state.chord_groups.size();

    // EVERY technique marker layers above EVERY head of its onset group. The notes of one onset
    // push lane-ascending into a single batch, so a marker written inline from a lower lane is
    // overdrawn by a higher groupmate's head — which reads as a neighbouring note's head covering
    // the mark that sits ON another head, and becomes obvious as soon as a marker is wide enough
    // to cross into the next lane. Markers therefore collect here during the group and append to
    // the head batch at the group boundary. That gives all-heads-then-all-markers WITHIN the
    // onset, while marker-over-marker still follows lane order, and the group's markers still sit
    // under nearer groups' flushes so depth ordering across onsets is untouched — deferring
    // globally instead would let a distant note's marker paint over a near note's head.
    std::vector<PendingMarker>& pending_markers = scratch.pending_markers;
    const auto emit_pending_markers = [&] {
        for (const PendingMarker& marker : pending_markers)
        {
            const std::array<float, 4> rect = atlases.head_layout.cellRect(marker.cell);
            // Swapping the cell's vertical texture coordinates mirrors the art: the pull-off IS
            // the hammer-on upside down, so the pair shares one cell.
            const float v_low = marker.flip_v ? rect[1] : rect[3];
            const float v_high = marker.flip_v ? rect[3] : rect[1];
            const auto corner =
                [&](const double dx, const double dy, const float u, const float v) {
                    return makeUvVertex(
                        marker.x + (dx * marker.cos_r) - (dy * marker.sin_r),
                        marker.y + (dx * marker.sin_r) + (dy * marker.cos_r),
                        marker.z,
                        marker.tint,
                        u,
                        v);
                };
            pushQuad(
                head_vertices,
                head_indices,
                corner(-marker.half_w, -marker.half_h, rect[0], v_low),
                corner(marker.half_w, -marker.half_h, rect[2], v_low),
                corner(marker.half_w, marker.half_h, rect[2], v_high),
                corner(-marker.half_w, marker.half_h, rect[0], v_high));
        }
        pending_markers.clear();
    };
    // Lane-dominant bracket submission at NOTE granularity (groups can hold lanes on both
    // sides of a glyph, so group-level slotting let a low open tail ride its higher groupmate
    // over the notation). A bracket glyph stays pending — compositing over every lower lane's
    // tails and heads, whatever their onsets — until the note about to draw sits on a lane
    // ABOVE the glyph AND overlaps its span; the batches flush and the glyph submits underneath
    // that note, mid-group when that is where the lane boundary falls (in-group notes iterate
    // lane-ascending, so lower lanes are already batched). Never-triggered glyphs drain after
    // the last group.
    const auto submit_brackets_below = [&](const common::core::NoteViewState& note) {
        const int lane = laneOf(note.string);
        const int note_lane = invert ? (displayed_count + 1 - lane) : lane;
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

    // --- Scrolling fret numbers: Charter's readability aid. Numbers ride the board floor
    // at each dotted fret on every measure downbeat (bright inside the current hand range, dim
    // elsewhere), mark each upcoming hand-position arrival in orange, and pin the current
    // hand's numbers at the hit line; all fade in as they approach. One ordering rule for every
    // floor number, blue or orange: a number joins the far-to-near note sweep at its own time,
    // draining just before the first onset group nearer than it — so any note struck earlier
    // than the number, truly nearer in 3D, paints over it, while the number paints over every
    // note at or behind its time (equal-time numbers drain after that group flushes, keeping
    // number-over-note on ties). Numbers still draw before the board face, whose fret lines and
    // skin keep occluding numbers scrolling in behind it (numbers popping through the fretboard
    // would read as a depth violation). ---
    std::vector<FloorNumber>& floor_numbers = scratch.floor_numbers;
    {
        // How deeply a fret's whole lane sits inside a window: the min of its two lines'
        // coverages — the shared signal the number fades and color blends follow.
        const auto fret_coverage = [](const common::core::HighwayHandWindow& window,
                                      const int fret) {
            return std::min(
                common::core::highwayHandWindowLineCoverage(window, static_cast<double>(fret - 1)),
                common::core::highwayHandWindowLineCoverage(window, static_cast<double>(fret)));
        };

        // A natural harmonic states its NODE — the first of a repeated series only (user rule
        // 2026-08-15). The fretting finger STANDS on the node, so a new node is a hand position
        // being established under the same one-rule model, and the decimal is the information:
        // position alone does not tell the player 2.3 from 2.4. The series themselves are chart
        // truth, derived once per revision (makeHighwayNodeSeries, stored beside
        // sustain_prefix_max), so this site only emits the labels for series establishing inside
        // the window; the spans also SUPPRESS the dotted-fret downbeat numbers on the node's own
        // fret below — two numbers in one slot muddy each other, and the node's is the one with
        // the information.
        for (const common::core::HighwayNodeSeries& series : std::ranges::subrange(
                 std::ranges::upper_bound(
                     node_series,
                     now_seconds,
                     std::ranges::less{},
                     &common::core::HighwayNodeSeries::begin_seconds),
                 std::ranges::upper_bound(
                     node_series,
                     span_end_seconds,
                     std::ranges::less{},
                     &common::core::HighwayNodeSeries::begin_seconds)))
        {
            floor_numbers.push_back(
                FloorNumber{
                    .seconds = series.begin_seconds,
                    .z = time_to_z(series.begin_seconds),
                    .fret = series.fret,
                    .node = series.node,
                    .base = g_fret_number_fhp_color,
                    .fade = true,
                    .alpha = 1.0,
                });
        }
        const auto node_suppresses = [&](const int fret, const double seconds) {
            // Series ascend by begin and their ends are likewise non-decreasing, so every span
            // containing `seconds` sits contiguously just before the first later-starting one.
            auto it = std::ranges::upper_bound(
                node_series,
                seconds,
                std::ranges::less{},
                &common::core::HighwayNodeSeries::begin_seconds);
            while (it != node_series.begin())
            {
                --it;
                if (it->end_seconds < seconds)
                {
                    return false;
                }
                if (it->fret == fret)
                {
                    return true;
                }
            }
            return false;
        };

        // Dotted-fret numbers on each visible measure downbeat, lit within the hand range (a
        // downbeat mid-transition blends the dim and active colors by its coverage). A downbeat
        // inside a harmonic series' span yields its number on the node's own fret.
        for (const common::core::HighwayBeatViewState& beat : std::ranges::subrange(
                 std::ranges::lower_bound(
                     state.beats,
                     now_seconds - 0.2,
                     std::ranges::less{},
                     &common::core::HighwayBeatViewState::seconds),
                 std::ranges::upper_bound(
                     state.beats,
                     span_end_seconds,
                     std::ranges::less{},
                     &common::core::HighwayBeatViewState::seconds)))
        {
            if (!beat.measure_downbeat)
            {
                continue;
            }
            const common::core::HighwayHandWindow beat_window =
                common::core::highwayHandWindowAt(state.chart.fret_hand_positions, beat.seconds);
            const double z = time_to_z(beat.seconds);
            for (int fret = 1; fret <= g_face_fret_count; ++fret)
            {
                if (!isDottedFret(fret) || node_suppresses(fret, beat.seconds))
                {
                    continue;
                }
                const double coverage = fret_coverage(beat_window, fret);
                floor_numbers.push_back(
                    FloorNumber{
                        .seconds = beat.seconds,
                        .z = z,
                        .fret = fret,
                        .base =
                            mixArgb(g_fret_number_dim_color, g_fret_number_active_color, coverage),
                        .fade = true,
                        .alpha = 1.0,
                    });
            }
        }

        // An upcoming floor target — a hand-position arrival, a tapped note, or a pitched slide
        // keyframe — gets the same orange number at its fret slot, fading in on approach. One
        // push owns the window gate and the argument bundle so the borrowed treatments can
        // never drift apart.
        const auto push_target_number = [&](const int fret, const double seconds) {
            if (seconds <= now_seconds || seconds > span_end_seconds)
            {
                return;
            }
            floor_numbers.push_back(
                FloorNumber{
                    .seconds = seconds,
                    .z = time_to_z(seconds),
                    .fret = fret,
                    .base = g_fret_number_fhp_color,
                    .fade = true,
                    .alpha = 1.0,
                });
        };

        // Upcoming hand-position arrivals, in the FHP orange. An arrival on a harmonic series'
        // own fret yields to the node number — the placement exists BECAUSE the hand goes to the
        // node, so the decimal label already states everything the integer would, and more.
        for (const common::core::FhpViewState& fhp : std::ranges::subrange(
                 std::ranges::upper_bound(
                     state.chart.fret_hand_positions,
                     now_seconds,
                     std::ranges::less{},
                     &common::core::FhpViewState::seconds),
                 std::ranges::upper_bound(
                     state.chart.fret_hand_positions,
                     span_end_seconds,
                     std::ranges::less{},
                     &common::core::FhpViewState::seconds)))
        {
            if (!node_suppresses(fhp.fret, fhp.seconds))
            {
                push_target_number(fhp.fret, fhp.seconds);
            }
        }

        // Tap numbers label POSITIONS, not notes (per-note numbers over-labeled dense runs):
        // one number per tap onset, at its low fret like a hand-position arrival, and only when
        // it tells the player something new — the first tap after the lighting lapsed, or a
        // move away from the position the previous tap's path LANDED in. A repeat inside a lit
        // run (the previous release plus the ribbon decay still bridging this rise — the same
        // bridge the lane edges show) is already established and stays unlabeled, as are chord
        // upper members. A tapped glide then establishes each landing as its own new position
        // (matching the placements a fretting-hand glide carries at its targets): every path
        // station that changes the extent gets an arrival number of its own.
        //
        // The one scan here that carries state across its subjects: `previous_tap` decides
        // whether this onset repeats an established position, so the walk cannot simply start
        // inside the window. It starts at the first onset that can push anything — every number
        // it pushes sits at the onset or at a later path station, and push_target_number drops
        // everything at or before now, so an onset whose whole path has passed pushes nothing —
        // and seeds the carry from the onset immediately before it, which is exactly what the
        // full walk would have held on arriving there. Ending the walk needs no carry at all:
        // past the range's far bound every station is past the span end, and that bound is the
        // looser padded one, so nothing that could still push a number is cut.
        const auto [first_tap, last_tap] = litTapOnsetRange(now_seconds, span_end_seconds, 0.0);
        const common::core::HighwayTapOnsetViewState* previous_tap =
            first_tap > 0 ? &state.tap_onsets[first_tap - 1] : nullptr;
        for (const common::core::HighwayTapOnsetViewState& tap :
             std::span<const common::core::HighwayTapOnsetViewState>(state.tap_onsets)
                 .subspan(first_tap, last_tap - first_tap))
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

        // Slide keyframes deliberately push no numbers of their own (completing the one-rule
        // model: an orange number marks a hand position being established, nothing else). A
        // fretting-hand glide that moves the window carries a hand-position placement at its
        // target (normalization rule 9), and a tapped glide's landings are labeled through the
        // path-station loop above — both hands' glides earn their numbers as POSITIONS, never
        // as keyframes. The keyframe glow posts and fret-span lines remain — they are target
        // furniture, not labels.

        // The current hand's numbers pinned at the hit line. Coverage fade: every glyph stays
        // at its own lane's fixed position and animates opacity only, fading out as the
        // sweeping border leaves its lane and in as the border reaches it. Their time is the
        // current instant, so any sounding note drains after and covers them.
        for (int fret = 1; fret <= g_face_fret_count; ++fret)
        {
            const double coverage = fret_coverage(current_window, fret);
            if (coverage > 0.0)
            {
                floor_numbers.push_back(
                    FloorNumber{
                        .seconds = now_seconds,
                        .z = 0.0,
                        .fret = fret,
                        .base = g_fret_number_fhp_color,
                        .fade = false,
                        .alpha = coverage,
                    });
            }
        }
    }
    // Far-to-near like the note sweep, with the build order as the tiebreak so same-time numbers
    // keep their authored layering (measure numbers under orange targets). A tiebreak rather than
    // stable_sort, for the reason the chord boxes already give: stable_sort allocates its merge
    // buffer inside the per-frame draw path.
    for (std::size_t index = 0; index < floor_numbers.size(); ++index)
    {
        floor_numbers[index].build_index = index;
    }
    std::ranges::sort(floor_numbers, [](const FloorNumber& lhs, const FloorNumber& rhs) {
        if (std::is_neq(lhs.seconds <=> rhs.seconds))
        {
            return lhs.seconds > rhs.seconds;
        }
        return lhs.build_index < rhs.build_index;
    });

    std::vector<PosColorUvVertex>& number_vertices = scratch.number_vertices;
    std::vector<std::uint16_t>& number_indices = scratch.number_indices;
    std::size_t next_floor_number = 0;
    const auto [number_z_faded, number_z_close] = fadeBandZ();
    // Drains every collected number strictly beyond `limit_seconds` into one glyph submit —
    // the numbers' slot in the painter order when the sweep reaches that time. Each glyph
    // billboards at its fret slot; alpha fades in between the hit line and z_close when the
    // number requests it (Charter bakes the fade into the color, since the glyph program has
    // no fade uniform), scaled by the number's own alpha (the window-coverage fades).
    const auto submit_numbers_beyond = [&](const double limit_seconds) {
        while (next_floor_number < floor_numbers.size() &&
               floor_numbers[next_floor_number].seconds > limit_seconds)
        {
            const FloorNumber& entry = floor_numbers[next_floor_number];
            ++next_floor_number;
            const double glyph_height = entry.z > 0.0 ? 0.70 : 0.40;
            const std::string label = entry.node.has_value()
                                          ? common::core::harmonicNodeText(*entry.node)
                                          : std::to_string(entry.fret);
            const double text_width = glyphTextWidth(label, glyph_height);
            const double center_x =
                entry.node.has_value()
                    ? common::core::highwayFretLineX(*entry.node, metrics, mirrored)
                    : common::core::highwayNoteCenterX(entry.fret, metrics, mirrored);
            const double left_x = center_x - (text_width / 2.0);
            double alpha_scale = entry.alpha;
            if (entry.fade && entry.z < number_z_close)
            {
                alpha_scale *= std::clamp(
                    (entry.z - number_z_faded) / (number_z_close - number_z_faded), 0.0, 1.0);
            }
            (void)pushGlyphText(
                number_vertices,
                number_indices,
                atlases.glyph_layout,
                label,
                left_x,
                -glyph_height / 2.0,
                entry.z,
                glyph_height,
                packAbgr(entry.base, alpha_scale));
        }
        if (number_vertices.empty())
        {
            return;
        }
        const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
        submitBatch(
            number_vertices,
            number_indices,
            posColorUvLayout(),
            glyph_program.get(),
            &glyph_texture);
        number_vertices.clear();
        number_indices.clear();
    };

    for (const std::size_t index : visible)
    {
        const common::core::NoteViewState& note = state.chart.notes[index];
        const std::size_t group_index = state.note_group[index];
        const common::core::HighwayChordGroupViewState& group = state.chord_groups[group_index];
        if (group.box_treatment == common::core::HighwayChordBoxTreatment::Repeat)
        {
            // A repeat box stands in for its whole strum: no heads, shadows, tails, or
            // anticipation for the group's notes.
            continue;
        }
        if (group_index != batched_group)
        {
            // Group boundary: the finished group's technique markers append last, over all its
            // heads. (The mid-group bracket flush deliberately skips this — markers keep pending
            // and ride a later batch, which still draws above the earlier one.)
            emit_pending_markers();
            flush_note_batches();
            batched_group = group_index;
            // Floor numbers beyond this group's onset take their painter slot here, under
            // this group and everything nearer.
            submit_numbers_beyond(group.start_seconds);
        }
        submit_brackets_below(note);
        const int lane = laneOf(note.string);
        const double lane_y =
            common::core::highwayStringLaneY(lane, displayed_count, metrics, invert);
        const ArgbColor base_color = stringLaneColor(lane, displayed_count, palette);
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
        const double hold_end_seconds = std::max(
            note.end_seconds,
            std::min(state.chart.display_hold_ends[index], group.hold_cap_seconds));
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
        const HighwaySlideState head_slide = highwaySlideStateAt(
            note,
            common::core::openString(note)
                ? 0.0
                : highwayNoteFretboardX(note, note.fret, metrics, mirrored),
            metrics,
            mirrored,
            head_seconds);

        // Bend geometry: highwayBentNoteY applies the lift per semitone, inverted on the upper
        // displayed half toward the roomier side, and holds the result inside the string grid --
        // the board containment this rule always claimed but did not enforce until the saturation
        // moved into the core seam. The chart-truth station is the curve's anchor-time value (a
        // pinned sounding head rides the curve with the tail centerline); an approaching pre-bent
        // head reveals that station progressively — see the reveal below.
        const int displayed_lane = invert ? (displayed_count + 1 - lane) : lane;
        const double bend_direction =
            common::core::highwayBendInverted(displayed_lane, displayed_count) ? -1.0 : 1.0;
        // The tail shows the wobble's whole swing; only the head breathes at a fraction of it.
        constexpr double full_vibrato_swing = 1.0;
        // The centerline, from the two channels that move it: the bend curve and whatever vibrato
        // region is in force. Both are read through their own core authority, so the envelope
        // that anchors a wobble on the string line lives with the wobble rather than being spelled
        // again at each sampling pass here (highwayVibratoSemitonesAt).
        const auto note_y_at = [&](const double seconds, const double depth_scale) {
            double semitones =
                common::core::highwayBendSemitonesAt(note.bend, note.start_seconds, seconds);
            semitones +=
                common::core::highwayVibratoSemitonesAt(note.vibrato, seconds, depth_scale);
            return common::core::highwayBentNoteY(
                lane_y, bend_direction < 0.0, semitones, displayed_count, metrics);
        };
        // Chart-truth head station: the curve's value at the anchor time, with the vibrato
        // swing scaled to the head's half depth — the head breathes with the wobble instead
        // of bouncing at the tail's full swing or sitting pinned, both of which read as odd.
        // A pre-bent curve is already lifted at the onset, so this sits off the lane for the
        // entire approach. A head pinned past the tail's end sits outside every region and holds
        // still, which is where the region envelope leaves it anyway.
        const double chart_head_y =
            note_y_at(head_seconds, common::core::g_highway_vibrato_head_depth_fraction);
        // Rolling-flip clock, hoisted from the head-art roll below because the pre-bend reveal
        // shares it: 0 once the art lies flat (g_flip_flat_lead_seconds before the hit line),
        // 1 at the visibility edge.
        const double roll_span_seconds = std::max(
            span_end_seconds - now_seconds - g_flip_flat_lead_seconds, g_flip_flat_lead_seconds);
        const double flip_remaining = std::clamp(
            (note.start_seconds - now_seconds - g_flip_flat_lead_seconds) / roll_span_seconds,
            0.0,
            1.0);
        // Pre-bend reveal: an approaching pre-bent head spawns on its own lane and rises toward
        // the outlined chart-truth station in step with the rolling flip, lining up exactly when
        // the art lands flat; the outline, chevron, tail, and anticipation ring hold the chart
        // truth throughout, so the rising head is the only moving element. Exact identity for
        // every non-pre-bent note: the curve is 0.0 at the onset, so chart_head_y == lane_y and
        // the mix collapses. Chords skip the roll but keep this clock, so chord pre-bends still
        // land with their groupmates.
        const double head_y = note.start_seconds > now_seconds
                                  ? lane_y + ((chart_head_y - lane_y) * (1.0 - flip_remaining))
                                  : chart_head_y;

        // Sustain tail: from the hit line (while sounding) or the onset to the sustain end, as
        // Charter's three-band ribbon (solid edges around a translucent core). Technique
        // notes modulate the centerline, sampled adaptively in screen space.
        //
        // The visible span is the shared clamp (highway_floor_geometry.h), which subsumes the three
        // conditions this used to spell out — a tail with no length, one already behind the hit
        // line, and one clamped to nothing at the horizon all report the same empty span.
        //
        // The note's own end is the whole of what bounds it, on this board and on the 2D lane
        // alike: where a FIGURE accounts for a member's whole ring, the presentation empties the
        // tail itself (the tail law, common::core::presentedChartNotes), so this draw never asks
        // about hand-shape spans and the two surfaces cannot disagree about one tail. The board
        // reads the same published verdict (common::core::NoteViewState::hidden) and draws nothing
        // of its own for it yet — the 2D lane's sighting mark is where that look is being settled.
        if (const std::optional<HighwaySpan> tail_span = highwayVisibleSpan(
                note.start_seconds, note.end_seconds, now_seconds, span_end_seconds);
            tail_span.has_value())
        {
            const double tail_from = tail_span->from;
            const double tail_to = tail_span->to;

            // The tail's alpha envelope, ramped at both ends for different reasons.
            //
            // Tip: alpha dissolves over the sustain's last fraction (the glow posts' fade,
            // mirrored), anchored to the full note duration so the fading tip stays put while
            // the hit line consumes the body.
            //
            // Onset: the tail rises from nothing over a FIXED span of its own time, the way the
            // tremolo teeth ramp in over a fixed count rather than a fraction — so the ramp
            // occupies the same stretch of board whether the note rings for a beat or for a bar.
            // It exists because a tail is brightest exactly where its own head covers it: the
            // ribbon emerges FROM the note rather than passing under it, which is both what the
            // gesture means and what stops a quieted head from showing its own tail through
            // itself.
            //
            // A ghosted note's ribbon quiets with its head, at the one ghost alpha: a
            // full-strength tail under a quieted head reads as a rendering fault rather than as
            // a note played softly.
            const double duration = note.end_seconds - note.start_seconds;
            const double ghost_tail_alpha = emphasisAlpha(note.emphasis);
            // ...and the loud end lights it, for the same reason and on the same surface. An
            // accent that stopped at the head made the axis say different things at its two ends.
            const bool tail_lit = common::core::isAccented(note.emphasis);
            const auto tip_alpha = [&](const double seconds) {
                const double tip =
                    (note.end_seconds - seconds) / (duration * g_tail_tip_fade_fraction);
                const double onset = (seconds - note.start_seconds) / g_tail_onset_fade_seconds;
                return ghost_tail_alpha * std::clamp(std::min(tip, onset), 0.0, 1.0);
            };

            // Band X stations. The OUTER pair is the shared floor footprint
            // (highwayFloorFootprint): a fretted tail straddling the note's own fretboard anchor
            // at the tail half-width, an open one spanning the hand window inset by its margin —
            // the anchor and the margin live in that one place so any further floor mark lands
            // between the same two numbers rather than restating them. The INNER
            // pair is Charter's cross-section and stays per case: a fretted ribbon splits its
            // width quarter/half/quarter, while an open one keeps edge bands one margin wide
            // across the whole window.
            const auto band_stations = [&](const HighwayFloorFootprint& footprint) {
                const double outer = footprint.half_width;
                const double inner = common::core::openString(note)
                                         ? std::max(0.0, outer - g_open_tail_margin)
                                         : outer / 2.0;
                return std::array<double, 4>{
                    footprint.center_x - outer,
                    footprint.center_x - inner,
                    footprint.center_x + inner,
                    footprint.center_x + outer,
                };
            };
            // An open band's stations follow the sliding window per time; a window narrowed past
            // the insets mid-transition collapses the footprint (floorFootprintAt), and the
            // stations collapse with it onto the window's centre rather than inverting the ribbon.
            const auto band_stations_at = [&](const double seconds) {
                return band_stations(floorFootprintAt(
                    state,
                    note,
                    common::core::highwayTailHalfWidth(metrics),
                    seconds,
                    metrics,
                    mirrored));
            };
            // Sampled at the VISIBLE tail start, not the onset: tail_from advances with playback,
            // and once a window move has scrolled fully behind the hit line the remaining tail
            // must hold the settled post-move window — the onset-time window is the pre-move one,
            // and using it snapped a ringing open tail back to the old hand position the instant
            // the ramp left the visible span. With no ramp inside [tail_from, tail_to] the window
            // is constant across the whole visible tail, so tail_from is exact.
            const HighwayFloorFootprint tail_footprint = floorFootprintAt(
                state,
                note,
                common::core::highwayTailHalfWidth(metrics),
                tail_from,
                metrics,
                mirrored);
            // The span itself is non-empty by construction now; only an open tail can still
            // collapse, on a window too narrow for its insets — a collapsed footprint has no
            // width at all, so it fails the margin test the same way.
            const bool band_valid =
                !common::core::openString(note) || tail_footprint.half_width > g_open_tail_margin;
            const double base_x = tail_footprint.center_x;
            const std::array<double, 4> band = band_stations(tail_footprint);

            const bool modulated = !note.vibrato.empty() || note.tremolo || !note.bend.empty() ||
                                   common::core::glideStopCount(note) > 0;
            // An open band whose window moves under it must sample its stations along the tail
            // (the tail travels with the hand — fhp-window-motion plan).
            const bool open_band_moves =
                common::core::openString(note) &&
                handWindowMovesWithin(state, tail_from, tail_to, max_fhp_ramp_seconds);
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
                        .outer_transparent = common::core::openString(note),
                    };
                };
                // Split at each corner of the envelope — where the onset ramp finishes and where
                // the tip fade begins — because alpha is linear only BETWEEN them. Interpolating
                // across a corner would draw a straight ramp over the whole tail instead of a
                // short rise, which is exactly the bug a two-segment split hid once the onset
                // ramp existed.
                const double body_begin =
                    std::clamp(note.start_seconds + g_tail_onset_fade_seconds, tail_from, tail_to);
                const double fade_begin_seconds =
                    note.end_seconds - (duration * g_tail_tip_fade_fraction);
                const double body_end = std::clamp(fade_begin_seconds, body_begin, tail_to);
                const auto push_span = [&](const double from_seconds, const double to_seconds) {
                    if (!(to_seconds > from_seconds))
                    {
                        return;
                    }
                    // Only a band that tapers ACROSS its width carries the product the split
                    // guards against — an open tail's dissolving outer stations, or a glow whose
                    // emission varies across its columns. A plain fretted ribbon holds one color
                    // across each band, so two triangles reproduce it exactly and it keeps paying
                    // one quad per span.
                    const bool carries_product = common::core::openString(note) || tail_lit;
                    const int steps =
                        carries_product
                            ? productAlphaSpanSteps(tip_alpha(from_seconds), tip_alpha(to_seconds))
                            : 1;
                    const double span = to_seconds - from_seconds;
                    for (int step = 0; step < steps; ++step)
                    {
                        const double a_seconds =
                            from_seconds + (span * static_cast<double>(step) / steps);
                        const double b_seconds =
                            from_seconds + (span * static_cast<double>(step + 1) / steps);
                        pushRibbonSegment(
                            rail_vertices,
                            rail_indices,
                            band[0],
                            band[1],
                            band[2],
                            band[3],
                            ribbon_end(a_seconds),
                            ribbon_end(b_seconds));
                        if (tail_lit)
                        {
                            pushTailGlowSegment(
                                accent_glow_vertices,
                                accent_glow_indices,
                                band,
                                band,
                                ribbon_end(a_seconds),
                                ribbon_end(b_seconds),
                                emitterSpectrum(style.tail),
                                tip_alpha(a_seconds),
                                tip_alpha(b_seconds),
                                scratch.glow_columns);
                        }
                    }
                };
                push_span(tail_from, body_begin);
                push_span(body_begin, body_end);
                push_span(body_end, tail_to);
            }
            else if (band_valid)
            {
                // Sample density comes from the projected arc length of the modulated
                // centerline (a coarse probe polyline), not the straight lane span: a bend's
                // vertical lift or a slide's lateral travel can dominate a tail's on-screen
                // length, and the flat measure starved exactly those tails of samples, so
                // their smooth curves rendered as chunky polylines with visible corners.
                constexpr std::size_t arc_probe_segments = 16;
                double pixels = 0.0;
                double probe_x = 0.0;
                double probe_y = 0.0;
                double probe_z = 0.0;
                for (std::size_t probe = 0; probe <= arc_probe_segments; ++probe)
                {
                    const double mix =
                        static_cast<double>(probe) / static_cast<double>(arc_probe_segments);
                    const double seconds = tail_from + ((tail_to - tail_from) * mix);
                    const double arc_x =
                        base_x +
                        highwaySlideStateAt(note, base_x, metrics, mirrored, seconds).x_offset;
                    const double arc_y = note_y_at(seconds, full_vibrato_swing);
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
                // Teeth step a constant distance along the tail, so the count is the tail's length
                // over that step: lengthen the note and it gains ridges, move the camera and the
                // same ridges are seen from elsewhere. Phase runs in cycles of tail length from
                // the onset, which is what makes the wave rigid on the note — nothing here reads
                // the camera. Seconds convert to length at the scroll rate, so the two conversions
                // below are exact inverses.
                //
                // The turning points are walked and handed to the sampler explicitly: without them
                // the uniform grid rounds every apex unevenly and aliases the wave outright. The
                // walk terminates because tail_to is clamped to the visible window.
                // The teeth mean REPEATED ATTACKS, so only `tremolo` wears them — a scrape is one
                // continuous drag and cannot be tremolo picked at all (E2), so teeth would
                // assert a repetition it never performs. Its plectrum head states the unpitched
                // noise instead, matching the 2D lane's division of the same fact (the head says
                // what kind of attack, the tail says what happens over time).
                const bool teethed = note.tremolo;
                const auto tooth_phase = [&](const double seconds) {
                    return common::core::highwayTremoloTailCycles(seconds - note.start_seconds);
                };
                // The envelope ramps against the visible span so its eased ends sit on the tail's
                // own ends, while the wobble triangle keeps the onset-relative phase so its zero
                // stays pinned to the note.
                const double tooth_start_cycles = teethed ? tooth_phase(tail_from) : 0.0;
                const double tooth_span_cycles =
                    teethed ? tooth_phase(tail_to) - tooth_start_cycles : 0.0;
                std::vector<double>& wobble_times = scratch.tail_wobble_times;
                wobble_times.clear();
                if (teethed)
                {
                    for (int tooth = static_cast<int>(std::floor(tooth_start_cycles / 0.5)) + 1;;
                         ++tooth)
                    {
                        const double seconds =
                            note.start_seconds +
                            common::core::highwayTremoloTailSecondsAtCycle(0.5 * tooth);
                        if (!(seconds < tail_to))
                        {
                            break;
                        }
                        if (seconds > tail_from)
                        {
                            wobble_times.push_back(seconds);
                        }
                    }
                }
                // The vibrato wave's own turning points, handed to the sampler exactly like the
                // teeth above. The uniform grid spans the VISIBLE window, which advances every
                // frame, so a wave sampled by the grid alone is re-sampled at new phases each
                // frame and visibly morphs on approach; the sine's extremes are REGION-anchored,
                // so pinning a sample to each keeps the drawn wave rigid on the note, the way the
                // teeth already are. WHERE those extremes fall is core's to state, not this walk's:
                // the turning-point pair below inverts the very phase highwayVibratoSemitonesAt
                // reads, so re-anchoring the lift moves the samples with it instead of aliasing
                // them.
                for (const common::core::VibratoSpanViewState& span : note.vibrato)
                {
                    // Each region is walked over its own overlap with the visible window. A
                    // region covering the whole tail clamps to exactly [tail_from, tail_to],
                    // which is the walk this replaced.
                    const double region_from = std::max(tail_from, span.start_seconds);
                    const double region_to = std::min(tail_to, span.end_seconds);
                    // The region's ENDS are corners of the envelope — flat outside, wobbling
                    // inside — so they are sampled exactly for the reason the extremes are. A
                    // whole-tail region has both ends outside the window and pushes neither.
                    if (span.start_seconds > tail_from && span.start_seconds < tail_to)
                    {
                        wobble_times.push_back(span.start_seconds);
                    }
                    if (span.end_seconds > tail_from && span.end_seconds < tail_to)
                    {
                        wobble_times.push_back(span.end_seconds);
                    }
                    const double from_index =
                        common::core::highwayVibratoTurningIndex(region_from - span.start_seconds);
                    for (int extreme = static_cast<int>(std::floor(from_index)) + 1;; ++extreme)
                    {
                        const double seconds =
                            span.start_seconds + common::core::highwayVibratoSecondsAtTurningIndex(
                                                     static_cast<double>(extreme));
                        if (!(seconds < region_to))
                        {
                            break;
                        }
                        if (seconds > region_from)
                        {
                            wobble_times.push_back(seconds);
                        }
                    }
                }
                // The onset ramp's own corner, handed to the sampler for the reason the wobble
                // turning points are: the alpha envelope bends there, and a uniform grid that
                // steps over a 0.05 s corner rounds the rise into whatever its spacing happens
                // to be.
                wobble_times.push_back(note.start_seconds + g_tail_onset_fade_seconds);
                // ...and enough times INSIDE each envelope ramp to hold the same product-error
                // bound the straight path's spans hold. This path's density follows projected
                // pixels, which is blind to how fast the envelope is moving, so a ramp seen at a
                // steep angle — or any ramp on a tail long enough to hit the sample cap — can
                // still land a large slice of the rise inside one quad and draw the asymmetric
                // taper `productAlphaSpanSteps` exists to prevent.
                if (common::core::openString(note) || tail_lit)
                {
                    const double modulated_fade_begin =
                        note.end_seconds - (duration * g_tail_tip_fade_fraction);
                    const auto push_ramp_times = [&](const double from_seconds,
                                                     const double to_seconds) {
                        const int steps =
                            productAlphaSpanSteps(tip_alpha(from_seconds), tip_alpha(to_seconds));
                        const double span = to_seconds - from_seconds;
                        for (int step = 1; step < steps; ++step)
                        {
                            const double seconds =
                                from_seconds + (span * static_cast<double>(step) / steps);
                            if (seconds > tail_from && seconds < tail_to)
                            {
                                wobble_times.push_back(seconds);
                            }
                        }
                    };
                    push_ramp_times(
                        note.start_seconds, note.start_seconds + g_tail_onset_fade_seconds);
                    push_ramp_times(modulated_fade_begin, note.end_seconds);
                }
                if (open_band_moves)
                {
                    // The window's own ramp samples join the exact set so the band tracks the
                    // eased border exactly instead of aliasing across it — and so they count
                    // against the one sample budget like every other exact time.
                    windowSampleTimes(
                        state, tail_from, tail_to, max_fhp_ramp_seconds, scratch.window_times);
                    wobble_times.insert(
                        wobble_times.end(),
                        scratch.window_times.begin(),
                        scratch.window_times.end());
                }
                // The one per-note allocation left on this path: makeHighwayTailSampleTimes
                // returns its list, so banking it needs the core seam to fill a caller's buffer
                // the way windowSampleTimes does.
                const std::vector<double> sample_times = common::core::makeHighwayTailSampleTimes(
                    note, tail_from, tail_to, uniform_count, wobble_times, g_tail_sample_cap);

                std::vector<TailSample>& samples = scratch.tail_samples;
                samples.clear();
                samples.reserve(sample_times.size());
                for (const double seconds : sample_times)
                {
                    const HighwaySlideState slide =
                        highwaySlideStateAt(note, base_x, metrics, mirrored, seconds);
                    double x_offset = slide.x_offset;
                    if (teethed)
                    {
                        // The teeth ramp in their own phase units, not the tail's duration
                        // taper: one tooth in and out, so the run stays uniform whatever the
                        // sustain's length.
                        const double absolute_cycles = tooth_phase(seconds);
                        x_offset += common::core::highwayTailHalfWidth(metrics) *
                                    common::core::highwayTremoloEnvelope(
                                        absolute_cycles - tooth_start_cycles, tooth_span_cycles) *
                                    common::core::highwayTremoloWobble(absolute_cycles);
                    }
                    samples.push_back(
                        TailSample{
                            .stations = open_band_moves ? band_stations_at(seconds) : band,
                            .x_offset = x_offset,
                            .y = note_y_at(seconds, full_vibrato_swing),
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
                std::vector<double>& lifts = scratch.tail_lifts;
                lifts.assign(samples.size(), 0.0);
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
                //
                // Done with running sums rather than a walk out from each sample. The tent
                // weight 1 - |z_j - z_i| / W is LINEAR in z_j, so a window's weighted total is
                // just (W -+ z_i) * (sum of lifts) +- (sum of lifts * z), and its weight total
                // the same expression with the lifts replaced by ones — three prefix sums, read
                // in constant time per sample. Samples ascend in z, because
                // makeHighwayTailSampleTimes sorts the times it returns and highwayTimeToZ is
                // linear and increasing, so each window is a contiguous run whose ends only ever
                // move forward: the same two ends the old outward walks found by stopping at
                // their first miss, reached here by two cursors that never rewind. That turns an
                // O(samples x window) pass into O(samples), which matters most exactly where the
                // old shape was worst — a tail whose whole visible length fits inside the
                // smoothing window made every sample walk every other one.
                //
                // That sort is load-bearing here in a way it was not before. The old walks
                // rebuilt each sample's window from scratch, so one out-of-order sample would
                // have spoiled only its own shade; a cursor that never rewinds carries the
                // damage into every LATER sample instead, silently — hence the assert.
                const double shade_window_z = std::abs(
                    time_to_z(now_seconds + g_tail_slope_shade_smooth_seconds) -
                    time_to_z(now_seconds));
                std::vector<TailShadeSums>& shade_sums = scratch.tail_shade_sums;
                shade_sums.assign(
                    samples.size() + 1, TailShadeSums{.lift = 0.0, .lift_z = 0.0, .z = 0.0});
                for (std::size_t sample = 0; sample < samples.size(); ++sample)
                {
                    assert(sample == 0 || samples[sample].z >= samples[sample - 1].z);
                    const TailShadeSums& before = shade_sums[sample];
                    shade_sums[sample + 1] = TailShadeSums{
                        .lift = before.lift + lifts[sample],
                        .lift_z = before.lift_z + (lifts[sample] * samples[sample].z),
                        .z = before.z + samples[sample].z,
                    };
                }
                // Sums over [from, to), the difference of the two running totals.
                const auto sums_over = [&](const std::size_t from, const std::size_t to) {
                    return TailShadeSums{
                        .lift = shade_sums[to].lift - shade_sums[from].lift,
                        .lift_z = shade_sums[to].lift_z - shade_sums[from].lift_z,
                        .z = shade_sums[to].z - shade_sums[from].z,
                    };
                };
                std::vector<ArgbColor>& shaded = scratch.tail_shaded;
                shaded.assign(samples.size(), style.tail);
                std::size_t window_first = 0;
                std::size_t window_last = 0;
                for (std::size_t sample = 0; sample < samples.size(); ++sample)
                {
                    double lift = lifts[sample];
                    if (shade_window_z > 0.0)
                    {
                        const double z_here = samples[sample].z;
                        const auto outside = [&](const std::size_t other) {
                            return std::abs(samples[other].z - z_here) >= shade_window_z;
                        };
                        while (window_first < sample && outside(window_first))
                        {
                            ++window_first;
                        }
                        window_last = std::max(window_last, sample + 1);
                        while (window_last < samples.size() && !outside(window_last))
                        {
                            ++window_last;
                        }
                        const TailShadeSums behind = sums_over(window_first, sample);
                        const TailShadeSums ahead = sums_over(sample + 1, window_last);
                        const auto behind_count = static_cast<double>(sample - window_first);
                        const auto ahead_count = static_cast<double>(window_last - sample - 1);
                        const double back_weight = shade_window_z - z_here;
                        const double front_weight = shade_window_z + z_here;
                        const double total =
                            lifts[sample] + ((((back_weight * behind.lift) + behind.lift_z) +
                                              ((front_weight * ahead.lift) - ahead.lift_z)) /
                                             shade_window_z);
                        const double total_weight =
                            1.0 + ((((back_weight * behind_count) + behind.z) +
                                    ((front_weight * ahead_count) - ahead.z)) /
                                   shade_window_z);
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
                    const RibbonEnd end_a{
                        .x_offset = a.x_offset,
                        .y = a.y,
                        .z = a.z,
                        .edge_abgr = packAbgr(tail_a, a.alpha),
                        .inner_abgr = packAbgr(tail_a, g_tail_inner_alpha * a.alpha),
                        .outer_transparent = common::core::openString(note),
                    };
                    const RibbonEnd end_b{
                        .x_offset = b.x_offset,
                        .y = b.y,
                        .z = b.z,
                        .edge_abgr = packAbgr(tail_b, b.alpha),
                        .inner_abgr = packAbgr(tail_b, g_tail_inner_alpha * b.alpha),
                        .outer_transparent = common::core::openString(note),
                    };
                    pushRibbonSegment(
                        rail_vertices, rail_indices, a.stations, b.stations, end_a, end_b);
                    if (tail_lit)
                    {
                        // The MODULATED path, so the light picks up the bend, slide, vibrato or
                        // moving hand window from the ribbon's own samples rather than deriving
                        // any of it a second time.
                        pushTailGlowSegment(
                            accent_glow_vertices,
                            accent_glow_indices,
                            a.stations,
                            b.stations,
                            end_a,
                            end_b,
                            emitterSpectrum(tail_a),
                            a.alpha,
                            b.alpha,
                            scratch.glow_columns);
                    }
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
                                     const std::uint32_t marker_tint,
                                     const bool flip_v = false) {
            // Deferred to the group boundary rather than written inline; see PendingMarker.
            // Both extents from the HEIGHT metric: markers are square art at the family size and
            // deliberately never follow the head's width, so a head narrower than tall changes
            // nothing about the mark riding it (the reference behaves the same way — its own
            // marks exceed its narrow gem).
            pending_markers.push_back(
                PendingMarker{
                    .x = center_x,
                    .y = center_y,
                    .z = marker_z,
                    .cos_r = cos_r,
                    .sin_r = sin_r,
                    .half_w = head_half_h,
                    .half_h = head_half_h,
                    .cell = cell,
                    .tint = marker_tint,
                    .flip_v = flip_v,
                });
        };

        // Chord membership decides the rolling flip, the shadow, and the chord box (Charter
        // skips shadows for chord notes).
        const bool in_chord = group.count >= 2;

        // Glow post: the sustain tails' three-band ribbon stood upright at a fraction of the
        // tail width, rising from the board toward the note's lane center and dissolving to
        // nothing at the fade-end fraction of that height. One geometry serves both users — the
        // note shadow at the onset (the note art overlays the post's top, so every lane down to
        // the bottom one carries a post scaled to its own height) and the pitched
        // slide-keyframe markers at their own slots and times — so a shape or banding tweak can
        // never desync them.
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
                    .outer_transparent = false,
                };
                const RibbonEnd head_end{
                    .x_offset = 0.0,
                    .y = post_top_y,
                    .z = post_z,
                    .edge_abgr = clear,
                    .inner_abgr = clear,
                    .outer_transparent = false,
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
        //
        // WHERE the line spans is the caller's question and the only one left here: a HARMONIC's
        // is node-centred (harmonicMarkFootprint, below), every other note's runs wire to wire
        // across its fret slot. HOW it ends is not a question at all — every floor line of this
        // family dissolves at its ends, which pushTaperedFloorQuad states once.
        const auto push_span_line = [&](const double span_x0, const double span_x1) {
            const double onset_z = time_to_z(note.start_seconds);
            const double core_from_z = onset_z - g_attack_line_half_length;
            const double core_to_z = onset_z + g_attack_line_half_length;
            pushTaperedFloorQuad(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                core_from_z,
                core_to_z,
                g_chord_box_color,
                g_attack_line_alpha * attack_fade,
                g_attack_line_alpha * attack_fade);
            pushTaperedFloorQuad(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                core_to_z,
                core_to_z + g_attack_fade_length,
                g_beat_bar_color,
                attack_fade,
                0.0);
        };

        // A pitched slide keyframe's board furniture: a glow post and fret-span line at its own
        // slot and time — the intermediate targets the hand glides through. No note head: the
        // slide is one sounded note, so only its picked head is drawn. Keyframes stay on the
        // note's string, so they share its lane and color; the post skips the head's slide-dim
        // (a keyframe has no sliding head above it). The fret number rides the board floor with
        // the scrolling numbers, pushed in that pass below.
        const auto push_keyframe_marker = [&](const int kf_fret, const double kf_seconds) {
            const double kf_x = common::core::highwayNoteCenterX(kf_fret, metrics, mirrored);
            const double kf_z = time_to_z(kf_seconds);
            push_glow_post(kf_x, kf_z, fade * g_shadow_post_floor_alpha);
            const double slot_low = common::core::highwayFretLineX(kf_fret - 1, metrics, mirrored);
            const double slot_high = common::core::highwayFretLineX(kf_fret, metrics, mirrored);
            const auto [span_x0, span_x1] = std::minmax(slot_low, slot_high);
            pushTaperedFloorQuad(
                shadow_vertices,
                shadow_indices,
                span_x0,
                span_x1,
                0.02,
                kf_z - g_attack_line_half_length,
                kf_z + g_attack_line_half_length,
                g_chord_box_color,
                g_attack_line_alpha * fade,
                g_attack_line_alpha * fade);
        };

        if (common::core::openString(note))
        {
            // Open string: Charter's thin rounded bar spanning the active hand window, in
            // the full note color (the flat tail-width slab it replaces read as a plank). A bar
            // landing mid-transition takes the eased window at its own anchor instant, so a
            // pinned sounding bar follows the sliding window like its ringing tail does.
            const auto [x0, x1] = handWindowXAt(state, head_seconds, metrics, mirrored);
            // L posts pointing inward from the bar ends (the chord box's bottom corner holders,
            // freestanding): one continuous two-leg ribbon per corner, its cross-section
            // turning 45 degrees at the corner station so the bands wrap the L outline unbroken
            // (an upright post plus a separate foot read as a cross at the corner, not an L).
            // Cross-section colors are the open-tail treatment — transparent boundaries around
            // edge strips around the translucent core — and both leg ends fade to nothing: the
            // upright at the shared post_top_y (the glow posts' fade end below the bar), skipped
            // entirely when that top leaves no room above the corner miter.
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
                    constexpr std::array<std::array<std::size_t, 2>, 3> band_colors{
                        {{0, 1}, {2, 2}, {1, 0}}
                    };
                    for (std::size_t segment = 0; segment + 1 < stations.size(); ++segment)
                    {
                        const LStation& a = stations.at(segment);
                        const LStation& b = stations.at(segment + 1);
                        const std::array<std::uint32_t, 3> colors_a = station_colors(a);
                        const std::array<std::uint32_t, 3> colors_b = station_colors(b);
                        for (std::size_t band = 0; band < band_colors.size(); ++band)
                        {
                            const auto [from_color, to_color] = band_colors.at(band);
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
            // An open string has no head, so the emphasis axis rides its BAR: a ghost thins it and
            // takes light out of it, an accent lights it. That is the seam where the old design
            // diverged — the fretted head wore an atlas cell an open bar could never wear, so the
            // same chart mark said two different things depending on the fret.
            //
            // The bar and the markers riding it share ONE alpha, for the same reason the fretted
            // head shares one with its markers: a mark left brighter than the thing it sits on
            // reads as a rendering fault rather than as dynamics.
            const double open_bar_thickness =
                common::core::isGhosted(note.emphasis) ? g_ghost_open_bar_thickness : 1.0;
            const double open_bar_alpha = emphasisAlpha(note.emphasis);
            pushOpenNoteBar(
                open_vertices,
                open_indices,
                x0,
                x1,
                head_y,
                z,
                base_color,
                fade * open_bar_alpha,
                open_bar_thickness);
            if (common::core::isAccented(note.emphasis))
            {
                // The same light the fretted head wears, around a capsule: half extents of
                // the bar's own middle cross-section, corner radius
                // equal to its half thickness. That is what the bar's rounded profile IS.
                //
                // The silhouette is the bar's FULL span, x0 to x1 — its GEOMETRY runs the whole
                // window and only its alpha ramps, so pulling the capsule in by a fade length
                // (the round before last) left the light stopping a third of a world unit short
                // of each tip.
                //
                // The bar's taper is then carried in the light's own strength along the axis,
                // through openBarEmission. A uniform strip was the round after that, and it
                // erased the taper by lighting the two stretches where the bar has faded out.
                //
                // Drawn as a STRIP rather than one quad because that axial profile has to live
                // somewhere: columns clustered at the two rounded corners of each end (one reach
                // either side of the tip and of the fade's end), which tracks the derived curve
                // to under 0.007 in alpha — three or four counts out of 255, and continuous
                // rather than stepped. The alternative was a fifth vertex attribute carried by
                // every head and chord box that will never use it.
                //
                // Note there is no halving here any more. The previous light redrew the bar's
                // PRISM, which is closed and unculled (the lefty mirror inverts winding), so
                // every ray crossed it twice and additive light accumulated twice; the correction
                // had to be applied by hand. A flat quad crosses once, like the head's, so the
                // one-weight promise now holds by construction instead of by compensation.
                const double bar_fade = openBarFadeLength(x0, x1);
                const double bar_center = (x0 + x1) / 2.0;
                const double bar_half = (x1 - x0) / 2.0;
                const GlowShape bar_shape{
                    .half_w = bar_half,
                    .half_h = g_open_note_middle_half_thickness,
                    .corner = g_open_note_middle_half_thickness,
                    .rhombus = false,
                };
                const double glow_half_h = g_open_note_middle_half_thickness + g_accent_reach;

                std::vector<double>& columns = scratch.glow_columns;
                columns.clear();
                columns.reserve(24);
                for (const double corner_s : {0.0, bar_fade})
                {
                    for (const double offset : {-1.0, -0.5, 0.0, 0.5, 1.0})
                    {
                        const double s = corner_s + (offset * g_accent_reach);
                        columns.push_back(s);
                        columns.push_back((x1 - x0) - s);
                    }
                }
                std::ranges::sort(columns);
                const auto duplicates = std::ranges::unique(columns);
                columns.erase(duplicates.begin(), duplicates.end());

                const auto column_at = [&](const double s) {
                    const double local_x = s - bar_half;
                    const double weight = openBarEmission(std::min(s, (x1 - x0) - s), bar_fade);
                    return std::pair{local_x, packAbgr(emitterSpectrum(base_color), fade * weight)};
                };
                for (std::size_t column = 0; column + 1 < columns.size(); ++column)
                {
                    const auto [left_x, left_abgr] = column_at(columns[column]);
                    const auto [right_x, right_abgr] = column_at(columns[column + 1]);
                    const auto at =
                        [&](const double local_x, const double local_y, const std::uint32_t abgr) {
                            return makeGlowVertex(
                                bar_center + local_x,
                                head_y + local_y,
                                z,
                                abgr,
                                local_x,
                                local_y,
                                bar_shape);
                        };
                    pushQuad(
                        accent_glow_vertices,
                        accent_glow_indices,
                        at(left_x, -glow_half_h, left_abgr),
                        at(right_x, -glow_half_h, right_abgr),
                        at(right_x, glow_half_h, right_abgr),
                        at(left_x, glow_half_h, left_abgr));
                }
            }
            // Technique markers at the window center, from the same ordered authority the
            // fretted head below asks. An open bar has no rolling flip, so every mark draws
            // upright here and the stack's per-mark roll flag simply does not apply.
            //
            // Asking the shared list also restores marks this branch silently dropped: an open
            // string carrying a harmonic node drew no harmonic cell, and a tapped one drew no tap
            // cell, because the hand-written set here was a subset nobody had reconciled.
            {
                const double center_x = (x0 + x1) / 2.0;
                const std::uint32_t marker_tint = packAbgr(base_color, fade * open_bar_alpha);
                for (const HighwayHeadMark& mark : highwayHeadMarks(note))
                {
                    push_marker(
                        center_x, head_y, z, 1.0, 0.0, mark.cell, marker_tint, mark.flipped);
                }
            }
            continue;
        }

        // Fretted head anchor: where the note sounds from at its own stop (highwayNoteFretboardX
        // states the rule, including which harmonics move onto their node).
        double x = highwayNoteFretboardX(note, note.fret, metrics, mirrored);
        // A sounding head travels with its glide, so it stays glued to the tail through bends,
        // vibrato, and slides. It does NOT ride the tremolo teeth: the teeth are a texture the
        // tail carries, and a head shaking with them reads as a jitter fighting its own digit
        // rather than as picking energy.
        x += head_slide.x_offset;

        if (!in_chord)
        {
            // A HARMONIC's line is centred on the NODE, where the touch that makes the figure a
            // harmonic actually lands: a slot line drew the hand a wire away from it (user
            // sighting 2026-08-30). The footprint function is shared rather than private so the
            // line and anything else marking a harmonic's place on the floor read one answer.
            //
            // Every other note spans from the FRETTING HAND's fret slot: the wires bound the
            // line so it sits aligned in a fret, which is exactly where a finger presses.
            if (highwayHarmonicMark(note))
            {
                const HighwayFloorFootprint footprint =
                    harmonicMarkFootprint(note, metrics, mirrored);
                const double span_x0 = footprint.center_x - footprint.half_width;
                const double span_x1 = footprint.center_x + footprint.half_width;
                push_span_line(span_x0, span_x1);
            }
            else
            {
                const int slot_fret = common::core::fretFor(note);
                const double slot_low_x =
                    common::core::highwayFretLineX(slot_fret - 1, metrics, mirrored);
                const double slot_high_x =
                    common::core::highwayFretLineX(slot_fret, metrics, mirrored);
                const auto [span_x0, span_x1] = std::minmax(slot_low_x, slot_high_x);
                push_span_line(span_x0, span_x1);
            }
            push_glow_post(x, z, post_floor_alpha);
        }

        const bool node_head = highwayNodeHead(note);

        // The hollow silhouette is the head's own outline: a node head's landing ring and
        // pre-bend outline are its base's shape (the harmonic hollow) while every other head
        // keeps the rectangle — one shape law for the filled head and everything that previews
        // it, asked from the same predicate the base cell asks.
        const std::array<float, 4> hollow_cell = atlases.head_layout.cellRect(
            node_head ? g_head_cell_harmonic_anticipation : g_head_cell_anticipation);

        // A node head's diamond is square art and holds the family size on both axes; only the
        // rectangle head takes the width metric on x. The hollow twin and the pre-bend outline
        // take the same extents on purpose: they are previews OF this shape, so a preview that
        // lands differently than the head it announced would make the approach lie.
        const double base_half_w = node_head ? head_half_h : head_half_w;
        const double base_half_h = head_half_h;

        // Anticipation ring: a hollow copy of the head parked AT THE HIT LINE (z = 0, not the
        // note's own z) that GROWS into full head size as the note arrives — 0.5625 of it when it
        // appears half a second out, reaching full size a quarter second out and holding there.
        // The scale is squared, so it stays small for most of the window and opens up over the
        // last stretch rather than creeping linearly. It announces where the note will land, not
        // where the note currently is (reference atlas cell; chart-driven, so the editor preview
        // shows it too — 44-Q1).
        //
        // The landing spot is the chart-truth station: a pre-bend's ring sits on the TARGET
        // outline, not on the still-rising head, so the ring names the destination while the head
        // is still on its way to it.
        const double seconds_out = note.start_seconds - now_seconds;
        if (seconds_out > 0.0 && seconds_out < g_anticipation_seconds)
        {
            double ring_scale =
                std::min(1.0, 1.0 - (0.5 * ((seconds_out - 0.25) / g_anticipation_seconds)));
            ring_scale *= ring_scale;
            const double ring_alpha =
                std::min(1.0, (g_anticipation_seconds - seconds_out) * (1000.0 / 255.0));
            const std::uint32_t ring_tint = packAbgr(base_color, ring_alpha);
            const double ring_half_w = base_half_w * ring_scale;
            const double ring_half_h = base_half_h * ring_scale;
            pushQuad(
                head_vertices,
                head_indices,
                makeUvVertex(
                    x - ring_half_w,
                    chart_head_y - ring_half_h,
                    0.0,
                    ring_tint,
                    hollow_cell[0],
                    hollow_cell[3]),
                makeUvVertex(
                    x + ring_half_w,
                    chart_head_y - ring_half_h,
                    0.0,
                    ring_tint,
                    hollow_cell[2],
                    hollow_cell[3]),
                makeUvVertex(
                    x + ring_half_w,
                    chart_head_y + ring_half_h,
                    0.0,
                    ring_tint,
                    hollow_cell[2],
                    hollow_cell[1]),
                makeUvVertex(
                    x - ring_half_w,
                    chart_head_y + ring_half_h,
                    0.0,
                    ring_tint,
                    hollow_cell[0],
                    hollow_cell[1]));
        }

        // Pre-bend target outline: the anticipation cell — already a hollow copy of the head's
        // rim in the reference atlas — parks at the chart-truth station for the whole approach,
        // so a pre-bent note reads as a slot the head rises into instead of passing for a
        // plainly fretted note on the lane it occupies. It rides the note's own z (unlike the
        // ring's hit-line landing preview), stays axis-aligned like the upright technique
        // markers, and stops at the onset, where the landed head covers it. chart_head_y -
        // lane_y is exactly the onset bend lift (the vibrato taper is zero at the onset) and
        // exactly 0.0 for a non-pre-bent curve, so the > 0.0 test is a precise pre-bend gate,
        // not a tolerance.
        if (note.start_seconds > now_seconds && std::is_neq(chart_head_y <=> lane_y))
        {
            const std::uint32_t outline_tint =
                packAbgr(base_color, g_prebend_outline_alpha * fade * head_slide.alpha);
            pushQuad(
                head_vertices,
                head_indices,
                makeUvVertex(
                    x - base_half_w,
                    chart_head_y - base_half_h,
                    z,
                    outline_tint,
                    hollow_cell[0],
                    hollow_cell[3]),
                makeUvVertex(
                    x + base_half_w,
                    chart_head_y - base_half_h,
                    z,
                    outline_tint,
                    hollow_cell[2],
                    hollow_cell[3]),
                makeUvVertex(
                    x + base_half_w,
                    chart_head_y + base_half_h,
                    z,
                    outline_tint,
                    hollow_cell[2],
                    hollow_cell[1]),
                makeUvVertex(
                    x - base_half_w,
                    chart_head_y + base_half_h,
                    z,
                    outline_tint,
                    hollow_cell[0],
                    hollow_cell[1]));
        }

        // Rolling flip: single notes stand vertical as they enter the visibility window and
        // roll flat around their travel axis across the whole approach, landing flat
        // g_flip_flat_lead_seconds before the hit line. Chords are the one exclusion, and it is
        // about the GROUP rather than the art: members must arrive as one object rather than as
        // a row of cards spinning out of step with each other.
        //
        // Node heads roll with everything else. They were held flat as well while the exclusion
        // was read as being about the art having a face to turn, but the diamond is an L1 ball
        // (\ref headArtProfile measures it by that edge law) drawn into a SQUARE quad, so a
        // quarter turn maps it onto itself: what the approach actually shows is the base easing
        // through an axis-aligned square at 45 degrees while the harmonic marker riding it turns
        // the full 90, which is the same motion every other head makes.
        //
        // The clock (flip_remaining) is computed beside the head station, where the pre-bend
        // reveal shares it, so a head rises onto a pre-bent station and rolls on one clock.
        const double rotation = in_chord ? 0.0 : (std::numbers::pi / 2.0) * flip_remaining;
        const double cos_r = std::cos(rotation);
        const double sin_r = std::sin(rotation);

        // The quiet end of the emphasis axis takes light out of the note. A ghost's markers quiet
        // with it: a full-brightness mark over a dim head reads as a rendering fault rather than
        // as dynamics. ONE tint for the head art and for every marker riding it, and the one
        // emphasis-to-alpha mapping every other quieting site asks — this was the site that
        // open-coded it.
        const std::uint32_t tint =
            packAbgr(base_color, fade * head_slide.alpha * emphasisAlpha(note.emphasis));

        // Head base: the diamond node base when the head sits ON its harmonic node (it lands
        // between fret wires, where the family rectangle reads as a misaligned ordinary note);
        // else the technique variant under left-hand technique markers and under a scrape — its
        // travel is unpitched noise, so it takes the darker base a dead note takes, and
        // the pick mark then sits on that base rather than on an X — else the standard head.
        // Both predicates are stated once, in highway_head_marks.h.
        const std::array<float, 4> base_cell =
            node_head               ? atlases.head_layout.cellRect(g_head_cell_harmonic_base)
            : highwayTechHead(note) ? atlases.head_layout.cellRect(g_head_cell_tech)
                                    : head_cell;
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
            corner(-base_half_w, -base_half_h, base_cell[0], base_cell[3]),
            corner(base_half_w, -base_half_h, base_cell[2], base_cell[3]),
            corner(base_half_w, base_half_h, base_cell[2], base_cell[1]),
            corner(-base_half_w, base_half_h, base_cell[0], base_cell[1]));

        // The loud end is added LIGHT, around the head's own silhouette, into a batch that submits
        // BEFORE the heads so it sits under the note rather than repainting it.
        //
        // The silhouette handed to the field is the head ART's extents, never the QUAD's. The quad
        // is nearly three times the art's height, so a light sized against it starts three
        // head-heights out and reads as a lit box the note sits inside — which is what the first
        // attempt at this did. A node head takes the RHOMBUS field instead: its base is a diamond,
        // and a rectangular glow around a diamond leaves four lit corners with nothing under them.
        if (common::core::isAccented(note.emphasis))
        {
            // World-per-texel per drawn axis. The rectangle head's quad takes the width metric
            // on x, so its texels turn anisotropic if that metric ever narrows; a node head's
            // quad holds the family size on both axes, so its texels stay square.
            const double texel_x = node_head ? headArtTexelHeight(metrics, atlases.head_layout)
                                             : headArtTexelWidth(metrics, atlases.head_layout);
            const double texel_y = headArtTexelHeight(metrics, atlases.head_layout);
            // The field is placed at the ART'S measured centre rather than the quad's — zero on
            // the shipped recentred atlas, but measured rather than assumed (head_art_profile.h).
            // The offset rides the rolling flip with everything else, which is why it is rotated
            // here instead of being folded into the shape.
            const double art_dx =
                (node_head ? head_art.node_center_x_texels : head_art.center_x_texels) * texel_x;
            const double art_dy =
                (node_head ? head_art.node_center_y_texels : head_art.center_y_texels) * texel_y;
            pushAccentGlow(
                accent_glow_vertices,
                accent_glow_indices,
                x + (art_dx * cos_r) - (art_dy * sin_r),
                head_y + (art_dx * sin_r) + (art_dy * cos_r),
                z,
                node_head
                    ? GlowShape{
                          .half_w = head_art.node_half_span_texels * texel_y,
                          .half_h = head_art.node_half_span_texels * texel_y,
                          .corner = 0.0,
                          .rhombus = true,
                      }
                    : GlowShape{
                          .half_w = head_art.half_width_texels * texel_x,
                          .half_h = head_art.half_height_texels * texel_y,
                          // Via the y texel: the two texel sizes are equal while the head
                          // metrics agree, and the height axis is the authoritative one for the
                          // single radius if they ever diverge.
                          .corner = head_art.corner_texels * texel_y,
                          .rhombus = false,
                      },
                g_accent_reach,
                packAbgr(
                    emitterSpectrum(base_color), fade * head_slide.alpha),
                cos_r,
                sin_r);
        }

        {
            // ONE authority for which marks a head wears and in what order: highwayHeadMarks, in
            // highway_head_marks.h. The open-string overlay above asks the very same function, so
            // the two branches can no longer answer the same question differently — which they
            // already did, each having hand-written the list. This branch drew the harmonic
            // underneath everything and the connection cell fifth; the open branch drew the
            // connection cell at the BOTTOM and drew no harmonic at all.
            //
            // Marks that ride the head's rolling flip take the roll here and the rest stay upright
            // through it, exactly as before — the stack carries that per mark, because the order
            // interleaves the two kinds. No accent MARK appears in it: emphasis is a rendered light
            // now, and the atlas ring it replaced is retired rather than drawn beneath it.
            for (const HighwayHeadMark& mark : highwayHeadMarks(note))
            {
                push_marker(
                    x,
                    head_y,
                    z,
                    mark.rides_roll ? cos_r : 1.0,
                    mark.rides_roll ? sin_r : 0.0,
                    mark.cell,
                    tint,
                    mark.flipped);
            }
        }

        // Bend notation (bend-head-indicators plan: chevron stacks read as clutter, amount
        // figures did not read at speed, and target rails were redundant furniture once the
        // tail itself carried the amount): the head carries ONE chevron marker — a caret-shaped
        // bend cue — announcing only that a bend is coming. It rides the bend-lift side of the
        // head (above the note for an upward curve, below on bend-inverted lanes) and its
        // 180-degree flip keeps it pointing where the drawn curve goes. Amount and stages are
        // the tail's own geometry: physical lift height plus slope shading. The station is the
        // chart-truth height: on an approaching pre-bend the chevron rides the target outline,
        // not the rising head; everywhere else chart and head coincide.
        if (!note.bend.empty())
        {
            // The station derives from the load-measured silhouette, so the chevron's legs keep
            // anchoring on the note's VISIBLE top edge whatever the art measures at load — see
            // the clearance constant for the authored relationship this preserves.
            const double head_art_edge_world =
                (head_art.center_y_texels + head_art.half_height_texels) *
                headArtTexelHeight(metrics, atlases.head_layout);
            const double bend_marker_lift =
                head_art_edge_world + (g_bend_marker_edge_clearance_heads * head_half_h);
            // The 180-degree flip is a rotation like any other marker's: cos_r carries the
            // direction and sin_r stays zero, which negates both offsets exactly as before.
            push_marker(
                x,
                chart_head_y + (bend_direction * bend_marker_lift),
                z,
                bend_direction,
                0.0,
                g_head_cell_bend,
                tint);
        }

        // Each pitched slide keyframe gets its own post and line; an unpitched slide-out
        // is a pressure release with no target to mark, so it gets no board furniture — only the
        // rail's own dimming trail. A keyframe carries its own time, which can sit well past its
        // note's onset, so each marker culls to the same upcoming window as its floor fret
        // number below: past span_end it would float beyond the board's far edge, and behind the
        // hit line it would stand at full alpha after its number vanished. Keyframe and
        // tapped-note fret numbers ride the board floor with the scrolling numbers, pushed in
        // that pass below.
        // Stacked chord slides dedup: members sliding together land keyframes on the same fret
        // at the same instant, and their markers would pile up in one slot — only the member on
        // the lowest displayed lane (nearest the floor, so its post overlaps nothing above it)
        // draws the shared marker.
        const auto stacked_below = [&](const common::core::KeyframeViewState& keyframe) {
            for (std::size_t member = group.first; member < group.first + group.count; ++member)
            {
                const common::core::NoteViewState& other = state.chart.notes[member];
                if (member == index ||
                    common::core::highwayStringLaneY(
                        laneOf(other.string), displayed_count, metrics, invert) >= lane_y)
                {
                    continue;
                }
                if (common::core::isScrape(other.attack))
                {
                    continue;
                }
                for (const common::core::KeyframeViewState& other_keyframe : other.slides)
                {
                    if (other_keyframe.fret == keyframe.fret &&
                        std::abs(other_keyframe.seconds - keyframe.seconds) < g_onset_match_epsilon)
                    {
                        return true;
                    }
                }
            }
            return false;
        };
        // A scrape's stops are the PICKING hand's travel, so none of them earns a fret-hand
        // marker; the falls-away terminal never earns one either, and it is no longer in this
        // list to be filtered out (W9-L).
        if (!common::core::isScrape(note.attack))
        {
            for (const common::core::KeyframeViewState& keyframe : note.slides)
            {
                if (keyframe.fret > 0 && keyframe.seconds > now_seconds &&
                    keyframe.seconds <= span_end_seconds && !stacked_below(keyframe))
                {
                    push_keyframe_marker(keyframe.fret, keyframe.seconds);
                }
            }
        }
    }

    emit_pending_markers();
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

    // Floor numbers nearer than every drawn note — including the hit-line-pinned set —
    // drain here, still before the board face.
    submit_numbers_beyond(std::numeric_limits<double>::lowest());

    // The board face over the passed content, and the strike glow last of all — each pass states
    // its own placement rule in its banner.
    drawStringLines();
    drawFretLines(frame);
    drawFretboardMarkers();
    drawCapo();
    drawSectionLabels(frame);
    drawStrikeGlow(frame);
}

// --- Lane border ribbons: one faded runway strip per fret line (Charter's floor
// grid). Alpha tiers: bright for the current hand range, mid for any visible window's
// range, faint elsewhere. ---
void HighwayRenderer::Impl::drawLaneBorderRibbons(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    std::array<bool, g_face_fret_count + 1> in_visible_window{};
    for (const HandWindow& window : frame.hand_windows)
    {
        for (int line = window.fret - 1; line <= window.fret + window.width - 1; ++line)
        {
            if (line >= 0 && line <= g_face_fret_count)
            {
                in_visible_window.at(static_cast<std::size_t>(line)) = true;
            }
        }
    }
    // Right-hand windows join the visible tier: lines the tapping hand's light path crosses
    // on screen brighten like any visible window's. The tier is a per-line array, so overlap
    // with the fretting hand's windows deduplicates itself, and the path union already
    // carries any tapped-slide morph — the eased-coverage machinery stays exclusive to the
    // current fretting-hand window's hit-line crossfade.
    for (const common::core::HighwayTapOnsetViewState& tap :
         litTaps(frame.span_start_seconds, frame.span_end_seconds, g_floor_light_release_seconds))
    {
        if (tap.path.front().seconds > frame.span_end_seconds ||
            tap.path.back().seconds + g_floor_light_release_seconds < frame.span_start_seconds)
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

    // The tapping hand's contribution to the bright tier (the left window's coverage
    // brightened its ribbons to full while the tap lanes stayed at the mid tier): each tap
    // active at NOW brightens its lines by its own light envelope — rising on approach, full
    // through the hold, fading over the decay — over the path extent at now (eased
    // mid-glide with the same curve the light travels). Coverage softens across one fret
    // past the extent's bounding lines, and lines take the max over taps, so hand overlap
    // deduplicates itself exactly like the other tiers.
    std::array<double, g_face_fret_count + 1> tap_coverage{};
    for (const common::core::HighwayTapOnsetViewState& tap :
         litTaps(frame.now_seconds, frame.now_seconds, g_tap_ribbon_decay_seconds))
    {
        const common::core::HighwayTapLightStation& front = tap.path.front();
        const common::core::HighwayTapLightStation& back = tap.path.back();
        // Ramps vary per onset, so the bounded range is padded by the longest of them and
        // the exact skip stays here; the ribbons use their own slower decay.
        if (front.seconds - tap.ramp_seconds > frame.now_seconds ||
            back.seconds + g_tap_ribbon_decay_seconds < frame.now_seconds)
        {
            continue;
        }
        double envelope = 1.0;
        double low = front.fret_low;
        double high = front.fret_high;
        if (frame.now_seconds < front.seconds)
        {
            envelope = (frame.now_seconds - (front.seconds - tap.ramp_seconds)) / tap.ramp_seconds;
        }
        else if (frame.now_seconds > back.seconds)
        {
            envelope = 1.0 - ((frame.now_seconds - back.seconds) / g_tap_ribbon_decay_seconds);
            low = back.fret_low;
            high = back.fret_high;
        }
        else
        {
            for (std::size_t station = 0; station + 1 < tap.path.size(); ++station)
            {
                const common::core::HighwayTapLightStation& a = tap.path[station];
                const common::core::HighwayTapLightStation& b = tap.path[station + 1];
                if (frame.now_seconds > b.seconds)
                {
                    continue;
                }
                const double span = b.seconds - a.seconds;
                const double progress =
                    span > 0.0 ? std::clamp((frame.now_seconds - a.seconds) / span, 0.0, 1.0) : 1.0;
                // The arrival station carries the segment's glide family: a scrape's
                // unpitched pick travel eases differently than a tapped pitched glide.
                const double weight = common::core::highwaySlideEaseWeight(progress, b.unpitched);
                low = a.fret_low + ((b.fret_low - a.fret_low) * weight);
                high = a.fret_high + ((b.fret_high - a.fret_high) * weight);
                break;
            }
        }
        for (int line = 0; line <= g_face_fret_count; ++line)
        {
            const double inside = std::min(static_cast<double>(line) - (low - 1.0), high - line);
            const double line_coverage = std::clamp(1.0 + inside, 0.0, 1.0) * envelope;
            double& slot = tap_coverage.at(static_cast<std::size_t>(line));
            slot = std::max(slot, line_coverage);
        }
    }

    setFadeUniform();
    auto [vertices, indices] = scratch.colorBatch();
    // One full-length strip per fret line, four vertices each.
    vertices.reserve(4 * (static_cast<std::size_t>(g_face_fret_count) + 1));
    const double z0 = timeToZ(frame, frame.span_start_seconds);
    const double z1 = timeToZ(frame, frame.span_end_seconds);
    for (int line = 0; line <= g_face_fret_count; ++line)
    {
        // Coverage crossfade: each full-length strip lerps from its non-current tier to full
        // brightness by how deeply the current eased window contains it, so the brightened
        // band hands off line-by-line in lockstep with the border crossing the hit line.
        // Lines stay straight and fixed; only alpha animates. Whichever hand covers a line
        // more deeply wins it (max-combine).
        const double base_alpha =
            in_visible_window.at(static_cast<std::size_t>(line)) ? 0.375 : 0.125;
        const double coverage = std::max(
            common::core::highwayHandWindowLineCoverage(
                frame.current_window, static_cast<double>(line)),
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
void HighwayRenderer::Impl::drawHandWindowLight(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    const std::array<float, 4> light_params{
        static_cast<float>(g_window_light_falloff), 0.0F, 0.0F, 0.0F
    };
    bgfx::setUniform(window_light_params.get(), light_params.data());
    auto [vertices, indices] = scratch.texturedBatch();
    std::vector<double>& times = scratch.window_times;
    windowSampleTimes(
        state, frame.span_start_seconds, frame.span_end_seconds, max_fhp_ramp_seconds, times);
    std::vector<WindowLightSlice>& slices = scratch.window_light_slices;
    slices.clear();
    slices.reserve(times.size());
    for (const double seconds : times)
    {
        const auto [low_x, high_x] = handWindowXAt(state, seconds, metrics, mirrored);
        slices.push_back(
            WindowLightSlice{
                .z = timeToZ(frame, seconds),
                .low_x = low_x,
                .high_x = high_x,
                .dim = 1.0,
            });
    }
    // Motion dim: the silhouette keeps the settled cross-section everywhere — the same
    // x-measured fade band along the whole eased contour — and the transition's fading
    // lives in the light's brightness instead. Each ramp dims along a sin-squared bell over
    // its own progress: full brightness at the ramp's start and end, deepest exactly
    // mid-transition, so the light reads as gradually fading out of the lit span, through
    // the dark middle of the sweep, and back into the arriving span (a per-sample speed dim
    // plateaued at maximum across most of a fast morph instead). The bell's depth scales
    // with the ramp's overall sweep steepness, so slow glides keep most of their glow.
    //
    // Placements ascend, so only a bounded run of them can dim this span: one arriving at or
    // before the span start is skipped outright, and from the first whose arrival sits the
    // longest ramp in the chart past the span end, no ramp — this one's or any later one's —
    // still reaches back into the span. (windowSampleTimes above bounds itself the same way.)
    for (const common::core::FhpViewState& fhp : std::ranges::subrange(
             std::ranges::upper_bound(
                 state.chart.fret_hand_positions,
                 frame.span_start_seconds,
                 std::ranges::less{},
                 &common::core::FhpViewState::seconds),
             std::ranges::partition_point(
                 state.chart.fret_hand_positions, [&](const common::core::FhpViewState& candidate) {
                     return candidate.seconds - max_fhp_ramp_seconds < frame.span_end_seconds;
                 })))
    {
        if (fhp.ramp_seconds <= 0.0 || fhp.seconds <= frame.span_start_seconds ||
            fhp.seconds - fhp.ramp_seconds >= frame.span_end_seconds)
        {
            continue;
        }
        const double ramp_start = fhp.seconds - fhp.ramp_seconds;
        const auto [low_from, high_from] = handWindowXAt(state, ramp_start, metrics, mirrored);
        const auto [low_to, high_to] = handWindowXAt(state, fhp.seconds, metrics, mirrored);
        const double dz = timeToZ(frame, fhp.seconds) - timeToZ(frame, ramp_start);
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
            double& dim = slices[sample].dim;
            dim = std::min(dim, 1.0 - (depth * bell * bell));
        }
    }
    const double spill = g_window_light_falloff / 2.0;
    for (std::size_t sample = 1; sample < slices.size(); ++sample)
    {
        const WindowLightSlice& slice_a = slices[sample - 1];
        const WindowLightSlice& slice_b = slices[sample];
        const double za = slice_a.z;
        const double zb = slice_b.z;
        const double low_a = slice_a.low_x;
        const double low_b = slice_b.low_x;
        const double high_a = slice_a.high_x;
        const double high_b = slice_b.high_x;
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
            const std::uint32_t tint_a = packAbgr(lane_color, slice_a.dim);
            const std::uint32_t tint_b = packAbgr(lane_color, slice_b.dim);
            const auto vertex = [&](const double x,
                                    const double z,
                                    const double low,
                                    const double high,
                                    const std::uint32_t tint) {
                return makeUvVertex(
                    x,
                    g_floor_light_y,
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
// merged into runs: the dip between consecutive taps mirrors the finger lifting.
// Consecutive same-lane patches simply alpha-compose where their envelopes overlap, which
// shallows the dip toward continuous light only at densities where the hand genuinely never
// leaves the board. Reuses the window light's per-fragment soft x edges; the tint leans
// toward the FHP orange so the two hands' lights read apart. ---
void HighwayRenderer::Impl::drawTappingHandLight(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    const std::array<float, 4> light_params{
        static_cast<float>(g_window_light_falloff), 0.0F, 0.0F, 0.0F
    };
    bgfx::setUniform(window_light_params.get(), light_params.data());
    auto [vertices, indices] = scratch.texturedBatch();
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
        if (time_b <= time_a || time_b <= frame.span_start_seconds ||
            time_a >= frame.span_end_seconds)
        {
            return;
        }
        const auto lerp = [](const double a, const double b, const double w) {
            return a + ((b - a) * w);
        };
        if (time_a < frame.span_start_seconds)
        {
            const double w = (frame.span_start_seconds - time_a) / (time_b - time_a);
            alpha_a = lerp(alpha_a, alpha_b, w);
            low_a = lerp(low_a, low_b, w);
            high_a = lerp(high_a, high_b, w);
            time_a = frame.span_start_seconds;
        }
        if (time_b > frame.span_end_seconds)
        {
            const double w = (frame.span_end_seconds - time_a) / (time_b - time_a);
            alpha_b = lerp(alpha_a, alpha_b, w);
            low_b = lerp(low_a, low_b, w);
            high_b = lerp(high_a, high_b, w);
            time_b = frame.span_end_seconds;
        }
        const double za = timeToZ(frame, time_a);
        const double zb = timeToZ(frame, time_b);
        const auto patch_edges = [&](const double low, const double high) {
            const double edge0 = common::core::highwayFretLineX(low - 1.0, metrics, mirrored);
            const double edge1 = common::core::highwayFretLineX(high, metrics, mirrored);
            return std::pair{std::min(edge0, edge1), std::max(edge0, edge1)};
        };
        const auto [low_x_a, high_x_a] = patch_edges(low_a, high_a);
        const auto [low_x_b, high_x_b] = patch_edges(low_b, high_b);
        const int lane_first = std::max(1, static_cast<int>(std::floor(std::min(low_a, low_b))));
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
            const ArgbColor base = isDottedFret(fret) ? g_lit_lane_dotted_color : g_lit_lane_color;
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
                    g_floor_light_y,
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
    for (const common::core::HighwayTapOnsetViewState& tap :
         litTaps(frame.span_start_seconds, frame.span_end_seconds, g_floor_light_release_seconds))
    {
        const common::core::HighwayTapLightStation& front = tap.path.front();
        const common::core::HighwayTapLightStation& back = tap.path.back();
        // Ramps vary per onset, so the bounded range is padded by the longest of them and
        // the exact skip stays here: two cheap POD compares.
        if (back.seconds + g_floor_light_release_seconds < frame.span_start_seconds ||
            front.seconds - tap.ramp_seconds > frame.span_end_seconds)
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
            const bool gliding =
                std::is_neq(a.fret_low <=> b.fret_low) || std::is_neq(a.fret_high <=> b.fret_high);
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
            // Slice density scales with the sweep (highwayGlideSliceCount, the one policy
            // every glide-following mark subdivides by), and the ease follows the arrival
            // station's glide family — a scrape's unpitched pick travel curves differently
            // than a tapped pitched glide.
            const double sweep =
                std::max(std::abs(b.fret_low - a.fret_low), std::abs(b.fret_high - a.fret_high));
            const int slices = highwayGlideSliceCount(sweep);
            double previous_seconds = a.seconds;
            double previous_low = a.fret_low;
            double previous_high = a.fret_high;
            for (int slice = 1; slice <= slices; ++slice)
            {
                const double progress = static_cast<double>(slice) / slices;
                const double seconds = a.seconds + ((b.seconds - a.seconds) * progress);
                const double weight = common::core::highwaySlideEaseWeight(progress, b.unpitched);
                const double low = a.fret_low + ((b.fret_low - a.fret_low) * weight);
                const double high = a.fret_high + ((b.fret_high - a.fret_high) * weight);
                emit_segment(
                    previous_seconds, 1.0, previous_low, previous_high, seconds, 1.0, low, high);
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
            back.seconds + g_floor_light_release_seconds,
            0.0,
            back.fret_low,
            back.fret_high);
    }
    submitBatch(vertices, indices, posColorUvLayout(), window_light_program.get(), nullptr);
}

// --- Beat and measure bars: Charter's gradient wings in its deep blue, clipped to
// each beat's hand window. Measures get a sharp teal attack line on the downbeat with a
// brief blue fade trailing into the measure; plain beats are two wings meeting at the
// line. Both ends of every bar dissolve along x, the same taper the note lines under them take
// (pushTaperedFloorQuad states it): a bar stopping flat at the window edge drew a hard end where
// the light beside it is already fading out. ---
void HighwayRenderer::Impl::drawBeatBars(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    setFadeUniform();

    auto [vertices, indices] = scratch.colorBatch();
    // Beats ascend, so the two skip tests are the two ends of a binary-searched range — the
    // same clamp the floor numbers' downbeat pass makes over the same list.
    for (const common::core::HighwayBeatViewState& beat : std::ranges::subrange(
             std::ranges::lower_bound(
                 state.beats,
                 frame.now_seconds - 0.2,
                 std::ranges::less{},
                 &common::core::HighwayBeatViewState::seconds),
             std::ranges::upper_bound(
                 state.beats,
                 frame.span_end_seconds,
                 std::ranges::less{},
                 &common::core::HighwayBeatViewState::seconds)))
    {
        const auto [x0, x1] = handWindowXAt(state, beat.seconds, metrics, mirrored);
        const double z = timeToZ(frame, beat.seconds);
        if (beat.measure_downbeat)
        {
            pushTaperedFloorQuad(
                vertices,
                indices,
                x0,
                x1,
                0.015,
                z - g_attack_line_half_length,
                z + g_attack_line_half_length,
                g_chord_box_color,
                g_attack_line_alpha,
                g_attack_line_alpha);
            pushTaperedFloorQuad(
                vertices,
                indices,
                x0,
                x1,
                0.015,
                z + g_attack_line_half_length,
                z + g_attack_line_half_length + g_attack_fade_length,
                g_beat_bar_color,
                1.0,
                0.0);
        }
        else
        {
            pushTaperedFloorQuad(
                vertices, indices, x0, x1, 0.015, z - 0.1, z, g_beat_bar_color, 0.0, 1.0);
            pushTaperedFloorQuad(
                vertices, indices, x0, x1, 0.015, z, z + 0.1, g_beat_bar_color, 1.0, 0.0);
        }
    }
    submitBatch(vertices, indices, posColorLayout(), color_fade_program.get(), nullptr);
}

// --- Hand-shape span rails: thick fading edge lines along each shape span at its hand
// window's fret lines, riding the hit line while active (purple marks arpeggio spans). ---
void HighwayRenderer::Impl::drawHandShapeRails(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    setFadeUniform();
    auto [vertices, indices] = scratch.colorBatch();
    for (const common::core::ShapeViewState& shape : frame.visible_shapes)
    {
        if (shape.drawn_end_seconds < frame.now_seconds ||
            shape.start_seconds > frame.span_end_seconds)
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
        const double rail_from = std::max(frame.now_seconds, shape.start_seconds);
        const double rail_to = std::min(shape.drawn_end_seconds, frame.span_end_seconds);
        std::vector<double>& times = scratch.window_times;
        windowSampleTimes(state, rail_from, rail_to, max_fhp_ramp_seconds, times);
        for (std::size_t sample = 1; sample < times.size(); ++sample)
        {
            const auto [a_x0, a_x1] = handWindowXAt(state, times[sample - 1], metrics, mirrored);
            const auto [b_x0, b_x1] = handWindowXAt(state, times[sample], metrics, mirrored);
            const double za = std::max(0.0, timeToZ(frame, times[sample - 1]));
            const double zb = std::max(0.0, timeToZ(frame, times[sample]));
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

// --- String lines (retained), under the fret lines and nut, on the z = 0 plane. The board
// paints in submission order (sequential view, depth test only), so the strings go down first
// and the fret lines and nut below draw over them. ---
void HighwayRenderer::Impl::drawStringLines()
{
    if (face_index_count > 0 && face_vertices.isValid() && face_indices.isValid())
    {
        bgfx::setVertexBuffer(0, face_vertices.get());
        bgfx::setIndexBuffer(face_indices.get(), 0, face_index_count);
        bgfx::setState(g_blended_state);
        bgfx::submit(g_board_view, color_program.get());
    }
}

// --- Board face: dynamic fret lines with Charter's three states (inactive, active
// within current and upcoming hand windows, and the sqrt-decay hit-flash that thickens up
// to 4x — a large part of the alive feel), drawn over the string lines and passing content.
// Fret lines run the board face's own vertical extent (the string grid alone — the gap below
// the grid base belongs to the chord boxes' bottom bars). ---
void HighwayRenderer::Impl::drawFretLines(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    const double face_bottom_y = faceBottomY();
    const double face_top_y = faceTopY();
    // Active fret lines: the current hand window's coverage (fractional mid-transition, so
    // the face lines' active state crossfades in lockstep with the sweeping border) plus
    // every window arriving soon at full weight.
    std::array<double, g_face_fret_count + 1> active{};
    for (int line = 0; line <= g_face_fret_count; ++line)
    {
        active.at(static_cast<std::size_t>(line)) = common::core::highwayHandWindowLineCoverage(
            frame.current_window, static_cast<double>(line));
    }
    for (const HandWindow& window : frame.hand_windows)
    {
        if (window.start_seconds > frame.now_seconds + g_fret_active_horizon_seconds ||
            window.end_seconds < frame.now_seconds)
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
    // Right-hand windows activate their lines under the same horizon: the lines the tapping
    // hand's light path crosses light up while the tap is held or arriving soon. Per-line
    // array, so overlap with the fretting hand's windows deduplicates itself; the path
    // union carries any tapped-slide morph.
    for (const common::core::HighwayTapOnsetViewState& tap : litTaps(
             frame.now_seconds,
             frame.now_seconds + g_fret_active_horizon_seconds,
             g_floor_light_release_seconds))
    {
        if (tap.path.front().seconds > frame.now_seconds + g_fret_active_horizon_seconds ||
            tap.path.back().seconds + g_floor_light_release_seconds < frame.now_seconds)
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
    auto [vertices, indices] = scratch.colorBatch();
    // One quad per fret line, four vertices each.
    vertices.reserve(4 * (static_cast<std::size_t>(g_face_fret_count) + 1));
    for (int line = 0; line <= g_face_fret_count; ++line)
    {
        const double x = common::core::highwayFretLineX(line, metrics, mirrored);
        const ArgbColor color = mixArgb(
            g_fret_inactive_color, g_fret_active_color, active.at(static_cast<std::size_t>(line)));
        const double half = line == 0 ? 0.05 : 0.025;
        pushFaceQuad(
            vertices, indices, x - half, x + half, face_bottom_y, face_top_y, 0.0, packAbgr(color));
    }
    submitBatch(vertices, indices, posColorLayout(), color_program.get(), nullptr);
}

// --- Fretboard markers: the classic inlay dots, drawn as world-square quads at
// code-derived positions from the single dot cell — round and exactly seated at every
// string count by construction. The stretched per-fret sheet this replaces rendered the
// dots elliptical (a fixed-aspect cell over the count-dependent board rect) and carried
// four hand-seat bugs; user-provided per-count art layers back on top as plan 58's
// override tier. Positions reuse the one marker law (isDottedFret): cycles 3/5/7/9 are
// singles at the grid's vertical middle, cycle 0 (frets 12, 24) the symmetric double.
// The quad is one fret slot square, so the dot's drawn width exactly matches the sheet it
// replaces (55 texels of a 256 cell over the slot) — only its height changes, by the
// 4.8% that made it elliptical. A double's upper quad may overhang the grid top; the quad
// is transparent outside the dot, and the dot itself stays inside the grid.
void HighwayRenderer::Impl::drawFretboardMarkers()
{
    if (inlay_texture.isValid())
    {
        const bool mirrored = state.options.mirrored;
        const double face_bottom_y = faceBottomY();
        const double face_top_y = faceTopY();
        auto [vertices, indices] = scratch.texturedBatch();
        // Half-texel inset so the quad samples strictly inside the dot cell's texels; zero
        // dimensions (decode failed) fall back to no inset.
        const float half_texel_u =
            inlay_texture_width > 0 ? 0.5F / static_cast<float>(inlay_texture_width) : 0.0F;
        const float half_texel_v =
            inlay_texture_height > 0 ? 0.5F / static_cast<float>(inlay_texture_height) : 0.0F;
        const float u0 = half_texel_u;
        const float v0 = half_texel_v;
        const float u1 = 1.0F - half_texel_u;
        const float v1 = 1.0F - half_texel_v;
        const double quad_half = metrics.first_fret_distance / 2.0;
        const double middle_y = (face_bottom_y + face_top_y) / 2.0;
        const double double_offset =
            g_inlay_double_separation_fraction * (face_top_y - face_bottom_y) / 2.0;
        const std::uint32_t white = packAbgr(0xFFFFFFFF);
        const auto push_dot = [&](const double center_x, const double center_y) {
            pushQuad(
                vertices,
                indices,
                makeUvVertex(center_x - quad_half, center_y - quad_half, 0.0, white, u0, v1),
                makeUvVertex(center_x + quad_half, center_y - quad_half, 0.0, white, u1, v1),
                makeUvVertex(center_x + quad_half, center_y + quad_half, 0.0, white, u1, v0),
                makeUvVertex(center_x - quad_half, center_y + quad_half, 0.0, white, u0, v0));
        };
        for (int fret = 1; fret <= g_face_fret_count; ++fret)
        {
            if (!isDottedFret(fret))
            {
                continue;
            }
            // The slot-midpoint law the note heads already use, not a restatement of it.
            const double center_x = common::core::highwayNoteCenterX(fret, metrics, mirrored);
            if (fret % 12 == 0)
            {
                push_dot(center_x, middle_y - double_offset);
                push_dot(center_x, middle_y + double_offset);
            }
            else
            {
                push_dot(center_x, middle_y);
            }
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
}

// --- Capo, over the skin like the hardware it is: the face from the nut to the capo's
// fret line dims (those frets do not exist to play — an absolute-fret chart is unreadable
// without seeing where its floor sits), and the clamp draws as a rimmed steel bar hugging
// the nut side of its line, overhanging the string grid. Crude first treatment (roadmap
// 25-Q6): flat quads, no art. ---
void HighwayRenderer::Impl::drawCapo()
{
    if (state.chart.capo > 0 && state.chart.capo <= g_face_fret_count)
    {
        const bool mirrored = state.options.mirrored;
        const double face_bottom_y = faceBottomY();
        const double face_top_y = faceTopY();
        auto [vertices, indices] = scratch.colorBatch();
        const auto capo_line = static_cast<double>(state.chart.capo);

        const double nut_x = common::core::highwayFretLineX(0, metrics, mirrored);
        const double capo_x = common::core::highwayFretLineX(capo_line, metrics, mirrored);
        const auto [dead_x0, dead_x1] = std::minmax(nut_x, capo_x);
        pushFaceQuad(
            vertices,
            indices,
            dead_x0,
            dead_x1,
            face_bottom_y,
            face_top_y,
            0.0,
            packAbgr(g_capo_dead_zone_color));

        const double near_x =
            common::core::highwayFretLineX(capo_line - g_capo_bar_near_fraction, metrics, mirrored);
        const double far_x =
            common::core::highwayFretLineX(capo_line - g_capo_bar_far_fraction, metrics, mirrored);
        const auto [bar_x0, bar_x1] = std::minmax(near_x, far_x);
        const double rim = g_capo_bar_rim_fraction * metrics.first_fret_distance;
        const double overhang = metrics.string_distance * g_capo_bar_overhang_strings;
        // The clamp lets the bar hang past the top lane freely but only as far DOWN as the floor
        // allows: the floor is the origin and nothing draws below it, while the gap under the
        // string grid is the chord box's bottom-bar thickness. At the shipped metrics the wanted
        // overhang (0.105) is larger than that gap (0.075), so the unclamped bar sat at y = -0.03
        // with its rim at -0.085 — punched through the board's floor from underneath.
        const double overhang_below = std::clamp(overhang, 0.0, std::max(0.0, face_bottom_y - rim));
        const double bar_y0 = face_bottom_y - overhang_below;
        const double bar_y1 = face_top_y + overhang;
        pushFaceQuad(
            vertices,
            indices,
            bar_x0 - rim,
            bar_x1 + rim,
            bar_y0 - rim,
            bar_y1 + rim,
            0.0,
            packAbgr(g_capo_bar_rim_color));
        pushFaceQuad(
            vertices, indices, bar_x0, bar_x1, bar_y0, bar_y1, 0.0, packAbgr(g_capo_bar_color));
        submitBatch(vertices, indices, posColorLayout(), color_program.get(), nullptr);
    }
}

// --- Section labels through the glyph atlas. ---
void HighwayRenderer::Impl::drawSectionLabels(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    auto [glyph_vertices, glyph_indices] = scratch.texturedBatch();

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
    const double section_y = faceTopY() + (metrics.string_distance * 1.5);
    // Sections ascend and a label is drawn at its own instant, so the two skip tests are the
    // two ends of a binary-searched range.
    for (const common::core::HighwaySectionViewState& section : std::ranges::subrange(
             std::ranges::lower_bound(
                 state.sections,
                 frame.now_seconds - 0.5,
                 std::ranges::less{},
                 &common::core::HighwaySectionViewState::seconds),
             std::ranges::upper_bound(
                 state.sections,
                 frame.span_end_seconds,
                 std::ranges::less{},
                 &common::core::HighwaySectionViewState::seconds)))
    {
        // Already upper-cased by the projection, which is where a pure function of the chart
        // belongs.
        (void)push_text(
            section.name,
            handWindowXAt(state, section.seconds, metrics, mirrored).first,
            section_y,
            timeToZ(frame, section.seconds),
            0.5,
            packAbgr(0xFFFFFFFF, 0.85));
    }

    const bgfx::TextureHandle glyph_texture = atlases.glyphs.get();
    submitBatch(
        glyph_vertices, glyph_indices, posColorUvLayout(), glyph_program.get(), &glyph_texture);
}

// --- Strike glow: an additive light that pops the instant a note crosses the fretboard and
// reads as a 100%-perfect strike (fret-hit-light-effect plan; deterministic note-arrival
// trigger — an input-gated game version swaps only the trigger source). Reuses the window
// light's soft-x-edge sprite under the additive blend, so a strike strictly ADDS luminance
// and pops identically on lit and unlit content. Deliberately the LAST board-view submission:
// the premultiplied inlay skin would punch dark dot silhouettes through a glow drawn earlier,
// and the hit-line text would dim it. The envelope is a stateless function of now - onset,
// per-onset with an inter-onset release clamp, so fast sections keep a discrete pop per strike
// instead of fusing into a shimmer. ---
void HighwayRenderer::Impl::drawStrikeGlow(const FrameContext& frame)
{
    const bool mirrored = state.options.mirrored;
    const double face_bottom_y = faceBottomY();
    const double face_top_y = faceTopY();
    auto [vertices, indices] = scratch.texturedBatch();
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
    // deliberately dark (what the interior does instead is an open decision). Both kinds
    // share the same two strips, so their onsets collect here in ascending order and each
    // clamps against the next window-edge strike of either kind.
    std::vector<double>& window_edge_onsets = scratch.window_edge_onsets;

    // Fretting-hand onset clusters, walked over the glow's own onset window: glow tails
    // outlive the passed-note fade, so the pass binary-searches state.chart.notes directly
    // instead of reusing the visible range (which drops a sustainless note
    // g_passed_fade_seconds after it crosses and would cap every tunable release). The walk
    // extends one clamp horizon past now so strikes at the hit line clamp against strikes
    // still approaching. Clusters use the chord boxes' own grouping rule: notes within the
    // onset epsilon strike together, and only non-tap members count toward the box. Tap
    // onsets are the other hand and glow from state.tap_onsets below.
    const auto glow_begin = std::ranges::lower_bound(
        state.chart.notes,
        frame.now_seconds - g_hit_glow_release_seconds,
        std::ranges::less{},
        [](const common::core::NoteViewState& note) { return note.start_seconds; });
    for (auto index = static_cast<std::size_t>(glow_begin - state.chart.notes.begin());
         index < state.chart.notes.size();)
    {
        const double cluster_start = state.chart.notes[index].start_seconds;
        if (cluster_start > frame.now_seconds + clamp_horizon)
        {
            break;
        }
        std::size_t cluster_end = index + 1;
        while (cluster_end < state.chart.notes.size() &&
               std::abs(state.chart.notes[cluster_end].start_seconds - cluster_start) <
                   g_onset_match_epsilon)
        {
            ++cluster_end;
        }
        bool any_open = false;
        for (std::size_t member = index; member < cluster_end; ++member)
        {
            const common::core::NoteViewState& note = state.chart.notes[member];
            // A silently-held stop strikes nothing, so it lights no fret line.
            if (!common::core::rightHandOnset(note.attack) &&
                !common::core::silentHold(note.attack))
            {
                any_open = any_open || common::core::openString(note);
            }
        }
        // Whether a box covers this cluster is the projection's answer, not a member count of this
        // loop's own: the glow lights a boxed cluster's window edges INSTEAD of its fret lines, so
        // a second reading of "is there a box here" would light both, or neither, wherever the two
        // disagreed — and the box rule has already moved twice under this reader ([C2], then LAW
        // IV's simultaneity amendment) without this line needing a word changed.
        //
        // BOTH PRODUCERS, which is what the line above only claimed until 2026-08-30 (review
        // R2(b)): a strum's own chord box, and the ARPEGGIO mark its covering span draws. A lone
        // note under a bracket wears no chord box, so the old reading lit its per-fret lines
        // straight through a mark already standing over them. Both answers are published per group,
        // whole-song, so this stays one read of one authority.
        const common::core::HighwayChordGroupViewState& covering =
            state.chord_groups[state.note_group[index]];
        const bool boxed = covering.box_treatment != common::core::HighwayChordBoxTreatment::None ||
                           covering.arpeggio_mark;
        if (boxed || any_open)
        {
            window_edge_onsets.push_back(cluster_start);
        }
        if (!boxed && cluster_start <= frame.now_seconds)
        {
            // Fretted singles (including a fretted note under a simultaneous tap): each
            // lights its own fret lines. A later strike on the same fret clamps the tail
            // even when it folds into a chord box whose edge frets miss these lines — the
            // error is a slightly shorter tail, erring toward discreteness.
            for (std::size_t member = index; member < cluster_end; ++member)
            {
                const common::core::NoteViewState& note = state.chart.notes[member];
                if (common::core::rightHandOnset(note.attack) ||
                    common::core::silentHold(note.attack) || note.fret <= 0)
                {
                    continue;
                }
                double spacing = std::numeric_limits<double>::infinity();
                for (std::size_t next = cluster_end; next < state.chart.notes.size(); ++next)
                {
                    const common::core::NoteViewState& later = state.chart.notes[next];
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
                    frame.now_seconds - note.start_seconds,
                    common::core::highwayHitGlowRelease(
                        g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing));
                if (envelope > 0.0)
                {
                    // The fretting hand's slot, so a natural's strike lights the fret its
                    // node sits in rather than the pair around fret zero.
                    const int slot_fret = common::core::fretFor(note);
                    light_line(slot_fret - 1, envelope);
                    light_line(slot_fret, envelope);
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
        if (window_edge_onsets[onset] > frame.now_seconds)
        {
            break;
        }
        const double spacing = onset + 1 < window_edge_onsets.size()
                                   ? window_edge_onsets[onset + 1] - window_edge_onsets[onset]
                                   : std::numeric_limits<double>::infinity();
        window_edge_glow = std::max(
            window_edge_glow,
            common::core::highwayHitGlowIntensity(
                frame.now_seconds - window_edge_onsets[onset],
                common::core::highwayHitGlowRelease(
                    g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds, spacing)));
    }

    // Slide landings and bend targets: every scored arrival pops the glow at its geometry
    // (the game registers these as hit-or-miss, and the editor previews 100%-perfect play,
    // so each one shows its success feedback). A pitched slide keyframe is a fret arrival —
    // the finger lands on a new fret, the tail kinks there, the FHP window ramps there —
    // and pops the landing's lines, whichever hand slides; unpitched trail-offs are
    // pressure already releasing and contribute nothing (the tap light's rule). A bend
    // target is a pitch arrival on the fret the finger stays planted on, so it pops that
    // same line pair: each curve point ending a sloped segment (bend reached, release
    // completed) is an arrival, while flat holds and the onset point are not — the strike
    // already covers the onset. No inter-onset clamp: these are sparse, never the
    // machine-gun case the clamp exists for, and the per-line max absorbs overlap. The
    // sustain-aware range query covers a long sustain sliding or bending at its very end,
    // whose onset left the cluster walk's window long ago.
    const auto [keyframe_first, keyframe_last] = common::core::visibleEventRange(
        state.chart.notes,
        sustain_prefix_max,
        frame.now_seconds - g_hit_glow_release_seconds,
        frame.now_seconds);
    for (std::size_t index = keyframe_first; index < keyframe_last; ++index)
    {
        const common::core::NoteViewState& note = state.chart.notes[index];
        for (const common::core::KeyframeViewState& keyframe : note.slides)
        {
            // A scrape's stops are unpitched pick travel and pop no fret line; the falls-away
            // terminal is not in this list at all (W9-L), which is the same exclusion it always
            // had through the flag.
            if (common::core::isScrape(note.attack) || keyframe.fret <= 0)
            {
                continue;
            }
            const double envelope = common::core::highwayHitGlowIntensity(
                frame.now_seconds - keyframe.seconds, g_hit_glow_release_seconds);
            if (envelope > 0.0)
            {
                light_line(keyframe.fret - 1, envelope);
                light_line(keyframe.fret, envelope);
            }
        }
        for (std::size_t point = 1; note.fret > 0 && point < note.bend.size(); ++point)
        {
            const common::core::BendPointViewState& segment_from = note.bend[point - 1];
            const common::core::BendPointViewState& arrival = note.bend[point];
            if (std::is_eq(arrival.semitones <=> segment_from.semitones))
            {
                continue; // a flat hold segment ends in no arrival
            }
            const double envelope = common::core::highwayHitGlowIntensity(
                frame.now_seconds - arrival.seconds, g_hit_glow_release_seconds);
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
    //
    // A tap's glow depends on its own onset alone (its path plays no part), so onsets
    // ascending makes the lit ones one binary-searched run: from the first whose strike is
    // still inside the release, up to the last that has already struck. The spacing walk
    // below still reads onsets past that run — it looks FORWARD for the next same-geometry
    // strike — which is why it indexes the whole list rather than the run.
    const auto glow_first = static_cast<std::size_t>(
        std::ranges::partition_point(
            state.tap_onsets,
            [&](const common::core::HighwayTapOnsetViewState& tap) {
                return frame.now_seconds - tap.seconds >= g_hit_glow_release_seconds;
            }) -
        state.tap_onsets.begin());
    const auto glow_last = static_cast<std::size_t>(
        std::ranges::upper_bound(
            state.tap_onsets,
            frame.now_seconds,
            std::ranges::less{},
            &common::core::HighwayTapOnsetViewState::seconds) -
        state.tap_onsets.begin());
    for (std::size_t tap_index = glow_first; tap_index < glow_last; ++tap_index)
    {
        const common::core::HighwayTapOnsetViewState& tap = state.tap_onsets[tap_index];
        const double since = frame.now_seconds - tap.seconds;
        double spacing = std::numeric_limits<double>::infinity();
        for (std::size_t next = tap_index + 1; next < state.tap_onsets.size(); ++next)
        {
            const common::core::HighwayTapOnsetViewState& later = state.tap_onsets[next];
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
        const auto [low_x, high_x] = handWindowXAt(state, frame.now_seconds, metrics, mirrored);
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

    // The overlay is one more build-and-submit pass, on the same frame deadline as the board's,
    // so it takes the same shared batch. Its buffers are handed out cleared, which is what makes
    // reusing them across draw() and this second entry point safe.
    auto [vertices, indices] = scratch.colorBatch();
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
