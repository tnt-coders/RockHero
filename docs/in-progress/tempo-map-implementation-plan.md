# Tempo Map and Grid-Relative Note Model — Implementation Plan

Status: in progress (planning). Defines the durable storage model for the tempo map **and** note
positions. Note positions are now **grid-relative**; this supersedes the earlier absolute-seconds
note storage and the BPM-segment tempo map (see
`docs/in-progress/tempo-map-storage-shape-discussion.md` for how the model was reached). The first
implementation slice remains read-only grid loading and display; tempo and note editing come later.

## Goal

Make the **tempo map (a beat grid) the source of truth for note positioning.** The grid is authored
to match the fixed backing recording as closely as possible, and notes are positioned relative to it
(bar / beat / fractional offset) — exactly like charting in Guitar Pro against a backing track.
Notes are baked to absolute seconds at load for the highway and scoring, but the **stored truth is
grid-relative**.

The grid is a **warp-anchor model**: a small set of beats pinned to absolute seconds (anchors) plus
the time-signature changes, with every other beat and measure interpolated. **Absolute seconds appear
in exactly one place — the anchors.** Everything else (measures, beats, notes, durations) is
musical/relative and resolves to seconds only through the anchors.

The tempo map lives on `Song` (shared by all arrangements). Runtime and scoring schedule in
seconds/sample time; the grid is the layer that maps musical positions to that timeline.

## Why this shape

- **Grid-relative notes match the authoring model** (snap-to-grid against a backing track) and
  **follow grid edits automatically** — fixing the grid moves the notes that were charted to it.
- **Warp anchors make grid editing drift-free**: moving an anchor re-resolves everything downstream,
  and because the only stored absolute time is the anchors, there is no second copy of the timeline
  to fall out of sync.
- **Sparse anchors + change-only meter store only what is not derivable.** Steady-tempo stretches
  cost a couple of anchors; meter changes cost nothing in time data.
- **Import snaps onsets to subdivisions**: a note's grid position is the fraction of the way between
  its surrounding beats, snapped to the nearest musical subdivision and stored as an exact rational —
  no floating-point drift, and tuplets fall out naturally (`1/3`, `1/6`, …).

## Design decisions (durable)

1. **The grid is the source of truth for note positioning.** Notes are authored grid-relative, the
   grid must be authored to match the recording, and a misaligned grid is a *defective chart* fixed
   by aligning the grid (notes follow). Scoring accuracy is therefore gated on grid accuracy — an
   accepted trade, paid back by editor grid-alignment tooling and authoring QA, not by runtime
   decoupling.

2. **Notes are stored grid-relative:** `{ measure, beat, offset }`. `beat` is 1-based; `offset` is an
   **exact rational fraction** in `[0, 1)` (a `numerator/denominator` such as `1/3` or `3/16`) giving
   the position within the beat (`0`, i.e. on the beat, is the default and is omitted). No seconds are
   stored on notes. A note bakes to seconds at load as `beatSeconds + offset × beatSpan`. Note
   durations are exact beat fractions too, not seconds. Both reduce to a denominator of at most 1024
   (the finest stored subdivision).

3. **The tempo map is a warp-anchor grid**, stored as two sparse lists:
   - `timeSignatures` — meter **changes only**: `{ measure, numerator, denominator }`, carried
     forward to later measures.
   - `anchors` — beats pinned to absolute seconds: `{ measure, beat, seconds }`, sparse. Two are
     always required: a **start** anchor (measure 1, beat 1) and a **terminal** anchor at the
     one-past-content downbeat (beat 1 of the bar after the last content bar), which closes the last
     span and bounds the grid. Interior anchors are added only where interpolation needs them.
   No BPM is stored; tempo is implied by adjacent anchors (the beat rate between them).

4. **Seconds live only on anchors.** Every other position is derived: non-anchored beats interpolate
   linearly between the two surrounding anchors by **global beat index**; a note resolves via its
   beat's (anchored or interpolated) seconds plus its offset. There is no other absolute-time value
   anywhere in the grid or note data. (Audio-file duration on an audio asset is unrelated metadata,
   not part of positioning.)

