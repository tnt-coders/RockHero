/*!
\file highway_board_scales.h
\brief EXPERIMENT SCAFFOLDING — the two candidate tables for the note-family-to-string-spacing
       ratio, one per axis, while that proportion is being sighted in the app.

Deleted whole once the user signs a ratio: the winner's numbers move inline into
`HighwayMetrics`' own defaults and this file, its two cycling commands, and their keybinds go
with it. Nothing else in the renderer knows these tables exist — both axes reach the board by
scaling a metric the renderer already reads, so no drawing code branches on them.

WHAT IS BEING JUDGED, and it is now MEASURED rather than open. A third-party reference was taken
apart by single-view metrology — its fret lines and strings are two orthogonal families of
parallel lines on one plane, so its camera is recoverable and its board can be un-projected and
measured in its own units. Against it, our note family is about 26% too large for its fret slot,
and the answer is a family scale of **0.790** with the string spacing left alone (0.966, a
narrowing so slight it sits inside its own band around 1.000).

THE YARDSTICK IS THE FRET, because it is the one quantity NEITHER axis moves. That is what makes
the two knobs separately solvable instead of jointly ambiguous: head half-width over fret slot
pins the family alone, head half-height over string pitch pins the spacing alone. Each length is
measured against the axis it spans — comparing a length to a PERPENDICULAR one is biased by
camera pitch (+10.5% at 8 degrees, and the reference's camera is pitched 7.35), which is the trap
that made the earlier head-height-over-pitch numbers look like they varied by camera when the
art does not vary at all.

TWO AXES, BUT NOT TWO ANSWERS. Both reach the same head-height-over-spacing ratio — that much is
arithmetic — but the fret separates them by 26.5% and only the family axis moves toward the
reference. The spacing table is kept so that ruling can be SEEN rather than taken on trust; see
its own comment.

Two blockers were expected and both are cleared. The reference's fret axis is equal-width like
ours, fitted through a free compression rate against its inlay markers (q = 1.000, rms 0.60 px,
with a real neck's 0.9439 excluded). And its camera dollies at fixed FOV rather than zooming —
focal length over image width holds at 0.772–0.783 across four frames whose framing differs
threefold — so there is one camera to compare against rather than a moving target.

What no scale can fix, stated so it is not mistaken for a tuning problem: the reference's head
art is about 6% taller for its width than ours (screen aspect 1.79–1.83 against our 1.923).
Scaling preserves aspect, so closing that gap needs the ART redrawn, not a metric moved. That
said, the terminal census reads the reference's aspect anywhere from 1.72 to 2.33 — its mark is
a 3-D box whose side face rides the silhouette by camera position — so our 1.923 sits inside
that spread and the redraw claim is weaker than the two clean samples first made it look.
*/

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief One candidate on either scale axis: a named multiplier against today's shipped metric.

A MULTIPLIER rather than an absolute value, so a row states its own claim — "eighty percent of
today" — instead of a world number whose meaning depends on what the metric happens to be. It
also makes 1.0 the honest control on both axes rather than a coincidence.
*/
struct BoardScaleCandidate
{
    /*!
    \brief Stable name, printed by the cycling command.

    Carries the resulting ratio in its own text rather than in a second field: the ratio exists
    only to be read in the log, and a number that is only ever printed belongs in the string it
    is printed from. Every printed ratio assumes the OTHER axis at 1.000, so rows stay
    comparable within their own table.
    */
    std::string_view name;

    /*! \brief Multiplier applied to the shipped metric. */
    double scale;
};

/*!
\brief Family-size candidates: scale `HighwayMetrics::note_half_width`, spacing held fixed.

The two head metrics carry the whole family because the renderer already derives everything from
them: the head quad spans them, the technique marks and arpeggio brackets share the height, the
sustain tail is a third of the width, and — the part that matters most here — the
`headArtTexelWidth()`/`headArtTexelHeight()` pair converts every art-silhouette constant through
them, so the accent glow's distance field follows the art automatically. This axis multiplies
both metrics, moving art, glow, marks, brackets and tail together by construction; the
head-width table below divides width from height on top of it.

That last property is why this axis is a metric rather than a set of rebaked atlases. Rescaling
the ART inside the atlas cells reaches the same head size, but it leaves the silhouette constants
describing a shape the art no longer has (light with nothing under it), and it silently changes
the authored head-to-tail proportion, since tail width is geometry rather than art. The atlas
route is still the only way to change one mark's size RELATIVE to the others; for a uniform
family scale it is strictly the more complicated path to a worse result.

No sharpness is lost. At 1920x1080 the head quad draws about 123 px wide from 41 texels of art —
already a 3x magnification — so a smaller quad merely magnifies less.

THE ROW TO PRESS IS 0.79 — the measured answer, reproducing both reference cameras' means through
the fret yardstick. 0.80 is kept beside it because it lands within 1.2% on width and 2.2% on
height while needing no new number, and because it is the point at which every technique mark
first fits inside the lane pitch. The rest bracket the answer so it can be judged against its
neighbours rather than alone: 0.89 is the smallest change anyone proposed, 0.76 sits just under
the measured value, and 0.72 is where stacked node-head diamonds stop overlapping at all.

The band: nine terminal-size marks across five captures spread 6.8% (one sigma) between marks,
putting 0.772-0.808 around the answer on the standard error of the mean — and the reference's
own two cameras disagree by 5.8% on this proportion, so "exactly" is undefined below about 6%
and any row from 0.76 to 0.82 is inside the evidence. The eye picks; the measurement only says
where to look.
*/
inline constexpr std::array<BoardScaleCandidate, 6> g_family_scale_candidates{{
    {.name = "today, ratio 0.471", .scale = 1.00},
    {.name = "0.89x, ratio 0.420 - smallest defensible change", .scale = 0.89},
    {.name = "0.80x, ratio 0.377 - within 1.2% of measured; every mark fits the lane",
     .scale = 0.80},
    // THE MEASURED ANSWER. Both reference cameras, through the fret yardstick: head half-width
    // over fret slot and head half-height over string pitch, each measured against the axis it
    // spans. The two cameras agree to 2.7% on width, and this reproduces both their means.
    // Terminal-size verified: the reference grows its approach heads exactly as ours does, but
    // the ramp completes before the note's own time, and the heads this factor was measured on
    // sit bracketed by certainly-at-play marks — a nine-mark terminal census across five
    // captures moves the factor by x1.0003, i.e. nothing.
    {.name = "0.79x, ratio 0.372 - MEASURED, terminal-size verified", .scale = 0.79},
    {.name = "0.76x, ratio 0.358 - just under the measured value", .scale = 0.76},
    {.name = "0.72x, ratio 0.339 - 1080p-weighted; stacked diamonds clear", .scale = 0.72},
}};

