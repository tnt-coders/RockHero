\page guide_musical_time Musical Time and the Tempo Map

*Applies to: Repo-wide — every projection, renderer, and the audio engine consume these types.*

Timing is the heart of a rhythm game, and this codebase keeps two time domains strictly
separated: **musical positions** (measures, beats, exact rational sub-beats) are what charts
author; **seconds** are what renderers and audio consume. Exactly one type converts between them.

# The value types (`rock-hero-common/core`)

- `TimePosition` / `TimeDuration` / `TimeRange` (`timeline/timeline.h`) — seconds as strong
  types. Equality is deliberately *exact* (`std::is_eq(a <=> b)`); tolerance belongs in named
  helpers at call sites, never hidden inside `operator==`.
- `Fraction` (`timeline/fraction.h`) — a gcd-reduced rational, so subdivisions like 1/3 or 3/16
  are exact, not floating-point approximations.
- `GridPosition` (`chart/chart.h`) — `{measure, beat, Fraction offset}` with `offset ∈ [0,1)`:
  the exact musical address every authored thing uses (notes, tone boundaries, automation
  points). Fully ordered, so positions sort and compare exactly.

# The chart model, briefly

The chart itself — "the true tab" of notes, hand shapes, and postures — is the arrangement-owned
model in `common/core/chart/chart.h`, addressed entirely in `GridPosition`s and consumed by the
tab and highway projections and by package IO. Chart *editing* is partly built rather than unbuilt:
seven pure planners live in editor core (`editor/core/src/chart/chart_edits.h`), two of them
technique verbs, all funnelling through one shared finalize gate. The model still gets no
field-by-field tour here — see \ref guide_file_formats for the persisted shape and
`docs/plans/roadmap/40-chart-editing.md` for the remaining phases.

# Grid arithmetic (`chart/grid_arithmetic.h`)

Exact arithmetic *on* the grid lives in one header, and it is the shared authority the 2D and 3D
projections, the editor's duration verb, the Guitar Pro import trims, the legato repair and the
validation gate all resolve through — so a spacing rule cannot mean two things:

- `g_minimum_sustain_distance_whole_note` (1/16 of a whole note) and
  `minimumSustainDistanceBeats(signature_denominator)` — the one settled gap every element keeps
  before the next event, expressed in signature beats so it scales with the meter.
- `g_minimum_kept_sustain_beats` (one beat) — the shortest notated ring that earns a tail. Import's
  drop rule and the legato hold test share it deliberately: that is what lets a missing tail read
  as a *proven* release.
- `chartEffectiveSustains(notes, shapes, tempo_map)` — per-note held lengths, extending a
  sustainless member of a strum that a hand-shape span holds. Callers pass notes in **saved** form.
  Spans may overlap, so what it remembers is the **furthest-reaching** span already started, not the
  latest-starting one: an earlier span running longer holds the same strum just as well, and
  tracking the latest start let a short span beginning inside a long one shadow it, so a held chord
  silently lost its extension and the legato that extension justified was repaired away.
- `predecessorHoldReaches(...)` — the legato hold test: true unless the chart proves the
  same-string predecessor was released before the onset.
- `globalBeatPosition`, `advanceGridPosition`, `beatDistance`, `sustainEndPosition`,
  `snapGridPosition` — the exact `GridPosition` ↔ beat conversions, signed and inverse-exact, all
  crossing beat, measure, and meter boundaries without floating-point drift.
