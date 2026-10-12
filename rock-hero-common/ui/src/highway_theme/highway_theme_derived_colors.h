/*!
\file highway_theme_derived_colors.h
\brief The colors the highway derives from its theme's roles, and the CIELAB measure they are
derived and judged in.
*/

#pragma once

#include <rock_hero/common/ui/highway_theme/highway_theme.h>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*! \brief A color in CIELAB under the D65 white point. */
struct Cielab
{
    /*! \brief L*, from 0 (black) to 100 (white). */
    double lightness;

    /*! \brief a*, green (negative) to red (positive). */
    double a;

    /*! \brief b*, blue (negative) to yellow (positive). */
    double b;
};

/*!
\brief Converts a color's RGB, ignoring its alpha, through linear sRGB and CIE XYZ to CIELAB.
\param color The color.
\return The color in CIELAB.
*/
[[nodiscard]] Cielab cielabOf(ArgbColor color);

/*!
\brief Measures the CIE76 color difference: the straight-line distance in CIELAB.
\param first One color; its alpha is ignored.
\param second The other color; its alpha is ignored.
\return The distance, zero for equal RGB.
*/
[[nodiscard]] double deltaE(ArgbColor first, ArgbColor second);

/*!
\brief Derives the picking hand's counterpart of one of the fretting hand's colors.

The two hands share each kind's hue, so a tapped chord reads as a chord, and differ by colorfulness,
so the hand reads apart: in CIELAB's lightness, chroma and hue, the hue is kept, the chroma drops to
a quarter, and the lightness becomes the source's perceived lightness (Fairchild and Pirrotta's L**,
which adds to L* the brightness a saturated color seems to carry beyond its luminance). The pale
result therefore looks as bright as the color it comes from rather than darker than it, and a grey
source, having no chroma to give up, yields itself.

A lightness past white is held at white, and a channel the result leaves outside sRGB is clamped to
its range, so the color returned is always the nearest one sRGB can show.

\param fretting_color The fretting hand's color for one kind of span.
\return The picking hand's color for the same kind, under the source's alpha.
*/
[[nodiscard]] ArgbColor pickingHandColor(ArgbColor fretting_color);

/*!
\brief Derives the dark a chord box's fill shades to at its middle from the box's own color.

Each RGB channel scales by 0.286 and rounds to the nearest byte, so the dark keeps the box's hue at
the depth the fill was tuned at (a box of `#00D2D5` darkens to `#003C3D`).

\param box_color The box's color, which its fill wears at its ends.
\return The fill's middle color, under the box color's alpha.
*/
[[nodiscard]] ArgbColor boxFillMiddle(ArgbColor box_color);

/*!
\brief Derives the ink the 2D lane paints a span's rails and label chip with from the span's kind.

The chip prints white text over this ink, so the ink is held to the one relative luminance at which
white reads at 4.45:1 against it: the balance, set by eye, between an ink light enough to read as
the lane's span color and one dark enough to carry its label. Whatever its base, every kind's ink
shares that luminance, so a theme's chord and arpeggio differ on the lane by hue and chroma alone.

The base is scaled in linear light, which keeps its hue and chromaticity. A base too dark to reach
the luminance by scaling (its brightest channel would pass full) blends toward white instead, which
keeps its hue but gives up chroma; black blends to the grey of that luminance.

\param base The kind's role color, chord or arpeggio.
\return The ink, under the base's alpha.
*/
[[nodiscard]] ArgbColor laneShapeInk(ArgbColor base);

/*!
\brief Derives the color of a dotted fret's number on a measure downbeat outside the hand window.

The number sits on the lane lines it labels, so it wears their lit color at half its alpha (rounded
half up); a number the window covers partly blends from it toward fret_number_lit.

\param theme The theme.
\return lane_line_lit at half alpha.
*/
[[nodiscard]] ArgbColor fretNumberDim(const HighwayTheme& theme);

} // namespace rock_hero::common::ui
