# Guitar Pro Track → Part Mapping on Import

Status: deferred plan, written 2026-07-06. Re-read `rock-hero-editor/core/src/project/gp_chart_builder.cpp`
(`partForTrack`, `buildGpSong`) and the current `common::core::Part` / `Arrangement` model before
implementing; the heuristic below is a known stopgap, not a settled design.

## Problem

Guitar Pro tracks carry no Rock Hero part. The importer currently guesses one in `partForTrack`:

- four strings or a track name containing "bass" → `Part::Bass`
- the first non-bass track → `Part::Lead`
- every remaining track → `Part::Rhythm`

This is workable today and imports the whole corpus without failure, but it is wrong in the
general case:

- a six-string track named neutrally (e.g. "Guitar 3") is guessed Lead or Rhythm by track order,
  not by musical role
- multiple rhythm guitars all collapse to `Rhythm` with no way to distinguish them
- a seven/eight-string lead is never Bass, but a down-tuned four-string guitar part would be
  misfiled as Bass
- the score's own track names, MIDI program, and tuning carry intent the heuristic ignores

The tradeoff is accepted for now: an imported song is usable, and the guessed parts can be edited
after import. The import also logs a conversion note when it applies the heuristic so the guess is
observable rather than silent.

## Proposed solution

Let the user map each Guitar Pro track to a Rock Hero part on import, instead of guessing:

- surface the parsed track list (name, string count, tuning) to the editor at import time
- present a small mapping step: one row per track, each assignable to Lead / Rhythm / Bass (or
  "skip this track")
- seed each row with the current heuristic as a default so the common case is one click
- carry the chosen mapping into `buildGpSong` in place of `partForTrack`

Open questions to resolve when implementing:

- where the mapping UI lives relative to the existing import flow (the importer is headless
  editor-core; the chooser and any mapping dialog live in editor-ui, so the mapping likely has to
  be gathered before or alongside `ISongImporter::importSong`, or the importer needs a
  caller-supplied mapping callback)
- whether Rock Hero should allow more than one arrangement of the same part (e.g. two Rhythm
  guitars) and how the game later selects between them
- whether to persist the last-used mapping per source file so re-importing is repeatable

Until this is built, `partForTrack` remains the single source of the guess and the conversion note
keeps it visible.
