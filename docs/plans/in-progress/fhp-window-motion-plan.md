# FHP Window Motion Plan

Status: PLANNED 2026-07-23. Design agreed with the user (slide-locked ramps, margin morphs,
shortened crowded transitions, shared ease); implementation not started.

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
- The window transition region gets its own subtle left/right edge rails (reusing the
  shape-rail core-plus-fade-wings cross-section at lower intensity): mid-transition the
  boundary sits between fret lines, so without an owned edge the sheared region has nothing
  defining it.

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

1. **FHP lane highlight — the window is a mask, the board owns the lanes (user model,
   2026-07-23).** Fret lines always stay straight, and the plain/dotted inlay tint is a fixed
   board-aligned property of each lane; the highlight reveals each lane's own tint wherever
   the lane falls inside the window region. Steady spans therefore keep today's per-lane
   quads unchanged. In a ramp's z-range, interior lanes still emit full straight-sided quads
   in their own tints; only the one or two lanes being entered/exited at each moving edge
   emit trapezoids clipped against the eased border position at the slice ends (z-sliced with
   the sampling idiom modulated tails already use). Nothing about the striping is
   compromised, and no lane geometry ever shears except at the border crossing.
2. **Window border rails.** Left and right edges of the lit region, full length (steady and
   ramp spans alike, so the chamfer connects visibly to its neighbors), styled as a subdued
   shape-rail cross-section. The sliding border is the visible moving element — the reason
   the window needs an owned edge at all — while the fret lines behind it never bend.
3. **Open-string note tails.** Today the band is sampled once at `note.start_seconds` and held
   flat for the whole tail. Open (and open modulated) tails move onto the z-sampled ribbon
   path with band stations evaluated per sample from the continuous query, so they shear with
   the window. Note the deliberate behavior change: a long open drone ringing across *any*
   later FHP change now travels with the hand instead of staying where it started.
4. **Hand-shape / arpeggio span rails.** Rails currently sample the window once at
   `shape.start_seconds`; they become z-sampled against the continuous query so a mid-span
   window move shears them identically.
5. **Single-instant consumers** (beat bars, chord/arpeggio boxes, fingering panel, section
   furniture): mechanical swap to the continuous query at their own z — they draw at the
   interpolated extent automatically, no geometry rework.
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
