# Fret hit-glow as a real additive light effect

**Status:** in progress — design approved; the 2026-07-30 pre-implementation review's fixes are
folded in below (submission point, envelope home, release/fade invariant) and implementation is
under way.
**Scope:** the editor app's 3D preview (shared highway renderer, so it also lands in the game
highway). The trigger here is deterministic *note arrival*; a later input-gated game version reuses
the same light pass unchanged (only the trigger source changes).

## Goal

Replace the current flat colour-replace fret hit-flash with a real **additive light** effect (bgfx +
shaders) that fires when a note reaches the fretboard and reads as a **100%-perfect strike**. The
light must:

1. **Add brightness** so a strike pops identically on an already-lit (active-hand) fret line and an
   unlit one — fixing today's masking bug where a hit on a covered fret barely changes luminance.
2. **Peak exactly at the crossing** — the instant the note meets the fretboard (`now_seconds ==
   note.start_seconds`), not a frame later.
3. Read as a **discrete per-note pop** even in the fastest sections, instead of smearing into a
   sustained glow.
4. Attach to the **right geometry per note type**: single note → its fret lines, chord → the chord
   box's left and right frets (interior dark), open string → both edges of the fret-hand-position
   (FHP) window.

## Why (what's wrong today)

The current flash pass (`highway_renderer.cpp:3621-3654`) writes a weight into a global per-fret-line
array via `std::max`, then the fret-line pass mixes the line colour toward orange and thickens it.
That structure produces five confirmed problems:

- **Open strings never glow** — the pass skips `note.fret <= 0`, so an open strike lights nothing
  (`:3628`).
- **Chords don't read as chords** — every member lights its own `{fret-1, fret}` lines at the same
  weight, so a six-string barre looks identical to a single note; a gapped chord fills the lines
  between; a chord anchored by a low open string flashes only up the neck.
- **The "delay" in fast sections** — the `since <= 0` gate (`:3628`) skips the exact crossing frame,
  so the flash starts ~1 frame late and only decays (never peaks at arrival); and above ~150 BPM
  sixteenths the fixed 0.1 s flashes merge via `max` into a continuous shimmer with no crisp per-note
  pop.
- **Active-window masking (the biggest one)** — the flash *replaces* colour under alpha blending
  (`BLEND_FUNC(SRC_ALPHA, INV_SRC_ALPHA)`), so on a fret line already lit by the active hand window
  (light grey) the shift to orange is a near-zero luminance change. Because the hand is by
  construction over the notes being played, most hits land on active lines and pop the weakest.

All of these dissolve under one change: make the strike a **real light that adds luminance**, in its
own pass, attached to per-note geometry.

## Target behaviour (perfect hit on arrival)

Deterministic, editor-app: no detection, velocity, scoring, or input. A glow fires when the render
clock crosses the onset time, peaks at intensity 1.0 at the crossing for **every** note (a perfect
strike), and is a pure stateless function of `since = now - onset` recomputed each frame. **One glow
per onset** is the invariant.

**Colour and intensity (decided):** the light is **orange** (continuous with the existing
`g_fret_highlight_color`, and complementary to the blue-dominant board — blue FHP window light, teal
ribbons — so it pops). Being additive and peaking bright, its core naturally whites out on strong
hits (a white-hot orange flare). A chord uses the **same per-pixel intensity** as a single note and
lights only its box's two edge frets (2026-07-30 direction — the interior panel was built, seen,
and dropped): a strum reads as the box's frame flashing, tonally consistent with singles.

