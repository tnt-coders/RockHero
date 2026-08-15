/*!
\file highway_emphasis_styles.h
\brief Candidate appearances for the note-emphasis axis, while the two ends are being chosen.

EXPERIMENT SCAFFOLDING. Accents became a rendered light and ghosts a transparency treatment, and
the exact look of each is being sighted in the app rather than argued: the editor cycles these
tables with a keybind and logs which pair is active. When the user picks, the winner's numbers
move inline, the tables and the cycling commands are deleted, and this file goes with them.

The tables are DATA rather than branches on purpose — every candidate differs only in numbers, so
a new one costs a row and no code, and the renderer holds one code path whichever is selected.
*/

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief One candidate accent light: a corona of added light around the note's own silhouette.

Reach is stated OUTSIDE the silhouette in world units, per axis, because the board's clearances
are wildly anisotropic — about 0.0125 world to the neighbouring lane's head against 0.238 to the
next fret's, a 19:1 budget. A light that reads at all must therefore spend most of its size
sideways, which is why every candidate carries its own two reaches rather than one radius.
*/
struct AccentLightStyle
{
    /*! \brief Stable short name, printed by the sampling toggle. */
    std::string_view name;

    /*! \brief Reach beyond the silhouette along the neck, in world units. */
    double reach_x;

    /*! \brief Reach beyond the silhouette across the strings, in world units. */
    double reach_y;

    /*!
    \brief Falloff exponent applied to (1 - t) across the reach.

    One is linear and terminates in a visible edge; two and above leave the outer derivative at
    zero, which is what dissolves rather than ending — the difference between light and a painted
    gradient.
    */
    double falloff_exponent;

    /*! \brief Alpha at the silhouette's own edge, where the light is brightest. */
    double peak_alpha;

    /*!
    \brief How far the light's colour is mixed toward white, from zero (the string's own colour).

    The palette spans 4.08x in luma from red to yellow, so a string-tinted light inherits that
    spread exactly and an accent is four times quieter on red than on yellow. White mixing is the
    only lever that lifts the dim strings without spending geometry: half white measures a 1.51x
    spread. The cost is string identity, which is why both ends ship as candidates.
    */
    double white_mix;
};

/*!
\brief The accent candidates, in stable index order — index 0 is "no light at all".

Index 0 exists so the sampling can include the board without any accent light, which is the only
honest reference for judging whether a candidate reads as emphasis or as decoration.
*/
inline constexpr std::array<AccentLightStyle, 6> g_accent_light_styles{{
    {.name = "none",
     .reach_x = 0.0,
     .reach_y = 0.0,
     .falloff_exponent = 1.0,
     .peak_alpha = 0.0,
     .white_mix = 0.0},
    {.name = "slot flare",
     .reach_x = 0.238,
     .reach_y = 0.033,
     .falloff_exponent = 3.0,
     .peak_alpha = 0.72,
     .white_mix = 0.0},
    {.name = "slot flare white",
     .reach_x = 0.238,
     .reach_y = 0.033,
     .falloff_exponent = 3.0,
     .peak_alpha = 0.72,
     .white_mix = 0.5},
    {.name = "soft bloom",
     .reach_x = 0.234,
     .reach_y = 0.075,
     .falloff_exponent = 2.0,
     .peak_alpha = 0.55,
     .white_mix = 0.0},
    {.name = "white flare",
     .reach_x = 0.172,
     .reach_y = 0.049,
     .falloff_exponent = 2.0,
     .peak_alpha = 0.45,
     .white_mix = 0.5},
    {.name = "hard corona",
     .reach_x = 0.125,
     .reach_y = 0.036,
     .falloff_exponent = 1.0,
     .peak_alpha = 0.85,
     .white_mix = 0.0},
}};

/*!
\brief Bands the light is drawn as, nested from the outside in.

The bands are FILLED and stack additively rather than being drawn as rings, so each is one quad:
because the head is opaque and drawn over them, the stacked centre is never seen, and the visible
profile is exactly the staircase the band alphas describe. Five bands hold the cubic falloff to a
few counts.
*/
inline constexpr std::size_t g_accent_light_bands = 5;

/*!
\brief One candidate ghost treatment: the quiet end of the axis, taken out of the note's mass.

Every factor is a multiplier on something already drawn, so a ghost costs no new geometry. The
tail dims LESS than the head deliberately: a ghost is an attack dynamic rather than a sustain
one, and a bright tail under a dim head reads as a rendering fault rather than as quiet.
*/
struct GhostStyle
{
    /*! \brief Stable short name, printed by the sampling toggle. */
    std::string_view name;

    /*! \brief Alpha applied to the head's own art. */
    double head_alpha;

    /*! \brief Scale applied to the head quad; below one takes mass out instead of light. */
    double head_scale;

    /*! \brief Alpha applied to the technique markers riding the head. */
    double marker_alpha;

    /*! \brief Alpha applied to the sustain tail. */
    double tail_alpha;

    /*! \brief Thickness multiplier for an open string's bar, which has no head to thin. */
    double open_bar_thickness;

    /*!
    \brief Alpha of a hollow rim drawn over the dimmed fill; zero draws none.

    Keeps the silhouette at full strength while the fill quiets, so the note cannot degrade into a
    ragged core — the failure that reads as a bug rather than as dynamics.
    */
    double rim_alpha;

    /*! \brief True to draw the hollow outline INSTEAD of the filled head. */
    bool hollow_head;
};

/*! \brief The ghost candidates, in stable index order — index 0 draws a ghost as a normal note. */
inline constexpr std::array<GhostStyle, 6> g_ghost_styles{{
    {.name = "none",
     .head_alpha = 1.0,
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 1.0,
     .open_bar_thickness = 1.0,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.name = "rim keep",
     .head_alpha = 0.40,
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 0.70,
     .open_bar_thickness = 0.55,
     .rim_alpha = 1.0,
     .hollow_head = false},
    {.name = "half light",
     .head_alpha = 0.45,
     .head_scale = 1.0,
     .marker_alpha = 0.45,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.45,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.name = "small head",
     .head_alpha = 1.0,
     .head_scale = 0.78,
     .marker_alpha = 1.0,
     .tail_alpha = 1.0,
     .open_bar_thickness = 0.55,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.name = "hollow",
     .head_alpha = 1.0,
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.55,
     .rim_alpha = 0.0,
     .hollow_head = true},
    {.name = "quiet small",
     .head_alpha = 0.70,
     .head_scale = 0.86,
     .marker_alpha = 0.70,
     .tail_alpha = 0.70,
     .open_bar_thickness = 0.70,
     .rim_alpha = 0.0,
     .hollow_head = false},
}};

} // namespace rock_hero::common::ui
