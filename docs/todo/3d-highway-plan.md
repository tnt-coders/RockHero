# 3D Note Highway Plan (Game View)

Status: deferred plan, written 2026-07-06 after the 2D tablature lane shipped. Re-read the chart
format spec (`docs/in-progress/note-format-and-tablature-plan.md`) and current game-module state
before implementing; this document is designed to be implementable from scratch by a coding agent
without re-deriving the reference analysis.

## Goal

A Rocksmith-feeling 3D note highway for the game executable: the player looks down a
guitar-neck-shaped board scrolling toward them, with string-colored note heads on fret lanes,
sustain rails, technique glyphs, and a camera that keeps the playable fret region readable at all
times. Rendering is SDL3 + bgfx (not OpenGL); the chart format v2 (arrangement sidecar
`charts/<uuid>.chart.json`) is the single data source, and it already carries everything listed
here — no format changes are required by this plan.

Two reference points define "correct":

- **Rocksmith 2014** defines the feel: readable at speed, calm camera, the note "lands" on a
  fretboard pinned near the bottom of the screen.
- **[Charter](https://github.com/Lordszynencja/Charter)** (MIT) is the implementation reference.
  Its `preview3D` package was analyzed at source level (2026-07-06, files under
  `src/main/java/log/charter/gui/components/preview3D/`). Charter generally achieves the
  Rocksmith feel and its geometry/camera decisions are worth adopting; its implementation has
  measurable rough edges that this plan explicitly fixes rather than ports (see
  [Charter defects and what we do instead](#charter-defects-and-what-we-do-instead)).

## Reference Analysis: How Charter's 3D View Works

### World coordinate system (`Preview3DUtils`)

One world space shared by every drawer; all sizes below are Charter's literal constants and are
good starting values:

- **X = fret axis.** `fretPositions[fret]`: fret 0 at x=0, each fret 1.2 units wide
  (`firstFretDistance = 1.2`, length multiplier 1 → equal-width frets by default; a multiplier
  under 1 would give realistic narrowing toward the body).
- **Y = string axis.** Board surface ("chartboard") at y=0; strings stacked upward with
  `stringDistance = 0.35`; bottom string at y=0.35. String order optionally inverted by config;
  Rocksmith's default puts the low string at the bottom of the screen.
- **Z = time axis.** `z = (t_note - t_now) * 0.02 / scrollSpeed` — a pure linear map from
  seconds-until-hit to depth. The hit line is z=0. Visibility window ~1600ms * scrollSpeed.
- Note head half-width 0.48 (`firstFretDistance / 2.5`), sustain tail half-width one third of
  that, a full-step bend lifts the tail by `0.35 * 0.8` (i.e. `stringDistance * 0.8` per
  half-step... Charter's `bendHalfstepDistance` is per half-step).

### Camera (`Preview3DCameraHandler`) — the part the user called out

Charter's camera is *nearly* fixed, and that is exactly why it reads well:

1. **Base projection**: a standard perspective frustum (`cameraMatrix(-0.2, -0.3, -0.3, -1)`
   with a z-flip), then per-frame NDC-space scale correcting aspect ratio
   (`screenScaleX = min(0.5, 1/aspect)`, `screenScaleY = min(1, 0.5 * aspect)`).
2. **Tiny fixed rotations, no roll**: `rotX = 0.06`, `rotY = 0.03`, `rotZ = 0.00` radians. Roll
   being exactly zero is what keeps screen verticals from tilting; the tiny X/Y rotations give
   the "looking slightly down and across" composition. (Because rotX is nonzero, verticals are
   not *mathematically* vertical — just imperceptibly close, and the pinning step below hides
   the residual. Our design does strictly better; see below.)
3. **The fretboard pin** (`anchorYZCrossingToBottom`): after building the full view-projection,
   Charter transforms the world point `(camX, 0, 0)` — the board surface at the camera's fret
   focus, at the hit line — into NDC, reads its NDC Y, and appends a **pure translation in NDC
   space** that places that point at NDC y = -0.9 (just above the bottom edge). The comment in
   the source says "IMPORTANT: X offset is intentionally zero": the board is pinned vertically
   only, and slides left/right freely as the fret focus moves. A pure NDC translation cannot
   rotate or skew anything, so the pin never disturbs the verticality of strings/frets. This is
   the "fixed position near the bottom, slides left and right" behavior that makes Charter feel
   right, and it must be reproduced.
4. **Fret focus**: scan fret-hand positions in the window `[now, now + 3000ms]`; take min/max
   fret; the camera X target is the world middle of that fret range, blended 10% toward a fixed
   whole-neck weighted position (damps extreme jumps); the fret *span* target drives camera
   Y/Z out-zoom (`camY = 5 + 0.2*(span-4)`, `camZ = -2.5 - 0.2*(span-4)`). Both targets are
   approached with exponential smoothing `mix = 1 - pow(1 - 0.7, frameTime)` — frame-rate
   independent, ~70% of remaining distance per second.
5. Background gets a separate matrix: same camera with position/parallax divided by 4 plus a
   slow sinusoidal sway — a cheap parallax sky.
6. Optional camera shake on hits (disabled-by-default "secret"); strength scales with chord
   size, decays cubically over 1s.

### Render pass structure (`Preview3DPanel`)

Depth test GEQUAL with reversed clear (depth 0), alpha blending on, multisampling optional.
Order: clear → showlights/background → **noteboard** (beat bars, lane borders, FHP lane
highlights) → hand shapes → **guitar sounds** (notes; sorted far-to-near, chord boxes drawn after
as a separate transparent list) → strings and frets → inlays → fingering → chord names → lyrics →
section name. Two scene matrices are bound (fretboard and background); each drawer submits
immediate-mode vertex lists per frame.

### Board furniture

- **Strings** (`Preview3DStringsFretsDrawer`): one colored line per string across the full neck
  at z=0..(actually drawn at the hit plane as lines along X), lane-colored per string.
- **Frets**: thin vertical quads at each fret position. Three states: inactive, *active* (within
  the current + upcoming FHP fret windows), and *highlighted* — a 100ms sqrt-decay flash of the
  two frets bracketing each note as it is hit, with the fret quad thickening up to 4x. This
  hit-flash is a large part of the "alive" feel.
- **Beat bars** (`Preview3DBeatsDrawer`): lines plus soft gradient quads lying on the board at
  each beat, wider/brighter for measure downbeats, clipped horizontally to the active FHP fret
  range (the board "is" only as wide as the current hand position), faded with distance by a
  dedicated fading shader (fade between z(250ms) and z(50ms) — near notes fade as they pass).
- **FHP lane highlights** (`Preview3DFHPsDrawer`): per-FHP-span quads lighting the lanes between
  `fretFrom..fretTo` on the board, dotted-fret lanes tinted differently, giving the "lit
  runway" effect ahead of the hand.
- **Fret numbers**: rendered along the board under active frets (text textures generated on the
  fly — a per-frame cost we will avoid with a glyph atlas).
- **Inlays + fingering + chord names + lyrics + section labels** complete the scene.

### Notes (`Preview3DGuitarSoundsDrawer`, 714 lines — the core)

- **Fretted note head**: a textured quad (texture atlas keyed by note status) centered at the
  fret-middle X, string Y (plus prebend offset), note-time Z, tinted the string color. A
  **rolling flip**: single notes rotate around Z from -90° to 0° during the last second of
  approach (`rotation = clamp(-π(t-now-100ms)/1000ms, -π/2, 0)`) — chord notes don't rotate.
- **Open-string note**: a procedurally built wide bar model spanning the current FHP fret
  window's full width (models cached per width).
- **Note shadow**: a small triangle fan dropped to the board under every unhit note — reads as
  contact-point and is load-bearing for depth perception; keep it.
- **Anticipation ring**: during the last 500ms before the hit time, a ring texture scales down
  onto the note's landing position at z=0 and brightens — the "here it comes" cue.
- **Technique overlays**: additional atlas quads stacked in front of the head — hammer-on,
  pull-off, palm mute, full mute, pop, slap. Accents draw a stretched glow copy behind open-note
  bars. Harmonics use distinct head textures in the atlas.
- **Sustain tails**: three triangle strips (left edge, inner, right edge — inner at alpha 192)
  following the tail's centerline sampled over time:
  - **Bends** move the centerline up by `bendValue * bendHalfstepDistance`, linearly
    interpolated between bend points; outer strings bend *downward* (`invertBend`) so the curve
    stays inside the board.
  - **Vibrato** adds `sin(t·π/80ms)` wobble in Y.
  - **Tremolo** adds a triangle-wave wobble in X (period 60ms).
  - **Slides** offset the centerline in X toward the target fret with easing:
    `pow(sin(progress·π/2), 3)` pitched, `1 - sin((1-progress)·π/2)` unpitched.
  - Sampling: plain tails use 2 points, bend tails sample at bend points, but vibrato / tremolo
    / slide tails are sampled **once per millisecond** — thousands of vertices per long note
    (defect; see below).
- **Hit explosions**: on note hit, ~100 point-sprites with per-particle random velocity and a
  gravity parabola, red-to-dark fade over 500ms, one burst per string of the sound; chords
  without visible per-string notes burst on the top half of the strings instead.
- **Chord boxes** (`Preview3DChordBoxDrawer`): translucent full-height panels at chord onsets
  (with the chord's fret span), drawn after opaque content.

### Shaders

Five tiny programs: base (position+color), base texture, **fading** (adds `fadeStart`/`fadeEnd`
uniforms and multiplies alpha by a clamped Z ramp — the board-content distance fade), **shadow
highlight texture** (atlas texture where R multiplies the tint color, G adds white highlight, B
is alpha mask — one texture serves every string color), video. All geometry is rebuilt and
streamed every frame; there are no persistent vertex buffers.

## Rocksmith Comparison (what to keep, add, or skip)

Charter reproduces most of Rocksmith's functional language: string-colored heads with technique
overlays, open-note bars spanning the hand window, bend-curved sustain rails, FHP-lit lanes,
anticipation cues, fret-number rail, pinned board. Differences that matter for feel:

- **Rocksmith's camera** cuts between framings and glides more cinematically; Charter's single
  smoothed camera is simpler and arguably *more* readable. Keep Charter's model; add framing
  variety later only if the static shot feels sterile.
- **Rocksmith's venue** (animated stage, lighting, crowd) is atmosphere we replace with a cheap
  parallax background layer initially (Charter's `/4` parallax trick), leaving venue rendering
  as a separate future feature.
- **Rocksmith renders misses/ghosts** and gameplay scoring feedback; that belongs to the
  detection/scoring system, not this plan — but the renderer must expose hooks (hit, miss,
  anticipation state come from outside).
- Rocksmith fades/dims already-passed notes quickly; Charter's fading shader does the same.

## Charter Defects and What We Do Instead

Verified in source; do not port these as-is:

1. **Per-millisecond tail tessellation** (`getTimeValuesToDrawForEveryPoint`): a 5-second
   vibrato note builds ~15,000 vertices every frame. Ours: sample tails adaptively at fixed
   screen-space resolution (target ~1 vertex per 4px of projected tail length, with a hard cap),
   which is visually identical and two orders of magnitude cheaper.
2. **Tremolo/vibrato wobble is unclamped at tail ends**: the modulation phase is absolute time
   (`pointTime % period`), so a tail can begin or end mid-oscillation — the same "unclean edges"
   the user flagged in Charter's 2D tremolo (we already fixed the 2D analog by clipping to the
   sustain rect). Ours: phase the modulation from the note onset and taper its amplitude to zero
   over the first/last ~10% of the tail so rails start and end on the string line.
3. **Per-frame text texture generation** (`setTextInTexture` per fret number per frame): ours
   pre-rasterizes a glyph atlas once (bgfx has no text; we need an atlas anyway).
4. **Immediate-mode rebuild of static geometry**: strings, frets, inlays never change; ours puts
   static board geometry in retained vertex buffers and only streams dynamic content (bgfx
   transient buffers), keeping the per-frame CPU cost proportional to visible notes.
5. **`new Random()` per camera-shake frame + wall-clock effects**: all animation time in ours
   derives from one frame clock, and randomness is seeded per event, so replays/pauses behave.
6. **Magic-constant soup**: Charter's camera/board constants work but live scattered as
   literals. Ours: one `HighwayMetrics` struct (documented defaults copied from the analysis
   above) so tuning is one file.
7. **GEQUAL/reversed depth without need**: fine in OpenGL, but we simply use conventional
   LESS-EQUAL depth in bgfx unless precision demands otherwise.

## Our Design

### Module placement (per `docs/design/architectural-principles.md`)

- **`rock-hero-game/core`** (headless, automated-testable):
  - `highway/highway_view_state.h` — seconds-resolved, camera-agnostic frame content: notes
    (start/end seconds, string, fret, technique fields, bend/slide payloads in seconds — same
    projection discipline as the editor's `TabViewState`), shape spans, FHP windows, beat/measure
    list, sections.
  - `highway/highway_projection.*` — `Chart` + `TempoMap` → `HighwayViewState` (mirror of the
    editor's `tabViewStateFor`; consider promoting the shared parts to `rock-hero-common/core`
    when this lands — decide then, per the dependency rules).
  - `highway/highway_camera.*` — pure math: fret-focus scan (FHP window → target camX/span),
    exponential smoothing step, world→clip matrices, and the NDC pin offset. Unit-test that a
    world-vertical segment projects to a screen-vertical segment for every legal camera state —
    that is the invariant the user cares about, and it becomes a regression test instead of a
    property we hope holds.
  - `highway/highway_metrics.h` — every world-space constant (fret width, string spacing,
    time-to-Z scale, visibility window, pin height, focus speeds) in one documented struct.
- **`rock-hero-game/ui`** — SDL3 window/input/loop, bgfx device and passes, drawers, glyph and
  technique-icon atlases. No chart or tempo-map types leak in; it consumes `HighwayViewState`
  plus per-frame transport time, exactly like editor views consume `EditorViewState`.
- **`rock-hero-game/audio`** — already owns playback; the render loop samples the transport for
  the authoritative song time every frame (never wall clock — A/V sync for a rhythm game must
  come from the audio clock).

### Coordinate system and camera (the definitive choice)

Adopt Charter's world axes and proven constants (X fret / Y string / Z seconds-scaled), but build
the camera **without any rotation at all**:

1. Place the eye above and behind the hit line, looking straight down -Z (no roll, no pitch, no
   yaw).
2. Achieve Charter's "looking slightly down and across" composition with an **off-axis
   (lens-shift) perspective frustum** — asymmetric left/right and top/bottom near-plane extents,
   exactly like a tilt-shift architectural photo. An off-axis frustum with zero rotation maps
   world-vertical lines to screen-vertical lines *exactly*, for all points, not just near the
   center; Charter's small rotations only approximate this. This is the superior form of the
   property the user named, and it is also simpler (one matrix, no rotation bookkeeping).
3. Keep Charter's **NDC-space vertical pin**: project the board point `(camX, 0, 0)`, then add a
   pure NDC translation putting it at a configured screen height (default NDC y = -0.9). Pin
   vertically only; X follows the fret focus so the board slides left/right. (With the off-axis
   frustum the pin could be folded into the frustum's vertical shift analytically; do that if it
   stays readable, otherwise keep the explicit two-step — behavior is identical.)
4. Keep Charter's **fret-focus algorithm** unchanged (FHP window 3s ahead → min/max fret →
   center target with 10% whole-neck bias; span drives out-zoom; exponential smoothing
   `1 - pow(1 - k, dt)` with k≈0.7/s). It is simple and proven; the TODO in Charter's source
   ("weighted average instead of focusing speed") is not needed.
5. Aspect correction via the frustum extents, not post-scale.

### Content, in implementation order

Phase 1 — **board and notes** (playable skeleton):
- Static board: strings (our 2D RYB palette — one authority for string colors shared with the
  tab view), frets, inlays, retained buffers.
- Beat/measure bars with distance fade; FHP lane highlights.
- Note heads (rounded-quad texture, string tint, fret-centered), open-note bars spanning the
  FHP window, note shadows, plain sustain rails, far-to-near sorting, passed-note fade.
- Fret-number rail from the glyph atlas.

Phase 2 — **camera**: fret focus + off-axis frustum + NDC pin + smoothing; the verticality unit
test; scroll-speed setting.

Phase 3 — **techniques** (all data already in the chart format):
- Bend-curved rails with per-bend-point interpolation, outer-string bend inversion, prebend
  offset at the head; bend amount labels only if readability testing wants them (Rocksmith
  doesn't label in 3D).
- Vibrato/tremolo rails with onset-phased, end-tapered modulation (the clean-edges fix).
- Slide rails with Charter's easing curves (pitched `sin³`, unpitched reversed sine) — these
  match Rocksmith's read well; unpitched slides additionally dim toward the end.
- Technique icon quads: hammer/pull/tap, palm/full mute, pop/slap, harmonic head variants
  (natural = diamond silhouette, matching our 2D language), accents.
- Chord boxes with template names and fingering panels; arpeggio (shape-span with sequential
  onsets) rendered as an outlined box, consistent with the 2D view's derivation — the highway
  derives chord-vs-arpeggio the same way (no stored flag).
- Anticipation rings and fret hit-flash.

Phase 4 — **feel polish**: hit particle bursts (from gameplay hit events, not chart data),
parallax background, section/lyric overlays if the game wants them, optional camera shake.

Forward extensions the format already sketches (render support added when the data lands):
whammy dives = signed bend curve on the rail; between-fret harmonics (`touch`) = diamond head
positioned at the fractional touch position instead of the fret middle.

### bgfx pipeline sketch

- One view for background, one for the board scene (conventional depth), one for screen-space
  overlays (chord names, lyrics).
- Programs mirroring Charter's five: `color`, `color_fade` (fadeStart/fadeEnd uniforms),
  `texture_tint` (atlas R=tint, G=highlight, B=alpha — keep this channel scheme; it lets one
  atlas serve all string colors), glyph text.
- Static board: retained vertex/index buffers. Dynamic content: bgfx transient buffers filled
  from `HighwayViewState` each frame; visible-note range via the same
  sorted-starts + prefix-max-sustain-end binary search we shipped in the 2D `TabView` (promote
  that helper if sharing is clean).
- SDL3 owns the window and input; bgfx initialized with the native window handle; vsync on;
  render loop samples transport time, steps camera smoothing with real dt, builds the frame.

### Testing

- `highway_projection` tests mirror `test_tab_projection` (positions→seconds, technique payload
  mapping, arpeggio derivation).
- `highway_camera` tests: focus targeting from FHP fixtures, smoothing convergence, **the
  verticality invariant**, and pin placement (projected anchor lands at the configured NDC y).
- Renderer stays untested-by-unit (thin), consistent with how the editor treats JUCE paint code
  beyond pixel smoke tests.

## Non-Goals

- No gameplay input/detection/scoring here (hit/miss events are inputs to the renderer).
- No venue/stage art beyond the parallax background layer.
- No vocals/lyrics/showlights initially (Charter draws lyrics; ours can follow later).
- No editor integration — the editor keeps the 2D tab lane; this is the game view.

## Open Questions

- Whether `highway_projection` shares code with the editor's `tab_projection` via
  `rock-hero-common/core` or stays a parallel game-core unit (decide when game work starts;
  dependency rules allow either).
- Fret width taper (`fretLengthMultiplier < 1`) for realism vs Charter's equal-width default —
  pick after seeing real charts on screen.
- Whether chord fingering panels show by default (Rocksmith shows them contextually).
- Scroll speed / visibility window as player settings and their interaction with difficulty.
