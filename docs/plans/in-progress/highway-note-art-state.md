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

**Sighting the ACCENT LIGHT on the highway** — the last open end of the emphasis axis, the ghost
end having been signed. Candidates live in `highway_emphasis_styles.h` and cycle with **F9**,
which logs the active one. The default index is the table's current front-runner, so the app opens
on the look last preferred.

**Open and being worked 2026-08-15**, after the user reported that the light *"doesn't fade
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

The fix under design is one per-fragment falloff program evaluating a rounded-box / rhombus /
capsule signed distance — the intersection of two precedents this renderer already ships (the
box-mute SDF program and the window light's soft edges) rather than a third mechanism. It deletes
`BoxPanelParts`, the second panel redraw, the spill quads, both rim lambdas, the open bar's `/2`
prism compensation, and two fields of the style table.

**Blend mode becomes a sighting axis too** (user, 2026-08-15): additive clips per channel in this
8-bit buffer with no HDR, and clipping desaturates toward white — an independent second cause of
"too white". `BGFX_STATE_BLEND_SCREEN` (`FUNC(ONE, INV_SRC_COLOR)`, verified present in the
vendored bgfx) is bounded by the source colour, so it converges on the STRING'S colour instead of
white and lets overlapping glows merge without blowing out; `BGFX_STATE_BLEND_LIGHTEN` (max) never
blows out at all. A/B them against additive rather than picking one.

Also outstanding: the chord box light must join the F9 cycle, which it currently does not.

- **Accent colour, RULED 2026-08-15: the light is the STRING'S colour.** The first round was 78%
  to 100% white on every candidate, which the user caught (*"Is the accent glow only using WHITE
  light? … It needs to use the string color for note heads and open strings"*). White was chosen
  because the atlas ring it replaced got most of its brightness from a white lift; the ruling is
  that a light which says the same thing on every string does not belong to the note it marks.

  The cost is paid in reach rather than in white: full string identity carries the palette's
  4.08x luma spread (the same alpha reads four times quieter on red than on yellow), and the only
  knob that closes that without whitening is AREA, since the core is already at full alpha. So
  the candidates now run tight → wide instead of tinted → white: `string tight` (the sighted
  1.5-texel width, as a control), `string wide`, `string bloom` (the widest that still belongs to
  one string — a head's glow ends 0.89 of a lane pitch from its centre, just short of the
  neighbour), and `string lifted` (wide, keeping a quarter of a white lift as the compromise).
  **Open question: how far may a string-coloured glow reach before it stops belonging to the
  note?** The first round's answer was measured against a WHITE field, which competes with the
  note in a way its own colour does not, so it is genuinely re-opened.
- **The three defects the same sighting reported are fixed and await re-sighting.** (a) Open
  strings looked like *"a box of light with sharp corners over the string"* — the light was a
  plain quad in a batch that submits AFTER the bars. It is now the BAR'S OWN GEOMETRY redrawn at a
  thicker cross-section, in its own batch submitted before the bars, so it inherits the rounded
  profile and the tapered fading ends; its alpha is HALVED because a closed unculled prism
  accumulates an additive pass twice where a head's flat art accumulates once. (b) No glow on
  chord-box edges — reported TWICE, and the band-outline approach was wrong three ways. The
  outline was hand-rolled, so it drew a top bar where a two-note chord has none and full-height
  columns beside ones that fade out at the midpoint; its inward reaches were negative, so no band
  landed on the bar at all; and it was queued into a NOTE batch that only reaches the screen when
  notes happen to be visible. **But the reason it read as nothing even where it landed is
perceptual and geometric**, and no amount of fixing the outline would have saved it. **The
  frame bar is 0.075 world thick, which projects to 0.7 px at the far end of the visible window
  and 2.3 px a third of a second out — halve that again in the editor preview.** Every "light the
  bar" design is therefore confined to a hairline and adds no screen AREA at any distance; and
  what little it added was the box's own teal on a frame already painted that exact teal, the
  least perceptible change available. The light is now two stages: the PANEL redrawn additively
  frame-only (so it follows every variant of the shape by construction), plus a gradient SPILL
  reaching 0.30 world outward onto the dark board, white-lifted. Outward is the only direction
  with room — the interior must stay see-through and below the box is the floor. Each accented
  box flushes its own batch so a far box's spill cannot wash over a nearer box's panel. Note the
  box light is deliberately NOT on the F9 cycle: a box has no string colour and shares none of the
  candidates' variables, so cycling note styles leaves it alone.
  (c) The accent atlas ring was still
  being drawn under the light; that draw, the `g_head_cell_accent` constant, and the art itself
  are all gone (cell 3 is now empty and byte-identical to spare cells 18 and 19).
- **Ghost: SIGNED 2026-08-15 and no longer a sighting item.** `half light` won on the highway
  (alpha 0.45 head and markers, 0.65 tail, 0.45 open-bar thickness), the opaque `lean` won on the
  2D lane, and every alternative is ripped out of both. F10 and its command are gone with them;
  only F9 remains, cycling the accent light.

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

Currently 52 measures, 343 notes, 26 sections, 34 FHPs, with 32 accents and 32 ghosts. It covers
emphasis on fretted heads, on open strings (a different code path entirely), composed with mutes
and slides, on full six-string strums, and — added 2026-08-15 after an audit found the gap — on
REPEAT boxes, the one case where the box draws no heads and is therefore the only surface left to
state the dynamic.

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

**The toggle mechanism, and why it is files rather than cells.** The engine loads the head atlas
as a whole file by name, so a sizing scheme is a complete atlas variant: switching is copying a
variant over `rock-hero-common/ui/resources/textures/notes.png` and rebuilding, with NO code
change and the cell vocabulary identical in every variant. Only the winner enters the repo; the
rest are deleted. An earlier per-cell candidate seam for the icon sizes was reverted in favour of
this — it cost atlas cells and code for a switch the file swap does for free.

Working tree holds `notes.png` at the sighted v7 icon sizes with the retired accent ring cleared
out of cell 3 (sha `b36f3da3…`). `git checkout -- rock-hero-common/ui/resources/textures/notes.png`
restores the committed atlas at any time.

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

**The harmonic base is D: a diamond whose EDGE equals the regular head's height** (the
head-height square rotated 45°, vertex span ≈ 30.6 tx), chosen 2026-08-15 from four measured
candidates. The marker's ring lands inscribed in it (clears the flats by 0.08 tx) with the points
4.4 tx proud. Accepted price: node heads stacked at the 23.33 lane pitch interpenetrate ≈ 3.7 tx
per side — the user sighted this and accepted it (*"D looks okay stacked even with a bit of
overlap"*). The rejected candidates (halo circle, 26.4-box diamond, diagonal-height diamond) are
recoverable in full at `21bfa768`.

**Atlas layout** (256×320, 4×5, capacity 20): cell 14 the harmonic marker, 16 the diamond base,
17 its hollow twin, and **3, 18 and 19 spare** — all three byte-identical and fully empty, so the
next mark the vocabulary needs is a one-cell bake. Cell 3 held the accent ring until emphasis
became a light. `g_head_cell_count` is 18.

**The marker family's sizing is SETTLED at v7** — every technique symbol at 85% of its authored
height, sighted and accepted 2026-08-15 (*"I think all these sizes look pretty good. Even pick
slide"*). The harmonic marker tracks the full mute's height per the rule above rather than
carrying its own. The one cost the shrink took is recorded and monitored rather than accepted
silently: the pick slide lost its deliberate over-coverage of the head, which is in
`docs/tracking/watch-items.md` with the measurement and the remedy that reverses it.

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
