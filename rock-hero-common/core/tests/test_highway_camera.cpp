#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Builds a state with only fret-hand positions, the sole input the camera focus consumes.
[[nodiscard]] HighwayViewState makeStateWithFhps(
    std::vector<HighwayFhpView> fhps, bool mirrored = false)
{
    HighwayViewState state;
    state.string_count = 6;
    state.options.mirrored = mirrored;
    state.fret_hand_positions = std::move(fhps);
    return state;
}

} // namespace

// Focus targeting: the active hand window plus every window in the current and next framing
// zone define the framed fret-line range; the focus is its world middle blended toward the
// derived whole-neck spot and nudged toward the body. The expected value is computed from the
// shipped constants independently rather than by re-running the production formula, so a change
// to the formula fails here instead of silently agreeing with itself.
TEST_CASE("Highway camera targets the scanned hand window", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    // Active window at fret 5 width 4 (lines 4..8); an upcoming window at fret 9 width 4
    // (lines 8..12) in the next zone widens the range to lines 4..12.
    HighwayViewState state = makeStateWithFhps({
        HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4},
        HighwayFhpView{.seconds = 1.5, .fret = 9, .width = 4},
        HighwayFhpView{.seconds = 60.0, .fret = 1, .width = 4}, // two zones out: not framed yet
    });
    state.camera_zone_starts = {0.0, 4.0, 8.0};

    const HighwayCameraTarget target = makeHighwayCameraTarget(state, 1.0, metrics);

    // Lines 4..12 at the shipped 1.1 fret width: world middle 8.8, pulled 10 percent toward the
    // neck reference (24 * 1.1 * 0.4 = 10.56), then shifted one fret width (1.1) toward the body.
    CHECK(target.focus_x == Catch::Approx((0.9 * 8.8) + (0.1 * 10.56) + 1.1));
    CHECK(target.span == Catch::Approx(8.0));

    // With no hand positions the fallback frames the reference window at the nut.
    const HighwayCameraTarget fallback =
        makeHighwayCameraTarget(makeStateWithFhps({}), 0.0, metrics);
    CHECK(fallback.span == Catch::Approx(metrics.camera_reference_span));

    // Before the first arrival the first placement's window already holds (the opening scroll
    // shows where the hand belongs), so the camera frames it even from outside the scanned zones.
    HighwayViewState opening_state =
        makeStateWithFhps({HighwayFhpView{.seconds = 60.0, .fret = 9, .width = 4}});
    opening_state.camera_zone_starts = {0.0, 4.0, 8.0};
    const HighwayCameraTarget opening = makeHighwayCameraTarget(opening_state, 0.0, metrics);
    // Lines 8..12: world middle 11.0, same blend and body shift.
    CHECK(opening.focus_x == Catch::Approx((0.9 * 11.0) + (0.1 * 10.56) + 1.1));
    CHECK(opening.span == Catch::Approx(4.0));
}

