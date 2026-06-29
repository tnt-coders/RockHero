# Chart Note Storage Future Work

Status: future work. This file intentionally parks the note/chord/tuning ideas that were removed
from the active tempo-map implementation slice.

Do not treat the details below as settled architecture. Re-read the current tempo map, tone-system,
editor, and gameplay needs before implementing any chart storage.

## Deferred Questions

- Whether playable notes should be stored grid-relative, seconds-relative, or in an importer-specific
  source representation.
- Whether a grid-relative position should use a token such as `"12:3+1/2"`, an object shape, or
  another representation.
- Whether single notes and chords belong in one `events` array or separate structures.
- Whether chords should be explicit templates plus instances, emergent same-onset clusters, or an
  importer-specific intermediate form.
- How sustains, per-string deviations, techniques, bends, slides, hammer-ons, pull-offs, and mutes
  should be represented.
- Whether tuning belongs on an arrangement, on an instrument profile, or only in importer/synthesis
  code until display needs pitch labels.
- Whether pitch labels such as `E5` or `G#/Ab4` should be persisted, generated on write, or only
  derived in UI.
- How arrangement difficulty should be calculated once chart data exists.

## Historical Notes To Revisit

Earlier planning explored exact rational beat fractions, chord template libraries, and a combined
note-or-chord event model. Those ideas may still be useful, but they were speculative for the current
tone-system slice and should be re-evaluated against real note display/gameplay/import requirements.

The old format-review finding about exact `Fraction` validation is also deferred. It only matters if
future chart storage exposes mutable in-memory fractions or a writer that can emit malformed chart
positions.
