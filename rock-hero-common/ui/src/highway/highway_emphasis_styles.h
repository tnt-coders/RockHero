/*!
\file highway_emphasis_styles.h
\brief Appearance numbers for the note-emphasis axis: a signed ghost, and the accent light's
       candidate table while that one look is still being chosen.

EXPERIMENT SCAFFOLDING, but only the accent table. Accents became a rendered light and ghosts a
transparency treatment; the ghost end is signed and its numbers are inline below, while the
accent's exact look is still being sighted in the app rather than argued — the editor cycles that
table with a keybind and logs which candidate is active. When the user picks, the winner's numbers
move inline beside the rest and the table and its cycling command are deleted; the file itself
stays, because the signed constants live here too.

The table is DATA rather than branches on purpose — every candidate differs only in numbers, so a
new one costs a row and no code, and the renderer holds one code path whichever is selected. Every
lit subject — fretted head, open string, chord box — reads the SAME row, so the toggle moves the
whole board's accents together and a candidate is judged as one look.
*/

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace rock_hero::common::ui
{

/*!
\brief How an accent glow combines with what is already on the board.

An axis of the sighting rather than a settled choice, because the three operators genuinely
differ on THIS board and the difference is not predictable from the maths alone. The backbuffer
is 8-bit LDR with no tonemap, so `Add` is the only one that can drive a lit pixel past its
current value and the only one that can clip; `Screen` approaches white asymptotically and never
clips, at the cost of losing contrast where the destination is already bright; `Lighten` cannot
darken or blow out but also cannot accumulate, so two overlapping lights read as one.

Every operator here consumes PREMULTIPLIED source, which the glow shader emits, so switching rows
compares the operators and nothing else.
*/
enum class AccentBlend : std::uint8_t
{
    /*! \brief `ONE, ONE` — light adds. Accumulates across overlaps; can clip to white. */
    Add,

    /*! \brief `ONE, INV_SRC_COLOR` — filmic; approaches white without ever clipping. */
    Screen,

    /*! \brief `MAX` — takes the brighter of the two. Never blows out, never accumulates. */
    Lighten
};

/*!
\brief One candidate accent light: a per-fragment glow around the lit object's own silhouette.

The light is a SIGNED DISTANCE FIELD evaluated in the fragment shader, not geometry. The design
before it stacked flat-alpha quads — the head's own atlas cell redrawn on slightly larger quads,
and for a chord box four independent linear ramps — and it failed for three measured reasons, all
of which this shape removes rather than tunes:

1. Redrawing the ART as light sampled the cell INWARD while drawing OUTWARD, so past about four
   texels of reach the band landed on the art's bevel (`R=255, G=107` against an interior of
   `R≈170, G≈33`) and the light got BRIGHTER toward its outer edge — the inverse of a falloff.
2. `fs_texture_tint` adds the atlas's G channel unconditionally, and G is the achromatic white
   lift. So that bevel contributed pure white no `white_mix` could suppress, which is why the
   light still read as white after its colour was ruled to be the string's.
3. Flat stages can only STEP, and the eye reads a step as an edge. On a chord box the four ramps
   had no radial term, so out past a corner the top cap carried its full alpha where the sides had
   already decayed to zero — a hard cut across zero width, exactly the one the user reported.

A distance field has none of these failure modes available to it: it never samples art, so it
cannot pick up a bevel or a white lift, and it is one continuous function of position, so its
falloff is identical in every direction and has no seams to disagree at.
*/
struct AccentLightStyle
{
    /*! \brief Stable short name, printed by the sampling toggle. */
    std::string_view name;

    /*!
    \brief How far the light reaches from the silhouette's edge, in WORLD units.

    One number for every subject — a fretted head, an open string's bar, a chord box frame —
    because reach is a property of the EMITTER'S BRIGHTNESS, not of its size: a short neon tube
    and a long one wear the same halo. That asymmetry is the point rather than a defect. Around a
    head 0.325 world tall it is a rim; around a frame bar 0.075 world thick it is several times
    the bar's own width, which is what makes a hairline read as GLOWING instead of merely brighter.

    Measured from the EMITTER, not from the silhouette's edge — see the shader. A solid subject
    emits across its whole interior, so this is purely the outward halo of a light sitting behind
    it; a chord box emits only across its frame band, so the same number spills both outward and
    inward from that band. One number, two shapes, because "how far the light carries" is a
    property of the light rather than of what it is behind.

    The ceiling that matters is the lane pitch, 0.35 world. A head's art reaches 0.16245 world
    from its centre, so a reach past about 0.18 puts one string's glow onto the next string's line
    and two adjacent accents merge into a single field.
    */
    double reach;

    /*! \brief Peak alpha, reached exactly ON the silhouette's edge. */
    double alpha;

    /*!
    \brief Falloff shape: the exponent the 0..1 ramp is raised to.

    At 1.0 the ramp is linear, which reads as a gradient rather than as light — a linear ramp
    spends as much of its width at half brightness as a real falloff spends at a tenth. Above 1.0
    the core tightens and the toe lengthens, which is how light actually distributes its energy
    and is the knob that answers "doesn't fade naturally".
    */
    double exponent;

    /*!
    \brief Radiance multiplier, allowed above one and then clipped per channel by the shader.

    This is where BRIGHTNESS comes from, and it replaced a mix-toward-white that could not supply
    any. Mixing a colour toward white at a fixed weight trades saturation for lightness and stops
    there — it can never exceed the emitter's own brightness, and it desaturates the far halo just
    as hard as the core, which is precisely how the light managed to read as washed out and dim at
    the same time.

    A gain does both jobs at once, because the CLIPPING is the effect rather than a defect. Past
    one the brightest channel saturates and stops while the others keep climbing, so the colour
    walks toward white exactly where the light is strongest and keeps its hue everywhere it is
    not. That is the white-hot core inside a coloured halo that every bright light actually has.

    It also closes the palette's 4.08x luma spread on its own: the red string, being dark, has the
    most headroom before its remaining channels clip, so the same gain lifts it furthest. No
    per-string compensation is needed and none is present.

    Gain 1.0 is the un-gained light, kept as a control row. The pedestal that lets a pure hue reach
    white at all is a separate constant applied where the colour is packed — see the renderer's
    `emitterSpectrum`.
    */
    double gain;

    /*! \brief Which operator combines this light with the board. */
    AccentBlend blend;
};

/*!
\brief The accent candidates, in stable index order — index 0 is "no light at all".

Index 0 exists so the sampling can include the board without any accent light, which is the only
honest reference for judging whether a candidate reads as emphasis or as decoration.

The rows are a designed sweep rather than a pile, each varying ONE thing against the `medium`
reference at row 3: rows 2-4 vary SIZE, row 5 varies the falloff SHAPE, rows 6-7 vary the BLEND
OPERATOR, and rows 1 and 8 bracket the GAIN — row 1 at gain 1.0 is the un-gained light the earlier
rounds shipped, kept as the control that shows what the gain is actually buying. Every row lights
notes and chord boxes together from these same numbers, so a candidate is judged as one look
across the whole board rather than as a note treatment with a box treatment beside it.
*/
inline constexpr std::array<AccentLightStyle, 9> g_accent_light_styles{{
    {.name = "none",
     .reach = 0.0,
     .alpha = 0.0,
     .exponent = 1.0,
     .gain = 0.0,
     .blend = AccentBlend::Add},
    // The control: the reference size and falloff with NO gain, which is exactly what shipped
    // before brightness became a knob. Every other row should beat this one or the gain is not
    // earning its place.
    {.name = "medium flat",
     .reach = 0.12,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 1.0,
     .blend = AccentBlend::Add},
    // Four texels of reach: a rim that stops well inside the lane, the tightest thing that still
    // reads as a light rather than as a thicker outline.
    {.name = "tight",
     .reach = 0.06,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 3.0,
     .blend = AccentBlend::Add},
    // Eight texels — about half the head art's own half-height. THE REFERENCE ROW: every row
    // above and below varies exactly one field against this one.
    {.name = "medium",
     .reach = 0.12,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 3.0,
     .blend = AccentBlend::Add},
    // Twelve texels, which lands a head's glow within a hair of the neighbouring string's line.
    // The widest a per-string light can be before two adjacent accents stop being two.
    {.name = "wide",
     .reach = 0.18,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 3.0,
     .blend = AccentBlend::Add},
    // The same width with a LINEAR falloff, to sight the exponent's contribution on its own: this
    // is what the previous design's ramps were, at a size that makes the difference visible.
    {.name = "wide linear",
     .reach = 0.18,
     .alpha = 0.85,
     .exponent = 1.0,
     .gain = 3.0,
     .blend = AccentBlend::Add},
    {.name = "medium screen",
     .reach = 0.12,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 3.0,
     .blend = AccentBlend::Screen},
    {.name = "medium lighten",
     .reach = 0.12,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 3.0,
     .blend = AccentBlend::Lighten},
    // Hot enough that even the palette's darkest string clips its remaining channels near the
    // core, so the white-hot centre is unmistakable. The upper bracket of the gain sweep.
    {.name = "medium hot",
     .reach = 0.12,
     .alpha = 1.00,
     .exponent = 2.0,
     .gain = 7.0,
     .blend = AccentBlend::Add},
}};

/*!
\brief The ghost look, SIGNED 2026-08-15 after sighting six candidates: `half light`.

A ghost is quieted by ALPHA on this surface, which composites over a dark 3D world - the opposite
choice from the 2D lane, which is opaque and leans its ink toward the lane's own ground instead.
Both surfaces spend the same weight; each spends it the way it actually composites.

The rejected candidates are recoverable from git history: `dim fill` and `dim fill deep` (opaque
darkening), `dim small` (thinning the head), and `hollow` (an outline instead of a fill).
*/

/*!
\brief Alpha a ghost keeps, everywhere: head art, technique markers, and sustain tail alike.

ONE number on purpose. The sighted look split it — 0.45 on the head and markers against 0.65 on
the tail — on the reasoning that a ghost is an attack dynamic rather than a sustain one, so a
ribbon dimmed as hard as its head would read as a rendering fault. Collapsing both to a half is
being tried against exactly that: if the note still reads as one quiet gesture, the split was a
distinction the eye never made, and the axis is simpler by a whole variable.
*/
inline constexpr double g_ghost_alpha{0.5};

/*!
\brief Thickness multiplier for a ghosted open string's bar, which has no head to thin.

Stays its own number even while the alphas collapse: it is a THICKNESS, not a light level. An
open string carries the axis on its bar because it has no head to wear it - the seam where the
old atlas-mark design diverged, since a mark drawn on a head could never be worn by a bar.
*/
inline constexpr double g_ghost_open_bar_thickness{0.5};

} // namespace rock_hero::common::ui
