#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <rock_hero/common/core/highway/highway_camera.h>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// View translation: camera-relative world coordinates.
[[nodiscard]] HighwayMat4 makeViewTranslation(const HighwayCameraPose& camera)
{
    HighwayMat4 translation = HighwayMat4::identity();
    translation.m[3] = -camera.x;
    translation.m[7] = -camera.y;
    translation.m[11] = -camera.z;
    return translation;
}

// Rotation about the Y axis (yaw). Positive angles bring the +X (body-side) neck end closer to
// the camera, which is what slopes the strings on screen.
[[nodiscard]] HighwayMat4 makeRotationY(const double radians)
{
    HighwayMat4 rotation = HighwayMat4::identity();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    rotation.m[0] = c;
    rotation.m[2] = s;
    rotation.m[8] = -s;
    rotation.m[10] = c;
    return rotation;
}

// The camera chain: translate -> yaw -> wide perspective -> NDC pin.
//
// The yaw is Charter's rotY constant and is load-bearing for the look (it slopes the strings
// like a held guitar neck; source analysis 2026-07-11). It is the chain's ONLY rotation:
// Charter's forward pitch (rotX = 0.06) tilts the whole picture and leans verticals, which the
// user rejected. Because no X rotation exists, a yaw-only chain never mixes world Y into clip W
// or X, which is what makes world-vertical project exactly screen-vertical — the property the
// regression tests pin at the shipped defaults. Reintroducing a pitch would break it.
[[nodiscard]] HighwayMat4 makePinnedProjection(
    const HighwayCameraPose& camera, double aspect_ratio, const bool mirrored,
    const HighwayMetrics& metrics)
{
    const double aspect = std::max(aspect_ratio, 1.0e-6);

    // Charter's frustum: camera-space X/Y scaled by scale_base times an aspect-dependent screen
    // scale — screenScaleX = min(0.5, 1/aspect), screenScaleY = min(1, aspect/2). For aspects up
    // to 2:1 the horizontal half-angle is constant (tan = 3 at the defaults) and the vertical
    // follows the aspect; wider viewports widen vertically instead.
    //
    // Both branches of that min() pair satisfy scale_y == scale_x * aspect, so the frustum is
    // exactly square-pixel at every viewport shape (regression-tested). Charter added +0.05 to
    // the vertical scale, which broke that identity by 5 to 10 percent depending on window shape
    // and rendered world-square note heads as tall rectangles; it was removed 2026-07-30. Recover
    // on-screen size with frustum_scale_base, which scales both axes and preserves the identity.
    const double screen_scale_x = std::min(0.5, 1.0 / aspect);
    const double screen_scale_y = std::min(1.0, aspect / 2.0);
    const double scale_x = metrics.frustum_scale_base * screen_scale_x;
    const double scale_y = metrics.frustum_scale_base * screen_scale_y;
    const double depth_range = metrics.far_plane - metrics.near_plane;

    HighwayMat4 perspective{};
    perspective.m[0] = scale_x;
    perspective.m[5] = scale_y;
    // Depth maps eye depth from [near, far] onto D3D's [0, 1]. The eye depth is camera-relative
    // by construction here (the view translation runs first) — anchoring it at world Z instead
    // was an earlier defect caught by the plan-25 Phase 3 checkpoint.
    perspective.m[10] = metrics.far_plane / depth_range;
    perspective.m[11] = -metrics.near_plane * metrics.far_plane / depth_range;
    perspective.m[14] = 1.0;

    // The lefty mirror reflects world X, so the yaw must flip with it for the mirrored picture
    // to be the true reflection of the unmirrored one.
    const double yaw = mirrored ? -metrics.camera_yaw_radians : metrics.camera_yaw_radians;
    const HighwayMat4 view = makeRotationY(yaw) * makeViewTranslation(camera);
    HighwayMat4 projection = perspective * view;

    // The board pin: project the anchor (focus X, board surface, hit line) and translate the
    // whole picture vertically so it lands at the configured NDC height. Adding ty * w to
    // clip.y is exactly a post-divide NDC translation, and it is vertical-only on purpose: the
    // board slides freely on X while the anchor height never moves. With rotations in the chain
    // the w row has X/Y terms, so the whole row folds into the translation.
    const std::array<double, 3> anchor_ndc = projection.projectPoint(camera.x, 0.0, 0.0);
    const double pin_offset = metrics.ndc_pin_y - anchor_ndc[1];
    for (std::size_t column = 0; column < 4; ++column)
    {
        projection.m.at(4 + column) += pin_offset * projection.m.at(12 + column);
    }

    return projection;
}

} // namespace