// With camera framing zones the scan window is quantized: everything defined during the
// current zone and the next is framed — consumed or not — so the target holds perfectly still
// for whole zones and steps only at their boundaries (user direction 2026-07-29, matching the
// source game's documented rule).
TEST_CASE("Highway camera frames the current and next zone", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    HighwayViewState state = makeStateWithFhps({
        HighwayFhpView{.seconds = 0.5, .fret = 5, .width = 4},   // zone 0: lines 4..8
        HighwayFhpView{.seconds = 8.5, .fret = 9, .width = 4},   // zone 1: lines 8..12
        HighwayFhpView{.seconds = 16.5, .fret = 14, .width = 4}, // zone 2: lines 13..17
    });
    state.camera_zone_starts = {0.0, 8.0, 16.0, 24.0};
    const auto target_at = [&](const double now) {
        return makeHighwayCameraTarget(state, now, metrics);
    };

    // Inside zone 0 the frame covers zones 0 and 1 (lines 4..12); zone 2's positions do not
    // pull yet, and the target holds perfectly still for the whole zone — including after the
    // fret-5 window is the consumed past (rest is the point).
    CHECK(target_at(1.0).span == Catch::Approx(8.0));
    CHECK(target_at(7.9).span == Catch::Approx(8.0));
    CHECK(target_at(7.9).focus_x == Catch::Approx(target_at(1.0).focus_x));

    // Crossing into zone 1 steps the window once: frames zones 1 and 2. Until the fret-9
    // placement arrives the active fret-5 window still holds the base (lines 4..17); after it
    // arrives the frame is lines 8..17.
    CHECK(target_at(8.1).span == Catch::Approx(13.0));
    CHECK(target_at(9.0).span == Catch::Approx(9.0));

    // A fretted note framed for its zone stays framed after it is consumed (no per-note
    // narrowing churn): a tap at fret 20 early in zone 0 keeps the frame open to line 20
    // through the zone even though it stopped ringing long ago.
    state.notes.push_back(HighwayNoteView{.start_seconds = 0.6, .end_seconds = 0.7, .fret = 20});
    CHECK(target_at(7.9).span == Catch::Approx(16.0));
}

// A fretted note outside the hand window — a two-hand tap floats far above the fretting hand,
// which no longer anchors it — must still be framed: the camera span widens up to the tap even
// though the hand window (and its light) stays low. Open strings never reframe, and content in
// an already-passed zone drops out of the frame (within a zone it deliberately stays).
TEST_CASE("Highway camera frames taps above the hand window", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    HighwayViewState state = makeStateWithFhps({
        HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4}, // hand window fret lines 4..8
    });
    state.camera_zone_starts = {0.0, 1.0, 2.0, 3.0};
    // Notes ascend by onset (the view-state contract the scan's horizon break relies on): a note
    // back in zone 0 (now past the scanned window) and an open string must not reframe, while the
    // tapped note at fret 15 inside the scanned zones — with no hand position covering it — must.
    state.notes.push_back(HighwayNoteView{.start_seconds = 0.5, .end_seconds = 0.6, .fret = 20});
    state.notes.push_back(HighwayNoteView{.start_seconds = 1.4, .end_seconds = 1.4, .fret = 0});
    state.notes.push_back(HighwayNoteView{.start_seconds = 1.5, .end_seconds = 1.5, .fret = 15});

    // At now = 1.5 the scan covers zones 1 and 2 (the window [1.0, 3.0)), so the fret-20 note
    // back in zone 0 is behind window_start and no longer widens the frame.
    const HighwayCameraTarget target = makeHighwayCameraTarget(state, 1.5, metrics);
    // The range widens from the hand window (lines 4..8) up to the tap's fret line 15: span 11.
    CHECK(target.span == Catch::Approx(11.0));

    // The same hand window with no tap frames only itself (lines 4..8 = span 4).
    HighwayViewState windowed_state =
        makeStateWithFhps({HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4}});
    windowed_state.camera_zone_starts = state.camera_zone_starts;
    CHECK(makeHighwayCameraTarget(windowed_state, 1.5, metrics).span == Catch::Approx(4.0));
}

// An empty zone list is one unbounded zone, not a special case: the camera has a single scan
// path (the seconds-window fallback was deleted 2026-07-30). This pins the contract that makes
// that safe — in production zones are empty only when the arrangement has no chart, and a state
// with no chart also has no hand positions and no notes, so the unbounded window frames nothing
// and the target is the reference window at the nut. A state that violates that invariant is
// framed whole, which is the honest consequence and is asserted here so it cannot surprise.
TEST_CASE("Highway camera treats missing framing zones as one open zone", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    // The production shape: no zones, and nothing to frame.
    const HighwayCameraTarget empty = makeHighwayCameraTarget(makeStateWithFhps({}), 0.0, metrics);
    CHECK(empty.span == Catch::Approx(metrics.camera_reference_span));

    // A hand-built state with content but no zones frames all of it at once, past and future.
    const HighwayViewState unzoned = makeStateWithFhps({
        HighwayFhpView{.seconds = -30.0, .fret = 2, .width = 4}, // lines 1..5
        HighwayFhpView{.seconds = 900.0, .fret = 9, .width = 4}, // lines 8..12
    });
    CHECK(makeHighwayCameraTarget(unzoned, 0.0, metrics).span == Catch::Approx(11.0));
}

