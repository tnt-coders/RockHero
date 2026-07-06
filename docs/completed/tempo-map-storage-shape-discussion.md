# Tempo Map Storage Shape Discussion

Status: completed. The discussion converged on a warp-anchor tempo map, which is now captured in
`docs/design/architecture.md` and `docs/in-progress/tempo-map-implementation-plan.md`. Historical
note-storage ideas from this discussion are now owned by
`docs/in-progress/note-format-and-tablature-plan.md`.

## Resolution

The tempo-map shape settled on is a **warp-anchor grid**, which supersedes the dense per-beat
candidates (A and B) below:

- **The grid stores only changes and pins.** Time-signature *changes* (`{ measure, numerator,
  denominator }`, carried forward) and a sparse set of **anchors** — beats pinned to absolute
  seconds (`{ measure, beat, seconds }`). Non-anchored beats interpolate linearly between anchors.
  **Absolute seconds appear only on anchors**; measures and beats are derived. Unchanged measures
  are omitted; the final addressed anchor closes the last span.
- **Anchor density is adaptive.** Place anchors only where even interpolation would miss the
  recording by more than ~1 ms (steady tempo → sparse; rubato → dense). Candidate B's per-beat
  storage is the maximal case of this; the chosen model keeps just the anchors that aren't
  derivable.
- **Note storage is deferred.** Earlier discussion assumed grid-relative notes, but notes are not
  part of the active tempo-map slice.

Why this beat the candidates: A/B both stored every beat (and B nested them in measures), optimizing
for fidelity Rock Hero's tight, grid-authored content does not need while paying for it in size and
edit-hostility. The warp-anchor model stores only the non-derivable timing, follows the
store-only-changes principle the rest of the format uses. The current validation and persistence
work is specified in the implementation plan.

## Original Question

Rock Hero needs a song-level tempo map that can preserve imported beat grids, support changing
meter, and remain readable enough to inspect by hand. At the time this comparison was written,
notes were assumed to remain stored by absolute song seconds; the tempo map was treated as the
musical grid used for display, snapping, conversion, and future authoring.

The original decision was between two dense beat-storage shapes:

- Flat beat list plus separate measure anchors.
- Measures containing their own beat timestamps.

This discussion assumes:

- Tempo map data lives in `song/song.json`, not arrangement JSON.
- Beat and note positions are absolute seconds on the fixed song recording timeline.
- BPM is derived from adjacent beat timestamps instead of stored as the source of truth.
- Pickup measures are not a special format feature; if a pickup is needed, earlier beats are
  represented as normal rests with no notes.
- Partial measures are not supported in the native format.

## External Format Research

Different rhythm-game formats optimize for different authoring and runtime needs:

- **osu! `.osu`** stores timing as a flat chronological list of timing points. Each timing point
  starts a timing section and defines beat length and meter. Hit objects store their own absolute
  hit time in milliseconds. This is close to a flat timing-event model: efficient to process and
  explicit about object timing, but not structured around measure objects.
  Source: https://osu.ppy.sh/wiki/en/Client/File_formats/osu_%28file_format%29

- **StepMania `.sm`** stores BPM and stop changes separately as beat-indexed header values, while
  chart note data is explicitly divided into measures. Each measure is a block of note rows, and
  the number of rows determines the subdivision. This favors human inspection and musical chart
  authoring, but the classic `.sm` shape is built around four-beat measures and a beat-domain note
  grid rather than exact seconds-first beat timestamps.
  Source: https://github.com/stepmania/stepmania/wiki/sm

- **Beat Saber** stores interactable objects at beat positions and stores audio BPM-region data as
  sample-index ranges mapped to beat ranges. This separates beatmap objects from audio analysis
  data and is strongly beat-domain. It is useful precedent for separating object timing from audio
  timing, but Rock Hero's notes are currently seconds-first because the fixed recording is the
  runtime authority.
  Sources:
  https://bsmg.wiki/mapping/map-format.html
  https://bsmg.wiki/mapping/map-format/audio.html
  https://bsmg.wiki/mapping/map-format/beatmap.html

