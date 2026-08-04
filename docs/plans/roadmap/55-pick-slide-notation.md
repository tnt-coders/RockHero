# Plan 55 — Pick-Slide Notation

**Status**: Phases 1–2 complete 2026-08-03 (note-carried model, format, rules, import — built and
green). Phase 3 executes next behind one open sight gate: the 55-Q1 head pick from mockup sheets.
55-Q2 (authoring) largely dissolved into ordinary note verbs; its remainder folds into plan 40.
Baseline `master @ 84bdfe32`.

## Goal

Pick slides (pick scrapes) are a first-class notated technique: a **note whose attack is a
scrape** — right-hand, fret-hand-transparent, occupying its string and participating in every
note rule — with lossless Guitar Pro import, ordinary note authoring, and a dedicated visual
language on the 3D highway and the 2D tab.

## The decision trail (2026-08-02/03)

1. **The borrowed-vocabulary experiment failed on sight.** Mapping pick slides onto existing
   note vocabulary (full mute + tremolo + unpitched trail-off) was built and REVERTED the same
   day: GP notates the gesture on fret-0 dead strings, so the trail-off had no travel and read
   as flat muted bars (Van Halen import, measure 20).
2. **It is a right-hand technique** (user rule): the fretting hand is uninvolved, so nothing
   about it may move, anchor, or dip the FHP window — the tap-transparency precedent, now the
   shared `rightHandOnset` predicate in the generator.
3. **The gesture carries a neck-position path** (user extension, superseding "direction is the
   only semantic content"): editable like regular slides — down to a neck position, then up, in
   one chain — so it stores a start fret plus a `SlideWaypoint` path whose frets are pick
   coordinates. Direction is derived per segment. The path's last offset IS the span (a scrape
   cannot ring past its path without sitting still), so no separate duration can disagree.
   GP encodes only direction, so import synthesizes a default path the user reshapes.
4. **The visual answer is the composition, interrogated and kept.** Evidence gathered on the
   way: the 4,100-arrangement local reference corpus shows **no standard composite** in its
   chart format (816 tremolo+unpitched suspects scatter across 8+ encodings; the
   tap+tremolo+slide triple covers ~31%; zero fret-0 unpitched slides); the printed-tab
   convention is an X notehead + **wavy slanted line** ("P.S." text is optional reinforcement,
   rejected here as unaggressive); BandFuse never charted scrapes; a dedicated "noise ribbon"
   counter-proposal was rejected. The composition holds up because the path makes the slide
   component literally true.
5. **Pick slides are NOTES, not a parallel stream** (user call, reversing the same-day separate
   `Chart::pick_slides` stream, which was built, then deleted). The deciding arguments: a
   scrape MUST occupy its string (it silences what it crosses — collision and sustain
   truncation are required, not hazards); slid tapped notes already established right-hand fret
   semantics inside the note stream; converting notes to/from scrapes should be trivial; and a
   separate stream re-implements the note machinery (selection, undo, range ops, distance
   rules) that scrapes need anyway. Model: **`NoteAttack::PickSlide`** — the attack axis is the
   honest home (structurally excludes the other attack kinds) and the tab's attack-icon slot
   becomes the natural 2D marker.
   - **Override, not forbid** (user design): other techniques may hold values in memory while
     the attack is a pick slide — switching back restores them within the session — but
     projections suppress them and the document writer omits them, so a saved scrape is always
     clean and validation rejects a hand-made file that is not.
   - **Accepted trades**, chosen deliberately over the stream's structural purity: technique
     exclusions are validation conjunctions (`InvalidPickSlide`), `sustain == slides.back().offset`
     is a validated invariant rather than an impossibility, transposition/retype needs a
     pick-slide special case (editor phase), and scrape-through-scrape follows the shared
     deliberate-hold rule instead of a bespoke one-hand hard cap.
   - The interim two-stream binding-timeline refactor was deleted with the stream: pick-slide
     notes ride the original trim scan natively. The one trim twist that remains: the path is
     gesture geometry like a slide-out, so the margin trim compresses its final point instead
     of flooring the tail on it.

## Non-goals

- **Measure-3-style figures**: dead notes with ordinary slide-out flags are LEFT-hand muted
  hand slides and never reclassify (pinned by regression test).
