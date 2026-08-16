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
\brief One candidate accent light: the note's own art redrawn as light around its silhouette.

A RIM, not a corona. A broad low-alpha field fails for a measured reason rather than a matter of
taste: the eye takes a glow's boundary at its steepest gradient, so a wide ramp puts that
boundary far outside the note and the light stops belonging to the note — it reads as a lit box
the note sits inside. The atlas ring it replaces holds its steepest gradient a fifth of a texel
from the note's own edge, which is exactly why it reads as a mark. A wide ramp also does not buy
its size back: measured, the broad version spent 1.6x more total light than the ring and read as
less.

So the light is the HEAD'S OWN CELL, redrawn additively on slightly larger quads. That buys the
exact silhouette, the exact rounded corners, and the authored antialiasing for two quads and no
measured extents in code — and it cannot drift when the art changes, which a hand-copied
silhouette would. Two stages: a hot core hugging the edge, and a faint ember reaching further.
*/
struct AccentLightStyle
{
    /*! \brief Stable short name, printed by the sampling toggle. */
    std::string_view name;

    /*! \brief How far the faint outer stage reaches past the art's edge, in texels. */
    double ember_reach_texels;

    /*! \brief Alpha of the outer stage. */
    double ember_alpha;

    /*! \brief Alpha of the hot stage hugging the art's edge. */
    double core_alpha;

    /*! \brief How far the hot stage reaches past the art's edge, in texels. */
    double core_reach_texels;

    /*!
    \brief How far the light's colour is mixed toward white, from zero (the string's own colour).

    Not decoration: the ring this must beat gets most of its brightness from a white lift, and an
    additive string-tinted light on the red string can add at most a fifth of what the ring adds.
    Zero keeps full string identity and costs the palette's 4.08x luma spread — the accent is
    four times quieter on red than on yellow. Half matches the ring. Higher flattens the spread
    toward one at the cost of the string's colour.
    */
    double white_mix;
};

/*!
\brief The accent candidates, in stable index order — index 0 is "no light at all".

Index 0 exists so the sampling can include the board without any accent light, which is the only
honest reference for judging whether a candidate reads as emphasis or as decoration.

The RIM WIDTH is settled at a texel and a half and is the same in every row: the wider rims were
sighted and lost. What is still open is how loud that rim should be, so each remaining row moves
exactly one thing about its intensity — brightness, colour, or a soft bloom outside it — and
nothing about its size. Comparing rows therefore answers one question at a time.
*/
inline constexpr std::array<AccentLightStyle, 5> g_accent_light_styles{{
    {.name = "none",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 0.0,
     .core_reach_texels = 0.0,
     .white_mix = 0.0},
    // The sighted rim, unchanged — the reference the other rows are judged against.
    {.name = "tight",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 0.95,
     .core_reach_texels = 1.5,
     .white_mix = 0.78},
    // Same rim, two thirds the light: is the accent still unmistakable when it stops shouting?
    {.name = "tight soft",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 0.60,
     .core_reach_texels = 1.5,
     .white_mix = 0.78},
    // The brightest this width can be: full alpha and a pure white edge. Costs the string's
    // identity in the light itself, which is the trade to judge here.
    {.name = "tight hot",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 1.00,
     .core_reach_texels = 1.5,
     .white_mix = 1.00},
    // The same rim with a faint falloff outside it. The bloom stays inside half the head's own
    // half-height, so the light's steepest gradient is still the rim's edge — which is what keeps
    // it reading as the note's glow rather than as a field the note sits in.
    {.name = "tight bloom",
     .ember_reach_texels = 4.0,
     .ember_alpha = 0.12,
     .core_alpha = 0.95,
     .core_reach_texels = 1.5,
     .white_mix = 0.78},
}};

/*!
\brief One candidate ghost treatment: the quiet end of the axis, taken out of the note's mass.

Every factor is a multiplier on something already drawn, so a ghost costs no new geometry. The
tail dims LESS than the head deliberately: a ghost is an attack dynamic rather than a sustain
one, and a bright tail under a dim head reads as a rendering fault rather than as quiet.
*/
struct GhostStyle
{
    /*!
    \brief Alpha applied to the head's own art.

    Below one this makes the head TRANSLUCENT, which reveals whatever sits behind it. Its own
    sustain ribbon is only the loudest case — a chord box's fill, a lane border, and an active
    fret line all show through too, and only the ribbon can be fixed by clipping. \ref fill_dim
    is the same weight with none of that.
    */
    double head_alpha;

