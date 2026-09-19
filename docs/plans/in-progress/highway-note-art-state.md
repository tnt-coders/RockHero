# Highway Note Art — the live state of the 3D visual pass

Status: **ACTIVE.** The record of the highway's note art: the accent light, the head family and the
silhouette measurement it rides, the mark-sizing law, vibrato motion, and the atlas. It is the
recovery point for this work — a later round starts from this file plus the committed `notes.png`.

**Two live queues, no overlap.** The technique-decision items (W3–W13) live in
`technique-review-walkthrough.md`'s own work queue and are NOT restated here; this file carries the
visual pass, which runs on a separate track. When resuming, read both.

No sighting rig exists in the tree — no appearance sampler, no atlas swapper, no candidate table, no
variant deploy script. The committed `notes.png` is therefore the single authority for every drawn
size, and a rebuild is the only thing that writes the deployed copies. Rejected sighting variants
live outside the repo in `rockhero-atlas-variants`; recover the swapper from git history if a future
round needs one, rather than writing a new one.

## The accent light

One per-fragment falloff program draws every accent on the board — notes, open strings and chord
boxes alike. `accent_glow` (`shaders/vs_accent_glow.sc`, `shaders/fs_accent_glow.sc`,
`HighwayShaderProgram::AccentGlow`) evaluates a rounded-box / rhombus signed distance, a capsule
being the rounded box at corner radius equal to half its thickness. That is the intersection of two
mechanisms this renderer already ships — the box-mute SDF program and the window light's soft edges
— rather than a third one.

The shape parameters ride the VERTEX rather than a uniform, which is what lets three unrelated
silhouettes at three different sizes batch into one draw. The field's four scalars ride
`u_accent_glow_params`: reach, exponent, emitter depth, radiance gain.

**The signed values.** `g_accent_reach` 0.12 world and `g_accent_exponent` 2.0 (both inline in
`highway_renderer.cpp`), alpha 1.0, additive blend, and a radiance gain that is per subject:
`g_accent_gain` 1.5 on a note (`highway_emphasis_styles.h`) against `g_accent_gain_boxes` 1.0 on a
chord box (`highway_renderer.cpp`). That split is a size compensation rather than two opinions about
the light — outside a silhouette the shader's inward term is zero, so the falloff is provably
identical for both subjects and only the lit LENGTH differs. The note value sits in
`highway_emphasis_styles.h` beside `g_ghost_alpha` 0.5, which it deliberately mirrors: both ends of
the emphasis axis stand the same distance either side of neutral, stated as separate constants
because alpha on an object and radiance on a light beside it are separate mechanisms. The gain lever
is spent; `docs/tracking/watch-items.md` carries reach as the remaining one.

**Why a per-fragment field and not an atlas ring.** The ring the light replaced was wrong three ways
at once, and each one is a reason the field has the shape it has.

1. **A rim that redraws the head's own cell on a larger quad samples the art INWARD as it draws
   OUTWARD**, so it brightens toward its outer edge — the inverse of a falloff. At a reach of four
   texels or more the visible band lands on the art's BEVEL, where `R=255, G=107` against an
   interior of `R≈170, G≈33`; the wider the ring, the more bevel it lands on.
2. **`fs_texture_tint` adds `texel.g` unconditionally** — `rgb = texel.r * tint + texel.g` — and G
   is the atlas's achromatic white-lift channel. So that bevel contributes 107/255 of PURE WHITE
   that no `white_mix` setting can suppress, since the mix only scales the tint multiplying R. This
   is why a ring reads white however the colour is ruled.
3. **Flat-alpha and per-side-linear stages step.** Two flat rim stages are a staircase, and four
   independent linear ramps around a box carry full alpha along an entire edge, including out past
   the corners where the adjacent ramps have already decayed to zero — a step of the halo's full
   alpha across ZERO width. That is the hard cut a reader sees at a chord box's top bar.