// The smoother is frame-rate independent (two half steps equal one full step; it is the exact
// closed-form solution over the frame), converges toward a fixed target, the first advance
// snaps, and the pose derives height/pull-back from the smoothed span.
TEST_CASE("Highway camera smoother is frame-rate independent", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};
    const HighwayCameraTarget start{.focus_x = 0.0, .span = 4.0};
    const HighwayCameraTarget target{.focus_x = 10.0, .span = 8.0};

    HighwayCamera whole_step;
    whole_step.advance(start, 0.0, metrics); // First advance snaps to the start target.
    whole_step.advance(target, 0.5, metrics);

    HighwayCamera half_steps;
    half_steps.advance(start, 0.0, metrics);
    half_steps.advance(target, 0.25, metrics);
    half_steps.advance(target, 0.25, metrics);

    CHECK(whole_step.pose(metrics).x == Catch::Approx(half_steps.pose(metrics).x));
    CHECK(whole_step.pose(metrics).y == Catch::Approx(half_steps.pose(metrics).y));

    // Convergence: after enough seconds the camera rests at the target, and the pose derives
    // height/pull-back from the smoothed span. The loop is generous (20 s) so it converges
    // across the slow settle of the languid default rate.
    HighwayCamera converged;
    converged.advance(start, 0.0, metrics);
    for (int frame = 0; frame < 1200; ++frame)
    {
        converged.advance(target, 1.0 / 60.0, metrics);
    }
    const HighwayCameraPose pose = converged.pose(metrics);
    CHECK(pose.x == Catch::Approx(10.0).margin(1.0e-3));
    CHECK(
        pose.y ==
        Catch::Approx(metrics.camera_y_base + (4.0 * metrics.camera_span_gain)).margin(1.0e-3));
    CHECK(
        pose.z ==
        Catch::Approx(metrics.camera_z_base - (4.0 * metrics.camera_span_gain)).margin(1.0e-3));
}

// The third-order smoother eases in from zero acceleration (no onset jolt): the first frame's
// travel from rest is cubic in dt, an order smaller than the second-order spring's quadratic
// kick. It carries velocity so a target reversal never flips direction instantly, and it
// settles before an entering window reaches the hit line without overshooting.
TEST_CASE("Highway camera smoother eases in without a jolt", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};
    const HighwayCameraTarget start{.focus_x = 0.0, .span = 4.0};
    const HighwayCameraTarget forward{.focus_x = 10.0, .span = 4.0};
    const HighwayCameraTarget backward{.focus_x = -10.0, .span = 4.0};
    const double dt = 1.0 / 60.0;
    const double omega = metrics.focus_spring_per_second;

    HighwayCamera camera;
    camera.advance(start, 0.0, metrics);

    // From rest the first frame's travel is cubic in dt (distance x omega^3 x dt^3 / 6), the
    // signature of continuous acceleration — an order below the second-order spring's quadratic
    // departure (distance x omega^2 x dt^2 / 2), so the onset has no instant kick.
    camera.advance(forward, dt, metrics);
    const double first_step = camera.pose(metrics).x;
    CHECK(first_step > 0.0);
    CHECK(first_step < 10.0 * omega * omega * omega * dt * dt * dt / 6.0);

    // Build up forward momentum, then reverse the target: the very next frame still moves
    // forward — the carried velocity decays smoothly instead of snapping to the new direction.
    for (int frame = 0; frame < 10; ++frame)
    {
        camera.advance(forward, dt, metrics);
    }
    const double before_reversal = camera.pose(metrics).x;
    camera.advance(backward, dt, metrics);
    CHECK(camera.pose(metrics).x > before_reversal);

    // Approaches the target monotonically and converges: the languid triple pole takes several
    // seconds to settle (~8 / omega), so the loop is generous; the per-frame check pins that it
    // never overshoots or wobbles on the way (three equal real poles, no zeros → monotone).
    HighwayCamera entering;
    entering.advance(start, 0.0, metrics);
    double previous = 0.0;
    for (int frame = 0; frame < 600; ++frame)
    {
        entering.advance(forward, dt, metrics);
        CHECK(entering.pose(metrics).x >= previous); // monotone: no overshoot, no wobble
        previous = entering.pose(metrics).x;
    }
    CHECK(entering.pose(metrics).x == Catch::Approx(10.0).margin(0.2));
}