5. **Adaptive anchor density.** Beats between anchors are spaced evenly (constant tempo per span), so
   anchors are placed wherever even interpolation would otherwise miss the recording's beats by more
   than ~1 ms. Steady tempo → sparse anchors; rubato or drift → denser, down to per-beat. Meter
   changes are structural and require no anchors.

6. **Store only what changes or is not derivable.** Unchanged-meter measures are omitted (meter
   carries forward). Measure numbers are kept as explicit, readable, self-locating addresses — in
   sparse lists they are the address, not redundant. On-beat notes omit `offset`.

7. **Anchor seconds use three decimals; note positions are exact fractions.** Anchor `seconds` are the
   only absolute time stored and use a fixed three-decimal (millisecond) grid (±0.5 ms quantization),
   which is below the onset-detection / latency / hit-window floor for the charting and scoring work
   planned here. Note `offset` and `duration_beats` are exact rational fractions of a beat — authored
   by snapping to subdivisions — so they carry no decimal grid and round-trip losslessly. See the
   timing note in `docs/design/architecture.md`.

8. **Warp-following is drift-free by construction.** Moving an anchor re-resolves every downstream
   beat and note. Because the offset is the stored invariant and seconds live only on anchors, there
   is nothing to accumulate; on-beat notes stay welded to their beat exactly. (Future editing
   capability — the storage model already supports it.)

9. **The tempo map lives on `Song`, not `Arrangement`.** Arrangements share one transport and one
   musical timeline; arrangement-specific audio is still allowed.

10. **The grid is mandatory once notes are grid-relative.** A song with notes must have a grid to
    resolve them. Legacy seconds-based songs and external imports build a grid and convert their note
    times to `{ measure, beat, offset }` (see Import). The fallback grid, when no beat grid is known,
    is a single 4/4 meter at 120 BPM expressed as a constant-tempo span (start + end anchors).

11. **Tracktion synchronization is an adapter step** in `common/audio` (`Edit::tempoSequence`),
    behind `ISongAudio`. Core owns the persisted grid and all musical-timing math; Tracktion types
    stay behind the audio boundary.

12. **Anchor seconds are validated onto the decimal grid; note fractions are exact.** Because note
    `offset` and `duration_beats` are exact rational fractions, there is no offset-rounding problem (an
    offset can never round up to a full beat) and **no on-grid check applies to notes**. Anchor
    `seconds`, however, are still decimal: the package reader requires every anchor second to already
    be on the three-decimal grid, so the writer is lossless and never emits a document its own reader
    rejects. Import and any future editing **snap anchor seconds to the grid before constructing the
    model**; two anchors that snap to the same second are simply equal, and the existing
    strictly-increasing-`seconds` check rejects them, so no separate minimum-separation rule is needed.
    Notes may share an onset only when they are on different strings, which represents a chord;
    duplicate same-string onsets are invalid (compared on the exact `{ global beat, reduced offset }`).

## Target core model

Add value types under `rock-hero-common/core/include/rock_hero/common/core/`:

```cpp
struct Fraction              // exact rational; reduced on construction, denominator > 0
{
    int numerator{0};
    int denominator{1};
};

struct TimeSignatureChange   // meter from this measure onward
{
    int measure{1};
    int numerator{4};
    int denominator{4};
};

struct BeatAnchor            // a beat pinned to absolute time — the only stored seconds
{
    int measure{1};
    int beat{1};             // 1-based
    double seconds{0.0};
};

class TempoMap
{
public:
    [[nodiscard]] static TempoMap defaultMap(double audioDurationSeconds);

    [[nodiscard]] const std::vector<TimeSignatureChange>& timeSignatures() const noexcept;
    [[nodiscard]] const std::vector<BeatAnchor>& anchors() const noexcept;
    // Musical structure
    [[nodiscard]] TimeSignatureChange meterAt(int measure) const noexcept;   // last change <= measure
    [[nodiscard]] long long globalBeatIndex(int measure, int beat) const;    // walk the meter map

    // Time resolution (anchors are pinned; everything else interpolates)
    [[nodiscard]] double secondsAtBeat(int measure, int beat) const;         // anchored or interpolated
    [[nodiscard]] double secondsAtNote(int measure, int beat, Fraction offset) const;
};
```