Collapsing all of it into one program removed rather than tuned: `BoxPanelParts` and the second
frame-only panel redraw, all six box spill quads, both note-side rim lambdas, the open bar's `/2`
prism compensation, the two-batch split for note accents, four fields of the style table, and every
`g_box_light_*` constant. A flat quad crosses the fragment once where a closed unculled prism
crossed twice, so the one-weight promise between a head and an open string now holds by construction
instead of by a hand-applied correction; and one quad and one program for both subjects means a
single batch under the notes, where a head's light used to be head ART and a bar's bar GEOMETRY.

**The blend is ADDITIVE**, on three grounds. It is the physically correct operator — light adds, and
`screen` is a compositing convention borrowed from film double-exposure that exists as the LDR
stand-in for "add, then tonemap"; with an HDR buffer you would always add. It is the operator every
other light on this board already uses (`g_additive_state`: the strike glow and the window light),
so anything else would make the board carry two light conventions for no gain. And `screen`'s only
real advantage — it cannot clip — is unreachable here: additive is `dst + src` and screen is `dst +
src − src·dst`, so they differ by exactly `src·dst`, and the glow lands almost entirely on a
near-black board where `dst ≈ 0`. The two can only diverge where the destination is already bright:
overlapping glows from adjacent accents, the lit fret window, a chord box frame. The program emits
PREMULTIPLIED source for the same reason (`g_glow_add_state`).

**Reach is ONE absolute world number for every subject**, which is a design ruling rather than a
convenience: reach is a property of the emitter's BRIGHTNESS, not its size — a short neon tube and a
long one wear the same halo. The asymmetry that falls out is the point. Around a head 0.325 world
tall it is a rim; around a frame bar 0.075 world thick it is several times the bar's own width,
which is exactly what makes a hairline read as glowing instead of merely brighter.

**The light is a BACK LIGHT, not a rim**, and the emitter's DEPTH is the one parameter carrying the
difference. The field measures distance from the EMITTER REGION rather than from the boundary; a
field peaking ON the boundary and falling off both ways from it is a rim by construction, which a
head hides its own inner half of but an open bar — 0.1 world thick with ends fading to transparent —
leaks out of as two bright lines tracing its outline.

- **solid** (note head, open bar): depth past the shape's own inradius, so the entire interior emits
  and the only falloff is outward. That is literally a lamp behind the object.
- **frame** (chord box): depth equal to the frame thickness, so only the band from the outer edge
  one thickness inward emits, spilling both ways. The interior stays dark, which is what keeps a box
  readable THROUGH.

A frame is the general case and a solid is the case where the depth exceeds the shape, so the two
are one field with two values.

**The chord box reads the shared light unmodified** and carries no number of its own. The hand-tuned
white lift it once needed — teal light on a teal frame being the least perceptible change available
— came from the gain's own clipping, and is moot at the box's gain of 1.0.

**Colour: the light is the STRING'S colour.** A light that says the same thing on every string does
not belong to the note it marks.

**Brightness: a RADIANCE GAIN, not a blend toward white.** A fixed lerp toward white is wrong in
both directions at once — it desaturates the far halo as hard as the core, so the light reads washed
out, while adding no radiance whatever, since a lerp can only trade saturation for lightness and
never exceed the emitter's own brightness. What a bright coloured light actually does is CLIP: the
brightest channel saturates and stops while the others keep climbing, so the colour walks toward
white exactly where the light is strongest and keeps its hue everywhere it is not — the white-hot
core inside a coloured halo that every photograph of a neon sign shows. The gain is therefore a
multiplier allowed above one and clipped per channel in the shader, and the desaturation falls out
of the clip rather than being authored. It also closes the palette's 4.08x luma spread on its own:
the red string, being dark, has the most headroom before its remaining channels clip, so the same
gain lifts it furthest and no per-string compensation exists.

