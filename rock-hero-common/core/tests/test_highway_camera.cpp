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

// Focus targeting: the active hand window plus upcoming windows inside the scan horizon define
// the framed fret-line range; the focus is its world middle blended toward the whole-neck spot.
TEST_CASE("Highway camera targets the scanned hand window", "[core][highway][camera]")
{
    const HighwayMetrics metrics{};

    // Active window at fret 5 width 4 (lines 4..8); an upcoming window at fret 9 width 4
    // (lines 8..12) inside the horizon widens the range to lines 4..12.
    const HighwayViewState state = makeStateWithFhps({
        HighwayFhpView{.seconds = 0.0, .fret = 5, .width = 4},
        HighwayFhpView{.seconds = 2.0, .fret = 9, .width = 4},
        HighwayFhpView{.seconds = 60.0, .fret = 1, .width = 4}, // beyond the horizon: ignored
    });

    const HighwayCameraTarget target = makeHighwayCameraTarget(state, 1.0, metrics);

    const double middle =
        (highwayFretLineX(4, metrics, false) + highwayFretLineX(12, metrics, false)) / 2.0;
    const double expected =
        middle + ((metrics.focus_whole_neck_x - middle) * metrics.focus_whole_neck_blend) +
        metrics.focus_x_offset;
    CHECK(target.focus_x == Catch::Approx(expected));
    CHECK(target.span == Catch::Approx(8.0));

    // With no hand positions the fallback frames the reference window at the nut.
    const HighwayCameraTarget fallback =
        makeHighwayCameraTarget(makeStateWithFhps({}), 0.0, metrics);
    CHECK(fallback.span == Catch::Approx(metrics.camera_reference_span));
}

// Smoothing is frame-rate independent (two half steps equal one full step), converges toward a
// fixed target, and the first advance snaps.
TEST_CASE("Highway camera smoothing is frame-rate independent", "[core][highway][camera]")
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

    // Convergence: after several seconds the camera is essentially at the target, and the pose
    // derives height/pull-back from the smoothed span.
    HighwayCamera converged;
    converged.advance(start, 0.0, metrics);
    for (int frame = 0; frame < 600; ++frame)
    {
        converged.advance(target, 1.0 / 60.0, metrics);
    }
    const HighwayCameraPose pose = converged.pose(metrics);
    CHECK(pose.x == Catch::Approx(10.0).margin(1.0e-6));
    CHECK(
        pose.y ==
        Catch::Approx(metrics.camera_y_base + (4.0 * metrics.camera_span_gain)).margin(1.0e-6));
    CHECK(
        pose.z ==
        Catch::Approx(metrics.camera_z_base - (4.0 * metrics.camera_span_gain)).margin(1.0e-6));
}

// The verticality invariant holds exactly at the zero-rotation configuration: with the Charter
// pitch/yaw zeroed the chain reduces to a pure translation plus perspective, and a
// world-vertical segment keeps a constant NDC X. (The shipped defaults deliberately rotate — see
// the next case — so this pins the machinery, not the default look.)
TEST_CASE("Highway camera projects world-vertical lines screen-vertical", "[core][highway][camera]")
{
    HighwayMetrics metrics{};
    metrics.camera_pitch_radians = 0.0;
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

// The shipped defaults carry Charter's small rotations: verticals tilt, but only slightly —
// the angled-neck look must never degenerate into a visibly skewed picture.
TEST_CASE(
    "Highway camera default rotations keep verticals near-vertical", "[core][highway][camera]")
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
            // Rotated but bounded: a few degrees of tilt at most across a string-stack height.
            const double ndc_dx = std::abs(top[0] - bottom[0]);
            const double ndc_dy = std::abs(top[1] - bottom[1]);
            CHECK(ndc_dx < 0.15 * ndc_dy);
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
        // With the default pitch/yaw the board floor slopes gently (the angled-neck look) —
        // about -0.13 NDC over 6 world units at the reference pose. Bound it so the slope can
        // never silently degenerate into a skewed picture.
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
