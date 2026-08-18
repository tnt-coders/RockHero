/*!
\file highway_emphasis_styles.h
\brief Appearance numbers for the note-emphasis axis: the signed ghost treatment.

Both ends of the axis are SIGNED. Ghosts are a transparency treatment (numbers below); accents
are a rendered light whose look was signed 2026-08-18 as the sighted "medium flat" candidate,
with its constants living beside the glow states in highway_renderer.cpp and the tried
alternatives recorded in docs/plans/in-progress/highway-note-art-state.md. The nine-row accent
candidate table and its cycling keybind were deleted with that signing, per the scaffold
lifecycle.
*/

#pragma once

namespace rock_hero::common::ui
{

/*!
\brief The ghost look, SIGNED 2026-08-15 after sighting six candidates: `half light`.

A ghost is quieted by ALPHA on this surface, which composites over a dark 3D world - the opposite
choice from the 2D lane, which is opaque and leans its ink toward the lane's own ground instead.
Both surfaces spend the same weight; each spends it the way it actually composites.

The rejected candidates are recoverable from git history: `dim fill` and `dim fill deep` (opaque
darkening), `dim small` (thinning the head), and `hollow` (an outline instead of a fill).
*/

/*!
\brief Alpha a ghost keeps, everywhere: head art, technique markers, and sustain tail alike.

ONE number on purpose. The sighted look split it — 0.45 on the head and markers against 0.65 on
the tail — on the reasoning that a ghost is an attack dynamic rather than a sustain one, so a
ribbon dimmed as hard as its head would read as a rendering fault. Collapsing both to a half is
being tried against exactly that: if the note still reads as one quiet gesture, the split was a
distinction the eye never made, and the axis is simpler by a whole variable.
*/
inline constexpr double g_ghost_alpha{0.5};

/*!
\brief Thickness multiplier for a ghosted open string's bar, which has no head to thin.

Stays its own number even while the alphas collapse: it is a THICKNESS, not a light level. An
open string carries the axis on its bar because it has no head to wear it - the seam where the
old atlas-mark design diverged, since a mark drawn on a head could never be worn by a bar.
*/
inline constexpr double g_ghost_open_bar_thickness{0.5};

} // namespace rock_hero::common::ui