`NoteEvent` becomes grid-relative:

```cpp
struct NoteEvent
{
    int measure{1};
    int beat{1};             // 1-based
    Fraction offset{};       // exact fraction in [0,1) to the next beat; 0 = on the beat
    int string_number{0};
    int fret{0};
    Fraction duration_beats{}; // exact beat fraction; 0 = non-sustained
    // optional techniques, omit-when-absent (slideTo, hammerOn, palmMute, bend, …)
};
```

Conversion outline: `globalBeatIndex` sums numerators of prior measures via the meter changes;
`secondsAtBeat` returns the anchor's seconds if that beat is anchored, otherwise linearly
interpolates between the two surrounding anchors by global beat index; `secondsAtNote` adds
`offset × (secondsAtBeat(nextBeat) − secondsAtBeat(beat))`. Linear scans are fine for the first
version; add an index only if profiling on long imported maps shows a need.

Validation rules:

- `timeSignatures` is non-empty and begins at measure 1; numerators positive; denominators a power of
  two; measures strictly increasing.
- `anchors` is strictly increasing in both `(measure, beat)` and `seconds`, with a start anchor at
  measure 1 beat 1 and a terminal anchor at the one-past-content downbeat. The terminal anchor is a
  grid boundary, not a content bar, and it needs no `timeSignatures` entry of its own.
- Every note onset lives within a real content bar: `measure` is before the terminal boundary and
  `beat` is in `1..numerator` of that measure's meter.
- Every note sustain resolves within the grid: the note's `{ measure, beat, offset }` plus
  `duration_beats` must end at or before the terminal anchor's global beat index.
- `seconds` finite, non-negative, and on the three-decimal grid; `offset` an exact fraction in
  `[0, 1)`; `duration_beats` a non-negative exact fraction. Both note fractions reduce to a
  denominator of at most 1024 (the finest stored subdivision).
- No two notes on the same string share an onset (a chord is the same onset on different strings).
- Missing grid on a song with notes is an error after migration has run; a freshly imported or
  default song uses `TempoMap::defaultMap()`.

## Persistence format

In `song.json`, alongside `metadata`, `audioAssets`, `arrangements`:

```json
"tempoMap": {
  "timeSignatures": [
    { "measure": 1, "numerator": 4, "denominator": 4 },
    { "measure": 2, "numerator": 3, "denominator": 4 }
  ],
  "anchors": [
    { "measure": 1,   "beat": 1, "seconds": 10.001 },
    { "measure": 188, "beat": 1, "seconds": 260.291 },
    { "measure": 229, "beat": 1, "seconds": 316.834 }
  ]
}
```

Notes in the per-arrangement document become grid-relative. `offset` and `durationBeats` are quoted
exact-fraction strings (`"1/3"`, `"3/16"`, whole values like `"1"` or `"0"`); `offset` is omitted when
the note is on the beat:

```json
"notes": [
  { "measure": 2,  "beat": 1, "durationBeats": "0",   "string": 1, "fret": 17 },
  { "measure": 53, "beat": 1, "offset": "1/6", "durationBeats": "1/2", "string": 2, "fret": 7 }
]
```

The note schema changed incompatibly, but `formatVersion` stays at `1` for now: the project is in
early development with a single owner of `.rhp` files, so the version bump is intentionally deferred
until the format stabilizes. The tempo map itself is durable song content in `song.json`, never in
editor-only `project.json`.

## Import (absolute seconds → grid)

External/legacy sources give beat grids and note onsets in seconds. Convert once:

1. **Meter changes** → `timeSignatures` directly from the source time-signature events.
2. **Anchors** → run line-simplification (Douglas–Peucker, ε ≈ 1 ms) over the source `(beat-index,
   seconds)` beat curve, keeping the minimal set of beats whose linear interpolation reproduces the
   rest within tolerance. Steady tempo collapses to a few anchors; wandering tempo keeps more.
