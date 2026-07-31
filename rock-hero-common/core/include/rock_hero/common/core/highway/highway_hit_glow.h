/*!
\file highway_hit_glow.h
\brief Strike-glow envelope math: peak-at-crossing decay and the inter-onset release clamp.
*/

#pragma once

namespace rock_hero::common::core
{

/*!
\brief Returns the strike-glow intensity at a time offset from a note's crossing.

The glow is a pure stateless function of the offset: zero before the crossing (strict zero
pre-glow — nothing lights before the note lands), exactly 1.0 at it, then a smoothstep fade to
zero at \p release_seconds — a momentary bright hold at the crossing (zero initial slope), an
even mid fade, and a soft landing, so the strike reads as a gradual dissolve rather than a
blink.

\param since_seconds Seconds since the note crossed the hit line (negative before it).
\param release_seconds Envelope length; a non-positive length yields zero.
\return Intensity in [0, 1].
*/
[[nodiscard]] double highwayHitGlowIntensity(double since_seconds, double release_seconds) noexcept;

/*!
\brief Returns the effective release for an onset given the gap to the next onset lighting the
same glow geometry.

Clamps the nominal release so a glow always ends a dark trough before the next pop on its own
geometry: spacing minus the guard, floored at half the spacing so ultra-dense charts keep both a
pop and a trough, and never longer than the nominal release. Because the clamp derives from onset
spacing alone it is implicitly tempo-relative with no BPM read. An infinite spacing (nothing
follows) and a non-positive spacing (coincident onsets are one strike, not a clamp pair) both
return the nominal release.

\param nominal_release_seconds Full release used when nothing follows closely.
\param trough_guard_seconds Dark-gap length wanted before the next pop.
\param spacing_seconds Onset gap to the next strike on the same geometry.
\return Clamped release, at most \p nominal_release_seconds.
*/
[[nodiscard]] double highwayHitGlowRelease(
    double nominal_release_seconds, double trough_guard_seconds, double spacing_seconds) noexcept;

} // namespace rock_hero::common::core