The important takeaway is that there is no single universal shape. Flat timing-event formats are
common when runtime lookup is the priority. Measure-structured formats are common when the file is
also an authoring artifact. Rock Hero can use a readable measure-shaped file and still normalize to
flat lookup arrays in memory.

## Candidate A: Flat Beats With Measure Anchors

```json
"tempoMap": {
  "measures": [
    {
      "number": 1,
      "positionSeconds": 10.001,
      "timeSignature": { "numerator": 4, "denominator": 4 }
    },
    {
      "number": 2,
      "positionSeconds": 11.715,
      "timeSignature": { "numerator": 3, "denominator": 4 }
    }
  ],
  "beats": [
    { "positionSeconds": 10.001 },
    { "positionSeconds": 10.430 },
    { "positionSeconds": 10.858 },
    { "positionSeconds": 11.287 },
    { "positionSeconds": 11.715 },
    { "positionSeconds": 12.144 },
    { "positionSeconds": 12.573 }
  ]
}
```

### Strengths

- One global beat list is simple for range queries, rendering, and seconds-to-beat conversion.
- Per-beat tempo changes are represented exactly because every beat has its own timestamp.
- Measures can be sparse metadata over the beat grid.
- It avoids nested parsing when consumers only need beat timestamps.

### Weaknesses

- The relationship between measures and beats is indirect.
- Validation must cross-reference measure starts against beat timestamps.
- A human reader must count beats between measure anchors to inspect meter correctness.
- `measure.positionSeconds` duplicates a value that must also exist in `beats`.
- It can drift toward two sources of truth unless validation is strict.

## Candidate B: Measures Containing Beats

```json
"tempoMap": {
  "measures": [
    {
      "number": 1,
      "timeSignature": { "numerator": 4, "denominator": 4 },
      "beats": [
        { "positionSeconds": 10.001 },
        { "positionSeconds": 10.430 },
        { "positionSeconds": 10.858 },
        { "positionSeconds": 11.287 }
      ]
    },
    {
      "number": 2,
      "timeSignature": { "numerator": 3, "denominator": 4 },
      "beats": [
        { "positionSeconds": 11.715 },
        { "positionSeconds": 12.144 },
        { "positionSeconds": 12.573 }
      ]
    }
  ]
}
```

In this shape, a measure's position is derived from its first beat. That avoids duplicating
`positionSeconds` on both the measure and the first beat.

### Strengths

- The file reads like music: measures contain beats.
- Meter validation is local: `beats.size()` should match the measure's numerator.
- There is no separate measure-start cross-reference to keep synchronized.
- Per-beat tempo changes are still exact because every beat still carries an absolute timestamp.
- The shape is well suited to imported RS-style `ebeat` data, where measure markers and beat
  timestamps are already interleaved.

### Weaknesses

- Runtime consumers that need all beats should flatten the nested structure after parsing.
- Cross-measure BPM derivation needs the next measure's first beat after the current measure's
  final beat.
- Repeating `timeSignature` on every measure is verbose.
- If `timeSignature` is inherited instead of repeated, the file becomes less locally readable.

## Time Signature Repetition

There are two sub-options for Candidate B:

1. Store `timeSignature` on every measure.
2. Store `timeSignature` only where it changes.

Storing it on every measure is more verbose, but it makes each measure self-contained and keeps
validation simple. A reader can inspect one measure without carrying inherited state. Tempo maps
are small enough that the extra JSON is not a meaningful package-size concern.

The current readability-first preference is to require `timeSignature` on every measure for v1.
If the file becomes noisy in real songs, a future compatible version could allow omission to mean
"same as previous measure", but that should not be the first shape.

## BPM Derivation

BPM is derived from adjacent beat timestamps:

```text
bpm = 60.0 / (nextBeat.positionSeconds - currentBeat.positionSeconds)
```

