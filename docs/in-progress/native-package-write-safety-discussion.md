# Native Package Write Safety Discussion

Status: scope narrowed; a minimal write-safety slice has landed (see Resolution). The broader
two-phase / structured-diagnostics work is deferred. The discussion below is kept as the design
record for that future work.

## Goal

Make native package saves predictable when they fail. A validation failure should not leave newly
written or partially updated package files behind, and IO failures should be contained enough that
the caller can report a clean failure without silently corrupting the workspace or archive.

## Resolution (current direction)

Scope is intentionally narrowed for now. There is no mechanism in the codebase to create or modify
note data, so note values normally enter the model through a read/import, where they are already
validated (`readArrangementNotes`). The writer keeps only minimal round-trip invariant checks so it
does not emit an arrangement document the reader would reject. The structured, per-field validation
diagnostics that fed the larger planning design exist to drive a note-editing repair UI that does
not exist, so they were removed along with the `SongPackageValidationConfig` that only bounded note
values.

What was kept is the one write-safety property with a real trigger today: tone authoring exists, so a
save can carry a non-canonical or missing `tone_document_ref`. The save path now validates each
arrangement's tone reference (canonical, safe, present) **before** writing that arrangement's
document, so a tone failure no longer leaves a written arrangement file behind — the specific bug in
"Current Issue" below. This is the minimal Option A reorder, returning the existing
`SongPackageError`; no new public types, no aggregated validation-error list, no validation config.

Deferred until there is both a producer and a consumer:

- Note-value validation belongs primarily at the read/import boundary, where it now lives. The
  writer should keep only minimal round-trip checks until a transforming importer or tab editor
  creates a real need for broader validation.
- The full plan/commit two-phase, the aggregated structured validation-error list with per-field
  locations, and commit-time atomicity (Options B/C/D below) wait until an editor save dialog
  actually consumes field-level diagnostics and a note-authoring path can produce invalid in-memory
  data. Design those against the real save dialog when it exists rather than speculatively.

### Residual gap

The landed slice validates each arrangement (tone reference and notes) before that arrangement's
side effects (audio copy, document write), so a **single-arrangement** save is fully clean: a
validation failure leaves nothing behind. It does **not** make a multi-arrangement save
transactional. Arrangements are validated and written one at a time, so if arrangement N fails
validation after arrangements 0..N-1 have already been written, those earlier audio copies and
arrangement documents remain on disk. Closing that gap requires the validate-all-then-write pass
(Option B): validate every arrangement up front and produce no write until the whole song passes.
That is deferred with the rest of Option B.

## Current Issue

The native package save path currently mixes validation, document construction, audio import, and
file writes in one flow:

- `writeRockSongPackageDirectory` calls `writeSongFilesForSave`.
- `writeSongFilesForSave` creates the package directory, builds package documents, and writes
  `song.json`.
- `buildSongDocumentForSave` validates arrangement metadata, imports audio assets when needed,
  writes arrangement documents, validates tone document references, and builds the final
  `song.json` object.

That means a later validation error can happen after earlier files have already been created or
overwritten. The clearest example is the current arrangement loop: an arrangement document is
written before the same arrangement's tone document reference is fully validated. If the tone
document is invalid or missing, the save returns an error after the arrangement file has already
changed.

## Why It Matters

This is not only a polish concern. Save operations are user-visible side effects, so callers need a
clear contract:

- either the package content was saved successfully;
- or the package content was not changed by a validation failure;
- or an IO failure occurred after a commit attempt and the caller can warn the user that the
  workspace may need recovery.

The project architecture already favors separating state decisions from side effects. Package save
should follow that same shape: first decide what should be written, then perform the writes.

## Failure Classes

There are two different problems hiding under "partial write":

1. Validation and planning failures.
   These are deterministic and can be made side-effect-free. Examples include missing audio,
   duplicate arrangement IDs, invalid note values, unsafe paths, missing tone documents, and an
   invalid validation config.

2. Commit-time filesystem failures.
   These happen while writing, copying, truncating, replacing, or archiving files. They cannot be
   completely eliminated, but they can be reduced with temporary files/directories and atomic
   replacement where the platform supports it.

The implementation should probably treat these separately instead of pretending one mechanism
solves both equally well.

## Validation Diagnostics

The planning phase should report all deterministic validation errors it can find in one pass, not
only the first error. This is especially important in the editor because a save dialog that reveals
one problem at a time creates a poor repair loop.

The useful contract is:

- collect all independent song, arrangement, note, path, tone-document, and audio-reference
  validation issues;
