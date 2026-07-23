/*!
\file highway_metrics.h
\brief World-space constants and pure geometry mapping for the 3D note highway.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <compare>

namespace rock_hero::common::core
{

/*!
\brief Every world-space constant of the 3D highway, in one documented struct.

Starting values reproduce Charter's 3D preview coordinate system (the settled visual target),
except where a field's comment records a deliberate user tuning away from it: X is the fret axis
with fret 0 at x = 0, Y is the string axis with the board surface at y = 0, and Z is the time
axis with the hit line at z = 0 and future notes at positive Z. Collecting the constants here is
a deliberate departure from the reference implementation's scattered magic numbers: tuning the
highway is edits to this one struct.

Shared by the game highway and the editor 3D preview; render backends consume these through the
headless scene model and camera only.
*/
struct HighwayMetrics
{
    /*!
    \brief Width of the first fret in world units.

    Charter's firstFretDistance is 1.2; ours is user-tuned narrower (2026-07-21) with the note
    head size deliberately kept at Charter's, so heads fill more of their fret slot.
    */
    double first_fret_distance{1.1};

    /*!
    \brief Successive-fret width ratio; 1.0 is Charter's equal-width default.

    Values below 1.0 give a realistic taper toward the body. Kept as data so the open fret-taper
    question is a constant flip, not a code change.
    */
    double fret_length_multiplier{1.0};

    /*! \brief Vertical distance between string lanes (Charter stringDistance). */
    double string_distance{0.35};

    /*!
    \brief World Y of the string grid's base above the floor; the floor stays the origin (y = 0).

    Sized to the chord-box frame thickness (the renderer keeps its constant equal to this on
    purpose): a chord box's bottom bar sits on the floor and fills this gap exactly, giving it
    the same half-string clearance from the bottom lane that the box's top bar — drawn just
    past the fret-line top — has from the top lane (user rule 2026-07-23). Fret lines span the
    string grid alone and do not extend down through this gap.
    */
    double string_grid_base_y{0.075};

    /*!
    \brief World Z per second of time distance at scroll speed 1.0.

    Charter maps milliseconds by 0.02 world units; this is the same constant in seconds.
    */
    double z_per_second{20.0};

    /*! \brief Seconds of chart visible ahead of the hit line at scroll speed 1.0. */
    double visibility_window_seconds{1.6};

    /*!
    \brief Note head half-width (Charter firstFretDistance 1.2 / 2.5).

    Deliberately NOT derived from the narrowed \ref first_fret_distance: the fret-width tuning
    kept note heads at Charter's size.
    */
    double note_half_width{0.48};

    /*! \brief Sustain tail half-width (one third of the note head half-width). */
    double tail_half_width{0.16};

    /*! \brief Tail lift per bent half-step (Charter stringDistance x 0.8). */
    double bend_lift_per_half_step{0.28};

    /*! \brief Camera height above the board at the reference fret span. */
    double camera_y_base{5.0};

    /*! \brief Camera Z behind the hit line at the reference fret span. */
    double camera_z_base{-2.5};

    /*! \brief Extra camera height and pull-back per fret of span beyond the reference. */
    double camera_span_gain{0.2};

    /*! \brief Fret span that uses the base camera position (Charter's 4-fret hand). */
    double camera_reference_span{4.0};

    /*! \brief How far ahead of now the fret-focus scan looks, in seconds. */
    double focus_scan_seconds{3.0};

    /*! \brief Blend of the focus target toward a fixed whole-neck weighted position. */
    double focus_whole_neck_blend{0.1};

    /*!
    \brief Neck position the focus blends toward (world X of Charter's weighted whole-neck spot).

    Charter's formula is fretPos(24) * 0.4 + fretPos(0) * 0.6 (source-verified 2026-07-11);
    for our 24-fret, equal-width neck at the tuned \ref first_fret_distance that is
    24 * 1.1 * 0.4 = 10.56. Flattened from the formula: keep in step with the fret width.
    */
    double focus_whole_neck_x{10.56};

    /*!
    \brief Constant world-X offset added to the focus target.

    Charter's focus formula is `1 + middle * 0.9 + weighted * 0.1` — the leading +1 shifts the
    framed window slightly toward the body. Mirrored setups negate it.
    */
    double focus_x_offset{1.0};