3. **Terminal anchor** → anchor the one-past-content downbeat (beat 1 of the bar after the last
   content bar). Its seconds come from the source's bar-end downbeat, or are extrapolated one beat
   from the final span if the source stops at the last beat. Audio-file duration stays audio-asset
   metadata; it is not a substitute for an addressed grid anchor.
4. **Notes** → for each onset second, find its beat bracket, take the raw fraction
   `(S − beatStart) / (beatNext − beatStart)`, and **snap it to the nearest musical subdivision** from
   a configured denominator set (1/2, 1/3, 1/4, … 1/16 and triplet families), storing the exact
   `Fraction`. The snapped denominator is the chart's subdivision; the residual to the source second is
   reported for QA (step 5), not stored.
5. **QA residuals** → report any note whose snapped position differs from its source second by more
   than tolerance. Large or biased residuals indicate a misaligned grid to fix, not notes to force.
6. **Quantize anchor seconds** → snap each anchor's `seconds` to the three-decimal grid before
   constructing the `TempoMap`, so the in-memory model equals its persisted form (note fractions are
   already exact and need no quantization). Two anchors that snap to the same second are rejected by
   validation. See design decision 12.

## Implementation steps

1. **Core grid + note model + conversion** — add the value types above, `Song::tempo_map`,
   grid-relative `NoteEvent`, and `tempo_map.cpp` with `globalBeatIndex` / `secondsAtBeat` /
   `secondsAtNote`. Pure tests for interpolation, meter walking, note baking, and validation.
2. **Package persistence** — read/write `tempoMap` and grid-relative notes (exact-fraction `offset`
   and `durationBeats`) in `rock_song_package.cpp`; round-trip and malformed-map tests. `formatVersion`
   stays at `1` for now (see Persistence format).
3. **Migration / import** — convert legacy seconds-notes and external imports to the grid model per
   the Import section; snap onsets to subdivisions and quantize anchor seconds (design decision 12);
   residual reporting for QA.
4. **Editor state + read-only display** — expose the grid via `EditorViewState`; render a read-only
   bar/beat ruler in `rock-hero-editor/ui` using `TempoMap` conversion helpers; do not mark projects
   dirty for loading/displaying.
5. **Tracktion adapter** — translate the grid into `Edit::tempoSequence` in `common/audio` behind
   `ISongAudio::prepareSong`; adapter tests at the project-owned boundary.

## Testing

- `rock-hero-common/core/tests/test_tempo_map.cpp`: default map; validation; `globalBeatIndex` across
  meter changes; `secondsAtBeat` for anchored and interpolated beats; `secondsAtNote` including
  offsets; warp invariance (moving an anchor re-resolves notes, on-beat notes stay welded).
- `rock-hero-common/core/tests/test_rock_song_package.cpp`: grid + grid-relative notes round-trip;
  malformed grid fails with `InvalidSongDocument`; import converts seconds-notes to `{measure, beat,
  offset}` within tolerance.
- `rock-hero-editor/core` / `ui` tests: loaded view state carries the grid; read-only ruler renders
  for loaded projects; bar/beat lines land at expected positions.
- `rock-hero-common/audio` tests: engine accepts prepared songs whose grid syncs to Tracktion behind
  `ISongAudio`.

## Non-goals

- No tempo or note editing UI yet (the storage model supports warp-following; the editor for it is
  later).
- No grid-alignment/auto-detection tooling in this slice (it is the dependency for *good* charts, but
  not for the storage model).
- No tone-change events, plugin automation authoring, or live-rig switching.
- No audio stretching or arrangement-duration changes from tempo edits.

## Exit criteria

- `Song` owns a validated grid (`timeSignatures` + addressed `anchors`); seconds appear only on
  anchors, with a start anchor and a one-past-content terminal anchor closing the grid.
- Notes persist and load as `{ measure, beat, offset, … }` and bake to seconds through the grid.
- Import/migration converts seconds-based note sources to the grid model within ~1 ms, with a QA
  residual report.
- The editor shows a read-only bar/beat timeline for loaded projects.
- Tracktion tempo-sequence setup is implemented behind `common/audio` or explicitly deferred as the
  next adapter slice.