    /*!
    \brief How far the head's fill is darkened toward black, from zero (its own colour).

    The OPAQUE way to read quiet: the note keeps alpha one, so nothing behind it can show
    through, and only its brightness drops. Hue survives, so the string still identifies itself —
    which is what separates this from leaning toward the board, measured as the WORST separation
    of the set on the red string. The head cell's white-lift bevel is added after the tint, so a
    darkened ghost keeps its outline for free however deep the fill goes.
    */
    double fill_dim;

    /*! \brief Stable short name, printed by the sampling toggle. */
    std::string_view name;

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

    Keeps the silhouette at full strength while the fill quiets. Largely redundant against
    \ref fill_dim, which keeps the outline for free, and carried only for comparison.
    */
    double rim_alpha;

    /*! \brief True to draw the hollow outline INSTEAD of the filled head. */
    bool hollow_head;
};

/*! \brief The ghost candidates, in stable index order — index 0 draws a ghost as a normal note. */
inline constexpr std::array<GhostStyle, 6> g_ghost_styles{{
    {.head_alpha = 1.0,
     .fill_dim = 0.0,
     .name = "none",
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 1.0,
     .open_bar_thickness = 1.0,
     .rim_alpha = 0.0,
     .hollow_head = false},
    // Half light's weight to within about two luma counts, measured across all six strings, and
    // a third more distinct from the board on the red string — darkening keeps the hue where
    // thinning dilutes it toward the board's blue.
    {.head_alpha = 1.0,
     .fill_dim = 0.58,
     .name = "dim fill",
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.55,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.head_alpha = 1.0,
     .fill_dim = 0.70,
     .name = "dim fill deep",
     .head_scale = 1.0,
     .marker_alpha = 0.80,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.55,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.head_alpha = 0.45,
     .fill_dim = 0.0,
     .name = "half light",
     .head_scale = 1.0,
     .marker_alpha = 0.45,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.45,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.head_alpha = 1.0,
     .fill_dim = 0.58,
     .name = "dim small",
     .head_scale = 0.86,
     .marker_alpha = 1.0,
     .tail_alpha = 0.70,
     .open_bar_thickness = 0.70,
     .rim_alpha = 0.0,
     .hollow_head = false},
    {.head_alpha = 1.0,
     .fill_dim = 0.0,
     .name = "hollow",
     .head_scale = 1.0,
     .marker_alpha = 1.0,
     .tail_alpha = 0.65,
     .open_bar_thickness = 0.55,
     .rim_alpha = 0.0,
     .hollow_head = true},
}};

/*!
\brief Texels the box's frame light reaches outward and inward from the frame bar.

A box is read THROUGH, so its light hugs the bar from both sides and stops well before the
interior: lighting the bar itself is what makes the frame emit, where an outside-only halo never
does. The innermost band still leaves the see-through region untouched.
*/
struct BoxLightBand
{
    /*! \brief Texels outward from the frame's outer boundary. */
    double out_texels;

    /*! \brief Texels inward from the frame's outer boundary. */
    double in_texels;

    /*! \brief Alpha this band adds on top of the bands outside it. */
    double step_alpha;
};

/*!
\brief The box frame light, outside in — the last band lands on the bar itself.

The three bands are a halo outside the boundary, a band straddling it, and the bar itself; their
alphas accumulate to about half the box colour added on the bar. A box takes its own teal rather
than a string colour, so its light cannot be matched to a note's rim by alpha anyway — the two are
tuned to read as one volume of "loud" by eye, in the same sighting pass that picks the note rim.

The inward reaches are stated in the same texels the note rim uses, and the last band's five texels
are exactly the frame's bar thickness — the panel takes `HighwayMetrics::string_grid_base_y` for
that, 0.075 world, which is five of these texels on the nose. Nothing reaches past the bar, so the
see-through interior a player reads notes through is untouched.
*/
inline constexpr std::array<BoxLightBand, 3> g_box_light_bands{{
    {.out_texels = 3.0, .in_texels = 0.0, .step_alpha = 0.07},
    {.out_texels = 1.5, .in_texels = 2.0, .step_alpha = 0.12},
    // The bar itself, inward by its own thickness: this is the band that makes the frame emit
    // rather than merely wear a halo.
    {.out_texels = 0.0, .in_texels = 5.0, .step_alpha = 0.30},
}};

} // namespace rock_hero::common::ui
