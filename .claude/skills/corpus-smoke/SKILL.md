---
name: corpus-smoke
description: Local-only smoke check that every package in the local .rock corpus still loads, reporting per-package failures. Strictly local — the corpus is commercial-derived and must never enter git, CI, or any committed file.
---

# corpus-smoke

Verify that all packages in the local `.rock` corpus (39 packages at last count) still load, and
report every failure with its package name and reason.

## Corpus firewall — read first

The corpus is **converted commercial content**. It is a local-only soak asset:

- Never commit corpus files, corpus paths, package names, or corpus-derived data to git.
- Never wire this check into CI or any committed test baseline. CI fixtures are self-authored,
  freely-licensed content only (see docs/roadmap/23-detection-verification-harness.md,
  "Corpus firewall").
- Results stay in the conversation or in local scratch files outside the repo.

The corpus location is supplied by the user, conventionally via the `ROCKHERO_CORPUS_DIR`
environment variable (the convention docs/roadmap/11 and 23 standardize). If it is not set and the
user has not given a path, ask — never guess or hardcode one.

## Current state: no headless corpus loader exists yet (the gap)

Verified against the tree (2026-07-06): no test, tool, or CLI in the repo references a corpus
path or loads packages in bulk. The loading machinery exists only as library code and the
interactive editor:

- `rock_hero::common::core::readRockSongPackage(package_path, workspace_directory)` in
  `rock-hero-common/core/include/rock_hero/common/core/package/rock_song_package.h` — extracts a
  `.rock` package into a workspace directory and parses it into a `Song`, returning a typed
  `SongPackageError` on failure. This is the exact seam a smoke loader would call per package.
- The editor's open flow wraps it via `rock-hero-editor/core/src/project/rock_song_importer.h`
  (`RockSongImporter::importSong`) into the `EditorController` project lifecycle.

Planned-but-unbuilt mechanisms (do not assume they exist — re-check before using):

- docs/roadmap/23-detection-verification-harness.md Phase 7: Catch2 hidden-tag `[.local-corpus]`
  soak tests reading `ROCKHERO_CORPUS_DIR`, run by invoking the built test executable directly
  with the hidden tag.
- docs/roadmap/11-derived-difficulty-calculator.md has an equivalent hidden-tag corpus sweep.

## Procedure

1. **Check whether a headless corpus runner has landed since this skill was written:**

   ```powershell
   rg -n "local-corpus|ROCKHERO_CORPUS_DIR" --glob "!docs/**" --glob "!.claude/**"
   ```

   If a hidden-tag corpus test exists, use it: build its test target through
   `.agents/rockhero-build.ps1` (never raw cmake/ninja/ctest), then run the built `*_tests.exe`
   directly with the hidden tag and `$env:ROCKHERO_CORPUS_DIR` set, and report per-package
   results. Prefer this path and update this skill to name the real target.

2. **If no runner exists (the current state), report the gap honestly** — there is no automated
   way to smoke the corpus today — and offer the closest manual procedure:

   - Build the editor through `.agents/rockhero-build.ps1 -Targets all`, launch it, and open each
     corpus package via the editor's open flow; a load failure surfaces as an editor error. This
     is interactive and slow for 39 packages — that slowness IS the gap this skill documents.
   - Alternatively (only with the user's explicit go-ahead): a throwaway local Catch2 hidden-tag
     test that iterates `$env:ROCKHERO_CORPUS_DIR` and calls `readRockSongPackage` per package.
     It must be deleted before any commit — the corpus firewall forbids committing anything that
     references the corpus, and the proper home for a permanent version is plan 23 Phase 7, not
     an ad-hoc file.

3. **Report**: packages attempted, packages loaded, and for each failure the package filename and
   the typed error — to the conversation only, never into a committed file.

## Open item

A small headless corpus-load CLI (or landing plan 23 Phase 7's hidden-tag soak, which subsumes
it) is the missing piece that would make this skill fully automatic. Until one exists, this
skill's automated path is aspirational; flag the gap in your report whenever step 2 is taken.
