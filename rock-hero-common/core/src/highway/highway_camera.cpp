#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <rock_hero/common/core/highway/highway_camera.h>

namespace rock_hero::common::core
{

namespace
{

// Perspective projection scale for a vertical field of view: 1 / tan(fov / 2).
[[nodiscard]] double projectionScale(const HighwayMetrics& metrics)
{
    const double half_fov_radians = metrics.field_of_view_y_degrees * std::numbers::pi / 360.0;
    return 1.0 / std::tan(half_fov_radians);
}

// Zero-rotation perspective looking toward +Z from the camera position: a pure view translation
// composed with an axis-aligned perspective (D3D-style depth in [0, 1]). Rotation-free by
// design — this is what makes world-vertical lines project exactly screen-vertical.
[[nodiscard]] HighwayMat4 makePinnedProjection(
    const HighwayCameraPose& camera, double aspect_ratio, const HighwayMetrics& metrics)
{
    const double scale = projectionScale(metrics);
    const double depth_range = metrics.far_plane - metrics.near_plane;

    HighwayMat4 projection{};
    // Row 0: clip.x = (scale / aspect) * (x - cam.x)
    projection.m[0] = scale / std::max(aspect_ratio, 1.0e-6);
    projection.m[3] = -camera.x * projection.m[0];
    // Row 1: clip.y = scale * (y - cam.y)
    projection.m[5] = scale;
    projection.m[7] = -camera.y * scale;
    // Row 2: clip.z maps eye depth [near, far] onto [0, far] pre-divide.
    projection.m[10] = metrics.far_plane / depth_range;
    projection.m[11] = (-camera.z - metrics.near_plane) * metrics.far_plane / depth_range -
                       (-camera.z) * projection.m[10];
    // Row 3: clip.w = eye depth (z - cam.z); positive for content in front of the camera.
    projection.m[14] = 1.0;
    projection.m[15] = -camera.z;

    // The board pin: project the anchor (focus X, board surface, hit line) and translate the
    // whole picture vertically so it lands at the configured NDC height. Adding ty * w to
    // clip.y is exactly a post-divide NDC translation, and it is vertical-only on purpose: the
    // board slides freely on X while the anchor height never moves.
    const std::array<double, 3> anchor_ndc = projection.projectPoint(camera.x, 0.0, 0.0);
    projection.m[7] += (metrics.ndc_pin_y - anchor_ndc[1]) * projection.m[15];
    projection.m[6] += (metrics.ndc_pin_y - anchor_ndc[1]) * projection.m[14];

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

    return HighwayCameraTarget{
        .focus_x = middle_x + ((whole_neck_x - middle_x) * metrics.focus_whole_neck_blend),
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
    const HighwayCameraPose& pose, double aspect_ratio, const HighwayMetrics& metrics)
{
    return makePinnedProjection(pose, aspect_ratio, metrics);
}

HighwayMat4 makeHighwayBackgroundWorldToClip(
    const HighwayCameraPose& pose, double aspect_ratio, double time_seconds,
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
    return makePinnedProjection(background_pose, aspect_ratio, metrics);
}

} // namespace rock_hero::common::core
