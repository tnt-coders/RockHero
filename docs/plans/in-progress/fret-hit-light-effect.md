# Fret hit-glow as a real additive light effect

**Status:** in progress — design approved-in-principle, open decisions below to confirm before coding.
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
   box, open string → both edges of the fret-hand-position (FHP) window.

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
hits (a white-hot orange flare). A chord uses the **same per-pixel intensity** as a single note — the
chord box is a much larger lit area, so a strum reads bigger without extra brightness, keeping single
notes and chords tonally consistent.

| Note type | Condition | Glow geometry |
|---|---|---|
| Single fretted | `fret > 0`, group `non_tap_count < 2` | Two additive soft strips over the fret lines `{fret-1, fret}` (`highwayFretLineX`), face bottom→top, z=0 |
| Chord (fretting hand) | group `non_tap_count >= 2` | **One** soft panel over the chord-box span `handWindowXAt(state, max(start,now), …)`, y=0→box top, at the box z — driven once per `ChordGroup`, not per member |
| Chord (tapped) | `tap.count >= 2` | **One** soft panel over the tapped-box span `highwayFretLineX(fret_low-1 … fret_high)`, per tap onset |
| Open string | `fret == 0`, group not boxed | Two additive soft strips at the two live FHP-window edges `current_window.low_line` / `.high_line` (`highwayHandWindowAt` at `now`), at their fractional swept X |

Edge cases: an all-open strum that forms a boxed group routes to the one box glow (per-member edge
lights suppressed by the existing `in_chord` guard at `:3038`); a fretted note under a simultaneous
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
   — `submitBatch` already takes a render-state argument, so no signature change. Submitted **after**
   the fret lines, chord boxes, and note heads so it brightens them.

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
full intensity on the first frame at/after the crossing, then decays 1→0 over `[0, t_release]`; gate
on `since >= 0 && since < t_release`, with the decay equal to 1.0 at `since == 0`. There is **no
pre-crossing attack** — nothing lights before the note lands. Accepted tradeoff: on frames not aligned
to the exact crossing, the first drawn frame is `since ∈ (0, dt]`, so it renders `decay(since)`
(~0.9 at 60 fps) rather than a literal 1.0 — the pop can read a hair softer, which is preferred over
any pre-arrival glow.