// The verticality invariant holds exactly at the zero-rotation configuration: with the yaw
// zeroed the chain reduces to a pure translation plus perspective, and a world-vertical segment
// keeps a constant NDC X. (This pins the machinery across many poses and aspects; the next case
// pins the same property at the shipped defaults.)
TEST_CASE("Highway camera projects world-vertical lines screen-vertical", "[core][highway][camera]")
{
    HighwayMetrics metrics{};
    metrics.camera_yaw_radians = 0.0;

    for (const double focus_x : {0.0, 2.4, 12.0, 28.8, -6.0})
    {
        for (const double span : {4.0, 8.0, 12.0})
        {
            const HighwayCameraPose pose{
                .x = focus_x,
                .y = metrics.camera_y_base +
                     ((span - metrics.camera_reference_span) * metrics.camera_span_gain),
                .z = metrics.camera_z_base -
                     ((span - metrics.camera_reference_span) * metrics.camera_span_gain),
            };
            for (const double aspect : {16.0 / 9.0, 4.0 / 3.0, 21.0 / 9.0})
            {
                const HighwayMat4 world_to_clip =
                    makeHighwayWorldToClip(pose, aspect, false, metrics);
                for (const double x : {0.0, 6.0, 14.4, 28.8})
                {
                    for (const double z : {0.5, 4.0, 16.0, 32.0})
                    {
                        const auto bottom = world_to_clip.projectPoint(x, 0.0, z);
                        const auto top = world_to_clip.projectPoint(x, 2.8, z);
                        CHECK(bottom[0] == Catch::Approx(top[0]).margin(1.0e-12));
                    }
                }
            }
        }
    }
}

// The shipped defaults carry only Charter's yaw (the string slope); the forward pitch is zero
// by user decision (2026-07-11). A yaw-only chain never mixes world Y into clip W or X, so
// verticals must project EXACTLY vertical — the regression that guards "no forward tilt".
TEST_CASE(
    "Highway camera default rotations keep verticals exactly vertical", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};
    const HighwayCameraPose pose{.x = 5.0, .y = metrics.camera_y_base, .z = metrics.camera_z_base};
    const HighwayMat4 world_to_clip = makeHighwayWorldToClip(pose, 16.0 / 9.0, false, metrics);

    for (const double x : {0.0, 6.0, 14.4})
    {
        for (const double z : {0.5, 4.0, 16.0})
        {
            const auto bottom = world_to_clip.projectPoint(x, 0.0, z);
            const auto top = world_to_clip.projectPoint(x, 2.8, z);
            CHECK(bottom[0] == Catch::Approx(top[0]).margin(1.0e-12));
        }
    }
}

