/*!
\file chart_technique.h
\brief The techniques the chart editor's one toggle verb sets or clears across a selection.
*/

#pragma once

#include <cstdint>

namespace rock_hero::editor::core
{

/*!
\brief A technique the toggle verb sets or clears across the selection.

One verb, one window, one law for every value: a selection already carrying the technique on
every note clears it, anything else sets it on all of them (the uniform-scope law), each press is
one compound undo entry, and a second press inside the toggle window reverses the first exactly.
Which notes can take the technique is the chart rule authority's to say, never this enum's: the
verb asks it per note and skips the rest.

`Legato` runs under the same verb and window with its own planning law — the connection resolver
decides eligibility, the assist grows a predecessor's tail, and a press that would set nothing
clears instead — because the toggle contract is identical even though the plan is not.

`Vibrato` and `WideVibrato` are two values rather than one because they are two VERBS, not two
fields: each is the toggle of its own tier under the one law above, so pressing either on a scope
already at that tier clears it, and pressing it on a scope at the OTHER tier is an ordinary set
that replaces the width in one entry. The alternative — one verb cycling off/narrow/wide — was
declined at the ruling: a cycling verb has no "already carries it" answer to give the uniform-scope
law, and `Shift+`letter is this keymap's stated shape for a magnitude variant of a plain letter's
own technique.

`PickSlide`, `Tap`, `Slap` and `Pop` are four values of that same shape one level up: they are
values of the note's ATTACK, which holds exactly one, so each is the toggle of its own attack
against the plain pick. A press on a scope already at that attack clears it back to the pick, and a
press on a scope at another one is an ordinary set that replaces it in a single entry — the vibrato
pair's rule, applied to a field with four claimants here instead of two. `LeftTap` is deliberately
NOT among them: the fretting hand's tap is a statement no toggle may withdraw, so it keeps its own
stating verb rather than a row (\ref IEditorController::onChartLeftTapRequested).

`Harmonic` and `PinchHarmonic` are the third such pair, and the one whose two rows SET through
different planners and CLEAR through the same one. Setting differs because the two hands do
different things: `H` turns the fret already typed into the node the fretting finger touches, while
`Shift+H` re-hands the note to the picking thumb, which is exactly the attack verb. Clearing is one
plan for both because each row's noun is a HARMONIC, so its clear must remove one — where clearing
the pinch through the attack row alone would leave a stop and a node behind, an artificial harmonic
nobody authored. `Harmonic` is also the one row whose SET states a VALUE, so the verb routes it
through the pending-entry machinery every stated value in this editor goes through; the row's own
plan is what a press that states no choice means.
*/
enum class ChartTechnique : std::uint8_t
{
    /*! \brief The picking hand's palm mute. */
    PalmMute,
    /*! \brief The dead (fully damped) note. */
    Dead,
    /*! \brief Tremolo picking. */
    Tremolo,
    /*! \brief The ordinary vibrato — the narrow tier of the width axis. */
    Vibrato,
    /*! \brief The deliberately exaggerated vibrato — the wide tier of the same axis. */
    WideVibrato,
    /*! \brief The accent — the loud end of the emphasis axis. */
    Accent,
    /*! \brief The ghost note — the quiet end of the emphasis axis. */
    Ghost,
    /*! \brief The pick-slide attack. */
    PickSlide,
    /*! \brief The two-hand tap attack — the PICKING hand's tap, the dark-T plate's letter. */
    Tap,
    /*! \brief The slapped attack. */
    Slap,
    /*! \brief The popped attack. */
    Pop,
    /*! \brief The fretting hand's harmonic: the finger touches its node and presses nothing. */
    Harmonic,
    /*! \brief The pinch harmonic: the picking thumb grazes a node as the plectrum passes. */
    PinchHarmonic,
    /*! \brief The legato connection claim. */
    Legato
};

} // namespace rock_hero::editor::core
