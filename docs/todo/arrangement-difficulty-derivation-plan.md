# Arrangement Difficulty Derivation — Plan

## Status

Deferred. Captures a decision made while building the JSON arrangement format. Nothing here is
implemented yet, and the algorithm cannot be meaningfully built or calibrated until a real note
source exists (chart import or editor authoring). Re-read the current code and design docs before
implementing.

## Decision

Arrangement difficulty is a **derived** value, computed from the chart — never set by hand. This
removes subjectivity by construction: there is no authoring path, only a calculator. The numeric
rating remains the source of truth for the display tier (Easy/Medium/Hard/Expert/Master); see
`docs/user/difficulty-ratings.md`.

The value is treated like **audio backing normalization** (see the completed
`backing-audio-minimal-normalization-metadata-plan.md`): a computed result cached alongside a
validity marker and recomputed when stale.

- Difficulty is to the note chart what normalization gain is to the audio bytes.
- Persist it in the `song.json` manifest entry (so it stays browsable without parsing every chart),
  tagged with a **calculator version**.
- On load, recompute when the stored calculator version does not match the current calculator (the
  analog of normalization's validation hash mismatch). Improving the algorithm re-rates old songs.
- Until the calculator exists, difficulty is **not persisted** and defaults to `Unknown` (0). The
  `difficulty` key was deliberately removed from the JSON layer to avoid implying authored data.

## Algorithm direction

Model the calculator on the strain-based approach proven by osu! "Star Rating" and Etterna's MSD,
adapted to guitar:

1. **Local difficulty** — per note (or per short time window), blend guitar-specific features:
   - note density (notes/sec in a local window),
   - fret-hand position shifts (some chart sources encode these as `anchors`; anchor changes and
     fret deltas can be a near-ready movement signal),
   - chord complexity / finger stretch (fret span and awkward fingerings),
   - technique weight (bends, slides, hammer-ons/pull-offs, tapping, palm mutes),
   - picking / string-skip cost (string distance between consecutive notes),
   - tempo (scales the time pressure of all the above).
2. **Temporal accumulation** — accumulate local difficulty into a decaying "strain" so both bursts
   and sustained density register.
3. **Aggregation** — combine the strain **peaks** (sorted descending, geometric weighting) into one
   scalar. Peak-weighting is deliberate: average density under-rates a chart that is easy except for
   one brutal passage, which humans perceive as hard.
4. **Calibration** — map the scalar to 1-10 against a reference set of charts whose ratings are
   trusted. "Objective" here means deterministic + calibrated, not absolute.

## Properties to preserve

- **Pure and deterministic** — `computeDifficulty(const Arrangement&)` (or over the richer chart once
  notes carry techniques) is a pure function over chart data, living in core, easy to unit-test.
- **Per arrangement** — each part (lead/rhythm/bass) rates independently; it already does in the
  model.
- **Versioned** — bump the calculator version on any change that alters output, so caches recompute.

## Data implications

The difficulty features want chord-finger and anchor data. Revisit how aggressively the arrangement
note model strips source-specific chart fields before assuming those are gone — the calculator is a
consumer of them.

## Phasing

1. Land a real note source (import or editor authoring) so there is something to calibrate against.
2. Ship a simple feature-weighted v1 (density + position-shift rate + chord stretch + technique
   weight), calibrated against a handful of reference charts.
3. Evolve toward the full strain + peak-weighted model only if v1 mis-ranks.
