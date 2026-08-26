/*!
\file tone_automation_curve.h
\brief The one evaluation of a tone parameter lane's curve, anchor included.

Both editor surfaces read a lane's curve: the projection resolves the value a new point lands on,
and the lanes view draws the line and places its ghost, caret, and readout on it. They evaluate the
same rule here so the drawn curve, the editor's landing values, and the backend curve the audio
write seam builds state one shape, never three.

The anchor is the one part of that shape neither side owns outright: the audio seam bakes it into
the backend curve when the curve is written, while the editor re-reads it from the live parameter
every frame so it follows a knob. They agree because the curve is re-derived whenever a plugin edit
settles (\c rebuildDerivedToneCurves), which is the only way a tone-state value can move under the
editor's own eyes. What is left is a knob still mid-gesture: the drawn anchor tracks it live and the
backend curve catches up when the gesture settles.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <span>

namespace rock_hero::editor::core
{

/*! \brief One (seconds, value) sample of a lane's curve, in ascending time. */
struct ToneAutomationCurveSample
{
    /*! \brief Absolute song seconds of the sample. */
    double seconds{0.0};

    /*! \brief Parameter value normalised to `[0, 1]`. */
    float norm_value{0.0F};
};

/*!
\brief The time every parameter lane's derived anchor sits at: the timeline origin.

Zero seconds, matching exactly where the audio write seam puts the anchor point it prepends, so the
drawn curve and the backend curve begin at the same instant and can never disagree about the
stretch before the first authored point.

\return The lane-start time in absolute song seconds.
*/
[[nodiscard]] double toneAutomationAnchorSeconds();

/*!
\brief The musical position a dragged anchor authors its real point at: the song's first beat.

The earliest slot the musical grid can address. It coincides with \ref toneAutomationAnchorSeconds
for any song whose first beat anchor sits at zero seconds; when a song opens with a lead-in it sits
slightly later, and the lane simply travels from the anchor to it across the lead-in — a ramp on a
continuous parameter, a hold that steps at the point on a stepped one — which is exactly what the
backend curve does there too.

\return The lane-start grid position.
*/
[[nodiscard]] common::core::GridPosition toneAutomationAnchorPosition();

/*!
\brief The value a lane's curve holds at a time.

The lane's curve is \p anchor followed by \p authored: linear segments on a continuous parameter,
held steps on a discrete one, and flat extensions off both ends. An authored sample at (or before)
the anchor's time is what the lane holds there, so the anchor contributes nothing — the same
collision the audio write seam resolves by anchoring on that sample's value.

\param anchor The lane's derived anchor sample (the parameter's pre-automation value at the start).
\param authored Authored samples in ascending time.
\param seconds Absolute song seconds to evaluate at.
\param is_discrete True when the parameter is stepped rather than continuous.
\return The curve's normalised value at \p seconds.
*/
[[nodiscard]] float toneAutomationCurveValueAtSeconds(
    ToneAutomationCurveSample anchor, std::span<const ToneAutomationCurveSample> authored,
    double seconds, bool is_discrete);

} // namespace rock_hero::editor::core
