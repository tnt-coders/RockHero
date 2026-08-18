/*!
\file highway_board_scales.h
\brief EXPERIMENT SCAFFOLDING — the candidate tables still being sighted in the app: the head's
       width against its fret slot, and the harmonic head's diamond and symbol.

Each table is deleted whole once the user signs its decision: the winner's numbers move inline
and the table, its cycling command, and its keybind go with it. Nothing else in the renderer
knows these tables exist — the width axis reaches the board by scaling a metric the renderer
already reads, so no drawing code branches on it.

Two axes that once lived here are SIGNED at 1.000 and deleted (2026-08-17). The uniform FAMILY
scale died twice: the eye ruled today's size "by far the closest" to the reference, and the
pitch-frame analysis proved no uniform value can be right — the reference's slot-to-pitch ratio
varies per fret (q ≈ 0.971–0.979, ~3.94 at the nut to ~2.62 at fret 13) while ours is a constant
3.143, so a uniform shrink that fixes the slot fill at mid-neck opens the chord stacks to a gap
the reference never shows. The STRING SPACING axis measured 0.966, inside its own band around
1.000 — the board's spacing was never the mismatch. The measurement narrative behind both
rulings lives in docs/plans/in-progress/highway-note-art-state.md.
*/

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief One scale candidate: a named multiplier against today's shipped metric.

A MULTIPLIER rather than an absolute value, so a row states its own claim — "eighty-seven
percent of today" — instead of a world number whose meaning depends on what the metric happens
to be. It also makes 1.0 the honest control rather than a coincidence.
*/
struct BoardScaleCandidate
{
    /*!
    \brief Stable name, printed by the cycling command.

    Carries the resulting slot fill in its own text rather than in a second field: the number
    exists only to be read in the log, and a number that is only ever printed belongs in the
    string it is printed from.
    */
    std::string_view name;

    /*! \brief Multiplier applied to the shipped metric. */
    double scale;
};

/*!
\brief Head-width candidates: scale `HighwayMetrics::note_half_width` alone, height held fixed.

The axis the measurement actually asked for. Two independent witnesses put the reference's head
at 45.5% of its fret slot against our 57.6%, while its on-screen chord stacks fill 0.78-0.98 of
the string pitch — which our HEIGHT already does (0.94). A uniform family scale can only reach
one of those at a time; this knob narrows the width and leaves the vertical presence alone.

What follows the knob and what holds, stated because the split is the point: the sustain tail
follows (it is a third of the width metric by derivation), and the accent glow's x-extents
follow the squashed art. Technique marks, arpeggio brackets and the node-head diamond are square
art sized from `note_half_height`, so they hold still — the reference behaves the same way; its
own marks exceed its narrow gem.

The art itself stretches anisotropically at runtime (13-21% across the rows), thinning vertical
strokes slightly and rounding corners into ellipses. That is accepted for sighting and never
ships: the winner's width gets baked into the art (the head-w079 atlas variants already bake the
far row undistorted, as the cross-check).
*/
inline constexpr std::array<BoardScaleCandidate, 3> g_head_width_candidates{{
    {.name = "today - the head fills 57.6% of its fret slot", .scale = 1.000},
    {.name = "0.87x - 50.0% of the slot, the round number between", .scale = 0.868},
    {.name = "0.79x - 45.5%, the reference's measured slot fill", .scale = 0.790},
}};

/*!
\brief One candidate for the harmonic head: its diamond base and its symbol, sized separately.

Its own axis, deliberately — the overflow this decides is a property of the art: symbol and base
are drawn in fixed proportion, so the symbol overhangs its diamond by the same 26% of the
diamond's half-span whatever the board's metrics do. No board knob moves it, which is exactly
why it needs its own rather than riding one.

The diamond's SIZE itself is signed (2026-08-17): it stays at today's span, deliberately larger
than the pitch standard the technique marks follow, because the tips standing proud of the ring
are what make the mark read. The costs are known and accepted pending evaluation: stacked
diamonds on adjacent strings interpenetrate ~4.2 texels per side, and on the outer strings the
tip extends ~4.2 texels past the string grid's edge line.

The measured fact this exists to settle: the symbol does not merely touch its diamond, it CROSSES
it by 4.07 texels — a containing span of 19.72 against a 15.65 half-span. Only three things can
give, and the rows below are those three: the symbol shrinks, the diamond grows, or the crossing
is declared deliberate. Shrinking the whole mark FAMILY is not among them; that was sighted and
rejected, and the two laws that would do it land within 2-4% of the rejected sizing.

External evidence for the third option, which is why "today" is a serious candidate rather than
just the control: the third-party reference draws its own circular modifier marks at about 1.07x
its string pitch, so marks overhanging their bases — and even their lane — is that product's
normal grammar rather than a defect.
*/
struct HarmonicSizeCandidate
{
    /*! \brief Stable name, printed by the cycling command. */
    std::string_view name;

    /*! \brief Multiplier on the diamond base's drawn size, and on the glow that traces it. */
    double diamond_scale;

    /*! \brief Multiplier on the harmonic symbol riding that base. */
    double mark_scale;
};

/*!
\brief The harmonic candidates, in stable order — index 0 is today's shipped art.

Rows 1 and 3 are the two live answers; 2 and 4 bracket row 3 so the sighting can see the size
either side of the smallest containing diamond rather than judging it alone.

Only the diamond and its own symbol move. Every other technique mark holds still, which is the
constraint that keeps this axis honest: the family stays consistent while one shape is judged.
*/
inline constexpr std::array<HarmonicSizeCandidate, 5> g_harmonic_size_candidates{{
    {.name = "today - the symbol crosses its diamond by 4.07 texels",
     .diamond_scale = 1.000,
     .mark_scale = 1.000},
    // Changes the symbol alone, so stacked node heads are untouched. Re-opens the 2026-08-15
    // ruling that pairs the harmonic's height to the full mute's — with in-family precedent,
    // since the pinch was already split out of that rule for sitting on a different base.
    {.name = "fit the symbol - it shrinks to sit inside; diamond and stacking untouched",
     .diamond_scale = 1.000,
     .mark_scale = 0.742},
    {.name = "grow 1.165x - the bracket's midpoint; still does not contain the symbol",
     .diamond_scale = 1.165,
     .mark_scale = 1.000},
    // Point-to-point then equals a rectangle head's full width; two independent rationales for
    // this size land 0.12 texels apart. Costs about 10 screen px per side of stacked overlap.
    {.name = "grow 1.332x - the smallest diamond that contains the symbol",
     .diamond_scale = 1.332,
     .mark_scale = 1.000},
    {.name = "grow 1.504x - past the answer, so the sighting can see its far side",
     .diamond_scale = 1.504,
     .mark_scale = 1.000},
}};

} // namespace rock_hero::common::ui
