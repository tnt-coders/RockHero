#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/highway/highway_camera.h>

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

// Rotation about the X axis (pitch). Positive angles pitch the view downward: far content moves
// down in camera space, lowering the vanishing point.
[[nodiscard]] HighwayMat4 makeRotationX(const double radians)
{
    HighwayMat4 rotation = HighwayMat4::identity();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    rotation.m[5] = c;
    rotation.m[6] = -s;
    rotation.m[9] = s;
    rotation.m[10] = c;
    return rotation;
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

// The reference camera chain: translate -> yaw -> pitch -> wide perspective -> NDC pin.
//
// The rotations are Charter's rotX/rotY constants — they are load-bearing for the look (the yaw
// slopes the strings like a held guitar neck; source analysis 2026-07-11). With both rotations
// zero this reduces to the original zero-rotation formulation whose exact-verticality property
// stays regression-tested at that configuration.
[[nodiscard]] HighwayMat4 makePinnedProjection(
    const HighwayCameraPose& camera, double aspect_ratio, const bool mirrored,
    const HighwayMetrics& metrics)
{
    const double aspect = std::max(aspect_ratio, 1.0e-6);

    // Charter's frustum: camera-space X/Y scaled by scale_base times an aspect-dependent screen
    // scale — screenScaleX = min(0.5, 1/aspect), screenScaleY = min(1, aspect/2) + lift. For
    // aspects up to 2:1 the horizontal half-angle is constant (tan = 3 at the defaults) and the
    // vertical follows the aspect; wider viewports widen vertically instead.
    const double screen_scale_x = std::min(0.5, 1.0 / aspect);
    const double screen_scale_y = std::min(1.0, aspect / 2.0) + metrics.frustum_y_lift;
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
    const HighwayMat4 view = makeRotationX(metrics.camera_pitch_radians) * makeRotationY(yaw) *
                             makeViewTranslation(camera);
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

    // The hand window active at now (the last placement at or before it) plus every window
    // arriving inside the scan horizon define the fret-line range the camera frames.
    int low_line = 0;
    int high_line = static_cast<int>(metrics.camera_reference_span);
    bool any_window = false;
    const double horizon = now_seconds + metrics.focus_scan_seconds;
    for (const HighwayFhpView& fhp : state.fret_hand_positions)
    {
        if (fhp.seconds > horizon)
        {
            break;
        }
        const int window_low = fhp.fret - 1;
        const int window_high = fhp.fret + fhp.width - 1;
        if (fhp.seconds < now_seconds || !any_window)
        {
            // Placements ascend, so each one still at or before now supersedes the previous
            // active window; the first in-horizon placement also replaces the no-hand fallback.
            low_line = window_low;
            high_line = window_high;
            any_window = true;
        }
        else
        {
            // Upcoming placements inside the horizon widen the framed range.
            low_line = std::min(low_line, window_low);
            high_line = std::max(high_line, window_high);
        }
    }

    const double middle_x = (highwayFretLineX(low_line, metrics, mirrored) +
                             highwayFretLineX(high_line, metrics, mirrored)) /
                            2.0;
    const double whole_neck_x = mirrored ? -metrics.focus_whole_neck_x : metrics.focus_whole_neck_x;
    const double offset = mirrored ? -metrics.focus_x_offset : metrics.focus_x_offset;

    return HighwayCameraTarget{
        .focus_x = middle_x + ((whole_neck_x - middle_x) * metrics.focus_whole_neck_blend) + offset,
        .span = static_cast<double>(high_line - low_line),
    };
}

// Frame-rate-independent exponential smoothing; the first advance snaps.
void HighwayCamera::advance(
    const HighwayCameraTarget& target, double dt_seconds, const HighwayMetrics& metrics)
{
    if (!m_initialized)
    {
        m_initialized = true;
        m_focus_x = target.focus_x;
        m_span = target.span;
        return;
    }

    const double mix =
        1.0 - std::pow(1.0 - metrics.focus_smoothing_per_second, std::max(dt_seconds, 0.0));
    m_focus_x += (target.focus_x - m_focus_x) * mix;
    m_span += (target.span - m_span) * mix;
}

void HighwayCamera::reset() noexcept
{
    m_initialized = false;
    m_focus_x = 0.0;
    m_span = 4.0;
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