HighwayMat4 HighwayMat4::identity()
{
    HighwayMat4 result{};
    result.m[0] = 1.0;
    result.m[5] = 1.0;
    result.m[10] = 1.0;
    result.m[15] = 1.0;
    return result;
}

// Standard row-major column-vector composition.
HighwayMat4 operator*(const HighwayMat4& lhs, const HighwayMat4& rhs)
{
    // at() rather than operator[]: the indices are loop-derived, which the enabled
    // pro-bounds-constant-array-index check rejects for subscripting; the bounds check is
    // negligible here (projections compose once per frame, not per vertex).
    HighwayMat4 result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            double sum = 0.0;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                sum += lhs.m.at((row * 4) + inner) * rhs.m.at((inner * 4) + column);
            }
            result.m.at((row * 4) + column) = sum;
        }
    }
    return result;
}

// Applies the transform to a point and divides by w; w is clamped away from zero so degenerate
// inputs stay finite for the callers' comparisons.
std::array<double, 3> HighwayMat4::projectPoint(double x, double y, double z) const
{
    const double clip_x = (m[0] * x) + (m[1] * y) + (m[2] * z) + m[3];
    const double clip_y = (m[4] * x) + (m[5] * y) + (m[6] * z) + m[7];
    const double clip_z = (m[8] * x) + (m[9] * y) + (m[10] * z) + m[11];
    double clip_w = (m[12] * x) + (m[13] * y) + (m[14] * z) + m[15];
    if (std::abs(clip_w) < 1.0e-12)
    {
        clip_w = clip_w < 0.0 ? -1.0e-12 : 1.0e-12;
    }
    return {clip_x / clip_w, clip_y / clip_w, clip_z / clip_w};
}

