/*!
\file highway_metrics.h
\brief World-space constants and pure geometry mapping for the 3D note highway.
*/

#pragma once

#include <algorithm>

namespace rock_hero::common::core
{

/*!
\brief Fret count of the highway board, counted from the nut.

A fixed property of the drawn board rather than a tuning knob: the board face always lays out
this many fret slots, however few a chart uses (chart validation caps fret numbers separately,
at \ref g_max_fret). A namespace constant rather than a \ref HighwayMetrics field because the
renderer sizes per-fret-line arrays with it, and because the metrics struct holds the world-space
distances that scale the board — this is how many frets those distances are laid out across.
*/
inline constexpr int g_highway_fret_count{24};

/*!
\brief Every world-space constant of the 3D highway, in one documented struct.

Starting values reproduce Charter's 3D preview coordinate system (the visual target), except
where a field's comment records a deliberate tuning away from it: X is the fret axis with fret 0
at x = 0, Y is the string axis with the board surface at y = 0, and Z is the time axis with the
hit line at z = 0 and future notes at positive Z.

The highway has no magic numbers scattered through its renderer: tuning it is edits to this one
header. The struct holds every *independently authored* constant; the free functions below it
hold the exact relationships derived from those constants — a tail's width, a bend's lift, the
whole-neck focus spot — so a derived value can never drift from the constant it is defined
against.

Shared by the game highway and the editor 3D preview; render backends consume these through the
headless scene model and camera only.
*/
struct HighwayMetrics
{
    /*!
    \brief Width of the first fret in world units.

    Charter's firstFretDistance is 1.2; ours is deliberately narrower with the note head size
    kept at Charter's, so heads fill more of their fret slot.
    */
    double first_fret_distance{1.1};

    /*! \brief Vertical distance between string lanes (Charter stringDistance). */
    double string_distance{0.35};

    /*!
    \brief World Y of the string grid's base above the floor; the floor stays the origin (y = 0).

    Doubles as the chord-box frame thickness, which the renderer reads from here rather than
    keeping its own copy: a chord box's bottom bar sits on the floor and fills this gap exactly,
    giving it the same half-string clearance from the bottom lane that the box's top bar — drawn
    just past the fret-line top — has from the top lane. Fret lines span the string grid alone and
    do not extend down through this gap.
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

    /*! \brief Camera height above the board at the reference fret span. */
    double camera_y_base{5.0};

    /*! \brief Camera Z behind the hit line at the reference fret span. */
    double camera_z_base{-2.5};

    /*! \brief Extra camera height and pull-back per fret of span beyond the reference span. */
    double camera_span_gain{0.2};

    /*! \brief Fret span that uses the base camera position (Charter's 4-fret hand). */
    double camera_reference_span{4.0};

    /*!
    \brief Rate of the camera's third-order critically damped smoother, per second.

    The single smoothing constant of the camera. The framing target steps the instant a zone
    boundary shifts the scan window, and this smoother is the only thing between that stepped
    target and the camera: three coincident real poles at this rate, so the motion carries
    position, velocity, and acceleration as state and stays C^2 across every step — it eases in
    from zero acceleration (no jolt) and lands with no overshoot. Three equal poles is the
    maximally smooth arrangement at a given speed (spreading them apart only sharpens the onset),
    and that maximally smooth, slow hover is the wanted feel — the "slow hover chasing the hand"
    the camera research describes. It settles visually in roughly 8 / value seconds — a
    deliberately languid drift that trails the action slightly on the busiest charts, accepted
    for the calmer feel everywhere else.
    */
    double focus_spring_per_second{1.3};

    /*!
    \brief Fraction of the way from the framed middle toward the neck reference the focus sits.

    One of the focus target's exactly two knobs. The target is an affine function of the framed
    window's world middle, and that family has exactly two parameters: this blend is its gain
    (the middle keeps 1 - blend of its pull) and \ref focus_body_shift_frets its offset. The neck
    reference itself is derived, not stored (\ref highwayFocusWholeNeckX) — a third stored
    constant would be redundant by construction, because moving the reference by d and the
    offset by -blend * d produces an identical picture.
    */
    double focus_whole_neck_blend{0.1};

