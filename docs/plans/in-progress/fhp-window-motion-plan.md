# FHP Window Motion Plan

Status: IMPLEMENTED 2026-07-23, pending the visual pass. Projection ramps
(`HighwayFhpView::ramp_seconds`), the continuous window query and coverage signal
(`highway_window.h`), and every renderer consumer (mask-model highlight with transition sweeps,
border rails, open tails/bars, shape rails, boxes and brackets at display time, chord names,
sections, beat bars, face-line crossfade, number fades) are built and unit-tested; the
verification section's visual confirmation in the editor 3D preview has not run yet.

## Goal

On the 3D highway (game view and editor 3D preview), the fret-hand position window and
everything visually anchored to it — the lit runway highlight, open-string note tails, and
arpeggio/hand-shape span rails — moves laterally as one unit instead of snapping:

1. **Slide-locked ramps.** A pitched slide drags the window across the board in lockstep with
   the sliding note's own tail: same start, same arrival, same ease. (The FHP *data* already
   arrives at the waypoint instant after the 2026-07-23 generator change — commit `a51fb314` —
   this plan animates the display.)
2. **Margin morphs.** Every other FHP transition morphs the window into its next shape
   (position *and* width) over the minimum-sustain-distance margin — 1/16 of a whole note,
   the same settled margin the sustain trim and the editor duration verb use — ending exactly
   at the FHP's arrival. The morph occupies the gap the trim rule already keeps clear, so the
   window lands in its new shape precisely as the first note of the new position lands.

Because the highway's z-axis is time, none of this is per-frame animation state: a transition
is a smooth eased sweep in the floor geometry connecting two window segments, built from the
projection and scrolled like everything else. The pitched ease (sin cubed) has zero slope at
both endpoints, so the border leaves and rejoins the straight steady-window edges tangentially
— a curved S, never a sharp-cornered diagonal (user requirement 2026-07-23).

## Signed decisions (user, 2026-07-23)

- Non-slide moves morph over the minimum-sustain-distance margin; nothing stays an instant snap.
- Crowded transitions **shorten** the ramp (clamp its start to the previous FHP's arrival)
  rather than overlapping neighbors.
- One ease curve for all window motion: the pitched-slide ease (`highwaySlideEaseWeight`),
  so slides and morphs feel identical and slide ramps cannot drift from the note tail they
  mirror.
- The morph duration is musical, so it scales with tempo (quick at fast tempos, languid at
  slow ones). This is intended behavior, not an artifact.
- The first FHP of a chart morphs in from the default nut window (fret 1, width 4) under the
  same margin rule rather than popping.
- The highlight is a per-fragment window light over a uniformly drawn board with subtle
  board-wide lane striping; its soft falloff is the only edge (user redesign 2026-07-23,
  after dedicated edge rails and then a phantom fret line both read wrong). See renderer
  items 1-2.

## Design

No chart-format change: every ramp is derived at projection time (derived-over-authored).

### Projection (`rock-hero-common/core`, unit-testable)

- `HighwayFhpView` gains `ramp_start_seconds` (equal to `seconds` means no preceding motion —
  the degenerate instant case, which after this plan only occurs when clamping collapses a
  ramp to zero).
- `makeHighwayViewState` derives it:
  - **Slide match first.** While building note views, record each pitched slide waypoint's
    exact arrival `GridPosition` (via the same `advanceGridPosition` the generator uses) with
    its glide-segment start seconds (note onset, or the previous waypoint). An FHP whose grid
    position matches a recorded waypoint exactly takes that segment start as its ramp start.
    Exact `GridPosition` equality, not seconds-epsilon matching — the generator places the FHP
    at exactly the waypoint's advanced position. Unpitched slide-outs are never recorded (they
    release pressure and never move the window).
  - **Margin morph otherwise.** Ramp start = the FHP's global beat position minus the margin
    (`signature denominator / 16` beats, i.e. 1/16 whole note, using the signature at the
    FHP's measure), resolved to seconds through the tempo map.
  - **Clamp.** Ramp start never precedes the previous FHP's arrival (`seconds` of the prior
    view); the synthetic pre-first nut window counts as arriving at chart start.
- A continuous window query joins the view-state helpers: fractional low/high fret-line
  coordinates at a time `t` — the previous window's edges eased toward the next along
  `[ramp_start_seconds, seconds]` with `highwaySlideEaseWeight` (pitched curve), each edge
  interpolating independently so width morphs and position moves are one mechanism. The
  renderer converts fractional line coordinates to x with a fractional variant of
  `highwayFretLineX` (linear between adjacent integer lines).

### Renderer (`rock-hero-common/ui/src/highway/highway_renderer.cpp`)

The discrete `activeFhpFretLines` step function is replaced at geometry call sites by the
continuous query; it survives only where integers are required (lane-brightness bookkeeping,
fret-number labels).