    /*!
    \brief Fraction of the remaining focus distance covered per second of smoothing.

    Applied frame-rate independently as `mix = 1 - pow(1 - value, dt_seconds)` (Charter's
    exponential smoothing).
    */
    double focus_smoothing_per_second{0.7};

    /*!
    \brief Screen height the board anchor is pinned to, in NDC.

    The camera projects the world point (focus X, board surface, hit line) and then translates
    the whole picture vertically so that point lands at this NDC height. The translation is
    deliberately vertical-only: the board slides freely left/right as the fret focus moves while
    the anchor height never changes.
    */
    double ndc_pin_y{-0.9};

    /*!
    \brief Downward camera pitch in radians; zero on purpose (deliberate reference divergence).

    Charter ships rotX = 0.06, but that forward tilt skews the whole picture (verticals lean)
    and was rejected by the user on sight (2026-07-11): the wanted angled-neck reading is the
    yaw's string slope alone. The yaw never mixes world Y into clip W or X, so with zero pitch
    fret lines project exactly vertical — regression-tested at the shipped defaults.
    */
    double camera_pitch_radians{0.0};

    /*!
    \brief Camera yaw in radians (Charter rotY = 0.03), negated under the lefty mirror.

    The yaw makes camera depth vary along a string, so strings slope ~2-3 degrees on screen and
    the body-side neck end renders slightly larger — the angled-neck look (source-verified
    2026-07-11; the zero-rotation formulation looked flat by comparison, the user's observation).
    This is the only nonzero rotation the shipped camera carries; see camera_pitch_radians.
    */
    double camera_yaw_radians{0.03};

    /*! \brief Near clip plane distance. */
    double near_plane{0.1};

    /*! \brief Far clip plane distance; covers the visibility window with headroom. */
    double far_plane{100.0};

    /*!
    \brief Base perspective scale of the reference frustum (Charter near / nearRight = 2/3).

    Charter's projection multiplies camera-space X and Y by this base times an aspect-dependent
    screen scale (see makeHighwayWorldToClip); the resulting field of view is very wide
    (~143 degrees horizontal at 16:9), which is a large part of the reference composition.
    */
    double frustum_scale_base{2.0 / 3.0};

    /*!
    \brief Extra vertical screen-scale lift (Charter's +0.05 on screenScaleY).

    A deliberate reference fudge: roughly 5 percent more vertical magnification than square
    pixels would give, kept for visual parity.
    */
    double frustum_y_lift{0.05};

    /*! \brief Divisor applied to the camera position for the parallax background layer. */
    double background_parallax_divisor{4.0};

    /*! \brief World amplitude of the background's slow sinusoidal sway. */
    double background_sway_amplitude{0.5};

    /*! \brief Sway rate in cycles per second; deliberately slow. */
    double background_sway_hertz{0.05};

    /*!
    \brief Compares two metrics structs by their stored fields.
    \param lhs Left-hand metrics.
    \param rhs Right-hand metrics.
    \return True when every constant is bit-equal.
    */
    friend bool operator==(const HighwayMetrics& lhs, const HighwayMetrics& rhs) = default;
};

/*!
\brief Returns the world X of a fret line.

Fret 0 (the nut) sits at x = 0. With the equal-width default each fret adds first_fret_distance;
a taper multiplier below 1.0 shrinks successive frets geometrically. The lefty mirror reflects
the fret axis through the nut, as pure math the renderer never sees.

\param fret Zero-based fret line (0 is the nut).
\param metrics World-space constants.
\param mirrored True to reflect the fret axis for left-handed display.
\return World X of the fret line.
*/
[[nodiscard]] inline double highwayFretLineX(int fret, const HighwayMetrics& metrics, bool mirrored)
{
    const double multiplier = metrics.fret_length_multiplier;
    double x = 0.0;
    // Exact comparison on purpose: the geometric-series branch divides by (1 - multiplier), so
    // only the exact value 1.0 must take the linear branch. is_eq keeps GCC's -Wfloat-equal
    // satisfied that the exactness is intended.
    if (std::is_eq(multiplier <=> 1.0))
    {
        x = static_cast<double>(fret) * metrics.first_fret_distance;
    }
    else
    {
        // Geometric series: widths d, d*m, d*m^2, ... summed up to the requested fret line.
        x = metrics.first_fret_distance * (1.0 - std::pow(multiplier, fret)) / (1.0 - multiplier);
    }
    return mirrored ? -x : x;
}