    /*!
    \brief Constant body-ward shift of the focus target, in fret widths.

    The focus target's second knob, and a plain distance along the neck rather than a position on
    it: the frame is nudged this far toward the body no matter which frets are being framed.

    Carried in fret widths so the whole fret axis rescales together. Charter's focus formula ends
    in a raw `+1` world unit (`1 + middle * 0.9 + weighted * 0.1`), which silently changed meaning
    when \ref first_fret_distance narrowed from Charter's 1.2 to ours — the same drift that had
    already required hand-recomputing the whole-neck spot. One fret width is what the shift was
    always meant to be, so the camera reads it through highwayFretLineX like every other fret
    coordinate, which also makes the lefty mirror structural instead of a hand-written negation.
    */
    double focus_body_shift_frets{1.0};

    /*!
    \brief Screen height the board anchor is pinned to, in NDC.

    The camera projects the world point (focus X, board surface, hit line) and then translates
    the whole picture vertically so that point lands at this NDC height. The translation is
    deliberately vertical-only: the board slides freely left/right as the fret focus moves while
    the anchor height never changes.
    */
    double ndc_pin_y{-0.9};

    /*!
    \brief Camera yaw in radians (Charter rotY = 0.03), negated under the lefty mirror.

    The yaw makes camera depth vary along a string, so strings slope ~2-3 degrees on screen and
    the body-side neck end renders slightly larger — the angled-neck look; the zero-rotation
    formulation looked flat by comparison.

    The camera's only rotation, deliberately. Charter also ships a forward pitch (rotX = 0.06),
    but that tilt skews the whole picture — verticals lean — and is not carried here: the wanted
    angled-neck reading is this yaw's string slope alone. A pitch parameter was carried at zero
    for a while and then removed; the camera chain has no X rotation at all now, which is *why*
    fret lines project exactly vertical (a yaw never mixes world Y into clip W or X). That
    exactness is regression-tested at the shipped defaults.
    */
    double camera_yaw_radians{0.03};

    /*! \brief Near clip plane distance. */
    double near_plane{0.1};

    /*! \brief Far clip plane distance; covers the visibility window with headroom. */
    double far_plane{100.0};

    /*!
    \brief Base perspective scale of the reference frustum (Charter near / nearRight = 2/3).

    The projection multiplies camera-space X and Y by this base times an aspect-dependent screen
    scale (see makeHighwayWorldToClip); the resulting field of view is very wide (~143 degrees
    horizontal at 16:9), which is a large part of the composition. The single magnification
    knob: it scales both axes together, so turning it changes how much board fills the screen
    without ever distorting it. Charter additionally lifted the vertical scale by +0.05 — an
    anamorphic stretch of 5 to 10 percent depending on window shape, which rendered square note
    heads as tall rectangles; it is deliberately not carried, in favor of an exactly square-pixel
    frustum (regression-tested), which costs roughly 5 percent of the board's on-screen height.
    Recover that here if the composition ever reads short, not with a vertical-only term.
    */
    double frustum_scale_base{2.0 / 3.0};

    /*! \brief Divisor applied to the camera position for the parallax background layer. */
    double background_parallax_divisor{4.0};

    /*! \brief World amplitude of the background's slow sinusoidal sway. */
    double background_sway_amplitude{0.5};

    /*! \brief Sway rate in cycles per second; deliberately slow. */
    double background_sway_hertz{0.05};