Clipping alone is not enough, and the PALETTE is why — our red is literally `(237, 0, 0)` and our
teal `(0, 181, 160)`. With a dead channel a gain clips the one live channel and simply stops: never
brighter, never desaturating. So `emitterSpectrum()` gives every light a broadband pedestal first,
mixing the colour 18% toward the achromatic grey AT ITS OWN PEAK so hue and peak channel are
untouched and only the dark channels lift. The warrant is physical rather than aesthetic: no emitter
is spectrally pure, and the scatter that produces a glow at all — in a lens, in air, in the eye's
own optics — is broadband. That is why the centre of a coloured light is white. (Reference:
<https://64.github.io/tonemapping/> on per-channel clamping versus luminance-preserving operators
and the hue/saturation shift each produces.)

**An accented open string's light is a CAPSULE under the bars**, with the half extents of the bar's
middle cross-section and a corner radius equal to its half thickness. Its silhouette stops one fade
length inside each tip rather than at the geometric end: the bar's own ends ramp to fully
transparent over that length, so a light drawn to the end would glow around string that is not
there.

**A chord box's light is ONE field around the frame's outer rectangle**, spending the reach outward
onto the dark board and inward across the bars themselves, and it draws OVER the panel — a bar has
to emit, and light under it is covered by the bar's own paint — where a note's light goes UNDER its
opaque head. The frame bar is 0.075 world thick, which projects to 0.7 px at the far end of the
visible window and 2.3 px a third of a second out, and half that again in the editor preview: every
"light the bar" design is confined to a hairline and adds no screen AREA at any distance, which is
why the field spends its reach on the board instead. Each accented box flushes its own batch, so a
far box's glow cannot wash over a nearer box's panel. Where a two-note chord has no top bar, the
field's rectangle is sized so its top boundary lands one reach ABOVE the drawn quad — the top edge
never registers — and the vertical fade rides vertex alpha over the same span the columns fade
across, read from the same `chordBoxFrame` derivation the panel reads. That shared derivation is
deliberate: the panel and its light disagreeing about where the columns end is exactly the "one rule
stated twice" defect, and it is now structurally impossible.

**A sustain tail's light is CLIPPED at the ribbon's last fully-opaque station.** The glow composites
BEHIND the ribbon, whose edge strips are opaque and whose core is 37.65% alpha, so a quad across the
whole band would light the ribbon only through the part authored to stay quiet — inverting a fretted
ribbon's contrast, washing an open core with a bright rim where the ribbon's own alpha is zero, and
brightening every tail through its tip fade. The rule: the emitter is the fully opaque
cross-section; light draws outward from the last full-alpha station, never inward, its strength
following the authored ramp through `openBarEmission` (a third call site, corner-clustered columns
like the bar strip), the case read off each end's packed colors (outer == edge means a hard
silhouette) with no note-kind branch. A fretted tail's own pixels are identical lit or unlit.

**Ghost is a single 0.5 everywhere** — head, markers and tail alike, plus 0.5 open-bar thickness.
The head/sustain split an earlier trial carried (0.45 head against 0.65 tail) was a distinction the
eye never made. The 2D lane keeps every normal note ink color, flattens each note's opaque tail and
head art together, then applies the shared 0.5 opacity once; fret numbers remain opaque overlays
while their plates use 0.75 opacity. See `docs/plans/completed/note-emphasis-axis.md` item 4 for
the group-compositing rationale.

## Decided, with the numbers the decisions rest on

**The head art's silhouette is MEASURED from `notes.png` at load, not authored as constants.**
`HeadArtProfile` (`head_art_profile.h`, measured in `head_art_profile.cpp`) carries the extents the
accent field and the mark geometry need, and every quantity is stated in TEXELS against a threshold
of **50% of peak coverage** — the numbers are meaningless without naming it, since the corner radius
alone reads 3.9 tx at 15% coverage and 1.82 tx at 50%.