For nested measures, the "next beat" may be in the same measure or may be the first beat of the
next measure. The final beat has no following interval, so display code should either reuse the
previous interval or treat the final BPM as unavailable.

The derived BPM is the tempo of the stored beat unit. With `4/4`, this is the usual quarter-note
BPM. With compound meters such as `6/8`, the stored beats need a clear definition. The simplest v1
rule is: the number of beat entries in a measure must equal `timeSignature.numerator`, and each
entry represents one denominator-unit grid beat.

## Pisces Example

The existing `Jinjer - Pisces.rhp` XML starts with this `ebeat` sequence in one arrangement:

```text
0.000  measure 0
10.001 measure 1
10.430
10.858
11.287
11.715 measure 2
12.144
12.573
13.001 measure 3
13.430
13.858
14.287
14.716 measure 4
```

Under the no-partial-measures rule, the isolated `0.000 measure 0` marker should not become a
native measure unless the converter can synthesize a complete valid count-in measure. The cleaner
conversion is to begin the native tempo map at the first complete measure:

```json
"tempoMap": {
  "measures": [
    {
      "number": 1,
      "timeSignature": { "numerator": 4, "denominator": 4 },
      "beats": [
        { "positionSeconds": 10.001 },
        { "positionSeconds": 10.430 },
        { "positionSeconds": 10.858 },
        { "positionSeconds": 11.287 }
      ]
    },
    {
      "number": 2,
      "timeSignature": { "numerator": 3, "denominator": 4 },
      "beats": [
        { "positionSeconds": 11.715 },
        { "positionSeconds": 12.144 },
        { "positionSeconds": 12.573 }
      ]
    },
    {
      "number": 3,
      "timeSignature": { "numerator": 4, "denominator": 4 },
      "beats": [
        { "positionSeconds": 13.001 },
        { "positionSeconds": 13.430 },
        { "positionSeconds": 13.858 },
        { "positionSeconds": 14.287 }
      ]
    }
  ]
}
```

The denominator is an import assumption here because the XML beat markers provide measure starts
and beat timestamps, but not an explicit denominator.

## Validation Implications

Candidate B with required per-measure signatures would validate as follows:

- `tempoMap.measures` is non-empty.
- Measure numbers are strictly increasing and contiguous after the first stored measure.
- Each measure has a valid `timeSignature`.
- Each time-signature numerator is positive.
- Each denominator is a supported power of two.
- Each measure has exactly `timeSignature.numerator` beats.
- Every beat position is finite and non-negative.
- Beat positions are strictly increasing within each measure and globally across measures.
- No partial measures are accepted.
- Notes do not need to reference measures or beats; note-to-grid relationships are derived by
  timestamp.

## Superseded Candidate B Recommendation

Before the final warp-anchor resolution, Candidate B was the preferred dense-beat persisted shape:

```json
"tempoMap": {
  "measures": [
    {
      "number": 1,
      "timeSignature": { "numerator": 4, "denominator": 4 },
      "beats": [
        { "positionSeconds": 10.001 },
        { "positionSeconds": 10.430 },
        { "positionSeconds": 10.858 },
        { "positionSeconds": 11.287 }
      ]
    }
  ]
}
```

This was the cleanest dense-beat shape for human inspection and validation. It preserved exact beat
timestamps, supported per-beat tempo changes, made meter obvious, and avoided duplicate
measure-start data.

The final tempo-map decision supersedes this by storing only the timing that is not derivable:
sparse measure/beat anchors. Candidate B remains useful as the maximal-density mental model; a
per-beat anchor map can still represent fully non-interpolated source timing when the recording
requires it. Note storage is deferred to future chart work.

## Historical Open Questions

- Should native measure numbers be required to start at 1, or should the first stored measure
  preserve the source number when importing?
- Should leading ungridded audio before the first valid measure be represented explicitly, or is
  it enough that the first measure's first beat starts later than zero seconds?
- Do we want to support compound-meter display later, where the perceived BPM may be a dotted
  pulse rather than the denominator-unit beat rate?