// Square pixels: world-square geometry must project screen-square at every viewport shape, or
// note heads and inlay dots render as ellipses. This guards the removal of Charter's +0.05
// vertical screen-scale lift (2026-07-30), which stretched the picture 5 to 10 percent
// vertically depending on window shape and which no test caught in either direction.
//
// The check is that a world X extent and an equal world Y extent, at the same depth, occupy the
// same fraction of the screen: NDC is normalized per axis, so equal screen lengths means the X
// delta times the aspect ratio equals the Y delta. The yaw is zeroed because it is a rigid
// rotation that varies depth along X — it cannot make pixels non-square, but it would tilt the
// probe pair and obscure the scale property being pinned here.
TEST_CASE("Highway camera projects square pixels at every aspect", "[core][highway][camera]")
{
    HighwayMetrics metrics{};
    metrics.camera_yaw_radians = 0.0;
    const HighwayCameraPose pose{.x = 6.0, .y = metrics.camera_y_base, .z = metrics.camera_z_base};
    constexpr double extent = 2.0;

    // Both branches of the projection's min() pair, plus the seam at exactly 2:1.
    for (const double aspect : {1.0, 4.0 / 3.0, 16.0 / 9.0, 2.0, 21.0 / 9.0, 32.0 / 9.0})
    {
        const HighwayMat4 world_to_clip = makeHighwayWorldToClip(pose, aspect, false, metrics);
        for (const double z : {2.0, 8.0, 24.0})
        {
            const auto origin = world_to_clip.projectPoint(pose.x, 0.0, z);
            const auto along_x = world_to_clip.projectPoint(pose.x + extent, 0.0, z);
            const auto along_y = world_to_clip.projectPoint(pose.x, extent, z);
            CHECK(
                (along_x[0] - origin[0]) * aspect ==
                Catch::Approx(along_y[1] - origin[1]).margin(1.0e-12));
        }
    }
}

// The board pin: the anchor point (focus X, board surface, hit line) lands exactly at the
// configured NDC height for every pose, while its screen X stays wherever the projection put
// it — the board slides freely left/right on a fixed-height anchor.
TEST_CASE(
    "Highway camera pins the board anchor to the configured height", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    for (const double focus_x : {0.0, 2.4, 12.0, 28.8})
    {
        const HighwayCameraPose pose{
            .x = focus_x, .y = metrics.camera_y_base, .z = metrics.camera_z_base
        };
        const HighwayMat4 world_to_clip = makeHighwayWorldToClip(pose, 16.0 / 9.0, false, metrics);

        const auto anchor = world_to_clip.projectPoint(focus_x, 0.0, 0.0);
        CHECK(anchor[1] == Catch::Approx(metrics.ndc_pin_y).margin(1.0e-9));
        // The anchor sits under the camera on X; the small default yaw shifts it slightly off
        // screen-center (about +0.01 NDC at the reference pose), and the pin never touches X.
        CHECK(std::abs(anchor[0]) < 0.03);

        const auto off_focus = world_to_clip.projectPoint(focus_x + 6.0, 0.0, 0.0);
        CHECK(off_focus[0] > 0.0);
        // With the default yaw the board floor slopes gently (the angled-neck look) — about
        // -0.09 NDC over 6 world units at the reference pose. Bound it so the slope can never
        // silently degenerate into a skewed picture.
        CHECK(off_focus[1] == Catch::Approx(metrics.ndc_pin_y).margin(0.2));
        CHECK(off_focus[1] < metrics.ndc_pin_y);
    }
}

