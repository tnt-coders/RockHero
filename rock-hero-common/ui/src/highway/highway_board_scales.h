/*!
\file highway_board_scales.h
\brief EXPERIMENT SCAFFOLDING — the two candidate tables for the note-family-to-string-spacing
       ratio, one per axis, while that proportion is being sighted in the app.

Deleted whole once the user signs a ratio: the winner's numbers move inline into
`HighwayMetrics`' own defaults and this file, its two cycling commands, and their keybinds go
with it. Nothing else in the renderer knows these tables exist — both axes reach the board by
scaling a metric the renderer already reads, so no drawing code branches on them.

WHAT IS BEING JUDGED. A note head's half-height as a fraction of the string spacing. Ours is
0.471 — the head art's 10.8218 texels over the 22.9688-texel lane pitch. Five screenshots of a
third-party reference were measured pixel by pixel and every clean sample landed BELOW ours, so
the family reads too tall for its spacing; the amount is a judgement, because that reference's
own ratio varies by camera (never by note role — the one sample that suggested otherwise proved
to be a mid-approach render state, not a plain head).

TWO AXES TO THE SAME RATIO, which is exactly why both are tables here rather than one. The
proportion can be corrected by shrinking the note family against a fixed spacing, or by widening
the spacing around a fixed family. They reach the same number and look nothing alike: the first
keeps the neck's size and makes its furniture smaller, the second keeps the furniture and grows
the neck. Only sighting them side by side settles which one the eye wanted.
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
    is printed from.
    */
    std::string_view name;

    /*! \brief Multiplier applied to the shipped metric. */
    double scale;
};

/*!
\brief Family-size candidates: scale `HighwayMetrics::note_half_width`, spacing held fixed.

ONE metric carries the whole family because the renderer already derives everything from it: the
head quad is `note_half_width` square, the arpeggio brackets share it, the sustain tail is a third
of it, and — the part that matters most here — `headArtTexelWorld()` converts every art-silhouette
constant through it, so the accent glow's distance field follows the art automatically. Scaling
this one number moves art, glow, brackets and tail together by construction.

That last property is why this axis is a metric rather than a set of rebaked atlases. Rescaling
the ART inside the atlas cells reaches the same head size, but it leaves the silhouette constants
describing a shape the art no longer has (light with nothing under it), and it silently changes
the authored head-to-tail proportion, since tail width is geometry rather than art. The atlas
route is still the only way to change one mark's size RELATIVE to the others; for a uniform
family scale it is strictly the more complicated path to a worse result.

No sharpness is lost. At 1920x1080 the head quad draws about 123 px wide from 41 texels of art —
already a 3x magnification — so a smaller quad merely magnifies less.

The rows are the measured ladder: 0.89 is the smallest defensible ratio change, 0.80 matches the
single best-verified reference camera and is the point at which every technique mark first fits
inside the lane pitch, 0.72 is the target weighted across the reference's 1080p cameras and the
point at which stacked node-head diamonds stop overlapping at all, and 0.76 sits between the two
serious candidates because the gap between them is where the eye is most likely to land.
*/
inline constexpr std::array<BoardScaleCandidate, 5> g_family_scale_candidates{{
    {.name = "today, ratio 0.471", .scale = 1.00},
    {.name = "0.89x, ratio 0.420 - smallest defensible change", .scale = 0.89},
    {.name = "0.80x, ratio 0.377 - best-verified camera; every mark fits the lane", .scale = 0.80},
    {.name = "0.76x, ratio 0.358 - between the two serious candidates", .scale = 0.76},
    {.name = "0.72x, ratio 0.339 - 1080p-weighted; stacked diamonds clear", .scale = 0.72},
}};

/*!
\brief Spacing candidates: scale `HighwayMetrics::string_distance`, family held fixed.

The same ratios approached from the other side, so each row here is the reciprocal of the family
row that targets the same number: widening the spacing by 1/k lands the same head-height-over-
spacing figure that shrinking the family by k does.

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
inline constexpr std::array<BoardScaleCandidate, 5> g_spacing_scale_candidates{{
    {.name = "today, ratio 0.471", .scale = 1.000},
    {.name = "1.124x wider, ratio 0.420 - smallest defensible change", .scale = 1.124},
    {.name = "1.250x wider, ratio 0.377 - best-verified camera", .scale = 1.250},
    {.name = "1.316x wider, ratio 0.358 - between", .scale = 1.316},
    {.name = "1.389x wider, ratio 0.339 - 1080p-weighted", .scale = 1.389},
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