| Profile field | What it measures | Why it must be measured |
|---|---|---|
| `half_width_texels` / `half_height_texels` | the rectangular head's 50% extents | the accent field's half extents are the art's, not the quad's |
| `corner_texels` | the corner of the 50% contour | there is no authored radius at all — the art is a solid 41×21 rectangle wrapped in a one-texel fringe with the corner texel omitted — so the value is a DESCRIPTION of the contour (worst deviation 0.026 tx), never a construction |
| `center_x_texels` / `center_y_texels` | where the art sits inside its cell | the silhouettes are ODD sized in an even 64-texel cell, so they cannot be quad-centred by construction; unmodelled, the ridge lands up to 1.04 tx off the art's edge and lopsided — bright on bare texture along two edges, buried under the head along the other two. Modelled, every edge is within 0.022 tx, which also absorbs the art's own top/bottom rim asymmetry |
| `node_half_span_texels` / `node_center_*` | the diamond's edge line `\|x\| + \|y\|` | the diamond is measured rather than derived from the signed construction; the art's edge line holds to 0.0000 tx across all 88 edge samples |

Texels reach world through `headArtTexelWidth` / `headArtTexelHeight` (`highway_renderer.cpp`), each
`(2 × the head's world half extent) / (cell_size − 1)`. The `− 1` is load-bearing:
`HighwayAtlasLayout::cellRect` insets each cell's UV rect by half a texel per side, so the head
quad's corners sample texel CENTRES 0.5 and 63.5 — **63** texels of range, not 64. One conversion in
one place, per axis, and the head's world size read from the metrics rather than from a literal.

Two further facts worth keeping. The rectangular corners are **neither circular nor chamfered**, per
the fringe construction above. And the rhombus branch is **exact, not approximate, for this cell**:
with `b.x == b.y` the shader's formula reduces to the true Euclidean distance to a 45° square, and
the diamond cell's two axes are equal to 0.000000 tx.

**Vibrato runs at ONE FIXED RATE, 6.0 Hz** — `g_highway_vibrato_period_seconds` = `1.0 / 6.0`
(`highway_tail.h`) — independent of tempo and meter, with depth `g_highway_vibrato_depth_semitones`
0.125 (doubled by `g_highway_wide_vibrato_depth_multiplier` for the `Wide` width) and the wave
anchored to the note's own extremes so it stays rigid on approach.

Vibrato rate is a property of the player's WRIST, not of the song's grid, so deriving it from tempo
is a category error at any value. A grid-locked wobble runs at literally BPM/30 Hz, which over the
102-song local corpus spanned 2.0 Hz to 7.1 Hz — a 3.6x spread with nothing musical behind it, and
against the 4—7 Hz real-vibrato band this project already researched for detection
(`docs/plans/roadmap/22-note-detection.md`) more than a quarter of the library drew vibrato slower
than any hand produces.

**6.0 Hz is where the literature's centre is**: production means cluster 5.2—6.6 Hz and every
perception study peaks at 6.0—6.5. Tempo-independence is confirmed about as firmly as this
literature confirms anything — the one direct experiment (Desain et al. 1999, five professionals
across a 26% tempo span) found scaling in two of five players, and the largest tempo effect measured
anywhere, 8.3% in a 40-bassist slow-vs-fast contrast, is smaller than the within-condition spread
and smaller than the effects of register and finger choice. Notation independently reaches the same
answer: SMuFL bakes vibrato speed into fixed-wavelength glyphs tiled along the note, never computed
from tempo — which is also why the 2D lane's spatial-period squiggle is the correct model there and
not a divergence from this surface.

**Rate and depth move together.** A DRAWN wobble reads busier than the pitch waver it depicts,
because the eye tracks the whole screen excursion, so the drawn rate belongs at or under the
physical one — a wider swing at this rate reads frantic. Listeners judge a wobble's speed partly
from its width, and production couples the two inversely at r = -0.62. So widening the depth without
slowing the rate walks back toward a setting that already failed on sight. Caveat kept in view: no
electric-guitar vibrato rate has ever been published, so every number here is transferred from voice
and bowed strings.