// The lefty mirror reflects the whole picture: a mirrored camera over mirrored geometry projects
// every point to the negated screen X of the unmirrored setup, with identical heights.
TEST_CASE("Highway camera mirror reflects the projected picture", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    const HighwayViewState plain =
        makeStateWithFhps({HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4}}, false);
    const HighwayViewState mirrored =
        makeStateWithFhps({HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4}}, true);

    const HighwayCameraTarget plain_target = makeHighwayCameraTarget(plain, 0.0, metrics);
    const HighwayCameraTarget mirrored_target = makeHighwayCameraTarget(mirrored, 0.0, metrics);
    CHECK(mirrored_target.focus_x == Catch::Approx(-plain_target.focus_x));
    CHECK(mirrored_target.span == Catch::Approx(plain_target.span));

    const HighwayCameraPose plain_pose{
        .x = plain_target.focus_x, .y = metrics.camera_y_base, .z = metrics.camera_z_base
    };
    const HighwayCameraPose mirrored_pose{
        .x = mirrored_target.focus_x, .y = metrics.camera_y_base, .z = metrics.camera_z_base
    };
    // The mirrored projection flips the yaw with the geometry, keeping the reflection exact.
    const HighwayMat4 plain_clip = makeHighwayWorldToClip(plain_pose, 16.0 / 9.0, false, metrics);
    const HighwayMat4 mirrored_clip =
        makeHighwayWorldToClip(mirrored_pose, 16.0 / 9.0, true, metrics);

    for (const double x : {0.0, 3.0, 8.4})
    {
        for (const double z : {0.5, 8.0, 24.0})
        {
            const auto plain_point = plain_clip.projectPoint(x, 0.7, z);
            const auto mirrored_point = mirrored_clip.projectPoint(-x, 0.7, z);
            CHECK(mirrored_point[0] == Catch::Approx(-plain_point[0]).margin(1.0e-9));
            CHECK(mirrored_point[1] == Catch::Approx(plain_point[1]).margin(1.0e-9));
        }
    }
}

// The background matrix keeps the pin while riding the divided camera; the sway is a pure
// function of injected time (zero at time zero).
TEST_CASE("Highway background matrix parallaxes with the pin intact", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};
    const HighwayCameraPose pose{.x = 12.0, .y = metrics.camera_y_base, .z = metrics.camera_z_base};

    const HighwayMat4 background =
        makeHighwayBackgroundWorldToClip(pose, 16.0 / 9.0, 0.0, false, metrics);

    const double divisor = metrics.background_parallax_divisor;
    const auto anchor = background.projectPoint(pose.x / divisor, 0.0, 0.0);
    CHECK(anchor[1] == Catch::Approx(metrics.ndc_pin_y).margin(1.0e-9));

    // A quarter sway period later the picture has shifted horizontally: injected time drives it.
    const HighwayMat4 swayed = makeHighwayBackgroundWorldToClip(
        pose, 16.0 / 9.0, 0.25 / metrics.background_sway_hertz, false, metrics);
    const auto swayed_anchor = swayed.projectPoint(pose.x / divisor, 0.0, 0.0);
    CHECK(swayed_anchor[0] != Catch::Approx(anchor[0]).margin(1.0e-6));
}

// Depth regression for the plan-25 Phase 3 checkpoint finding: the near plane must be
// camera-relative (eye depth), never anchored at world Z. The hit line (world z = 0) and the
// short passed-note region behind it sit inside the depth volume, and depth stays monotonic
// along the time axis so far-to-near draw ordering can rely on the depth test.
TEST_CASE("Highway camera keeps the hit line inside the depth volume", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};
    const HighwayCameraPose pose{.x = 6.0, .y = metrics.camera_y_base, .z = metrics.camera_z_base};
    const HighwayMat4 world_to_clip = makeHighwayWorldToClip(pose, 16.0 / 9.0, false, metrics);

    const auto hit_line = world_to_clip.projectPoint(pose.x, 0.0, 0.0);
    CHECK(hit_line[2] >= 0.0);
    CHECK(hit_line[2] < 1.0);

    // One and a half world units behind the hit line: still in front of the camera, still
    // inside the depth volume (passed notes fade out in this region).
    const auto behind = world_to_clip.projectPoint(pose.x, 0.0, -1.5);
    CHECK(behind[2] >= 0.0);
    CHECK(behind[2] < 1.0);

    // Depth increases monotonically toward the horizon.
    const auto near_note = world_to_clip.projectPoint(pose.x, 0.35, 4.0);
    const auto far_note = world_to_clip.projectPoint(pose.x, 0.35, 24.0);
    CHECK(behind[2] < hit_line[2]);
    CHECK(hit_line[2] < near_note[2]);
    CHECK(near_note[2] < far_note[2]);
}

} // namespace rock_hero::common::core
