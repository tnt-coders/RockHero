/*!
\file highway_camera.h
\brief Pure camera mathematics for the 3D note highway: focus, smoothing, and projection.
*/

#pragma once

#include <array>
#include <rock_hero/common/core/highway/highway_metrics.h>
#include <rock_hero/common/core/highway/highway_view_state.h>

namespace rock_hero::common::core
{

/*!
\brief 4x4 transform, row-major storage with column-vector convention (`clip = M * world`).

Element (row, column) lives at `m[row * 4 + column]`. Render backends convert to their native
layout at the drawer boundary; the headless math and its tests use this one documented
convention.
*/
struct HighwayMat4
{
    /*! \brief Matrix elements, row-major. */
    std::array<double, 16> m{};

    /*!
    \brief Returns the identity transform.
    \return Identity matrix.
    */
    [[nodiscard]] static HighwayMat4 identity();

    /*!
    \brief Composes two transforms.
    \param lhs Transform applied second.
    \param rhs Transform applied first.
    \return Product lhs * rhs.
    */
    friend HighwayMat4 operator*(const HighwayMat4& lhs, const HighwayMat4& rhs);

    /*!
    \brief Transforms a world point through the matrix and performs the perspective divide.
    \param x World X.
    \param y World Y.
    \param z World Z.
    \return Normalized device coordinates {x, y, z}.
    */
    [[nodiscard]] std::array<double, 3> projectPoint(double x, double y, double z) const;

    /*!
    \brief Compares two matrices element-wise.
    \param lhs Left-hand matrix.
    \param rhs Right-hand matrix.
    \return True when every element is bit-equal.
    */
    friend bool operator==(const HighwayMat4& lhs, const HighwayMat4& rhs) = default;
};

/*! \brief Instantaneous camera targets derived from the chart content around now. */
struct HighwayCameraTarget
{
    /*! \brief World X the camera centers on (already mirrored when the state is). */
    double focus_x{0.0};

    /*! \brief Fret span of the scanned hand window; drives the out-zoom. */
    double span{4.0};

    /*!
    \brief Compares two targets by their stored fields.
    \param lhs Left-hand target.
    \param rhs Right-hand target.
    \return True when both targets store equal values.
    */
    friend constexpr bool operator==(
        const HighwayCameraTarget& lhs, const HighwayCameraTarget& rhs) noexcept = default;
};

/*! \brief Smoothed world-space camera position. */
struct HighwayCameraPose
{
    /*! \brief Camera world X (the smoothed fret focus). */
    double x{0.0};

    /*! \brief Camera height above the board. */
    double y{0.0};

    /*! \brief Camera Z behind the hit line (negative). */
    double z{0.0};

    /*!
    \brief Compares two poses by their stored fields.
    \param lhs Left-hand pose.
    \param rhs Right-hand pose.
    \return True when both poses store equal values.
    */
    friend constexpr bool operator==(
        const HighwayCameraPose& lhs, const HighwayCameraPose& rhs) noexcept = default;
};

/*!
\brief Scans upcoming fret-hand positions and derives the camera's instantaneous targets.

Reproduces the reference behavior: the hand window active at `now` plus every window arriving
within the scan horizon define a min/max fret-line range; the focus is that range's world middle
blended a fixed fraction toward a whole-neck position, and the range width is the span driving
the out-zoom. A chart with no hand positions falls back to the reference four-fret window at the
nut. The state's mirror flag reflects the focus (and the whole-neck blend point) as pure math.

\param state Seconds-resolved highway content (its display options supply the mirror flag).
\param now_seconds Current playback time.
\param metrics World-space constants.
\return Instantaneous focus and span targets.
*/
[[nodiscard]] HighwayCameraTarget makeHighwayCameraTarget(
    const HighwayViewState& state, double now_seconds, const HighwayMetrics& metrics);

/*!
\brief Exponentially smoothed camera following the instantaneous targets.

One instance per rendering consumer; no internal synchronization. Smoothing is frame-rate
independent: each advance covers `1 - pow(1 - rate, dt)` of the remaining distance, so two half
steps equal one full step.
*/
class HighwayCamera
{
public:
    /*!
    \brief Advances the smoothed focus and span toward the targets.

    The first advance after construction or reset() snaps directly to the target.

    \param target Instantaneous targets from makeHighwayCameraTarget.
    \param dt_seconds Frame delta in seconds; clamped non-negative.
    \param metrics World-space constants (smoothing rate).
    */
    void advance(
        const HighwayCameraTarget& target, double dt_seconds, const HighwayMetrics& metrics);

    /*! \brief Forgets the smoothing state so the next advance() snaps like a first call. */
    void reset() noexcept;

    /*!
    \brief Returns the smoothed world-space camera position.

    Height and pull-back derive from the smoothed span: wider hand windows lift and retreat the
    camera by the configured gain around the reference span.

    \param metrics World-space constants.
    \return Smoothed camera pose.
    */
    [[nodiscard]] HighwayCameraPose pose(const HighwayMetrics& metrics) const;

private:
    // Smoothed values; meaningful only after the first advance snapped them to a target.
    bool m_initialized{false};
    double m_focus_x{0.0};
    double m_span{4.0};
};

/*!
\brief Builds the world-to-clip transform: the reference camera chain plus the board pin.

The chain reproduces the reference exactly (source-verified 2026-07-11): view translation, the
small yaw and pitch rotations that give the board its held-guitar-neck reading (the yaw slopes
the strings ~2-3 degrees and magnifies the body-side neck end; the pitch places the vanishing
point), Charter's very wide frustum, and finally **the board pin** — the world point (camera
focus X, board surface, hit line) is projected and the whole picture is translated vertically so
that point lands at the configured NDC height. The translation is vertical-only: the board
slides freely left/right as the focus moves while the anchor height never changes.

With both rotation metrics zeroed the chain reduces to the zero-rotation formulation whose exact
verticality stays regression-tested at that configuration.

\param pose Smoothed camera position.
\param aspect_ratio Viewport width over height.
\param mirrored True for left-handed display; flips the yaw so the picture mirrors exactly.
\param metrics World-space constants (rotations, frustum, clip planes, pin height).
\return World-to-clip transform with the pin applied.
*/
[[nodiscard]] HighwayMat4 makeHighwayWorldToClip(
    const HighwayCameraPose& pose, double aspect_ratio, bool mirrored,
    const HighwayMetrics& metrics);

/*!
\brief Builds the background layer's world-to-clip transform: parallax plus slow sway.

The background rides the same camera divided by the parallax divisor, with a slow sinusoidal
sway on the fret axis driven by injected time (never a wall clock), and the same pin mechanism
so both layers share the anchor height.

\param pose Smoothed camera position (the foreground pose; the divisor is applied here).
\param aspect_ratio Viewport width over height.
\param time_seconds Injected animation time driving the sway.
\param mirrored True for left-handed display; flips the yaw so the picture mirrors exactly.
\param metrics World-space constants.
\return Background world-to-clip transform.
*/
[[nodiscard]] HighwayMat4 makeHighwayBackgroundWorldToClip(
    const HighwayCameraPose& pose, double aspect_ratio, double time_seconds, bool mirrored,
    const HighwayMetrics& metrics);

} // namespace rock_hero::common::core