**Two better axes than tempo, if vibrato ever earns time-variation.** (1) Rate rises ~15% toward the
END of a sustained note (Prame 1994, replicated by Desain et al. for all five instruments) — a
larger and better-established effect than tempo. (2) If a WIDE vibrato is ever drawn wider still,
bias it SLOWER (~5—5.5 Hz), since production couples extent and rate inversely and notation's own
wide-vibrato glyphs vary amplitude, not speed.

**No grid-derived vibrato is planned**, deliberately: it would re-introduce the defect the fixed
rate removes. The musically real axis is per-note CHARACTER — wide-and-slow against narrow-and-fast
— and the chart now carries the width half of that as `VibratoState` (`Off` / `Narrow` / `Wide`,
`chart.h`) with `Wide` doubling the drawn depth. Rate remains one number for every note.

Untouched by this: the 2D lane's vibrato squiggle runs on a SPATIAL period (a multiple of the tail
height in pixels), because there it is a notation mark rather than a real-time oscillation. The two
surfaces model different things on purpose; this is not a divergence to reconcile.

**The complete mark-sizing law.** Every head-riding technique mark is **1.07 x the string pitch**
tall — **24.5766 tx**. Width by group:

- Palm mute, pinch harmonic, slap, pop and tap are **44.52 tx** wide, which is equal overhang past
  the head on all four sides.
- Legato, full mute and natural harmonic SQUARE at the same height (the full mute is widened 3.6% to
  get there, arm angles ±1.06 degrees; the harmonic is concentric circles, so its squareness is
  exact by construction).
- The pick slide is the one EXPLICIT EXCEPTION at **31.149 x 32.966 tx**, sized at 1.5 x the head's
  own solid height (its 50%-contour height, 21.976 tx — not the 41x21 solid-texel rectangle),
  because its design law is covering the head's footprint rather than lane adjacency.

**The TEXEL number is the law and the ratio is its provenance, not a live formula.** 1.07 is the
third-party reference's own measured overhang — adjacent-lane marks kiss rather than merely touch,
overlapping by ~0.07 of the pitch, where exact tangency reads timid. But the width estimator that
1.07 was cut against has since shifted with the art's recentring, so re-deriving the five marks of
equal overhang from today's reading would satisfy the ratio by breaking the equal-overhang half of
the same sentence, taking overhang asymmetry from 0.087 px to 0.319 px. The shipped texels stand.

Two consequences that fall out of the height law:

- **Width leaves the uniform rule.** At one shared height the palm mute and slap shrink away from
  the note's edge while tap and pop grow past it, so palm mute spans the note like tap and slap like
  pop. Width is a per-mark authored quantity, not a consequence of the height scale.
- **The marks are REDRAWN, not resampled.** Resizing by resample softens edges (the palm mute most
  visibly). Per the true-to-size rule, each mark's construction is recovered from the source art and
  re-rendered analytically at its target extents, so the shipped art is pixel-exact at its own size.
  The control that proves the pipeline faithful is that the same construction at scale 1.0
  reproduces a shipped cell byte for byte.

**Every mark and all five head-family cells are centred at their cell centres.** Half-texel
authoring offsets draw heads about a pixel low-right of the string, and the mutes' authored low
seats reached 1.24 tx; centred, sibling splits (slap against pop, hammer against pull) sit at
0.01–0.02 tx. Two accepted notes, each with its knob: the recentred head's one-texel edge rasterizes
at a phase that reads crisper (the softness knob on cells 0/1/2 exists if it ever reads hard), and
the harmonic symbol's 1.07 height halves its crossing of the diamond, leaving the tips more proud —
the direction the diamond ruling favours, with the symbol's own height as the knob.