/*!
\brief Returns the world X of a fractional fret-line coordinate.

The continuous extension of the integer overload — linear per fret at the equal-width default,
the geometric series' continuous form under a taper — matching it exactly at integer inputs.
Used by the sliding hand window, whose edges travel between the fixed fret lines.

\param fret Fret-line coordinate; fractional values sit between the integer lines.
\param metrics World-space constants.
\param mirrored True to reflect the fret axis for left-handed display.
\return World X of the fractional line coordinate.
*/
[[nodiscard]] inline double highwayFretLineX(
    double fret, const HighwayMetrics& metrics, bool mirrored)
{
    const double multiplier = metrics.fret_length_multiplier;
    double x = 0.0;
    // Exact comparison for the same reason as the integer overload: only exactly 1.0 may take
    // the linear branch.
    if (std::is_eq(multiplier <=> 1.0))
    {
        x = fret * metrics.first_fret_distance;
    }
    else
    {
        x = metrics.first_fret_distance * (1.0 - std::pow(multiplier, fret)) / (1.0 - multiplier);
    }
    return mirrored ? -x : x;
}

/*!
\brief Returns the world X of a fretted note head: the midpoint of its fret slot.
\param fret One-based fret the note sounds at; open strings (fret 0) span the hand window
       instead and take their geometry from the fret lines directly.
\param metrics World-space constants.
\param mirrored True to reflect the fret axis for left-handed display.
\return World X of the note head center.
*/
[[nodiscard]] inline double highwayNoteCenterX(
    int fret, const HighwayMetrics& metrics, bool mirrored)
{
    // Reflection is linear, so mirroring the midpoint equals the midpoint of the mirrored lines.
    return (highwayFretLineX(fret - 1, metrics, mirrored) +
            highwayFretLineX(fret, metrics, mirrored)) /
           2.0;
}

/*!
\brief Returns the world Y of a one-based lane, counted from the bottom.

Lanes are centered on half-string offsets above the string grid's base
(\ref HighwayMetrics::string_grid_base_y), so the bottom lane (1) sits the base plus half a
string spacing off the floor and the fret lines spanning the grid stick out the same half-string
distance above the top lane as below the bottom lane. The grid hugs the flat notes on purpose (a
far-away head standing vertical in the rolling flip dips below the floor at the horizon; a full
one-string stack lift clearing it was tried and reverted 2026-07-23 — the flat-against-the-board
look wins). The shared seam so every string-plane consumer (string lanes, fingering spots) maps
lanes identically.

\param lane One-based lane index, 1 at the bottom.
\param metrics World-space constants.
\return World Y of the lane center.
*/
[[nodiscard]] inline double highwayLaneToY(int lane, const HighwayMetrics& metrics)
{
    return metrics.string_grid_base_y +
           ((static_cast<double>(lane) - 0.5) * metrics.string_distance);
}

/*!
\brief Returns the world Y of a string lane above the board surface.

The lowest-pitched string sits at the bottom by default (Charter's orientation); the invert flag
flips the stacking for players who prefer the mirrored string order.

\param string One-based string, counted from the lowest-pitched string.
\param string_count Number of strings the arrangement uses.
\param metrics World-space constants.
\param invert_string_order True to stack the lowest-pitched string on top.
\return World Y of the string lane.
*/
[[nodiscard]] inline double highwayStringLaneY(
    int string, int string_count, const HighwayMetrics& metrics, bool invert_string_order)
{
    const int lane = invert_string_order ? (string_count + 1 - string) : string;
    return highwayLaneToY(lane, metrics);
}

/*!
\brief Maps a time distance from the hit line to world Z.
\param seconds_from_now Note time minus current playback time; negative is already passed.
\param scroll_speed Player scroll-speed setting; clamped to a small positive floor.
\param metrics World-space constants.
\return World Z, positive toward the horizon.
*/
[[nodiscard]] inline double highwayTimeToZ(
    double seconds_from_now, double scroll_speed, const HighwayMetrics& metrics)
{
    return seconds_from_now * metrics.z_per_second / std::max(scroll_speed, 0.01);
}

} // namespace rock_hero::common::core