HighwayCameraTarget makeHighwayCameraTarget(
    const HighwayViewState& state, double now_seconds, const HighwayMetrics& metrics)
{
    const bool mirrored = state.options.mirrored;

    // The hand window active at now plus every window arriving inside the scan horizon define
    // the fret-line range the camera frames. The active placement is the last at or before now
    // or — before the first arrival — the first placement itself, whose window already holds
    // during the opening scroll (matching highwayHandWindowAt), so the camera frames it even
    // when it sits beyond the horizon. Placements ascend, so the scan jumps straight to the
    // active one instead of rewalking the consumed prefix every frame.
    double low_line = 0.0;
    double high_line = metrics.camera_reference_span;
    const auto after_now = std::ranges::upper_bound(
        state.fret_hand_positions, now_seconds, std::ranges::less{}, &HighwayFhpView::seconds);
    if (!state.fret_hand_positions.empty())
    {
        const auto active =
            after_now == state.fret_hand_positions.begin() ? after_now : after_now - 1;
        low_line = static_cast<double>(active->fret - 1);
        high_line = static_cast<double>(active->fret + active->width - 1);
    }

    // The scan window is quantized to the derived camera framing zones (user direction
    // 2026-07-29): everything defined during the current zone and the next one is framed,
    // consumed or not, so the target holds perfectly still for whole zones and steps only at
    // their boundaries — the HighwayCamera spring is the single mechanism turning those steps
    // into motion, and each step lands a full zone before the hand needs to be in place.
    //
    // An empty zone list means the whole timeline is one unbounded zone, which falls straight
    // out of the bounds below rather than needing a second scan path. That degenerate state is
    // only reachable with no chart at all: zones derive from measure downbeats, a chart always
    // yields at least beat 0, and makeHighwayViewState returns early — before notes, hand
    // positions, or beats are filled — when the arrangement carries no chart. So an unbounded
    // window is always a window over nothing, and both loops below simply do not run.
    const std::vector<double>& zones = state.camera_zone_starts;
    const auto next_index =
        static_cast<std::size_t>(std::ranges::upper_bound(zones, now_seconds) - zones.begin());
    const double window_start =
        next_index == 0 ? -std::numeric_limits<double>::infinity() : zones[next_index - 1];
    const double horizon = next_index + 1 < zones.size() ? zones[next_index + 1]
                                                         : std::numeric_limits<double>::infinity();

    const auto scan_begin = std::ranges::lower_bound(
        state.fret_hand_positions, window_start, std::ranges::less{}, &HighwayFhpView::seconds);
    for (auto it = scan_begin; it != state.fret_hand_positions.end(); ++it)
    {
        const HighwayFhpView& fhp = *it;
        if (fhp.seconds >= horizon)
        {
            break;
        }
        low_line = std::min(low_line, static_cast<double>(fhp.fret - 1));
        high_line = std::max(high_line, static_cast<double>(fhp.fret + fhp.width - 1));
    }

    // A fretted note can sit outside the hand window: a two-hand tap floats far above the fretting
    // hand, which no longer anchors it (taps are excluded from the fret-hand track, matching how
    // charters place anchors). The window light stays on the left hand, but the camera still has to
    // frame the tap, so any fretted note defined in the scanned zones widens the range as if the
    // hand reached it. Open strings never reframe (played from anywhere, like the hand window they
    // do not constrain). A note framed for its zone stays framed once consumed, which is the point
    // of zone quantization: the target rests instead of narrowing note by note.
    const auto note_begin = std::ranges::lower_bound(
        state.notes, window_start, std::ranges::less{}, &HighwayNoteView::start_seconds);
    for (auto it = note_begin; it != state.notes.end(); ++it)
    {
        const HighwayNoteView& note = *it;
        if (note.start_seconds >= horizon)
        {
            break; // notes ascend by onset, so nothing later is in the scan window
        }
        if (note.fret <= 0)
        {
            continue;
        }
        low_line = std::min(low_line, static_cast<double>(note.fret - 1));
        high_line = std::max(high_line, static_cast<double>(note.fret));
        // A pick slide's head rides its whole traveled path with no fret-hand anchor chasing
        // it (the scrape is excluded from the fret-hand track like the tap above), so the
        // framing must cover every neck position the path reaches, not just the start.
        if (note.attack == NoteAttack::PickSlide)
        {
            for (const HighwaySlideView& waypoint : note.slides)
            {
                if (waypoint.fret <= 0)
                {
                    continue;
                }
                low_line = std::min(low_line, static_cast<double>(waypoint.fret - 1));
                high_line = std::max(high_line, static_cast<double>(waypoint.fret));
            }
        }
    }

    // The focus is an affine function of the framed window's world middle: pulled a fixed
    // fraction toward the neck's weighted reference, then nudged a constant distance toward the
    // body. Blend and shift are the only two knobs, which is exactly the affine family's own
    // degrees of freedom — the neck reference is derived geometry, not a third knob.
    //
    // Every term comes out of highwayFretLineX, so the lefty mirror is structural: each input is
    // already reflected, and the whole expression is linear in them, so the mirrored focus is the
    // exact negation of the unmirrored one with no per-term negation left to keep in sync.
    const double middle_x = (highwayFretLineX(low_line, metrics, mirrored) +
                             highwayFretLineX(high_line, metrics, mirrored)) /
                            2.0;
    const double whole_neck_x = highwayFocusWholeNeckX(metrics, mirrored);
    const double body_shift = highwayFretLineX(metrics.focus_body_shift_frets, metrics, mirrored);

    return HighwayCameraTarget{
        .focus_x = std::lerp(middle_x, whole_neck_x, metrics.focus_whole_neck_blend) + body_shift,
        .span = high_line - low_line,
    };
}