**The head's INTERIOR is concentric with its silhouette.** A silhouette seated on the string to
±0.15 px still reads low if the bright rim ring inside it is authored half a texel low, because that
puts the brightest feature over a pixel below the string at 1920. Ring bands are therefore
equidistant from the mask centre, with the deliberate top-lit fill gradient preserved, and the
anticipation ring's authored stroke asymmetry equalized with its outer contour still tracking the
head. The rule this states: eccentric brightness inside a centred silhouette reads as mis-seating,
so an interior is centred even where preserving the authored look would be the softer choice.

**The uniform family scale and the string spacing are both 1.000.** The family axis is dead twice
over: today's head size measures by far the closest to the third-party reference, and the
pitch-frame analysis proves NO uniform value can be right — the reference's slot-to-pitch ratio
varies per fret (about 3.94 at the nut to 2.62 at fret 13) where ours is a constant 3.143, so a
uniform shrink that fixes the mid-neck slot fill opens the chord stacks to a gap the reference never
shows. The spacing axis measured 0.966, inside its own band around 1.000 — the board's spacing was
never the mismatch. The direction that won instead: heads hold their dimensions and the technique
MARKS carry the pitch standard.

**Scope.** The rectangular head bases (standard, tech, anticipation), the diamond harmonic base and
its hollow, and the arpeggio brackets and bend chevron are SETTLED and not to be resized by any
mark-sizing round. Because the head bases are fixed they are the yardstick: a mark's size is
reasoned as a ratio to the settled head, and the resulting texel extent is what ships and what
binds.

**The harmonic base is a diamond whose EDGE equals the regular head's height** — the head-height
square rotated 45°, vertex span ≈ 30.6 tx. It stands deliberately past the pitch standard: the tips
standing proud of the ring are what make the mark read. The measured costs are accepted pending an
in-app evaluation and registered in `docs/tracking/watch-items.md`: stacked diamonds on adjacent
strings interpenetrate ~4.2 texels per side, and on the outer strings the tip extends ~4.2 texels
(0.6–1.9 screen px) past the string grid's edge line.

Three facts about the diamond that govern any future resize. The marker's ring does NOT land
inscribed — it crosses the diamond's flats, containing span 19.72 tx against a 15.65 tx half-span.
The overflow is scale-INVARIANT, since mark and base scale together, so no uniform family rescale
changes it. And the pinch harmonic NEVER rides the diamond, because `nodeIsOnNeck` (`chart.h`)
excludes `Pinch`, so the pinch cell never constrains the diamond's size. The lane pitch, for the
same arithmetic, is 0.35 world — about 22.97 texels at the head art's texel scale.

**The harmonic marker's height EQUALS the full mute's, in every scheme**, which the squared trio's
one shared height states directly: it tracks that mark rather than carrying a size of its own.

**The pick slide is 1.5 x the head's solid height**, at the extents given above. The rejected
alternative is on record with its measurement, because it is the reason 1.5 is the number: matching
the head's WIDTH would be 41.830 tx tall — 1.90x the head, 81.6 px at 1080p, its half-height
reaching **91.1%** of the way to the next string's centre. At 1.5x that reach is 71.8%, and half the
height stays 6.5 tx clear of the lane pitch.

Two accepted costs, measured rather than assumed. Ink overlap with a neighbouring string's HEAD is
about 112 texels of 50%-coverage ink, asymmetric because the tapered lower tip intrudes less than
the shoulder — but same-onset notes paint in ascending lane order (`highway_renderer.cpp`), so
within a chord the string above paints over some 84 of those texels. Overlap with a neighbouring
family MARK is 4 texels. Nothing clips: the art stays wholly inside its cell and the drawn quad is
unchanged.

**A scrape KEEPS its note head.** The bare-scrape reading — the plectrum alone, no base, no
anticipation ring, accent on the tail only — was rejected on sight. A scrape wears the pick mark on
the darker TECH base (`highwayTechHead`, `highway_head_marks.h`, whose scrape clause is why), with
the anticipation ring and the head's accent light intact and no X beneath.

