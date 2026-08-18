# Highway Note Art — the live state of the 3D visual pass

Status: **ACTIVE.** The recovery point for the highway's note-art work: harmonic heads, vibrato
motion, and the 2D tail geometry that moves with them. Opened 2026-08-15 at the user's direction
— *"make sure our task list is continuing to be written somewhere that we can recover it and
continue EXACTLY where we left off if we get disconnected"* — on the same convention
`technique-review-walkthrough.md` already uses for the technique decision queue.

**Two live queues, no overlap.** The technique-decision items (W3–W13) live in that walkthrough's
own LIVE work queue and are NOT restated here; this file carries the visual pass, which runs on a
separate track. When resuming, read both.

## In flight right now

**The sizing pass is SETTLED as of 2026-08-18** and this file is becoming a record rather than a
work queue. Every size question that opened it is signed: the mark-sizing law, the head family's
centring and interior concentricity, the accent light, the pick slide's head and its restored
size. The sighting rig is gone with them — no appearance sampler, no atlas swapper, no candidate
table anywhere in the tree — so the committed `notes.png` (sha256 `337927b3…`) is the single
authority for every drawn size and a rebuild is the only thing that writes the deployed copies.
What remains open is listed under *Open decisions* and *Watching* below, and neither is a size.

**RULED 2026-08-18: the pick slide KEEPS its rectangular head.** The bare-scrape experiment
(the plectrum alone, no base, no anticipation ring, accent on the tail only — built at
`15576f52` from the user's *"the rectangle feels like it doesn't really fit"*) was sighted the
same day and rejected: *"Add the note head back to the pick slide."* The revert restores all
three coupled surfaces — the tech base under the plectrum, the anticipation ring, the head's
accent light — and `highwayTechHead`'s scrape clause with them. A scrape wears the pick mark on
the darker tech base; that is now a sighted decision, not a default.

**The ACCENT LIGHT is SIGNED 2026-08-18: `medium flat`** — reach 0.12 world, alpha 1.0, exponent
2.0, gain 1.0, additive blend. The user: *"'accent light: medium flat' looks best"*, signed with
the reservation *"it is a bit subtle but looks good"* — recorded as a watch item
(`docs/tracking/watch-items.md`) whose trigger is accents not standing out enough in practice
and whose knob is `g_accent_gain`. The winning numbers live inline in `highway_renderer.cpp`
(`g_accent_reach` / `g_accent_exponent` / `g_accent_gain`); the candidate table, its
`AccentBlend` enum, the F9 sampler, the F4/F10 size samplers and the F6 atlas-variant cycler
are all deleted with their plumbing (`highway_board_scales.h` gone, `highway_emphasis_styles.h`
reduced to the ghost constants); the variant swapper was trimmed to deploy/restore/list and
then deleted outright when the last size signed. The blend axis closed with the same signing — additive, exactly as the recommendation
below argued.

The nine rows tried, each varying one field against `medium` (all premultiplied source):

| Row | Reach (world) | Alpha | Exponent | Gain | Blend | Role |
|---|---|---|---|---|---|---|
| `none` | 0 | 0 | — | 0 | Add | the no-light reference |
| **`medium flat`** | 0.12 | 1.0 | 2.0 | 1.0 | Add | the un-gained control — **SIGNED** |
| `tight` | 0.06 | 1.0 | 2.0 | 3.0 | Add | four texels; the tightest that reads as light |
| `medium` | 0.12 | 1.0 | 2.0 | 3.0 | Add | the reference row the others varied against |
| `wide` | 0.18 | 1.0 | 2.0 | 3.0 | Add | widest reach that still belongs to one string |
| `wide linear` | 0.18 | 0.85 | 1.0 | 3.0 | Add | the old design's linear ramp, isolated |
| `medium screen` | 0.12 | 1.0 | 2.0 | 3.0 | Screen | near-identical to `medium` on a dark board |
| `medium lighten` | 0.12 | 1.0 | 2.0 | 3.0 | Lighten | overlapping glows stop accumulating |
| `medium hot` | 0.12 | 1.0 | 2.0 | 7.0 | Add | upper gain bracket; even red clips at the core |

What the eye rejected is the gain itself: the sweep was built to show what radiance gain buys,
and the signed row is the gain-1.0 control. The clipping model stays in the shader — raising
`g_accent_gain` needs no code change and is the recorded remedy if the subtlety reservation
fires — but at 1.0 the light holds its string's hue end to end. The narrative below records how
the shader got its shape; those measurements still govern it.

**Opened 2026-08-15**, after the user reported that the light *"doesn't fade
naturally like real light and looks boxy"*, that on the chord box *"the top bar light looks
completely disconnected from the side bar glows — they clash with a hard cut"*, and that
*"something is really off in how these light shaders are being used"*. Three causes are now
MEASURED, not suspected:

1. **The head's rim samples the art INWARD as it draws OUTWARD, so it brightens toward its outer
   edge — the inverse of a falloff.** The rim redraws the head's own cell on a larger quad, so a
   point further out in the drawn quad maps to a point further IN in the art. Measured on cell 0:
   the visible ring covers art rows `10.83/grow .. 10.83`, and at a reach of 4 texels or more that
   band reaches the art's BEVEL, where `R=255, G=107` against an interior of `R≈170, G≈33`. The
   wider the candidate, the more bevel it lands on.
2. **`fs_texture_tint` adds `texel.g` unconditionally** — `rgb = texel.r * tint + texel.g` — and G
   is the atlas's achromatic white-lift channel. So the bevel above contributes 107/255 of PURE
   WHITE that no `white_mix` setting can suppress: the mix only scales the tint that multiplies R.
   **This is why the light still read as white after the colour was ruled to be the string's.**
3. **Every stage is flat-alpha or per-side linear.** The head and open-bar rims are two flat
   stages (a staircase). The box spill is four independent linear ramps with no radial term: the
   top cap carries full alpha along its entire bottom edge, including out past the corners where
   the side ramps have already decayed to zero — a step of up to the halo's full 0.55 alpha across
   ZERO width. That is the "hard cut", exactly located.

**BUILT 2026-08-16 and awaiting sighting.** One per-fragment falloff program, `accent_glow`,
evaluating a rounded-box / rhombus signed distance (a capsule is the rounded box at corner radius
== half thickness) — the intersection of two precedents this renderer already shipped, the
box-mute SDF program and the window light's soft edges, rather than a third mechanism. What it
removed, rather than tuned:

- `BoxPanelParts` and the `parts` parameter, the second frame-only panel redraw, and all six spill
  quads on the box side
- both rim lambdas on the note side, and with them the open bar's `/2` prism compensation — a flat
  quad crosses once where a closed unculled prism crossed twice, so the one-weight promise between
  a head and an open string now holds by construction instead of by a hand-applied correction
- the two-batch split for note accents: a head's light was head ART and a bar's was bar GEOMETRY,
  so each had to slot in just above its own subject; one quad and one program for both collapses
  that to a single batch under the notes
- four fields of the style table (two reaches, two alphas), replaced by one reach and an exponent
- all four `g_box_light_*` constants; a box reads the shared candidate and carries no number of
  its own at all, once the gain below replaced the last box-only white lift

The SHAPE parameters ride the vertex rather than a uniform, which is what lets three unrelated
silhouettes at three different sizes still batch into one draw.

**Blend mode became a sighting axis** (user, 2026-08-15): additive clips per channel in this 8-bit
buffer with no HDR, and clipping desaturates toward white — an independent second cause of "too
white". `Screen` (`FUNC(ONE, INV_SRC_COLOR)`) is bounded by the source colour, so it converges on
the STRING'S colour instead of white and lets overlapping glows merge without blowing out;
`Lighten` (max) never blows out at all but also never accumulates. All three are rows in the table,
sharing a base with the additive `medium` row so the comparison isolates the operator. Every
operator consumes PREMULTIPLIED source, which the shader emits, for the same reason.

**Sighted 2026-08-16: `medium` and `medium screen` look almost identical, and that is structural
rather than a coincidence.** Additive is `dst + src`; screen is `dst + src − src·dst`. They differ
by exactly `src·dst`, and the glow lands almost entirely on a near-black board where `dst ≈ 0`, so
the difference is ≈ 0 everywhere it can be seen. They can only diverge where the destination is
already bright: overlapping glows from adjacent accents, the lit fret window, a chord box frame.

**Recommendation: sign ADDITIVE and delete the axis.** Not a coin flip, on three grounds. (1) It is
the physically correct operator — light adds, and `screen` is a compositing convention borrowed
from film double-exposure that exists as the LDR stand-in for "add, then tonemap"; with an HDR
buffer you would always add. (2) It is already the operator every other light on this board uses
(`g_additive_state`: the strike glow and the window light), so keeping `screen` would make the
board carry two light conventions for no gain. (3) `screen`'s only real advantage — it cannot clip
— is unreachable where `dst ≈ 0`, and where clipping does occur the honest fixes are lower alpha or
a tonemap; `screen` would mask the symptom instead. Signing it deletes the `AccentBlend` enum, two
blend states, `accentGlowState()`, one struct field and two table rows. **Held open only until
`lighten` is sighted**, since that one genuinely differs — it takes a maximum, so overlapping glows
stop accumulating altogether. Sighted, and it lost; the axis closed with the 2026-08-18 signing.

**Reach is now ONE absolute world number for every subject**, which is a design ruling rather than
a convenience: reach is a property of the emitter's BRIGHTNESS, not its size — a short neon tube
and a long one wear the same halo. The asymmetry that falls out is the point. Around a head 0.325
world tall it is a rim; around a frame bar 0.075 world thick it is several times the bar's own
width, which is exactly what makes a hairline read as glowing instead of merely brighter.

**Corrected 2026-08-16 after sighting: the light is a BACK LIGHT, not a rim.** The user reported
that an accented open string looked like *"drawing a border around the open string with light
rather than a light emanating from behind the open string itself"*, and that heads were probably
doing the same but got away with it because no part of a head is transparent. Both readings were
right, and the cause was in the field: it peaked ON the boundary and fell off both ways from it,
which is a rim by construction. A head hides its own inner half and so still read as light; an
open bar is 0.1 world thick with ends that fade to transparent, so its inner half leaked out and
showed as two bright lines tracing the outline.

The field now measures distance from the EMITTER REGION rather than from the boundary, with one
new parameter — the emitter's DEPTH — carrying the whole difference:

- **solid** (note head, open bar): depth past the shape's own inradius, so the entire interior
  emits and the only falloff is outward. That is literally a lamp behind the object.
- **frame** (chord box): depth equal to the frame thickness, so only the band from the outer edge
  one thickness in emits, spilling both ways. The interior stays dark, which is what keeps a box
  readable THROUGH — the property the user said already looked right.

One field, two values; a frame is the general case and a solid is the case where the depth exceeds
the shape. The depth is a UNIFORM rather than a vertex attribute because notes and boxes are
already separate batches with separate submits, so it costs nothing per vertex. Deleting the
inward half of the old ramp also made the shader shorter.

**The chord box reads the same accent light the notes read**, which it did not before — the user
reported that (via the then-live sampler) as the toggle being broken, and it was. After the gain
ruling below it carries NO number of its own: the hand-tuned white lift it once needed (teal
light on a teal frame being the least perceptible change available) came from the gain's own
clipping — moot at the signed gain of 1.0, where the box simply wears the shared light
unmodified.

**The silhouette constants were re-measured against `notes.png` on 2026-08-16, and four of five
were wrong.** All the errors were sub-pixel individually (worst 0.32 px at the near end), but three
were definition errors rather than tuning, so they are corrected rather than left:

| Constant | Was | Measured | Why it was wrong |
|---|---|---|---|
| world per drawn texel | 0.015 | **0.48/31.5 = 0.0152381** | `HighwayAtlasLayout::cellRect` insets each cell's UV rect by half a texel per side, so the head quad's corners sample texel CENTRES 0.5 and 63.5 — **63** texels of range, not 64. Every constant built on it was 1.5625% short. |
| corner radius | 3.7 tx | **1.8205 tx** | Fitted to the antialias TAIL where the extents were fitted to the 50% contour — the block silently mixed two definitions of "the edge". The radius is a strong function of that threshold (3.9 tx at 15% coverage, 1.82 at 50%). |
| node half span | 15.31 tx | **15.65 tx** | Derived from the signed construction (head-height square rotated 45°) instead of measured. The art's edge line `\|x\|+\|y\| = 15.65` holds to 0.0000 tx across all 88 edge samples. |
| art centre offset | not modelled | **(+0.5, −0.5224) tx** | The silhouettes are ODD sized (41×21 solid texels) in an even 64-texel cell, so they cannot be quad-centred by construction. Unmodelled, the ridge landed up to **1.04 tx** off the art's edge and lopsided — bright on bare texture along two edges, buried under the head along the other two. Modelled, every edge is within 0.022 tx, which also absorbs the art's own top/bottom rim asymmetry (10.7994 against 10.8442). |
| half width / half height | 20.8 / 10.83 tx | 20.7994 / 10.8218 tx | Correct as TEXEL measurements; wrong only through the conversion above. |

Constants are now stated in TEXELS and converted through `headArtTexelWorld(metrics)`, so the
conversion can be wrong in one place instead of four, and the head's world size is read from the
metrics rather than a literal `0.48`. The 50% threshold is named in the block, since the numbers
are meaningless without it. `notes.png` was not touched (sha256 still `cd8c5c4d68b922a2…`).

Two further facts worth keeping. The rectangular corners are **neither circular nor chamfered** —
the art is a solid 41×21 rectangle wrapped in a one-texel fringe with the corner texel omitted, so
there is no authored radius at all; 1.8205 is a description of the 50% contour (worst deviation
0.026 tx), not a construction. And the rhombus branch is **exact, not approximate, for this cell**:
with `b.x == b.y` the shader's formula reduces to the true Euclidean distance to a 45° square, and
cell 4's two axes are equal to 0.000000 tx.

- **Accent colour, RULED 2026-08-15: the light is the STRING'S colour.** The first round was 78%
  to 100% white on every candidate, which the user caught (*"Is the accent glow only using WHITE
  light? … It needs to use the string color for note heads and open strings"*). White was chosen
  because the atlas ring it replaced got most of its brightness from a white lift; the ruling is
  that a light which says the same thing on every string does not belong to the note it marks.

  The candidates run tight → wide: `tight` (0.06 world, four texels), `medium` (0.12, the
  reference row every other row varies one field against), `wide` (0.18 — the widest that still
  belongs to one string, since a head's art reaches 0.16245 from its centre and the lane pitch is
  0.35), plus `wide linear`, `medium screen`, `medium lighten`, and the gain bracket below.
  **Open question: how far may a string-coloured glow reach before it stops belonging to the
  note?** The first round's answer was measured against a WHITE field, which competes with the
  note in a way its own colour does not, so it was genuinely re-opened. Closed by the 2026-08-18
  signing: the `medium` reach, 0.12 world, held in the string's own colour.

- **Brightness, RULED 2026-08-16: a RADIANCE GAIN, not a blend toward white.** The user asked
  whether the light needed "a bit of white light blended in" to read as bright. It does not, and
  the mix-toward-white it replaces was wrong in both directions at once: it desaturated the far
  halo as hard as the core (so the light read washed out) while adding no radiance whatever, since
  a fixed lerp can only trade saturation for lightness and never exceed the emitter's own
  brightness. Washed out and dim, from one wrong model.

  What a bright coloured light actually does is clip. The brightest channel saturates and stops
  while the others keep climbing, so the colour walks toward white exactly where the light is
  strongest and keeps its hue everywhere it is not — the white-hot core inside a coloured halo
  that every photograph of a neon sign shows. So `white_mix` became `gain`, a multiplier allowed
  above one and clipped per channel in the shader, and the desaturation falls out of the clip
  rather than being authored. It closes the 4.08x luma spread on its own, too: the red string,
  being dark, has the most headroom before its remaining channels clip, so the same gain lifts it
  furthest, and no per-string compensation exists.

  Clipping alone is not enough and the PALETTE is why — our red is literally `(237, 0, 0)` and our
  teal `(0, 181, 160)`. With a dead channel a gain clips the one live channel and simply stops:
  never brighter, never desaturating. So `emitterSpectrum()` gives every light a broadband pedestal
  first, mixing the colour 18% toward the achromatic grey AT ITS OWN PEAK so hue and peak channel
  are untouched and only the dark channels lift. The warrant is physical rather than aesthetic: no
  emitter is spectrally pure, and the scatter that produces a glow at all — in a lens, in air, in
  the eye's own optics — is broadband. That is why the centre of a coloured light is white.
  (Reference: <https://64.github.io/tonemapping/> on per-channel clamping versus luminance-
  preserving operators and the hue/saturation shift each produces.)

  The table grew to nine rows, each varying ONE field against `medium`: `medium flat` (gain 1.0) is
  the un-gained control that shows what the gain buys, and `medium hot` (gain 7.0) is the upper
  bracket, hot enough that even the palette's darkest string clips its remaining channels near the
  core. The chord box lost its hand-tuned white lift entirely — the gain whitens its core by the
  same mechanism it uses on every note, so a box now shares the candidate outright and carries no
  number of its own.
- **The three defects the same sighting reported are fixed and await re-sighting.** (a) Open
  strings looked like *"a box of light with sharp corners over the string"* — the light was a
  plain quad in a batch that submits AFTER the bars. It is now a CAPSULE distance field (half
  extents of the bar's middle cross-section, corner radius equal to its half thickness), under the
  bars, and its silhouette stops one fade length inside each tip rather than at the geometric end:
  the bar's own ends ramp to fully transparent over that length, so a light drawn to the end would
  glow around string that is not there. (b) No glow on chord-box edges — reported TWICE, and the
  band-outline approach was wrong three ways. The outline was hand-rolled, so it drew a top bar
  where a two-note chord has none and full-height columns beside ones that fade out at the
  midpoint; its inward reaches were negative, so no band landed on the bar at all; and it was
  queued into a NOTE batch that only reaches the screen when notes happen to be visible. **But the
  reason it read as nothing even where it landed is perceptual and geometric**, and no amount of
  fixing the outline would have saved it. **The frame bar is 0.075 world thick, which projects to
  0.7 px at the far end of the visible window and 2.3 px a third of a second out — halve that
  again in the editor preview.** Every "light the bar" design is therefore confined to a hairline
  and adds no screen AREA at any distance. The light is now ONE field around the frame's outer
  rectangle, spending the reach outward onto the dark board and inward across the bars themselves;
  it draws OVER the panel (a bar has to emit, and light under it is covered by the bar's own
  paint) where a note's light goes under its opaque head. Each accented box still flushes its own
  batch so a far box's glow cannot wash over a nearer box's panel. Where a two-note chord has no
  top bar, the field's rectangle is sized so its top boundary lands one reach above the drawn quad
  — the top edge never registers — and the vertical fade rides vertex alpha over the same span the
  columns fade across, read from the same `chordBoxFrame` derivation the panel reads. That shared
  derivation is deliberate: the panel and its light disagreeing about where the columns end is
  exactly the "one rule stated twice" defect, and it is now structurally impossible.
  (c) The accent atlas ring was still
  being drawn under the light; that draw, the `g_head_cell_accent` constant, and the art itself
  are all gone. The slot it vacated became one of the sheet's three spares in the reorder below.
- **The sustain tail's light is CLIPPED at the ribbon's last fully-opaque station** (`3c20eab4`,
  2026-08-17, from a measured audit of the user's "translucency is being ignored / it makes the
  middle bold" report). The glow composites BEHIND the ribbon, whose edge strips are opaque and
  whose core is 37.65% alpha, so a quad across the whole band lit the ribbon only through the part
  authored to stay quiet — inverting a fretted ribbon's contrast, washing an open core 15x with a
  bright rim where the ribbon's own alpha is zero, and brightening every tail 89% mid tip-fade.
  The rule: the emitter is the fully opaque cross-section; light draws outward from the last
  full-alpha station, never inward, its strength following the authored ramp through
  `openBarEmission` (third call site, corner-clustered columns like the bar strip), the case
  read off each end's packed colors (outer == edge = hard silhouette) with no note-kind branch.
  A fretted tail's own pixels are identical lit or unlit; awaiting the user's 1x sighting for
  whether the pure halo reads loud enough at far z (the knob is `g_accent_gain`).
- **Ghost: SIGNED 2026-08-15 and no longer a sighting item.** `half light` won on the highway
  (sighted at alpha 0.45 head and markers against 0.65 tail; **collapsed on trial to a single 0.5
  everywhere** at the user's suggestion, plus 0.5 open-bar thickness, to test whether the
  head/sustain split was a distinction the eye ever made), the opaque `lean` won on the
  2D lane, and every alternative is ripped out of both. F10 and its command are gone with them,
  and F9 followed when the accent light signed (2026-08-18); no sighting sampler remains.

**The 2D lane's ghost is SHIPPED and is not a sighting item** — it took the opposite mechanism on
purpose (an opaque lean toward the lane's ground); see `note-emphasis-axis.md` item 4 for why, and
for the ink-authority refactor that made it a loop instead of a parameter threaded through every
drawing helper.

**The evaluation vehicle.** A hand-authored project package exercising every technique —
each one alone, stacked in a chord, and on sliding notes where that is legal — lives outside the
repo at `C:\__MAIN__\Coding\__scratch__\rockhero-showcase\technique-showcase.rhp`, beside the
generator that produced it. Load it to judge any art change against the full vocabulary at once
rather than hunting a real song for an example. Its silent backing track carries precomputed
normalization metadata, because the loudness analyzer refuses silence outright.

Currently 71 measures, 403 notes, 45 sections, 45 FHPs, with 51 accents and 51 ghosts. It covers
emphasis on fretted heads, on open strings (a different code path entirely), composed with mutes
and slides, on full six-string strums, and — added 2026-08-15 after an audit found the gap — on
REPEAT boxes, the one case where the box draws no heads and is therefore the only surface left to
state the dynamic.

**Added 2026-08-16 at the user's request: an emphasis trio for EVERY technique**, one measure each,
ghost on beat one, plain on beat two, accent on beat three — nineteen sections covering plain, palm
mute, full mute, vibrato, tremolo, left-hand tap, tap, slap, pop, legato, all four harmonic
families, bend, slide, slide-out, pick scrape and open string. Not redundant with the blocks above,
for a mechanical reason: emphasis is drawn from the note's SILHOUETTE, so a technique that changes
the head changes which code path the emphasis takes. Every harmonic puts a node head on the board,
and a node head is a diamond the accent light traces with the RHOMBUS distance field rather than the
rounded box; a scrape wears the plectrum; an open string has no head at all. Three tiers in one bar
rather than three bars because this axis is a comparison — a ghost only reads as quiet against the
note beside it. (The generator's output paths were absolute into a coding session's own scratch
directory, so it wrote correctly once and then to a path that no longer existed; they are now
resolved beside the script.)

**Ruled 2026-08-15: the harmonic marker's height EQUALS the full mute's, in every scheme.** It
tracks that mark rather than carrying a size of its own, so whatever a sizing scheme sets the
mutes to is what the harmonic gets. This supersedes both the seat-scale fitting the round-base
rounds produced and an intermediate "keep it head-matched" rule — at family sizes above the head
those left the harmonic visibly smaller than the marks beside it.

**Scope, ruled by the user 2026-08-15.** SETTLED and not to be resized — the rectangular head
bases (standard, tech, anticipation hollow), the diamond harmonic base and its hollow, and the
arpeggio brackets ("look proper at their current size"). UNDER EVALUATION — the technique
symbols only: accent, legato, tap, pick slide, slap, pop, palm mute, full mute, harmonic, pinch.
The bend chevron is borderline ("seems to look good too, but can be considered"). Because the
head bases are fixed, they are the yardstick: sizes and scheme rules are expressed as ratios to
the settled head rather than as absolute texels.

**The toggle mechanism that ran this pass, and why it was files rather than cells — RETIRED
2026-08-18 with the last size question.** The engine loads the head atlas as a whole file by
name, so a sizing scheme was a complete atlas variant: switching meant deploying a variant beside
the executables and reopening the preview, with NO code change and the cell vocabulary identical
in every variant. Only the winner entered the repo. An earlier per-cell candidate seam for the
icon sizes was reverted in favour of this — it cost atlas cells and code for a switch the file
swap did for free. `.agents/atlas-variant.ps1` drove it and is DELETED now that every size is
signed; if a future round needs sighting again, recover it from git history rather than writing a
new one (it carried two non-obvious properties worth keeping: it stamped the write time, because
a plain copy preserves the source's mtime and is then silently ignored by staging, and it
verified every deployed copy by hash so a swap either provably happened or failed loudly).

The committed atlas carries every signed size (sha256 `337927b3…`): the 1.07-of-pitch mark law
from `marks-final`, and the pick slide restored to 1.5x the head's solid height. The rejected
sighting variants live outside the repo in `rockhero-atlas-variants`; a rebuild is now the only
thing that writes the deployed copies.

## Shipped 2026-08-15 (newest first)

| Commit | What |
|---|---|
| `33e43599` | Node heads hold flat through the approach (no rolling flip) |
| `832558b7` | This state file |
| `ae589b2e` | Locked in the edge-height diamond harmonic base (D); rejected candidates removed |
| `21bfa768` | The A/B/C/D candidate rounds, all four in the atlas behind one alias seam |
| `528c412b` | (superseded by the two above) the E-fit round base and `highwayNodeHead` |
| `41af229e` | Vibrato: rigid approach sampling, eighth-note period, depth halved to 0.125 st |
| `fc686489` | Tail envelope made symmetric; technique marks compress into the interior |

## Decided, with the numbers the decisions rest on

**The complete mark-sizing law, SIGNED 2026-08-18 and shipped in the marks-final atlas.**
Every head-riding technique mark is 1.07 x the string pitch tall (24.5766 tx). Width by group:
palm mute, pinch harmonic, slap, pop and tap at **44.52 tx** wide — equal overhang past the head
on all four sides, and 1.07 x the head's width AS MEASURED WHEN THE TARGET WAS CUT (41.60 tx at
`8b818d49`). Against the shipped head the same art reads 1.064x, because the marks-final
recentring shifted the head's ext50 width estimator by +0.23 tx at scale 1.0 while phase-free
estimators moved +0.007 — the identical ageing the pick-slide ratio took (1.293 -> 1.274). The
TEXEL number is the law and 1.07 is its provenance, not a live formula: rebaking these five to
1.07 x today's reading (44.76 tx, +0.47 px) would satisfy the ratio by breaking the equal-overhang
half of the same sentence, taking overhang asymmetry from 0.087 px to 0.319 px. Audited
2026-08-18 and left as shipped; legato, full mute and natural harmonic SQUARE at the
same height (the full mute widened 3.6% to get there, arm angles +/-1.06 degrees; the harmonic
is concentric circles, so its squareness is exact by construction); the pick slide is the one
EXPLICIT EXCEPTION, sized at 1.5 x the head's own solid height — 31.149 x 32.966 tx — because
its design law is covering the head's footprint, not lane adjacency (user 2026-08-18: too small
under the family law; see the restore below). Every mark
and all five head-family cells are centred at their cell centres — the half-texel authoring
offsets that drew heads ~1 px low-right of the string, and the mutes' authored low seats
(up to 1.24 tx), are gone; the slap-vs-pop sibling split fell 1.51 -> 0.01 tx and the
hammer-vs-pull flip split 0.41 -> 0.02 tx. Verified independently after the bake by
re-measuring the written file: four centre estimators, a per-axis mirror test at each shape's
own intrinsic-asymmetry floor, per-side overhang tables, and the 1:1 faithfulness controls.

Two accepted notes from the bake, each with its knob: the recentred head's one-texel edge now
rasterizes at a phase that reads crisper (~2.3 -> ~1.4 px transition at the hit line; the
softness knob on cells 0/1/2 exists if it ever reads hard), and the harmonic symbol's 1.07
height halves its crossing of the diamond (+2.4 -> +1.2 tx, tips more proud — the direction
the diamond ruling favours; the symbol's own height is the knob).

**The head's INTERIOR is concentric with its silhouette** — found by measuring the user's own
rendered frame after the recentring: the silhouette sat on the string to +/-0.15 px, but the
bright rim ring inside it was authored +0.5 tx low, putting the brightest feature ~1.4 px
below the string at 1920 — exactly the "head looks very slightly low" the user kept seeing on
a perfectly-seated head. Fixed in the same atlas: ring bands now equidistant from the mask
centre (equidistance error 0.98 -> 0.02 tx, whole-art mirror centre +0.46 -> -0.004 tx) with
the deliberate top-lit fill gradient preserved, and the anticipation ring's authored stroke
asymmetry (1.0 tx top-heavy, opening 0.5 tx low) equalized with its outer contour still
tracking the head — overriding an earlier preserve-by-default, on the now-measured rule that
eccentric brightness inside a centred silhouette reads as mis-seating.

**A technique mark's HEIGHT is 1.07 x the string pitch — 24.5766 atlas texels.** SIGNED
2026-08-17 from a four-state sighting (today / 1.00 tangent / 1.07 / 1.10) cycled in the app.
It is the third-party reference's own measured overhang: adjacent-lane marks kiss rather than
merely touch, overlapping by ~0.07 of the pitch. Exact tangency (1.00) read as timid beside it.

Two consequences the sighting exposed, both being worked now:
- **Width leaves the uniform rule.** At 1.07 the palm mute (35.61 tx) and slap (41.31 tx)
  shrank away from the note's edge and read wrong, while tap (48.12) and pop (53.18) grew past
  it. The user's ruling: palm mute should span the note like tap does, slap like pop. Width is
  therefore a per-mark authored quantity, not a consequence of the height scale.
- **The marks are REDRAWN, not resampled.** Resizing by resample softened edges (the palm mute
  most visibly). Per the true-to-size rule, each mark's construction is recovered from the
  source art and re-rendered analytically at its target extents, so the shipped art is
  pixel-exact at its own size.

**The uniform family scale and the string spacing are SIGNED at 1.000 and their samplers
deleted.** SIGNED 2026-08-17. The family axis died twice: the eye ruled today's head size "by
far the closest" to the reference, and the pitch-frame analysis proved no uniform value can be
right — the reference's slot-to-pitch ratio varies per fret (q ≈ 0.971–0.979, ~3.94 at the nut
to ~2.62 at fret 13) while ours is a constant 3.143, so a uniform shrink that fixes the
mid-neck slot fill opens the chord stacks to a gap the reference never shows. The spacing axis
measured 0.966, inside its own band around 1.000 — the board's spacing was never the mismatch.
The current direction instead: heads hold today's dimensions and the technique MARKS rescale to
the pitch standard (the marks-pitch-* atlas variants). F6 and F7 are unbound again; ids 0x1305
and 0x1306 are retired forever.

**The harmonic diamond stays at today's span, deliberately past the pitch standard.** SIGNED
2026-08-17: the tips standing proud of the ring are what make the mark read. Accepted pending
evaluation, with the measured costs: stacked diamonds on adjacent strings interpenetrate ~4.2
texels per side, and on the outer strings the tip extends ~4.2 texels (0.6–1.9 screen px) past
the string grid's edge line — the user has asked to evaluate both in the app.

**The arpeggio brackets and the bend chevron are SETTLED at their current size.** SIGNED
2026-08-17, stated while scoping the technique-mark tangency bake: no mark-sizing round may
rescale either. Both were already excluded from that bake's cell list; this records the
exclusion as a ruling rather than a round-local choice.

**The 2D sustain tail ends BARE — no cap, no dissolve — and every mark riding it runs the full
ribbon.** SIGNED 2026-08-16 from three candidates sighted on `F10`, which is free again now that
the sampler, its candidate table and its command are deleted:

| Candidate | What it drew | Outcome |
|---|---|---|
| `mark to end` | bare ends; slide diagonals and the bend's held run take no final inset | **WON** |
| `end cap` | the cap restored, marks inset one stroke to meet its inner face | close second |
| `fade out` | bare ends, last stretch of ribbon and every mark on it dissolving together | rejected |

The ruling turns on this being the EDITOR, in the user's words: *"in 2D because it is the editor
the precision of seeing the exact end point feels like it is needed."* A dissolve trades that
endpoint away for softness, which is the wrong trade on a charting surface. That reasoning does
NOT transfer to the game's highway, so the two surfaces legitimately end a tail differently and
the highway's dissolve is not a divergence waiting to be reconciled. **If the bare end ever reads
as unfinished, restore the CAP rather than reaching for the dissolve** — and note the cap and the
marks' final inset are one decision, not two knobs: restoring the cap means restoring the inset
with it. Both losers are recoverable from git history (`0d511335` added the sighting).

The defect that opened the question: removing the cap earlier left the inset behind, so a glide
whose last waypoint sat on the sustain end stopped one stroke short of its own ribbon. The inset
was never overhang protection — JUCE strokes with butt caps, whose ink ends exactly at the
endpoint — so dropping it costs nothing. Only the FINAL leg loses its inset; the insets between a
multi-waypoint glide's legs open the hairline that makes them read as separate legs, a different
job entirely.

**The harmonic base is D: a diamond whose EDGE equals the regular head's height** (the
head-height square rotated 45°, vertex span ≈ 30.6 tx), chosen 2026-08-15 from four measured
candidates. The user sighted the stacked overlap and accepted it (*"D looks okay stacked even
with a bit of overlap"*). The rejected candidates (halo circle, 26.4-box diamond, diagonal-height
diamond) are recoverable in full at `21bfa768`.

**Two measurements that ruling was signed on were WRONG** (re-measured 2026-08-17, whole-family
audit round): the marker's ring does NOT land inscribed — it crosses the diamond's flats by
2.0 tx, containing span 19.72 tx against a 15.65 tx half-span (the "clears the flats by 0.08 tx"
figure measured the wrong contour); and the lane pitch is **22.9688 tx** (0.35 world over
`headArtTexelWorld`'s 63-texel span), not 23.33. The same round found the overflow is
scale-invariant — mark and base scale together, so NO uniform family rescale changes the 26%
overflow — and that the pinch harmonic NEVER rides the diamond (`nodeIsOnNeck` excludes
`Pinch`, chart.h), so cell 15 never constrains the diamond's size. The re-opened decision CLOSED with the
2026-08-17 span ruling above and the 2026-08-18 mark law: the symbol dropped to 1.07 x pitch,
halving the ring's crossing of the flats (+2.4 -> +1.2 tx, tips more proud — the direction this
ruling favours), and the diamond kept its span. The reference-ratio family baseline (user ruling
2026-08-16: match the third-party reference's height-over-pitch as the default) was measured
pixel-by-pixel from the five screenshots in `__scratch__/reference/` and produced the 1.07
standard; the baked candidates remain in `rockhero-atlas-variants`, though the swapper that
deployed them is retired — a rebuild is the only path to the board now.

**Atlas layout** (256×320, 4×5, capacity 20), REORDERED 2026-08-15 — two rules, one per half of
the sheet. Head bases take a row per SHAPE FAMILY complete with its hollow: row 0 the rectangle
family (standard, tech, anticipation), row 1 the diamond family (base, hollow). Technique marks
take two KEYBIND SIBLING PAIRS per row — `M`/`Shift+M` palm and full mute adjacent,
`H`/`Shift+H` natural and pinch harmonic adjacent. That ordering is affordable because the HAND
is carried by the art (fill polarity, measured 31..91 picking against 246..255 fretting, no
overlap), not by position; it falls out anyway, with rows 2 and 4 hand-pure and the two
hand-spanning pairs between them in row 3.

Spares are **3, 6 and 7** — byte-identical empties, each the growth slot of the family whose row
it sits in. `g_head_cell_count` is **20**, which is full capacity: no headroom, and a 21st named
cell needs a sixth row (256×384).

**The marker family's sizing is SETTLED at v7** — every technique symbol at 85% of its authored
height, sighted and accepted 2026-08-15 (*"I think all these sizes look pretty good. Even pick
slide"*). The harmonic marker tracks the full mute's height per the rule above rather than
carrying its own. The one cost that shrink took has since been PAID BACK: it cost the pick slide
its deliberate over-coverage of the head, the watch item opened for it fired, and the mark is
restored below.

**The pick slide is RESTORED to 1.5 x the head's solid height. SIGNED 2026-08-18** (user, after
sighting the baked variant in the app: *"The new size looks good"*). The full history, measured
cell by cell from every committed atlas: it entered at 33.96 x 23.89 tx as a wide V (`04cbb147`,
08-04), was reshaped to 20.55 x 20.09 the same day, then took its present form at **31.000 x
32.997 tx — 1.524 x the head's solid height** (`f33757d1`, 08-06), its largest ever. It held that
for nine days until the 85% family shrink (`c463230f`) cut it to 26.43 x 28.05, and it shipped at
26.42 x 27.99 (1.274x) after the marks-final recentring. The restored mark measures **31.149 x
32.966 tx, 1.5001x** — within **0.034 tx (0.067 px at 1080p)** of the 08-06 height, so the round
number the user chose is the historical size rather than a new one, and the variant baked at the
literal historical size was never worth sighting separately.

The rejected alternative is on record with its measurement: matching the head's WIDTH (the user's
first proposal) would have been 41.830 tx tall — 1.90x the head, 81.6 px at 1080p, its half-height
reaching **91.1%** of the way to the next string's centre. The user called it before it was baked
(*"1.9x will likely look terrible"*), so it was measured and dropped rather than rendered. At 1.5x
that reach is 71.8%, and half the height stays 6.5 tx clear of the lane pitch.

Two accepted costs, measured rather than assumed. Ink overlap with a neighbouring string's HEAD
roughly triples (34 -> 112 texels of 50%-coverage ink), asymmetric because the tapered lower tip
intrudes less than the shoulder — but same-onset notes paint in ascending lane order
(`highway_renderer.cpp`), so within a chord the string above paints over ~84 of those texels.
Overlap with a neighbouring family MARK goes 0 -> 4 texels. Nothing clips: the art stays wholly
inside its cell and the drawn quad is unchanged.

The mark was REDRAWN analytically at the new extents, never resampled, through the marks-final
pipeline's recovered outline; the control that proves the pipeline is faithful is that the same
construction at scale 1.0 reproduces the shipped cell byte for byte (0 of 4096 texels differ).
The bake touched cell 19 alone — 826 texels changed, the other nineteen cells byte-identical.

**One shape law.** The filled base, the landing ring, and the pre-bend outline all select their
silhouette from `highwayNodeHead` (`highway_head_marks.h`), which asks the board's own placement
rule, so the approach can never preview a shape different from the one that lands. The same
predicate now also holds node heads flat through the approach.

**Vibrato**: one wobble per eighth note (quarter-note-referenced through the measure denominator),
depth 0.125 semitones, wave anchored to the note's own extremes so it stays rigid on approach.

## Open decisions

1. **The bend display anchor** — is *half step = exactly one string gap, every string* right?

   **The user's proposal (2026-08-15), now the leading candidate:** anchor it so a THREE-WHOLE-STEP
   bend travels exactly **two string spacings** — a full bend on one lane reaches the lane two
   away and touches it. The curve's shape is fixed (three steps is 2.86x a half step's travel), so
   that anchor makes a half step **≈ 0.70 gaps**, about 30% smaller than today's 1.0. Two things
   fall out. It is *below* the physically measured range for the user's stated standard (real
   half-step travel is ≈ 0.9–1.5 gaps depending on string), so this is a legibility choice that
   trades away some of the physical accuracy the same conversation asked about — worth stating
   plainly rather than presenting the two as compatible. And it *removes* the saturation problem
   rather than creating one: at 2.0 gaps a full bend fits any six-lane grid with room to spare,
   where today's 2.86 was already near the edge and a larger anchor would have clamped legal
   bends. Needs sighting in the app before it is signed.

   The curve SHAPE in `highwayBendLiftY` is verified physics; the anchor is a display choice. For
   the user's stated standard (25.5" scale, .009 set, measured at the 12th fret) true travel is
   per-string: high E ≈ 1.5 gaps, B ≈ 1.15, G ≈ 0.9, wound ≈ 0.9–1.1 — so physical accuracy means
   per-string anchors, and equal-pitch bends would then draw unequal heights. Two couplings before
   changing it: at ≈ 1.5 the three-whole-step ceiling (2.86 gaps → 4.3) outruns a six-lane grid, so
   `highwayBentNoteY`'s saturation guard would clamp LEGAL bends and break its own stated
   guarantee; and vibrato's just-tuned depth re-opens, since drawn swing scales with the anchor.
   Scale length barely matters (24.75" differs ≈ 6%; travel goes as L²); fret position is ±15%;
   real setups need somewhat MORE travel than these figures (stretch behind nut and saddle), so
   they are lower bounds.

## Watching

- **Stacked node heads.** D's overlap is accepted on sight, not measured against the corpus. If it
  reads wrong in real charts, the evidence to gather first is how often simultaneous harmonics land
  on adjacent strings — a corpus count, not a guess.

## Conventions this pass established

- Every filled head cell has a HOLLOW twin derived the way the rectangular anticipation ring
  derives from the standard head; a new base shape needs both or the approach lies about the
  landing.
- Marks carry their seat scale in their own art on one uniform quad. The harmonic marker cannot be
  merged into a base cell: per-quad shader clamping is load-bearing (the family highlight
  overdrives past white and the icon's translucent moat darkens the CLAMPED result — a merged
  structural cell measured 74 counts off).
- Texture work goes through the texture-author agent, which verifies byte-preservation of untouched
  cells and reports measured extents; candidate rounds are committed before the rejects are ripped
  out, so a reversal is a `git show` away.