**Discrete per-note in fast sections.** Removing the global `max` array (per-onset emission) fixes
half of it. For the rest, keep a per-onset attack-then-decay envelope that returns to ~0, and make the
**release inter-onset-aware**: clamp each onset's release end to `min(start + nominal, next_onset_on_
same_geometry - guard)`, where "same geometry" means the same fret-line pair / same chord box / same
open-edge pair. That guarantees a dark trough before the next pop on that geometry *at any tempo*,
while a lone or spaced note still gets its full decay — implicitly tempo-relative from onset spacing,
with no BPM read (consistent with the derive-don't-author principle). Resolve overlapping onsets on
shared geometry by **max, not sum**, so tails can't wash past 1.0 and erase the trough.

**Chords** drive off the group: one `since = now - group.start_seconds`, one envelope, one box pop,
with the fire predicate (`non_tap_count >= 2`) aligned to the box-draw predicate so glow and box never
disagree.

## Phased implementation

**Phase 0 — additive blend state (foundation, no visible change).** Add `g_additive_state` (`:135`)
with the comment about the `BLEND_ADD` pitfall; confirm `submitBatch` already accepts the render
state. Build only.

**Phase 1 — per-onset glow envelope (timing).** Add an `intensity(since, t_release)` helper: 0 for
`since < 0`, else a fast decay from 1.0 at `since == 0` to 0 at `since == t_release` (sqrt or
`(1-t)^2`). No pre-crossing attack (strict zero pre-glow). Add tuning constants
(`g_hit_glow_release_seconds ≈ 0.09`, a trough guard); retire/replace `g_fret_flash_seconds`
(`:127`). Implement the inter-onset release clamp (next-onset-on-same-geometry) with max resolution.
Unit-test the envelope shape (1.0 at `since==0`, 0 for `since<0` and at `since==t_release`, monotone
decay) in the common/ui Catch2 suite.

**Phase 2 — single-note additive fret-line light.** Add the late additive pass building soft strips
over `{fret-1, fret}` with the envelope in vertex alpha and soft edges in texcoords (mirroring the
FHP/tap light). Route only singles here (`fret > 0`, group `non_tap_count < 2`), gating on `since >= 0 && since <
release_clamped` (full at the crossing frame, no pre-glow). Revert the fret-line
pass (`3649-3654`) to just the inactive↔active state colour — drop the orange mix and the
`half *= (1 + 3*weight)` thickening, and remove the old `flash[]` array. All strike energy now lives
in the additive pass. Build + verify peak-at-arrival on lit and unlit lines and discrete pops on fast
sixteenths.

**Phase 3 — chord-box additive panel.** In the additive pass, iterate `chord_groups`; for each with
`non_tap_count >= 2` emit one panel over `handWindowXAt(state, max(start, now), …)`, keyed on
`group.start_seconds`. Iterate `state.tap_onsets` for tapped chords (`count >= 2`). Suppress fretted
members of a boxed group from the Phase 2 single path so a strum lights only the box. Verify one box
pop per strum, no double-light.

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

- `rock-hero-common/ui/src/highway/highway_renderer.cpp` — all functional changes (the additive
  state constant, the envelope helper + constants, the new late additive glow pass, single-note
  routing + the revert of the flash colour-mix/thickening, chord-group and tap-onset box panels,
  open-note FHP-edge strips, removal of the `flash[]` array).
- `rock-hero-common/ui/tests/test_*.cpp` — Catch2 coverage for the glow envelope (1.0 at the
  crossing, zero before it and at the release end, inter-onset release clamp).
- **Not touched** (explicitly avoided): no new `.sc` shader, no shader-set member, no CMake
  program-list edit, no shader-enum value, no game/editor shader-loader edits.

## Risks

- **Additive clip to white** where many strikes stack in the fastest sections (no HDR/tonemap).
  Mitigate via envelope amplitude, short decay, and max (not sum) resolution; verify on a dense chart.
  Tuning, not architecture.
- **Discrete pop is an envelope problem**, not solved by additive blend alone — the inter-onset
  release clamp is the robust fallback for the very fastest charts (a 16th at 150 BPM is 0.1 s,
  exactly today's flash length, which is why they fuse).
- **Submission order** — the additive pass must submit after its targets within the Sequential board
  view (a single late pass satisfies this).
- **`WRITE_A` omission** — confirm nothing downstream (overlay composite, premultiplied inlay) depends
  on the board dest alpha the glow would otherwise leave intact.
- **Depth interaction is benign only because board content writes no Z** — flag for anyone who later
  enables board depth-write.
- **Open-edge tracking** — the edges come from the live (fractional, sliding) FHP window; confirm the
  decay tail should follow the hand as it slides (they coincide at the crossing).

## Decided

- **Chord glow size** — same per-pixel intensity as a single note; the box's larger area does the
  work (no per-chord brightness boost).
- **Colour** — orange (white-hot core on strong hits), for contrast against the blue-dominant board.
- **Peak timing** — strict zero pre-glow: full at the crossing frame, no pre-arrival attack.
- **Rendering approach** — additive soft-sprite reusing the `window_light` shader (Approach A);
  dedicated radial glow shader and scene-wide bloom recorded as future options only if art direction
  later wants a true radial silhouette or scene-wide light-bleed.

## Open decisions (still to confirm)

1. **Envelope feel** — release ~90 ms is a starting point tuned in Phase 5; and whether a chord box
   wants a slightly longer tail than a thin single strip.
2. **Decay policy** — the inter-onset-aware clamp (tempo-relative, derived from onset spacing, no BPM
   read) vs. a fixed wall-clock duration. The clamp is recommended for fast-section separation and
   fits the derive-don't-author principle, but adds a little logic — confirm you want it over a flat
   number.
3. **Tap-light distinctness** — the tapping-hand light is already a warm-leaning blue, so if a
   fretting-hand strike and a tap should read as clearly different cues, the strike may want a
   distinctly whiter-cored or more red/gold orange. A Phase 5 tuning call.
4. **Retire vs. keep** `g_fret_flash_seconds` / `g_fret_highlight_color` once the additive pass owns
   all strike brightening.