**One shape law.** The filled base, the landing ring, and the pre-bend outline all select their
silhouette from `highwayNodeHead` (`highway_head_marks.h`), which asks the board's own placement
rule, so the approach can never preview a shape different from the one that lands. The same
predicate holds node heads flat through the approach.

**Mark draw order is one ordered authority**, `highwayHeadMarks` (`highway_head_marks.h`), which
both the open-string overlay and the fretted head ask. The order is derived from how much of the
note's identity each mark overrides — palm mute, hand mark, connection, harmonic, dead X — with the
pick slide a category of one that returns alone, because the chart rules prove a scrape carries
nothing else.

**Atlas layout** (256×320, four columns by five rows, capacity 20) follows two rules, one per half
of the sheet. Head bases take a row per SHAPE FAMILY complete with its hollow: row 0 the rectangle
family (standard, tech, anticipation), row 1 the diamond family (base, hollow). Technique marks take
two KEYBIND SIBLING PAIRS per row — `M`/`Shift+M` palm and full mute adjacent, `H`/`Shift+H` natural
and pinch harmonic adjacent. That ordering is affordable because the HAND is carried by the art
(fill polarity, measured 31..91 picking against 246..255 fretting, no overlap) rather than by
position; it falls out anyway, with rows 2 and 4 hand-pure and the two hand-spanning pairs between
them in row 3.

Spares are **3, 6 and 7** — byte-identical empties, each the growth slot of the family whose row it
sits in. `g_head_cell_count` (`highway_atlas.h`) is **20**, which is full capacity: no headroom, and
a 21st named cell needs a sixth row (256×384). The layout itself is built from the decoded PNG at
runtime rather than hard-coded, so the committed art's dimensions are what define it.

**The 2D sustain tail ends BARE — no cap, no dissolve — and every mark riding it runs the full
ribbon.** The ruling turns on this being the EDITOR: precision about the exact end point is what a
charting surface owes, and a dissolve trades that endpoint away for softness. The reasoning does NOT
transfer to the game's highway, so the two surfaces legitimately end a tail differently and the
highway's dissolve is not a divergence waiting to be reconciled. **If the bare end ever reads as
unfinished, restore the CAP rather than reaching for the dissolve** — and the cap and the marks'
final inset are ONE decision, not two knobs: restoring the cap means restoring the inset with it.

Only the FINAL leg loses its inset. The insets between a multi-keyframe glide's legs open the
hairline that makes them read as separate legs, a different job entirely. The final inset was never
overhang protection — JUCE strokes with butt caps, whose ink ends exactly at the endpoint — so
dropping it costs nothing, and leaving it behind is what makes a glide whose last keyframe sits on
the sustain end stop one stroke short of its own ribbon.

**The evaluation vehicle.** A hand-authored project package exercising every technique — each one
alone, stacked in a chord, and on sliding notes where that is legal — lives outside the repo at
`C:\__MAIN__\Coding\__scratch__\rockhero-showcase\technique-showcase.rhp`, beside the generator that
produced it. Load it to judge any art change against the full vocabulary at once rather than hunting
a real song for an example. Its silent backing track carries precomputed normalization metadata,
because the loudness analyzer refuses silence outright.

It runs 71 measures, 403 notes, 45 sections, 45 FHPs, with 51 accents and 51 ghosts. It covers
emphasis on fretted heads, on open strings (a different code path entirely), composed with mutes and
slides, on full six-string strums, and on REPEAT boxes — the one case where the box draws no heads
and is therefore the only surface left to state the dynamic.

