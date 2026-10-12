/*!
\file highway_render_look.h
\brief The render look: how the finished highway frame is presented, chosen by a theme.
*/

#pragma once

#include <variant>

namespace rock_hero::common::ui
{

/*! \brief The scene presented exactly as rendered, at the window's resolution. */
struct HighwayPlainLook
{
};

/*!
\brief The scene shown as hard-edged virtual pixels, each the mean of the full-resolution scene it
covers, optionally with scanlines.
*/
struct HighwayPixelLook
{
    /*! \brief Edge of one virtual pixel in window pixels, g_pixel_look_min_scale to max. */
    int scale;

    /*!
    \brief How much every other virtual row darkens, 0 (none) to 1 (black).

    Any amount above 0 also softens each virtual pixel 1:2:1 over the rows above and below, one
    scanline period, so a note moving down the board never pulses between a bright row and a dark
    one; at 0 the pixels stay crisp.
    */
    double scanlines;
};

/*! \brief The smallest pixel-look scale: 1 would be the plain look. */
inline constexpr int g_pixel_look_min_scale = 2;

/*! \brief The largest pixel-look scale, a sanity cap beyond which lanes and heads merge. */
inline constexpr int g_pixel_look_max_scale = 8;

/*!
\brief The scene rendered at a fraction of the window's resolution, then ordered-dithered (a 4x4
Bayer matrix) to the theme's own opaque colors.

The palette is every color the theme states, alpha dropped — each role and each string color —
plus black and white, so the dither can only show colors the theme's guardrails already approved.
*/
struct HighwayDitherLook
{
    /*! \brief Edge of one virtual pixel in window pixels, g_dither_look_min_scale to max. */
    int scale;
};

/*! \brief The smallest dither scale: the window's own resolution. */
inline constexpr int g_dither_look_min_scale = 1;

/*! \brief The largest dither scale, the pixel look's cap for the same reason. */
inline constexpr int g_dither_look_max_scale = 8;

/*! \brief The dither scale a theme gets when it states none. */
inline constexpr int g_dither_look_default_scale = 1;

/*!
\brief Comic print: a 45-degree dot screen whose dots grow with each cell's luminance and wear
its color over the theme's backdrop, with dark ink outlines traced from the full-resolution
scene's luminance edges.
*/
struct HighwayHalftoneLook
{
    /*! \brief Edge of one screen cell in window pixels, g_halftone_look_min_cell to max. */
    int cell;
};

/*! \brief The smallest halftone cell, below which the dots stop reading as a screen. */
inline constexpr int g_halftone_look_min_cell = 4;

/*! \brief The largest halftone cell, beyond which a note head is a single dot. */
inline constexpr int g_halftone_look_max_cell = 16;

/*! \brief The halftone cell a theme gets when it states none. */
inline constexpr int g_halftone_look_default_cell = 8;

/*!
\brief How a theme's finished frame is presented: a closed set of built-in looks.

A look changes the finished image, never what is drawn or where. The default is the plain look.
*/
using HighwayRenderLook =
    std::variant<HighwayPlainLook, HighwayPixelLook, HighwayDitherLook, HighwayHalftoneLook>;

} // namespace rock_hero::common::ui