- keep each issue structured enough for UI or tests to identify the affected field/path;
- return no write plan when any validation issue exists;
- perform no filesystem writes for validation failures;
- keep commit-time IO failures as single operation failures, because failed writes depend on the
  state of the filesystem at the time of commit.

Some validation errors will naturally suppress deeper checks for the same object. For example, an
arrangement entry with an invalid generated path may not be able to validate the target document
path meaningfully. That is acceptable as long as the validator continues checking the rest of the
song and reports every independent issue it can still evaluate.

## Option A: Reorder Local Validation

Validate tone document references before writing arrangement documents. This is the smallest fix
for the specific review finding.

Pros:

- Smallest code change.
- Easy to test with the current package tests.
- Removes the known "write arrangement, then fail on tone document" path.

Cons:

- Still leaves validation and side effects interleaved.
- Audio import can still happen before a later validation error.
- Future package fields could reintroduce the same class of bug.

This is a reasonable short-term patch if we want minimal churn, but it is not the strongest
long-term shape.

## Option B: Validate Then Write In Place

Split the save path into two phases:

1. Build a complete write plan in memory.
2. Commit that plan to the existing package directory.

The write plan would contain the final `song.json` object, arrangement document contents, required
audio copy operations, required directory paths, and any archive-level metadata needed by the
writer. The planning phase validates all domain and package rules without writing files.

Pros:

- Validation failures become side-effect-free.
- Keeps the public package API mostly unchanged.
- Gives tests a clean target: bad songs fail before any file is written.
- Matches the project principle of separating state decisions from side effects.

Cons:

- Commit-time failures can still leave partial output in the destination directory.
- Requires careful handling for audio imports because copied audio files are still side effects.
- More refactoring than Option A, especially around helpers that currently validate while writing.

This is probably the right first structural step even if we later add stronger commit atomicity.

## Option C: Stage Then Replace

Write the complete package directory to a staging directory first, then replace or publish the
destination only after the staging directory is complete.

Pros:

- Strongest protection for directory saves.
- Lets the writer freely create files during staging without touching the live destination.
- Naturally supports archive creation from a known-complete staging tree.

Cons:

- More complex when saving into an existing workspace that also contains source audio or tone
  documents referenced by the song.
- Directory replacement semantics are platform-sensitive.
- Needs a cleanup policy for failed staging directories.
- Can be more expensive for large audio assets.

This may be the best final shape for publishing a `.rock` package, but it may be heavier than we
need for every editor workspace save.

## Option D: Temporary Files Per Output

Write each generated output file to a temporary sibling, then rename it over the destination after
the file has been fully written and flushed.

Pros:

- Reduces corruption risk for individual files.
- Useful even if we also validate first.
- Simpler than whole-directory staging.

Cons:

- Does not make the multi-file package transaction atomic.
- Still needs cleanup for abandoned temporary files.
- Does not solve partial audio imports by itself.

This is best viewed as a commit-hardening detail, not the whole design.

## Recommended Direction

The best long-term direction looks like a layered approach:

1. Add a pure planning phase that validates the song and produces a complete write plan.
2. Collect all independent validation diagnostics before returning a validation failure.
3. Commit generated JSON files from that plan only after planning succeeds.
4. Treat audio copy/import operations explicitly in the plan instead of performing them while
   building `song.json`.
5. Use temporary files for generated JSON outputs.
6. Consider full staging for publish/archive operations once the editor save path is clearer.

This avoids overbuilding atomic directory replacement now, but it still removes the dangerous
validation-after-write shape.

## Open Questions

- Should `writeRockSongPackageDirectory` promise that validation failures never modify the target
  directory?
- Should package APIs expose a structured validation-issue list publicly, or aggregate the issues
  into the existing `SongPackageError` shape until the editor needs field-level diagnostics?
- Should it also attempt best-effort rollback after commit-time failures, or only report that a
  commit failure may have left partial output?
- Are imported audio assets considered part of the generated package output, or should the editor
  manage audio import into a workspace before calling the package writer?
- Should `.rock` archive publishing always use a staging directory, even if directory saves do not?

## Candidate Tests

- Saving with a missing tone document returns `InvalidSongDocument` and leaves existing
  arrangement documents unchanged.
- Saving with an invalid note returns `InvalidArrangement` and creates no new package files.
- Saving with a missing external audio asset creates no new audio asset entries or arrangement
  documents.
- Saving with multiple independent validation problems reports every problem in one result.
- A successful save still writes `song.json`, arrangement documents, copied audio assets, and the
  archive contents exactly as before.

## Current Lean

Prefer Option B as the next implementation slice. It directly addresses the remaining review issue
and sets up better commit behavior without requiring a full transaction system immediately.