It also carries an emphasis trio for EVERY technique, one measure each: ghost on beat one, plain on
beat two, accent on beat three, across nineteen sections covering plain, palm mute, full mute,
vibrato, tremolo, left-hand tap, tap, slap, pop, legato, all four harmonic families, bend, slide,
slide-out, pick scrape and open string. Not redundant with the blocks above, for a mechanical
reason: emphasis is drawn from the note's SILHOUETTE, so a technique that changes the head changes
which code path the emphasis takes. Every harmonic puts a node head on the board, and a node head is
a diamond the accent light traces with the RHOMBUS distance field rather than the rounded box; a
scrape wears the plectrum; an open string has no head at all. Three tiers in one bar rather than
three bars because this axis is a comparison — a ghost only reads as quiet against the note beside
it.

## Open decisions

1. **The bend display anchor** — is *half step = exactly one string gap, every string* right?

   **The leading candidate:** anchor it so a THREE-WHOLE-STEP bend travels exactly **two string
   spacings** — a full bend on one lane reaches the lane two away and touches it. The curve's shape
   is fixed (three steps is 2.86x a half step's travel), so that anchor makes a half step **≈ 0.70
   gaps**, about 30% smaller than today's 1.0. Two things fall out. It is *below* the physically
   measured range for the user's stated standard (real half-step travel is ≈ 0.9–1.5 gaps depending
   on string), so this is a legibility choice that trades away some of the physical accuracy the
   same question asked about — worth stating plainly rather than presenting the two as compatible.
   And it *removes* the saturation problem rather than creating one: at 2.0 gaps a full bend fits
   any six-lane grid with room to spare, where today's 2.86 was already near the edge and a larger
   anchor would have clamped legal bends. Needs sighting in the app before it is signed.

   The curve SHAPE in `highwayBendLiftY` (`highway_metrics.h`) is verified physics; the anchor is a
   display choice. For the stated standard (25.5" scale, .009 set, measured at the 12th fret) true
   travel is per-string: high E ≈ 1.5 gaps, B ≈ 1.15, G ≈ 0.9, wound ≈ 0.9–1.1 — so physical
   accuracy means per-string anchors, and equal-pitch bends would then draw unequal heights. Two
   couplings before changing it: at ≈ 1.5 the three-whole-step ceiling (2.86 gaps → 4.3) outruns a
   six-lane grid, so `highwayBentNoteY`'s saturation guard would clamp LEGAL bends and break its own
   stated guarantee; and vibrato's tuned depth re-opens, since drawn swing scales with the anchor.
   Scale length barely matters (24.75" differs ≈ 6%; travel goes as L²); fret position is ±15%; real
   setups need somewhat MORE travel than these figures (stretch behind nut and saddle), so they are
   lower bounds.

## Watching

Registered in `docs/tracking/watch-items.md` (the standing registry, which outlives this file):

- **Stacked node heads.** The diamond's overlap is accepted on sight, not measured against the
  corpus. If it reads wrong in real charts, the evidence to gather first is how often simultaneous
  harmonics land on adjacent strings — a corpus count, not a guess.
- **The harmonic diamond's stacking and edge overhang** — the in-app evaluation, with the measured
  costs stated above.
- **Accent strength.** The gain lever is spent; if accents still read as decoration rather than
  emphasis in real play, the knob to sight is `g_accent_reach`, not the gain.

## Conventions this pass established

- Every filled head cell has a HOLLOW twin derived the way the rectangular anticipation ring derives
  from the standard head; a new base shape needs both or the approach lies about the landing.
- Marks carry their seat scale in their own art on one uniform quad. The harmonic marker cannot be
  merged into a base cell: per-quad shader clamping is load-bearing (the family highlight overdrives
  past white and the icon's translucent moat darkens the CLAMPED result — a merged structural cell
  measures 74 counts off).
- Texture work goes through the texture-author agent, which verifies byte-preservation of untouched
  cells and reports measured extents; candidate rounds are committed before the rejects are ripped
  out, so a reversal is a `git show` away.
- A sighting round that swaps whole atlas variants beside the executables must STAMP the write time
  of each deployed copy — a plain copy preserves the source's mtime and is then silently ignored by
  staging — and verify each copy by hash, so a swap either provably happened or fails loudly.
