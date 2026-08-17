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

} // namespace rock_hero::common::ui