| Note type | Condition | Glow geometry |
|---|---|---|
| Single fretted | `fret > 0`, cluster `non_tap_count < 2` | Two additive soft strips over the fret lines `{fret-1, fret}` (`highwayFretLineX`), face bottom→top, z=0 |
| Chord (fretting hand) | cluster `non_tap_count >= 2` | Two additive soft strips at the chord box's **left and right frets** — the live window edges, shared with the open-string strips; the box interior stays deliberately dark (2026-07-30 direction, replacing an interior panel) |
| Chord (tapped) | `tap.count >= 2` | Two additive soft strips at the tapped box's edge lines `fret_low - 1` / `fret_high`, per tap onset; interior dark |
| Open string | `fret == 0`, cluster not boxed | Two additive soft strips at the two live FHP-window edges `current_window.low_line` / `.high_line` (`highwayHandWindowAt` at `now`), at their fractional swept X |
| Slide landing | pitched waypoint, either hand | Strips at the landing's fret lines `{wp.fret-1, wp.fret}` — a pitched waypoint is a fret arrival like a strike; unpitched trail-offs contribute nothing; no inter-onset clamp (landings are sparse; per-line max absorbs overlap) |
| Bend target | bend curve point ending a sloped segment | Strips at the planted fret's lines `{fret-1, fret}` — a pitch arrival the game scores hit-or-miss, so the editor's perfect play pops it; flat holds and the onset point are not arrivals |

Edge cases: an all-open strum that forms a boxed cluster routes to the one window-edge glow — the
glow pass carries its own routing predicates (the note pass's `in_chord` guard at `:3038` governs
only the open-note span bar and does not apply to the glow); a fretted note under a simultaneous
tap counts toward the group but not `non_tap_count`, so it lights its own fret lines like a single
note (matching the box pass). An open string with no FHP yet falls back to the default window
(lines 0 and 4).

## Rendering approach — additive soft-sprite reusing the existing light shader

This is the constraint-driven choice, not merely the smallest diff. The board is one
`ViewMode::Sequential` pass with depth-test but **no depth write** — board content never writes Z, so
an additive submission's fragments always pass the depth test and its colour contribution is
order-independent. The `window_light` shader is *already* a soft, per-event, vertex-alpha-enveloped
2D light sprite (its fragment shader outputs `vec4(rgb, a * softMask)` **unpremultiplied**), and the
renderer already reuses it for the FHP window (`:1472`) and the per-onset tapping-hand light
(`:1655`). The **only** thing separating today's soft-but-alpha-blended light from a real additive
light is the blend function.

So the whole effect needs:

1. **One new blend-state constant** beside `g_blended_state` (`:135`):
   ```
   g_additive_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS
                    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE)
                    | BGFX_STATE_MSAA;
   ```
   No `WRITE_Z` (matches the board), no `WRITE_A` (don't stomp dest alpha). **Pitfall:** bgfx's
   `BGFX_STATE_BLEND_ADD` is `FUNC(ONE, ONE)` — it ignores alpha and would hard-edge the soft mask.
   We need `SRC_ALPHA → ONE`, which adds `rgb * a * softMask` on top of the existing pixel, strictly
   *increasing* luminance regardless of the active-fret baseline — that is exactly the masking fix.
2. **One new late glow pass** in `draw()` that builds the per-onset soft geometry and submits it via
   the existing `submitBatch(…, window_light_program.get(), nullptr, g_board_view, g_additive_state)`
   — `submitBatch` already takes a render-state argument, so no signature change. Submitted as the
   **last board-view pass** — after the glyph text batch that closes `draw()`, not merely after the
   fret lines: the inlay skin composites premultiplied (`ONE, INV_SRC_ALPHA`; an opaque dot texel
   multiplies whatever is underneath by ≈0, punching dark dot silhouettes through a glow drawn
   earlier) and the fingering panel alpha-blends exactly at the hit line, so anything drawn after
   the glow would dim or hole it. Last-in-view, the light adds over everything it should brighten.

Reuses `window_light_program`, its falloff uniform, `PosColorUvVertex`/`posColorUvLayout`, and the
per-edge signed-distance UV convention — **no new `.sc` shader, no shader-set member, no CMake
program-list edit, no shader-enum value, no game/editor loader changes.** (A dedicated glow shader or
a scene-wide bloom post-process were considered and rejected — see Open decisions — as large
cross-module infra for a look the reused sprite already delivers.) The window-light mask is
horizontal-only, so vertical softness is graded into vertex alpha top-to-bottom; acceptable for
strips and panels.

## Timing model

Two independent problems; additive blend alone fixes neither — both are CPU-envelope concerns.

**Peak at the crossing — strict zero pre-glow (decided).** The light is zero before arrival, jumps to
full intensity on the first frame at/after the crossing, then fades 1→0 over `[0, t_release]`; gate
on `since >= 0 && since < t_release`, with the fade equal to 1.0 at `since == 0`. There is **no
pre-crossing attack** — nothing lights before the note lands. The fade is a **smoothstep over the
remaining fraction** (2026-07-30, replacing a front-loaded quadratic that read as a blink): a
momentary bright hold at the crossing (zero initial slope), an even mid fade, and a soft landing at
zero — which also makes the first drawn frame after an unaligned crossing render ≈1.0 rather than
noticeably decayed.

**Discrete per-note in fast sections.** Removing the global `max` array (per-onset emission) fixes
half of it. For the rest, keep a per-onset attack-then-decay envelope that returns to ~0, and make the
**release inter-onset-aware**: clamp each onset's release end to `min(start + nominal, next_onset_on_
same_geometry - guard)`, where "same geometry" means the same fret-line pair for singles, the same
tapped-box edges for taps, or the shared window-edge strips (strums and lone opens clamp against
each other — they relight the same two frets). That guarantees a dark trough before the next pop on
that geometry *at any tempo*,
while a lone or spaced note still gets its full decay — implicitly tempo-relative from onset spacing,
with no BPM read (consistent with the derive-don't-author principle). Resolve overlapping onsets on
shared geometry by **max, not sum**, so tails can't wash past 1.0 and erase the trough.

**Chords** drive off the onset cluster: one `since = now - cluster_start`, one envelope, one
edge-frets pop, with the fire predicate (`non_tap_count >= 2`) aligned to the box-draw predicate so
glow and box never disagree.

**The glow pass owns its scan window.** Glow tails outlive the passed-note fade (a sustainless note
leaves the renderer's visible range ~`g_passed_fade_seconds` (0.15 s) after crossing), so the pass
does not reuse the visible range: it binary-searches `state.notes` for onsets in
`[now - release, now + clamp_horizon]` and walks onset clusters directly. The release therefore
tunes freely — it is not bounded by the fade — and the look-ahead lets strikes at the hit line
clamp against strikes still approaching.

## Phased implementation

**Phase 0 — additive blend state (foundation, no visible change).** Add `g_additive_state` (`:135`)
with the comment about the `BLEND_ADD` pitfall; confirm `submitBatch` already accepts the render
state. Build only.

**Phase 1 — per-onset glow envelope (timing).** The pure envelope math lives as a new headless unit
beside the other highway math in common/core (the `highwayHandWindowAt` precedent), because a
renderer-file-local helper would be unreachable from any test:
`rock_hero/common/core/highway/highway_hit_glow.h` / `.cpp` with `highwayHitGlowIntensity(since,
release)` — 0 for `since < 0`, a smoothstep fade from 1.0 at `since == 0` to 0 at `since ==
release` — and the inter-onset release clamp `highwayHitGlowRelease(nominal, guard, spacing)`
with a half-spacing floor so ultra-dense charts keep both a pop and a trough. No pre-crossing attack
(strict zero pre-glow). Add renderer tuning constants (`g_hit_glow_release_seconds ≈ 0.2`, a trough
guard; the renderer constants are the authoritative current values); retire `g_fret_flash_seconds`
(`:127`). Overlaps on shared geometry resolve by max.
Unit-test the envelope shape (1.0 at `since==0`, 0 for `since<0` and at `since==release`, monotone
decay, the clamp) in the **common/core** Catch2 suite beside `test_highway_window.cpp`. The CMake
source-list additions for the new TU and test file are part of this phase.

**Phase 2 — single-note additive fret-line light.** Add the late additive pass building soft strips
over `{fret-1, fret}` with the envelope in vertex alpha and soft edges in texcoords (mirroring the
FHP/tap light). Route only singles here (`fret > 0`, group `non_tap_count < 2`), gating on `since >= 0 && since <
release_clamped` (full at the crossing frame, no pre-glow). Revert the fret-line
pass (`3649-3654`) to just the inactive↔active state colour — drop the orange mix and the
`half *= (1 + 3*weight)` thickening, and remove the old `flash[]` array. All strike energy now lives
in the additive pass. Build + verify peak-at-arrival on lit and unlit lines and discrete pops on fast
sixteenths.

**Phase 3 — chord edge-frets glow.** In the additive pass, a boxed onset cluster
(`non_tap_count >= 2`) emits the shared window-edge strips (the chord box's left and right frets,
`handWindowXAt` at `now`), keyed on the cluster onset; the box interior stays dark. (First built as
one interior panel per the original plan; seen 2026-07-30 and replaced with the edge frets — what,
if anything, the interior does instead is an open decision.) Tapped chords (`tap.count >= 2`) light
their box's edge lines `fret_low - 1` / `fret_high`. Suppress fretted members of a boxed cluster
from the Phase 2 single path so a strum lights only its edges. Verify one edge pop per strum, no
double-light.

**Phase 4 — open-string FHP-edge light.** For a visible open note (`fret == 0`) whose group is not
boxed, emit two soft strips at `highwayFretLineX(current_window.low_line/.high_line)`
(`highwayHandWindowAt` at `now`, `:1213`), keyed on `note.start_seconds`. Route all-open boxed strums
to the Phase 3 box glow. Handle the empty-FHP default window. Verify open notes now glow and slide
with the window mid-transition.

**Phase 5 — tuning, dense-chart validation, cleanup.** Tune glow rgb (orange, white-hot core),
amplitude, release, and falloff on a dense chart; confirm additive overlap doesn't hard-clip toward white in the fastest
sections. Confirm submission order (after fret lines, boxes, heads). Confirm the omitted `WRITE_A`
doesn't break the overlay composite or the premultiplied inlay state. Remove any now-dead constants
(`g_fret_flash_seconds`, `g_fret_highlight_color` if unused). Run the sanctioned verify bundle.

## Files touched

- `rock-hero-common/core/include/rock_hero/common/core/highway/highway_hit_glow.h` +
  `rock-hero-common/core/src/highway/highway_hit_glow.cpp` — the pure glow envelope and
  release-clamp math (headless, beside the other highway math).
- `rock-hero-common/core/tests/test_highway_hit_glow.cpp` — Catch2 coverage for the glow envelope
  (1.0 at the crossing, zero before it and at the release end, inter-onset release clamp).
- `rock-hero-common/core/CMakeLists.txt` + `rock-hero-common/core/tests/CMakeLists.txt` — the new
  TU and test-file source-list lines.
- `rock-hero-common/ui/src/highway/highway_renderer.cpp` — all rendering changes (the additive
  state constant, the tuning constants, the new last-in-view additive glow pass, single-note
  routing + the revert of the flash colour-mix/thickening, chord-group and tap-onset box panels,
  open-note FHP-edge strips, removal of the `flash[]` array).
- **Not touched** (explicitly avoided): no new `.sc` shader, no shader-set member, no CMake shader
  program-list edit, no shader-enum value, no game/editor shader-loader edits.

## Risks

- **Additive clip to white** where many strikes stack in the fastest sections (no HDR/tonemap).
  Mitigate via envelope amplitude, short decay, and max (not sum) resolution; verify on a dense chart.
  Tuning, not architecture.
- **Discrete pop is an envelope problem**, not solved by additive blend alone — the inter-onset
  release clamp is the robust fallback for the very fastest charts (a 16th at 150 BPM is 0.1 s,
  exactly today's flash length, which is why they fuse).
- **Submission order** — the additive pass must be the *last* board-view submission: the
  premultiplied inlay skin, the fingering panel, and the glyph text all land after the fret lines,
  and any of them drawn after the glow dims or holes it (the inlay's `ONE, INV_SRC_ALPHA` would
  punch dark dot silhouettes through the light).
- **Release vs. passed-note fade** — resolved: the glow pass binary-searches its own onset window
  over `state.notes` instead of reusing the visible range, so the release tunes freely.
- **`WRITE_A` omission** — confirm nothing downstream (overlay composite, premultiplied inlay) depends
  on the board dest alpha the glow would otherwise leave intact.
- **Depth interaction is benign only because board content writes no Z** — flag for anyone who later
  enables board depth-write.
- **Open-edge tracking** — the edges come from the live (fractional, sliding) FHP window; confirm the
  decay tail should follow the hand as it slides (they coincide at the crossing).

## Decided

- **Chord glow** — only the box's left and right frets glow (the shared window-edge strips, same
  per-pixel intensity as a single note); the interior panel was built, seen 2026-07-30, and
  dropped — the box interior stays dark.
- **Envelope shape** — smoothstep fade over the remaining fraction (momentary hold at the
  crossing, even mid fade, soft landing); the front-loaded quadratic at 90 ms read as a blink, and
  0.2 s still felt too brief (release now ~0.35 s — the renderer constants are authoritative).
- **Colour** — orange (white-hot core on strong hits), for contrast against the blue-dominant board.
- **Peak timing** — strict zero pre-glow: full at the crossing frame, no pre-arrival attack.
- **Rendering approach** — additive soft-sprite reusing the `window_light` shader (Approach A);
  dedicated radial glow shader and scene-wide bloom recorded as future options only if art direction
  later wants a true radial silhouette or scene-wide light-bleed.
- **Decay policy** — the inter-onset-aware clamp (tempo-relative, derived from onset spacing, no
  BPM read), with a half-spacing floor for ultra-dense charts; a flat wall-clock duration was
  rejected because it is exactly what fuses today's fast sections.
- **Envelope home** — the pure envelope/clamp math lives in common/core beside the other highway
  math, unit-tested in the core suite (a renderer-internal helper is unreachable from any test).
- **Slide landings and bend targets glow** (2026-07-30). The governing rule: **every scored
  arrival pops the glow at its geometry** — the game registers these hit-or-miss, and the editor
  previews perfect play. A pitched slide waypoint is a fret arrival (the finger lands on a new
  fret, the tail kinks there, the FHP window ramps there) and pops the landing's lines, whichever
  hand slides; unpitched trail-offs contribute nothing. A bend target is a pitch arrival on the
  fret the finger stays planted on and pops that same pair — each curve point ending a sloped
  segment (bend reached, release completed), never flat holds or the onset. (An earlier
  bends-stay-dark call keyed on "no fret change"; the hit/miss frame superseded it — the pop is
  success feedback, not a ghost re-strike.) Both use the sustain-aware range query so an
  end-of-sustain arrival outlives the passed-note fade, and neither clamps — they are sparse, and
  the per-line max absorbs overlap.
- **Retire the old flash machinery** — `g_fret_flash_seconds` and the fret-line colour-mix/
  thickening go; the additive pass owns all strike brightening (the glow gets its own colour
  constant, so `g_fret_highlight_color` retires with the mix).

## Open decisions (still to confirm)

1. **Envelope feel** — iterating by eye (90 ms → 200 ms → 350 ms so far; the strip width came in
   from falloff 0.35 to 0.2 as too wide); the renderer constants are the authoritative current
   tune. Refine amplitude, release, and falloff on a dense chart in Phase 5.
2. **Chord-box interior treatment** — the interior gets no glow for now (only the edge frets
   flash); whether a strum wants something else inside the frame, and what, is open — revisit
   after the edge-frets look settles.
3. **Tap-light distinctness** — the tapping-hand light is already a warm-leaning blue, so if a
   fretting-hand strike and a tap should read as clearly different cues, the strike may want a
   distinctly whiter-cored or more red/gold orange. A Phase 5 tuning call.
4. **Bend waypoint posts (proposed follow-up, raised 2026-07-30).** Slide landings already get
   approach furniture on the floor; bend targets could get their own posts under each waypoint,
   rising proportional to the bend's semitones at that point — pre-visualizing how far each push
   goes before it reaches the hit line, the way slide posts pre-visualize landings. Not part of
   the glow pass (note-furniture geometry); sized as its own small change.
5. **Paused-transport look (follow-up — decide after seeing it land).** The envelope is stateless in
   `since` with a `since >= 0` gate, so parking the cursor exactly on an onset (the snap-to-note
   gesture) shows a static, full-intensity glow while paused; today's flash shows nothing there. A
   static full glow may look odd — live with the stateless behavior first, then decide whether the
   glow should gate on the transport playing.