    // Deliberately NOT comparable. Every field above is a double of this struct's OWN, so a
    // defaulted operator== compares floats directly and trips -Wfloat-equal on GCC, Clang, and
    // clang-cl -- and because a defaulted comparison is only defined once it is odr-used, an unused
    // one breaks all three at once on the day someone first writes `a == b`, on a line nobody
    // edited. Nothing compared two metrics structs, so the operator was deleted rather than
    // hand-written. If a comparison is ever needed, spell it out with std::is_eq(lhs.x <=> rhs.x)
    // per the coding conventions; HighwayHandWindow next door is the worked example.
};

/*!
\brief Returns the world X of a fret-line coordinate.

Fret 0 (the nut) sits at x = 0 and every fret line adds \ref HighwayMetrics::first_fret_distance:
the neck is equal-width, matching Charter. Integer coordinates are the fixed fret lines;
fractional values sit between them — used by the sliding hand window, whose edges travel between
the fixed lines. The lefty mirror reflects the fret axis through the nut, as pure math the
renderer never sees. Integer call sites convert to double and get the identical value.

A realistic taper toward the body remains an open product question (roadmap 25-Q1). It is *not*
a constant flip: fret-relative note-head and chord-box widths would have to come with it, so the
geometry lands here as a real change when the question is answered rather than as a dormant knob.

\param fret Fret-line coordinate; fractional values sit between the integer lines.
\param metrics World-space constants.
\param mirrored True to reflect the fret axis for left-handed display.
\return World X of the fret-line coordinate.
*/
[[nodiscard]] inline double highwayFretLineX(
    double fret, const HighwayMetrics& metrics, bool mirrored)
{
    const double x = fret * metrics.first_fret_distance;
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
\brief Returns the fixed whole-neck world X that the camera's fret focus blends toward.

Charter's weighted whole-neck spot is fretPos(24) * 0.4 + fretPos(0) * 0.6; the nut term is zero
by construction, leaving 40 percent of the top fret line's X.

Derived rather than stored so it cannot fall out of step with the fret axis it is defined
against — the value was hand-recomputed once already when
\ref HighwayMetrics::first_fret_distance narrowed from 1.2 to 1.1, which is exactly the drift a
stored constant invites. Routing it through highwayFretLineX also means the lefty mirror is the
same reflection every other fret coordinate gets, instead of a hand-written negation at the call
site. The weight and the fret count are board geometry, not tuning:
\ref HighwayMetrics::focus_whole_neck_blend and \ref HighwayMetrics::focus_body_shift_frets are
the focus target's only knobs, and they map one-to-one onto its gain and its offset.

\param metrics World-space constants.
\param mirrored True to reflect the fret axis for left-handed display.
\return World X of the whole-neck focus spot.
*/
[[nodiscard]] inline double highwayFocusWholeNeckX(const HighwayMetrics& metrics, bool mirrored)
{
    // Charter's body-end weight; the nut end carries the remaining 0.6 at x = 0, so it vanishes.
    constexpr double body_end_weight = 0.4;
    return highwayFretLineX(g_highway_fret_count, metrics, mirrored) * body_end_weight;
}

/*!
\brief Returns the sustain tail's half-width: one third of the note head's.

Derived rather than stored so the proportion cannot drift from the head size it is defined
against — the tail is the head's slimmer echo, not an independently tuned width.

\param metrics World-space constants.
\return Half-width of a sustain tail in world units.
*/
[[nodiscard]] inline double highwayTailHalfWidth(const HighwayMetrics& metrics)
{
    return metrics.note_half_width / 3.0;
}

/*!
\brief Returns the world Y of a one-based lane, counted from the bottom.

Lanes are centered on half-string offsets above the string grid's base
(\ref HighwayMetrics::string_grid_base_y), so the bottom lane (1) sits the base plus half a
string spacing off the floor and the fret lines spanning the grid stick out the same half-string
distance above the top lane as below the bottom lane. The grid hugs the flat notes on purpose (a
far-away head standing vertical in the rolling flip dips below the floor at the horizon; a full
one-string stack lift clears that but loses the flat-against-the-board look, which wins). The
shared seam so every string-plane consumer (string lanes, fingering spots) maps lanes
identically.

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
\brief Returns the world Y of the string grid's top edge.

The far end of the span \ref HighwayMetrics::string_grid_base_y opens. Because
\ref highwayLaneToY centers lanes on half-string offsets above the base, closing the grid a whole
string spacing per lane above the base leaves exactly the same half-string margin above the top
lane as the base leaves below the bottom one, which is what makes the fret lines stick out
symmetrically.

Together with the base this is the vertical extent nothing on the board face may leave: the fret
lines span it, and anything that must not rise past the fret grid or sink toward the floor
saturates against it.

\param string_count Number of displayed lanes; values below one count as one, so an empty chart
       still yields a one-lane grid rather than a collapsed one.
\param metrics World-space constants.
\return World Y of the grid's top edge.
*/
[[nodiscard]] inline double highwayStringGridTopY(int string_count, const HighwayMetrics& metrics)
{
    return metrics.string_grid_base_y +
           (static_cast<double>(std::max(string_count, 1)) * metrics.string_distance);
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
\brief Returns how far a bent tail lifts above its unbent lane, for a pitch offset in half steps.

Exactly one string-lane gap per semitone, so lift reads as pitch the same way lane position does:
a bent tail reaches the lane N gaps away when the pitch is N semitones up, and a whole-step bend
visibly crosses two lanes. That identity is why the rate is the string spacing itself rather than
a constant stored beside it — Charter's separate stringDistance x 0.8 lift is deliberately
superseded. Vibrato depth is authored in semitones and rides the same scale on purpose.

Unbounded on purpose: this is the pitch-space rate, not a drawable position. Use
\ref highwayBentNoteY to place a bent note, which applies this rate and then holds the result on
the board.

PROVISIONAL RATE. Two decisions have overtaken the linear identity above and it is expected to be
replaced. The supported bend range is three whole steps, which is six semitones and therefore six
lane gaps — more than a six-lane grid is tall — so at the ceiling the identity cannot hold no
matter which lane the note starts on. And the drawn shape is meant to read as a physical bend,
where displacement is NOT linear in pitch: stretching a string laterally by d lengthens it by
about d squared, tension rises with that, and pitch rises with the square root of tension, so
n semitones needs a displacement proportional to the square root of n. Each extra semitone
therefore moves the string LESS than the one before (1.00, then 0.41, then 0.32 of the first
semitone's distance), which is why a real bend's travel bunches up as it goes even though the
force keeps climbing. Reworking this into a concave mapping over the three-whole-step range is
open work; the saturation in \ref highwayBentNoteY keeps the current rate safe meanwhile.

\param semitones Pitch offset above the unbent string in half steps; fractional values are
       ordinary (a bend in progress, a vibrato wobble).
\param metrics World-space constants.
\return World Y offset from the unbent lane center.
*/
[[nodiscard]] inline double highwayBendLiftY(double semitones, const HighwayMetrics& metrics)
{
    return semitones * metrics.string_distance;
}

/*!
\brief Returns the world Y a bent note draws at, held inside the string grid.

The one authority for where a bent note sits, because the lift alone is not a position. Applying
\ref highwayBendLiftY raw put a two-whole-step bend on a middle lane of a six-string stack BELOW
THE FLOOR — the floor is the origin and nothing draws beneath it — and put the mirrored case above
the top fret line. Both are states the inversion rule's own rationale said could not happen, so the
boundary is enforced here instead of merely asserted in a comment.

Saturating is the correct yield rather than a fudge. The lift is deliberately literal: N semitones
reaches the lane N gaps away, so a bend arrives at the pitch it now sounds. A two-whole-step bend
needs four gaps, which a six-lane grid cannot supply from a middle lane in either direction, so the
arrival lane stops being literal exactly when the board cannot express it — and it stops at the
edge rather than through it. This is NOT the 2D tab lane's rule and must not be unified with it: a
tab lane has no pitch axis, so it maps a bend onto a fraction of the tail instead, and that
difference is a property of the two surfaces rather than a disagreement between them.

The direction still comes from the caller (highwayBendInverted), which prefers the roomier side,
so saturation only ever bites the residue that side could not hold.

\param lane_y World Y of the note's unbent lane center.
\param inverted True when the lift points downward, per highwayBendInverted.
\param semitones Pitch offset in half steps; may be negative (a vibrato wobble dipping below the
       unbent pitch), in which case the drawn offset flips with it.
\param string_count Number of displayed lanes, which sets the grid's top edge.
\param metrics World-space constants.
\return World Y of the bent note, never outside the string grid.
*/
[[nodiscard]] inline double highwayBentNoteY(
    double lane_y, bool inverted, double semitones, int string_count, const HighwayMetrics& metrics)
{
    const double lift = (inverted ? -1.0 : 1.0) * highwayBendLiftY(semitones, metrics);
    return std::clamp(
        lane_y + lift, metrics.string_grid_base_y, highwayStringGridTopY(string_count, metrics));
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