1. **FHP highlight = a per-fragment window light (user redesign, 2026-07-23, supersedes the
   geometric mask).** The board draws uniform: the plain/dotted lane striping is a permanent,
   subtle, board-wide property of the neck (`g_board_lane_*`). The highlight itself is one
   continuous brightness calculation across the window width, evaluated per pixel: a single
   slab tessellated at the window sample times whose vertices carry each fragment's distances
   inside the two eased edges (linear within a slice, so interpolation is exact), with the
   `window_light` fragment shader dissolving the light over a smoothstep falloff
   (`g_window_light_falloff`, half a fret) past each edge. Soft edges at any zoom;
   transitions cannot facet or misalign because nothing is clipped. Two earlier geometric
   treatments (per-lane clipped sweeps with dedicated edge rails `a59bcdd7`, then a phantom
   fret line `3a18527c`) are superseded and deleted.
2. **No drawn border.** The light's soft falloff *is* the edge; the fret lines behind it stay
   straight and untouched. Adding a shader program touches the six silent steps in
   `docs/developer/the-3d-highway.md` (sources, CMake staging list, `HighwayShaderSet`,
   `GameShaderProgram`, both product loaders, renderer link).
3. **Open-string note tails.** Today the band is sampled once at `note.start_seconds` and held
   flat for the whole tail. Open (and open modulated) tails move onto the z-sampled ribbon
   path with band stations evaluated per sample from the continuous query, so they shear with
   the window. Note the deliberate behavior change: a long open drone ringing across *any*
   later FHP change now travels with the hand instead of staying where it started.
4. **Hand-shape / arpeggio span rails.** Rails currently sample the window once at
   `shape.start_seconds`; they become z-sampled against the continuous query so a mid-span
   window move shears them identically.
5. **Single-instant consumers sample the window at their display time, not their start time
   (user catch, 2026-07-23).** Board-fixed furniture (beat bars, section furniture, plain
   chord boxes) swaps to the continuous query at its own onset z. Elements that *ride the hit
   line* for a span — arpeggio boxes with their brackets, and the fingering panel — evaluate
   the window at `max(start_seconds, now)` per frame, so a held arpeggio or chord shape
   slides along with the notes sliding under it instead of staying frozen at its onset
   window. Shape span rails need no special case: once z-sampled, their near end at the hit
   line is the current instant's window by construction.
6. **Fret numbers — coverage fade (signed 2026-07-23).** Numbers never move: every glyph is
   pinned at its own lane's fixed position, labeling the physical fret. During a transition
   each hit-line number animates opacity only, fading out as the sweeping border leaves its
   lane and in as the border reaches it, driven by the shared coverage signal below. The
   downbeat dotted-fret numbers blend their dim/active colors by the same coverage at their
   own z; the orange upcoming-arrival number stays at the arrival instant unchanged.
7. **Lane-border brightness — coverage crossfade (signed 2026-07-23).** The full-length fret
   line strips keep one alpha per frame (they indicate the current window at the hit line and
   cannot vary along z). Per line, alpha lerps between its non-current tier and the bright
   tier by the shared coverage signal: how deeply the eased window at the current instant
   contains that line. The brightened band thus hands off line-by-line in lockstep with the
   border crossing the hit line — including transiently lighting intermediate lines the
   window passes over — while every line stays straight and fixed.

The **shared coverage signal**: per fret line, a [0, 1] measure of containment by the eased
window edges evaluated at the current instant — one formula feeding both the number fades and
the brightness crossfade so everything at the hit line moves as a single gesture.

### Out of scope

- The 2D tab lane (`tab_projection` / `tab_paint_core`) keeps its discrete FHP markers — this
  plan is the 3D highway only.
- The FHP *generation* algorithm is untouched (its slide rule shipped separately in
  `a51fb314`; the corpus-derived generator remains
  `docs/plans/todo/fhp-corpus-derived-generation.md`).

## Behavior notes

- Signed 2026-07-23: open-string drones follow the hand across **every** FHP change (not just
  slides); the 2D tab view has no meaningful analog of window motion and stays untouched; a
  slide clamped at the neck edge legitimately moves the window a shorter distance than the
  note (start, end, and ease still coincide).

- Fretted notes are anchored to their own fret and never move with the window, so notes still
  sounding through a ramp (protected-technique adjacency, sub-margin 32nd-note runs) cannot
  conflict with it; an onset landing inside a ramp draws its furniture at the interpolated
  window extent.
- Chord slides: simultaneous waypoints share one delta and one segment, so the merged FHP
  matches any of them to the same ramp.

## Verification

- Core: Catch2 tests in `rock-hero-common/core/tests/test_highway_projection.cpp` for ramp
  derivation (slide match by exact grid position, margin morph, clamping when FHPs crowd,
  first-FHP nut morph, slide-out exclusion) and for the continuous window query (edge
  interpolation, independent width morph, ease endpoints).
- Renderer: build + existing suites via `.agents/rockhero-build.ps1`; visual confirmation in
  the editor 3D preview against a slide-heavy corpus chart (shift slide, legato slide,
  unpitched slide-out, an open drone across an FHP change, a wide-chord width morph).