/*!
\brief Spacing candidates: scale `HighwayMetrics::string_distance`, family held fixed.

**MEASUREMENT HAS SINCE RULED AGAINST THIS AXIS, and the rows are kept only so that ruling can be
seen rather than taken on trust.** Each widening row reaches the same head-height-over-spacing
figure that the matching family row reaches — that much is arithmetic and still true — but the two
are not interchangeable, because that one ratio cannot separate them. Measured against the FRET,
which neither axis moves, the reference wants a head 0.2277 of a fret slot wide where ours is
0.2881. Shrinking the family reaches that; widening the spacing leaves the head 26.5% too wide for
its slot and only moves the strings apart around it. The reference's own spacing needs no widening
at all — the measured factor is 0.966, slightly NARROWER than today and inside the measurement's
own band around 1.0.

So the honest summary is: this axis was a hypothesis built on the one ratio available before the
fret yardstick existed, and the yardstick killed it. Sight it to see that, then leave it at 1.000.

This axis is the more invasive of the two and its side effects are the point of sighting it. The
string grid's height is `string_count * string_distance`, so the whole neck grows: the fret lines
lengthen, the chord boxes and hand window grow with the lanes they span, and the camera frames a
taller board. It also moves a decision that is still open — a bend's drawn travel is expressed in
STRING GAPS, so widening the gaps changes what a given bend looks like and the bend-anchor
question has to be re-read against whichever row wins here.

Kept as its own table rather than folded into the family's rows because the two axes are
independent: a sighting may well want a little of each, and a single combined row could not say
so.
*/
inline constexpr std::array<BoardScaleCandidate, 6> g_spacing_scale_candidates{{
    {.name = "today, ratio 0.471", .scale = 1.000},
    // The measured factor, and it is a NARROWING so slight it sits inside its own band around
    // 1.000. The row exists because every other row on this axis widens, which would have made
    // the range itself an unstated claim that widening was the direction.
    {.name = "0.966x, ratio 0.488 - MEASURED; spacing needs no widening at all", .scale = 0.966},
    {.name = "1.124x wider, ratio 0.420 - reaches the ratio, wrong for the slot", .scale = 1.124},
    {.name = "1.250x wider, ratio 0.377 - reaches the ratio, head still 26.5% too wide",
     .scale = 1.250},
    {.name = "1.316x wider, ratio 0.358 - further from the reference, not closer", .scale = 1.316},
    {.name = "1.389x wider, ratio 0.339 - the far end of a ruled-out direction", .scale = 1.389},
}};

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
far row undistorted, as the cross-check). Composes with the family axis, which multiplies both
head metrics, so the family rows keep meaning "the whole family" while this knob divides width
from height on top.
*/
inline constexpr std::array<BoardScaleCandidate, 3> g_head_width_candidates{{
    {.name = "today - the head fills 57.6% of its fret slot", .scale = 1.000},
    {.name = "0.87x - 50.0% of the slot, the round number between", .scale = 0.868},
    {.name = "0.79x - 45.5%, the reference's measured slot fill", .scale = 0.790},
}};

/*!
\brief One candidate for the harmonic head: its diamond base and its symbol, sized separately.

A SEPARATE axis from the two above, and deliberately so — the overflow this decides is
SCALE-INVARIANT. The symbol and the base scale together under either board scale, so the symbol
overhangs its diamond by the same 26% of the diamond's half-span at every family size and every
string spacing. No row of `g_family_scale_candidates` moves it, which is exactly why it needs its
own knob rather than riding one of theirs.

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
