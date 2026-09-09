/*!
\file highway_emphasis_styles.h
\brief Appearance numbers for the note-emphasis axis: the ghost and accent treatments.

Both ends sit the same distance either side of a neutral note — a ghost keeps half the light, an
accent spends half again more. That symmetry is deliberate and worth keeping, but the two are NOT
one number and are not derived from one: they act through different mechanisms (see \ref
g_ghost_alpha and \ref g_accent_gain, each of which names the other), they are judged by eye
separately, and only the accent has a hard constraint of its own. Stating them apart is what lets
either be retuned without silently dragging the other.

The accent light's SHAPE — its reach and falloff exponent — is a different question and lives beside
the glow states in highway_renderer.cpp, with the alternatives tabulated in
docs/plans/in-progress/highway-note-art-state.md. Only its strength belongs to the axis.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>

namespace rock_hero::common::ui
{

/*!
\brief Alpha a ghost keeps, everywhere: head art, technique markers, and sustain tail alike.

The ghost look is `half light`, chosen from six candidates. A ghost is quieted by ALPHA on this
surface, which composites over a dark 3D world - the opposite choice from the 2D lane, which is
opaque and leans its ink toward the lane's own ground instead. Both surfaces spend the same weight;
each spends it the way it actually composites. The candidates it beat: `dim fill` and `dim fill
deep` (opaque darkening), `dim small` (thinning the head), and `hollow` (an outline instead of a
fill).

ONE number on purpose, head and markers and tail alike. Splitting it - 0.45 on the head and markers
against 0.65 on the tail - argues that a ghost is an attack dynamic rather than a sustain one, so a
ribbon dimmed as hard as its head would read as a rendering fault. The eye never makes that
distinction: the note reads as one quiet gesture at a single half, and the axis is simpler by a
whole variable.

Mirrored by \ref g_accent_gain, which spends half again MORE than neutral where this keeps half.
The pair is deliberately symmetric and deliberately NOT shared: a ghost is alpha on the object
itself, an accent is radiance on a light drawn beside it, and they are sighted separately. Keep
them symmetric by intent when either moves; do not derive one from the other.
*/
inline constexpr double g_ghost_alpha{0.5};

/*!
\brief The alpha this surface draws an emphasis at: \ref g_ghost_alpha for a ghost, else full.

The one mapping from the emphasis axis to this surface's transparency, so the renderer's several
quieting sites (head, markers, tail, open bar, chord box) cannot drift apart.

\param emphasis How hard the note is struck.

\return Alpha in (0, 1].
*/
[[nodiscard]] constexpr double emphasisAlpha(common::core::NoteEmphasis emphasis) noexcept
{
    return common::core::isGhosted(emphasis) ? g_ghost_alpha : 1.0;
}

/*!
\brief Radiance the accent light is emitted at on a NOTE, as a multiple of neutral.

The mirror of \ref g_ghost_alpha: where a ghost keeps half the light, an accented note spends half
again more, so the two ends sit the same distance either side of neutral. Mirror by INTENT, not by
derivation — the two are separate constants because they are separate mechanisms. This multiplies
the glow shader's field (`u_accent_glow_params.w`), where 1.0 reproduces the un-gained light
exactly; the ghost's number scales the object's own alpha. Nothing in the renderer requires them to
match, and only this end has a hard constraint (the clip below), so a future retune of either must
be sighted on its own and the symmetry re-chosen rather than inherited.

Above the shader's per-channel clip, which begins at 255/237 = 1.076 for the palette's brightest
channel, so an accent grows a white-hot core rather than only a brighter pedestal — the behaviour
the glow shader's own comment describes as what makes a bright light read as bright. The gain also
widens the halo, because its visible edge is wherever gain times the falloff clears the display
threshold.

NOTES ONLY. A chord box draws its accent at neutral radiance instead; that is a compensation for
how much of each subject ends up lit, and the reasoning lives with the constant in
highway_renderer.cpp rather than here, because it is a property of the subject rather than of the
emphasis axis.
*/
inline constexpr double g_accent_gain{1.5};

/*!
\brief Thickness multiplier for a ghosted open string's bar, which has no head to thin.

Its own number rather than one of the alphas: it is a THICKNESS, not a light level. An open string
carries the axis on its bar because it has no head to wear it, which is also why an atlas mark
drawn on a head cannot express the axis here.
*/
inline constexpr double g_ghost_open_bar_thickness{0.5};

} // namespace rock_hero::common::ui