// Third-order critically damped smoother (user direction 2026-07-29). A second-order spring
// left acceleration discontinuous — from rest a step began with the peak acceleration
// x''(0) = -d w^2, an instant kick that read as a jolt. Carrying acceleration as state too
// (three coincident real poles at -w) makes the response C^2: from rest the motion eases in
// from zero acceleration, cubic in time (displacement ~ d w^3 t^3 / 6), and lands with no
// overshoot. Three equal poles is the maximally smooth arrangement at a given speed — spreading
// them apart only sharpens the onset — and the maximally smooth, slow hover is what read most
// correct. The error relaxes as e(t) = (c0 + c1 t + c2 t^2) e^{-w t}, and the update below is
// that exact closed-form solution over the frame, so smoothing is exactly frame-rate
// independent; the first advance snaps at rest.
void HighwayCamera::advance(
    const HighwayCameraTarget& target, double dt_seconds, const HighwayMetrics& metrics)
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_focus_x = target.focus_x;
        m_span = target.span;
        m_focus_x_velocity = 0.0;
        m_span_velocity = 0.0;
        m_focus_x_accel = 0.0;
        m_span_accel = 0.0;
        return;
    }

    const double dt = std::max(dt_seconds, 0.0);
    const double omega = metrics.focus_spring_per_second;
    const double decay = std::exp(-omega * dt);
    const auto follow = [&](double& value, double& velocity, double& accel, const double toward) {
        // Fit e(t) = (c0 + c1 t + c2 t^2) e^{-w t} to the carried error, velocity, and
        // acceleration, then evaluate it and its first two derivatives at dt. Velocity and
        // acceleration are preserved across a target step (only the error jumps), so the motion
        // stays C^2 through every zone boundary.
        const double error = value - toward;
        const double c0 = error;
        const double c1 = velocity + (omega * error);
        const double c2 = 0.5 * (accel + (2.0 * omega * velocity) + (omega * omega * error));
        const double poly = c0 + (c1 * dt) + (c2 * dt * dt);
        const double poly_derivative = c1 + (2.0 * c2 * dt);
        value = toward + (poly * decay);
        velocity = (poly_derivative - (omega * poly)) * decay;
        accel = ((2.0 * c2) - (2.0 * omega * poly_derivative) + (omega * omega * poly)) * decay;
    };
    follow(m_focus_x, m_focus_x_velocity, m_focus_x_accel, target.focus_x);
    follow(m_span, m_span_velocity, m_span_accel, target.span);
}

void HighwayCamera::reset() noexcept
{
    m_initialized = false;
    m_focus_x = 0.0;
    m_span = 4.0;
    m_focus_x_velocity = 0.0;
    m_span_velocity = 0.0;
    m_focus_x_accel = 0.0;
    m_span_accel = 0.0;
}

// Height and pull-back derive from the smoothed span around the reference hand width.
HighwayCameraPose HighwayCamera::pose(const HighwayMetrics& metrics) const
{
    const double extra_span = m_span - metrics.camera_reference_span;
    return HighwayCameraPose{
        .x = m_focus_x,
        .y = metrics.camera_y_base + (metrics.camera_span_gain * extra_span),
        .z = metrics.camera_z_base - (metrics.camera_span_gain * extra_span),
    };
}

HighwayMat4 makeHighwayWorldToClip(
    const HighwayCameraPose& pose, double aspect_ratio, const bool mirrored,
    const HighwayMetrics& metrics)
{
    return makePinnedProjection(pose, aspect_ratio, mirrored, metrics);
}

HighwayMat4 makeHighwayBackgroundWorldToClip(
    const HighwayCameraPose& pose, double aspect_ratio, double time_seconds, const bool mirrored,
    const HighwayMetrics& metrics)
{
    const double divisor = std::max(metrics.background_parallax_divisor, 1.0);
    const double sway =
        metrics.background_sway_amplitude *
        std::sin(2.0 * std::numbers::pi * metrics.background_sway_hertz * time_seconds);
    const HighwayCameraPose background_pose{
        .x = (pose.x / divisor) + sway,
        .y = pose.y / divisor,
        .z = pose.z / divisor,
    };
    return makePinnedProjection(background_pose, aspect_ratio, mirrored, metrics);
}

} // namespace rock_hero::common::core
