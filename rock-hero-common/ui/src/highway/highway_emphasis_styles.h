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

    Ruled 2026-08-15: the light is the STRING'S colour, so this sits at or near zero. A white glow
    was tried first, on the reasoning that the atlas ring it replaced got most of its brightness
    from a white lift — but a white light says the same thing on every string, and the whole point
    of a light on a coloured note is that it belongs to that note.

    The cost is real and is paid elsewhere. Full string identity carries the palette's 4.08x luma
    spread, so the same alpha reads four times quieter on the red string than on the yellow. The
    knob that closes that WITHOUT touching the colour is \ref core_reach_texels and
    \ref ember_reach_texels — more area, not more white — which is why the candidates below run
    from tight to wide rather than from tinted to white. One row keeps a quarter of a white lift
    as the compromise, for comparison against the pure ones.
    */
    double white_mix;
};

/*!
\brief The accent candidates, in stable index order — index 0 is "no light at all".

Index 0 exists so the sampling can include the board without any accent light, which is the only
honest reference for judging whether a candidate reads as emphasis or as decoration.

Every row is the STRING'S OWN COLOUR now (see \ref AccentLightStyle::white_mix), so what the rows
vary is SIZE: the tight rim that was sighted first, then progressively more reach. Width is the
right axis to open once the colour is fixed, because width is what buys back the brightness the
white lift used to supply — and it is the only knob that can, since the hot core is already at
full alpha and a string colour cannot be made brighter without becoming white again.

The wide rows are deliberately allowed past the boundary the first round enforced. That round's
rule — keep the light's steepest gradient at the note's own edge, or it reads as a lit box the
note sits inside — was measured against a WHITE field, which competes with the note. A light in
the note's own colour does not compete with it the same way, so how far it may reach before it
stops belonging to the note is genuinely re-opened rather than settled.
*/
inline constexpr std::array<AccentLightStyle, 5> g_accent_light_styles{{
    {.name = "none",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 0.0,
     .core_reach_texels = 0.0,
     .white_mix = 0.0},
    // The width sighted in the white round, now in the string's colour: the control that isolates
    // the colour change from every other variable.
    {.name = "string tight",
     .ember_reach_texels = 0.0,
     .ember_alpha = 0.0,
     .core_alpha = 1.00,
     .core_reach_texels = 1.5,
     .white_mix = 0.0},
    // A thicker core with a real ember behind it — about a head's own half-height of reach.
    {.name = "string wide",
     .ember_reach_texels = 6.0,
     .ember_alpha = 0.25,
     .core_alpha = 1.00,
     .core_reach_texels = 2.5,
     .white_mix = 0.0},
    // The widest that still belongs to one string. Ten texels is 0.15 world past the art, so a
    // head's glow ends 0.89 of the 0.35 lane pitch from its own centre — just short of the
    // neighbouring string's line. Wider than this and two adjacent accents merge into one field.
    {.name = "string bloom",
     .ember_reach_texels = 10.0,
     .ember_alpha = 0.35,
     .core_alpha = 1.00,
     .core_reach_texels = 2.0,
     .white_mix = 0.0},
    // Wide, with a quarter of a white lift kept: closes most of the red-to-yellow spread while
    // the string is still plainly the source of the light. The compromise row.
    {.name = "string lifted",
     .ember_reach_texels = 6.0,
     .ember_alpha = 0.28,
     .core_alpha = 1.00,
     .core_reach_texels = 2.0,
     .white_mix = 0.25},
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
\brief An accented chord box's light: its own frame redrawn additively, twice.

A box spans several strings, so unlike a note it has no string colour to take — its light is the
box's own teal, which is also why it cannot be matched to a note's rim by alpha and is instead
tuned by eye in the same sighting pass.

Both stages redraw the PANEL rather than laying an outline beside it, so the light follows every
variant of the shape (half-height repeat boxes, the top bar that only appears on three-note
chords, the columns that fade out at the midpoint when it does not, the corner holders) and cannot
drift from it. The interior fill is suppressed in both: a box is read THROUGH, and light in the
middle is opacity where the notes behind it have to stay legible.

**Why the HALO carries this, where a note's rim is carried by its core.** Adding a colour on top
of itself is the least perceptible change available: the first version of this light put nearly
all of its alpha on the frame bar, which is already painted this exact teal, so it only made teal
slightly more teal and the user reported seeing no effect at all — twice. The stage that actually
reads is the one OUTSIDE the frame, where the same teal lands on the near-black board and the
contrast is enormous. So the halo reaches further and carries more weight than its counterpart on
a note, and the on-bar stage is there to keep the frame from looking like it is merely wearing a
ring that does not belong to it.

The alphas are the panel's own `alpha_scale`, so they land on the frame at about half these
numbers (its bars draw at 128/255 of the scale) and on the corner holders at the full value.

These are NOT part of the F9 accent cycle. A box has no string colour, so it shares none of the
candidates' variables, and cycling the note styles deliberately leaves boxes untouched.
*/

/*!
\brief How far the spill reaches outward from the box, in world units (twenty texels).

Sized from the measurement, not by eye. The frame bar is 0.075 world, which projects to 0.7 px at
the far end of the visible window and 2.3 px a third of a second out (and half that again in the
editor's preview pane), so every on-frame stage is confined to a hairline. This reach reads about
nine pixels wide a third of a second out and stays visible to the horizon, which is what gives the
accent any screen area at all.
*/
inline constexpr double g_box_light_halo_reach{0.30};

/*!
\brief Alpha of the spill where it meets the frame, falling to nothing across the reach above.
*/
inline constexpr double g_box_light_halo_alpha{0.55};

/*!
\brief How far the spill's colour is lifted toward white, from zero (the box's own teal).

The frame is already painted this exact teal, so a pure-teal light adds a hue the eye has no
reference for — measured as the least perceptible change available, and the reason two rounds of
this light were reported as no effect at all. The lift is what makes it read as light falling on
the box rather than as more of the box's own paint. A note's rim needs no equivalent because it
now takes its STRING'S colour, which is not the colour of anything else around it.
*/
inline constexpr double g_box_light_white_mix{0.55};

/*!
\brief Alpha of the hot stage, laid exactly on the frame.

Near one on purpose: additively at this scale the bars land at roughly double their unlit value.
Against a same-coloured frame even that is a modest read, which is exactly why it is not asked to
carry the effect alone.
*/
inline constexpr double g_box_light_core_alpha{0.85};

} // namespace rock_hero::common::ui