- `terminalGridPosition(tempo_map)` — the chart's closing barline as a `GridPosition`. Four callers
  need the same end of the chart (package read closing the last tone region, tone-track
  normalization, tone-track validation, and the editor's chart-end navigation), so the
  terminal-anchor lookup and the position it becomes are spelled once here.

# The TempoMap

`TempoMap` (`timeline/tempo_map.h`) is the **sole durable timing authority**. Its authored data
is small: time-signature changes (per measure) and sparse *beat anchors* pinning specific beats
to absolute seconds. Between anchors, time interpolates linearly in **quarter-note (metronome)
time** — a meter change between anchors re-slices beat durations rather than stretching them, so
tempo changes only ever happen at anchors (the "metronome-linear" invariant).

Two performance mechanisms matter when querying it:

- Construction builds **derived index tables** once (`buildDerivedIndices()`), so queries like
  `secondsAtGlobalBeatPosition` binary-search monotonic tables instead of rescanning authored
  lists. (This is what fixed the 1/128-grid lag; do not reintroduce per-query scans.)
- For sequential scans (grid lines, projections), `TempoMap::ForwardBeatTimeCursor` resolves
  non-decreasing positions in amortized constant time, bit-identical to the random-access query.

The flow every feature follows: charts author `GridPosition` → a projection resolves seconds
through the `TempoMap` once (`highway_projection.cpp` and `tab_projection.cpp` both walk a
`ForwardBeatTimeCursor`) → renderers consume seconds only and never do musical math per frame.

# Who owns tempo truth (and the one-way mirror)

Tracktion's edit is **tempo-inert**: RockHero never reads tempo back from it. Hosted VST plugins
still need host tempo, so the engine *mirrors* the TempoMap into the edit's tempo sequence —
one-way, write-only (`src/tracktion/tempo_mirror.cpp`, entered via `Engine::mirrorTempoMap`).
Any code that reads `edit.tempoSequence` for timing is a bug by decision, not by style.

# The playback clock

Song time reaches frames through a dedicated read-only port, deliberately separate from the
side-effecting `ITransport`:

- `IPlaybackClock::snapshot()` returns `PlaybackClockSnapshot{position, capture time, rate,
  playing}` — wait-free from any thread (`AtomicPlaybackClock` stores integer nanoseconds and
  parts-per-million so every store is lock-free).
- The engine publishes boundaries on every transport event and republishes at 60 Hz while
  playing (`engine_clock.cpp`, `publishClockBoundary`).
- Consumers never use raw snapshots directly for animation: `PlaybackClockExtrapolator` advances
  the last snapshot to "now" with slew-limited correction and never moves backward during
  continuous play. The game's `FrameClock` wraps exactly this; the editor's vblank-driven views
  sample the same way.

# Grid and snapping (editor)

Distinct from the grid arithmetic above, and the split is intentional: `grid_arithmetic.h` in
common/core answers *musical* questions every consumer shares (how far apart, where does this
sustain end, snap to a note value), while `tempo_grid_geometry.h` in editor/core answers the
editor's *timeline* questions — which grid lines are visible, which line is nearest this pixel,
where does one keyboard step land.

The editor's grid note value (a `Fraction` of a whole note, default 1/4) is the *shared
authority* for both drawing and snapping: `tempo_grid_geometry.cpp` computes visible grid lines
and `nearestTempoGridPosition(...)` returns the exact rational `GridPosition` — the same math, so
what you see is what you snap to. Ctrl bypasses to a 1/960-beat fine grid, uniformly across
surfaces and input families: pointer placement, keyboard moves, and the sustain extent verb all
compose the same fine tier (the off-grid unification). Pointer gestures must go through
`musicalGridPositionForX` (see \ref guide_2d_views); keyboard stepping has its own single
primitives in the same header — `gridStepBeats(...)` (one step's size at a measure) and
`adjacentTempoGridPosition(...)` (the next line from any position, exact-rational, so a coarse
step from an off-grid start lands on the adjacent line, never overshoots) — and
`secondsAtGridPosition(...)` turns an exact position back into seconds for geometry.

*Design in flux: making the tempo map user-visible/editable is active work
(`docs/plans/in-progress/tone-track-tempo-map-plan.md`), and tempo-anchor authoring is an
unbuilt roadmap area (`docs/plans/roadmap/41-tempo-map-authoring.md`) — the model above is
stable; its editing UI is not.*