- **Detection** (plan 22's matrix) and a **right-hand placement cue** (kin to 25-Q5).

## Shipped (Phases 1–2, 2026-08-03)

- `NoteAttack::PickSlide` + attack token `pickSlide`; writer omits overridden technique keys on
  scrape notes; rules: no other techniques in a saved document, non-empty always-traveling path
  (consecutive neck positions strictly differ, start fret included), path end == sustain.
- Import: carriers (flags 64/128) convert in place — shed the mute, gain the attack and the
  corpus-derived default path (**down 17 → 3, up 3 → 17**; ~70% of corpus down-slides start at
  fret 13+ and ~80% end at or below fret 7). The lowest-string simultaneous carrier survives
  with the longest span; conflicting directions drop with a conversion note. Scrapes then ride
  the ordinary distance rules (margin trims both directions, deliberate holds) — pinned by
  tests — and `rightHandOnset` keeps the FHP generator, span derivation, ring rules, and
  anchoring blind to them (transparency pinned by the carrier-vs-rest FHP equality test).

## Decision gates

- **55-Q1 — the head. SIGNED 2026-08-04: A-refined on both surfaces**, chosen through two
  adversarial panel rounds whose record matters:
  - A first panel (three lenses + critic) unanimously preferred an X-shaped head — built on two
    FALSE premises the user caught: it assumed the 3D head could be numberless (the digit is
    the only early-read position channel while perspective compresses board-X on approach, and
    a digitless gem would be the game's only one), and it called the V-family tie a false
    cognate "crossing hands" (wrong: `NoteAttack::Tap` IS the right-hand tap, and
    `rightHandOnset = Tap || PickSlide` is the code's own grouping).
  - A corrected-premise adversarial pass then found the 2D icon vocabulary already encodes the
    hand in FILL COLOR (hammer/pull white = fretting hand; tap/slap/pop black = picking hand,
    `drawAttackIcon`), that tap gems already made gem digits hand-agnostic position
    instructions, and that an X head would collide with the full-mute X-plus-boxed-digit in
    the same acceptance song (measures 3 and 20) — reviving the confusion the first reverted
    experiment died of. Misread directions also favor A: scrape-read-as-tap stages the right
    hand at the neck at the digit; X-read-as-mute keeps it picking at home.
  - **The signed design**: 3D = digit-bearing string-colored gem + narrow whitened V on the
    face (the tap cell's V, whitened/narrowed — the palm→full intensification grammar inside
    the right-hand family; currently rendered as the tap cell in a whitened tint, the bespoke
    narrower cell is a queued atlas edit); 2D = normal digit-bearing head + black V with the
    family border (0xC0C0C0) in the attack-icon slot (joining the picking-hand black-fill icon
    family).
  - **Superseded on sight 2026-08-04 (user redesign): the muted-note composite plus the V,
    on BOTH surfaces.** A scrape head dresses exactly like a full-muted note — 3D: the tech
    gem base with its digit under the upright full-mute X marker (an interim X-as-head-base
    hybrid was sighted and retired the same day); 2D: the mute X over the digit with the
    full-mute readability plate — with the V cue hovering over the X pointing down at the
    note (3D: upright through the roll flip, tip dipped into the X's upper notch but never
    its center crossing; 2D: white mute-styled V in the X's band weight, tip dipped one band
    into the head, compact so stacked scrapes on adjacent strings stay clear). The V cell was
    reshaped through sighted rounds: arms hold a steeper-than-X angle (dxdy 0.517, three
    quarters of the original tap-derived width), the height then shrank at that locked angle
    with the tip anchored, the arm tops end in the X's own squared caps (flat hard cuts — the
    prior perpendicular tips read as soft points), and the glyph centers on the X's measured
    solid axis, half a pixel right of the cell's middle. The original X-collision objection
    dissolved once the V and the glowing tremolo tail carry the scrape identity over the mute
    reading.
- **Settled rendering (both surfaces), simplified 2026-08-04 (user redesign)**: the path
  renders through the unpitched slide machinery (dimmed glide, head rides the path, no board
  furniture, no hand-window contribution — all by the projection marking scrape waypoints
  unpitched), and the tail is the **ordinary tremolo vocabulary, worn outright** — no bespoke
  waveform and no differentiator, because `tremolo` itself was redefined (user 2026-08-04):
  the teeth mean UNMEASURED noise picking (as fast as possible, no real timing), the charting
  standard spells out measured repetition as discrete notes, and a scrape IS that noise
  dragged along the string, so sharing the notation is the point. Import follows the
  standard: GP's measured tremolo marks spell out into their strokes at the marked
  subdivision, a bent tremolo becoming progressively larger prebent picks (each stroke
  samples the master curve at its onset — user model 2026-08-04); only slide-entangled
  beats — the pick-slide carriers included — keep the mark as noise with a conversion note
  (lifecycle guide rule 20). 3D: the tail rides
  `highwayTremoloWobble` with the head shaking in step; 2D: the plain tremolo gem strip with
  slide diagonals over it carrying the travel and per-leg target chips.
- **Tooth geometry, sharpened 2026-08-04** (user: the zigzag must read much sharper and more
  compressed). What the eye reads is the tooth's ASPECT — advance against swing — not depth
  alone, so the wave went from a 60 ms / 0.75-half-width tooth (a 5:1 lazy ripple) to
  **25 ms / 1.25 half-widths**, roughly 1:1, with the head at half depth like the vibrato
  head. The load-bearing half of the change is SAMPLING: a triangle is piecewise linear, so
  its turning points are the only samples its shape needs, and the uniform screen-space grid
  alone rounded every apex unevenly and aliased outright at the tighter pitch —
  `makeHighwayTailSampleTimes` now folds the wobble's turning points in beside the bend and
  slide control points (uniform samples stay capped; turning points ride on top, bounded by
  the clipped visible span). Corroborated by an independent research pass over notation
  engraving metrics, open charting-tool renderers, and pixel measurements of published
  reference art: sharp mitered corners with no fillet, constant ribbon width through every
  fold, spatially uniform pitch, and no amplitude decay — and the shipped swept envelope
  (0.75 note-head widths) lands within a few percent of the measured reference envelope
  (~0.72), i.e. wide enough that consecutive legs clear each other and open dark notches,
  while never invading the neighbouring lane. The end-anchor taper stays: it is an anchoring
  device, not an energy decay, and its damped teeth sit under the head and at the horizon. Deleted along the
  way, each after a sighted round: the bespoke serrated/chirped scrape wave and 2D decaying
  wavy diagonals ("not jagged enough" ↔ "too random"), the desaturated 2D tail, the white
  tail-edge frames, a baked glow halo (forked the mute-X art, could not match the tail's
  gradient), and finally a full renderer-side `texture_glow` blur program — built through all
  six shader touchpoints, then removed the same day when the noise redefinition made every
  white-glow differentiator moot. The moving right-hand light sweeps its glide segments with
  the arrival waypoint's own ease (stations carry an unpitched flag) at span-scaled slice
  density — the fixed six slices and pitched ease that served tapped glides faceted a
  scrape's dozen-fret legs into rough edges. The root impact shards stay a queued flourish.
- **55-Q2 — authoring.** Mostly dissolved by the note-carried model: scrapes select, move,
  delete, and undo as ordinary notes. Remaining verbs for plan 40 Phase 5's technique surface:
  the attack toggle to/from `PickSlide` (synthesizing the default path on entry, restoring
  overridden techniques within the session on exit) and path-waypoint editing via the ordinary
  slide-editing patterns. Transposition/retype gains its pick-slide special case there.

## Remaining phases

3. **Projection + 2D tab. FIRST CUT SHIPPED 2026-08-04.** Both projections suppress the latent
   overrides (the one seam) and mark scrape waypoints unpitched/unlinked — which routes the
   whole 3D path through the existing unpitched-glide machinery and keeps scrape legs out of
   the hand window's slide-locked ramps (pinned by test alongside no-tap-lighting). The tab
   now draws the 2026-08-04 composite: mute X + plate over the digit, white mute-styled V
   above the head, white-framed tremolo strip with plain slide diagonals, per-leg chips.
   **Exit.** Tab renders a down-then-up chain per the signed head. **Verify.** Build; suites —
   green 2026-08-04.
4. **Highway treatment. SIGHT ROUNDS 1-2 SHIPPED 2026-08-04** (user directions after the first
   sightings): a REAL atlas cell — the atlas grew to 4x5, was re-sorted into semantic rows
   (head bases + emphasis / fretting-hand brackets + legato / picking-hand family / damping +
   timbre / bend + growth, superseding the Charter reference order — user signed), and the
   pick-slide V was authored, never stretched — after several failed methods (LUT profile
   transfer read blurry; pixel-copying the X's arms staggered, since the X's 31x33 slope is
   irrational to the grid), the landed construction is ANALYTIC: straight arm lines at the
   X's own slope rendered at 8x through the X's super-resolved cross-section (17 perpendicular
   cuts, plateau-centered, median-folded), Chebyshev-capped squared tips, a mirror-mitre
   vertex, and a two-ended correction loop pinning the V's SOLID extent to the tap V's exact
   rows (verified in-bake, hard-fail otherwise). The tail treatment then went through trials —
   an X-mute composite under the V (reverted), a second incommensurate grit layer (reverted,
   read as noise), a bespoke serrated chirped scrape wave (shipped, then deleted in the
   2026-08-04 simplification) — and landed on the ordinary tremolo teeth framed in glowing
   white ribbon edges, with the head reborn as the full-mute X under a near-touching V (see
   the settled rendering above). The scrape drives a **moving right-hand light** through the
   tap-light machinery — the light path rides the waypoint travel exactly as the fret-hand
   window rides left-hand glides, with the tap's margin rise.
   Still queued: the root impact shards. Sight-iterate on Van Halen measure 20.
   **Exit.** User signs the treatment on sight. **Verify.** Build; sight pass; plan 54 color
   coordination.
5. **Editor verbs + acceptance.** 55-Q2's remainder; then the acceptance bundle.
   **Exit.** Acceptance below in full. **Verify.** Build, full suites, pre-commit, the
   two-measure sight pass.

## Acceptance

- Van Halen measure 20 renders the chosen treatment; measure 3 imports byte-identical.
- A chart round-trips scrape notes (latent overrides cleared on save); the editor can author,
  reshape, and remove them, and toggling the attack away and back within a session restores
  overridden techniques.
- FHP output is identical with scrape notes present or absent (the transparency invariant, as
  a test).
