# Plan 43 — Song Information and Art

**Status**: Decision-gated | 2026-07-06 | baseline `refactor @ 13e82fb0`

## 1. Goal

Charters can author complete song presentation data and every published `.rock` package carries it:

- A Song Information dialog edits title, artist, album, year (already persisted in `song.json`
  metadata), and album art — a new package file referenced from the JSON.
- Publishing a `.rock` requires the full metadata set, with actionable messages instead of silent
  "Unknown Artist, year 0" exports (every GP import today has year 0 — no importer sets it).
- Optional, toggleable sort fields (sortTitle/sortArtist/sortAlbum) override how the game library
  sorts ("The Beatles" under B), auto-filled with a suggestion when enabled.
- An optional preview start/length pair marks the snippet the game plays while browsing
  (`docs/plans/roadmap/26-game-startup-menus-library.md` Phase 9 consumes it).
- All metadata edits are ordinary undoable edits in the editor's memento history.

This plan also explicitly resolves the tension between required-on-export validation and the
established save==publish / normalize-don't-reject invariant (Open question Q1).

## 2. Non-goals

- No chart content validation (impossible spans, coverage gaps) — that is
  `docs/plans/roadmap/42-chart-validation.md`; this plan validates metadata *presence* only, leaving a
  seam so plan 42's content gate joins the same publish check later.
- No formatVersion machinery — `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` owns the
  bump rules and migration ladder; this plan ships the first *consumer* of that ladder.
- No game-side rendering of art, sort columns, or preview playback —
  `docs/plans/roadmap/26-game-startup-menus-library.md` (its Phase 3 album-art adapter consumes the JUCE
  facts verified here).
- No arrangement `part` editing UI and no authored or serialized difficulty — difficulty stays
  derived (`docs/plans/roadmap/11-derived-difficulty-calculator.md`); its editor display can join this
  dialog later per that plan's non-goals. No authored chart "version" field (Q6).
- No preview *audition* (play the snippet inside the editor dialog) — future nicety; the fields
  ship first.
- No WebP/HEIC support: vendored JUCE 8 cannot decode them (see inventory) — import rejects them
  with a message telling the user to convert externally.
- `docs/plans/todo/audio-asset-catalog-thumbnail-cache-plan.md` is about *waveform* thumbnails for audio
  clips, not album art; it is unrelated and stays untouched.

## 3. Constraints

Applicable subset of the roadmap constraint block (see `docs/plans/roadmap/00-roadmap.md`):

- (a) **Layering**: format/persistence rules live in `rock-hero-common/core`; anything both
  products need (the art image codec helpers) is extracted to `rock-hero-common` FIRST, as its own
  phase with tests, before game code consumes it. Common never depends on editor or game code.
- (b) **Public-header minimalism**: new public surface is limited to the metadata fields on
  `SongMetadata`, one narrow `Session` mutation accessor, controller intents/view state, and the
  shared art-codec header; everything else stays `src/`-private
  (`docs/design/architectural-principles.md` "Placement Procedure for New Files", step 4).
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (d) **Derived over authored**: only presentation facts a machine cannot derive are authored here
  (title, artist, album, year, art, sort overrides, preview window). No authored difficulty, no
  authored chart version (Q6).
- (e) **FLAC**: the art file is a new *image* package entry; nothing here weakens the FLAC-only
  audio enforcement in the package reader.
- (f) **Undo**: every metadata/art edit is a RockHero-owned full-state memento in the editor-core
  history; Tracktion is never the product undo stack.
- (h) **Builds**: all verification through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`); intermediate phases run only determinately warranted checks; the final
  acceptance phase runs the sanctioned bundle as separate invocations.

## 4. Current state inventory

Verified with `rg`/reads against the tree; all paths repo-relative.

**Metadata model and serialization**
- `SongMetadata { title, artist, album, year }` with `year == 0` meaning unknown:
  `rock-hero-common/core/include/rock_hero/common/core/song/song.h:17-30`.
- Writer emits the metadata block unconditionally (empty strings, year 0 included):
  `rock-hero-common/core/src/package/rock_song_package_write.cpp:346-351` inside
  `songDocumentContents` (lines 339-385); `formatVersion: 1` at line 345.
- Reader is fully tolerant: `readMetadata`
  (`rock-hero-common/core/src/package/rock_song_package_read.cpp:169-183`) returns defaults when
  the metadata object is missing; every field is optional. No required-field enforcement exists
  anywhere in the read or write path.
- Reader hard-rejects `formatVersion != 1` (`rock_song_package_read.cpp:976-983`); the migration
  ladder that changes this is `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` Phase 2.
- There is NO art field, NO sort fields, NO preview fields, NO serialized difficulty in
  `song.json`. `DifficultyRating` exists in-memory only (`song/difficulty.h`, carried on
  `Arrangement` at `song/arrangement.h:48`), never computed or serialized —
  `docs/design/architecture.md` "Song Data Model" confirms "not persisted yet".

**Save == publish in code**
- The `.rhp` project save writes the song directory through the exact same writer publish uses:
  `writeProjectFiles` → `writeRockSongPackageDirectory`
  (`rock-hero-editor/core/src/project/project_io.cpp:177-199`), and `Project::publish` →
  `writeRockSongPackage` over the same workspace `song/` directory
  (`rock-hero-editor/core/src/project/project.cpp:603-644`). Validation added inside the shared
  writer therefore blocks *saves*, not just exports — the mechanical root of the Q1 tension.
- Publish already exists end-to-end as a distinct user action: `onPublishRequested` intent
  (`rock-hero-editor/core/include/rock_hero/editor/core/controller/i_editor_controller.h:63`),
  `EditorActionId::PublishProject` availability gating, busy state `PublishingProject`, File menu
  item "Publish..." (`rock-hero-editor/ui/src/main_window/editor_view.cpp:669-671`), and a test
  proving a blocked publish never calls the publish function
  (`rock-hero-editor/core/tests/test_editor_controller_busy.cpp:452-467`) — the pattern the
  Phase 5 gate reuses.
- Existing normalize-don't-reject precedents: legacy per-region tone names rebuilt into the tone
  catalog on load (`rock_song_package_read.cpp`, `readToneCatalog`), and missing audio
  normalization analyzed on open then surfaced as unsaved changes
  (`rock-hero-editor/core/src/project/project_handlers.cpp:268`).

**Editor session, undo, and UI patterns the new work follows**
- `Session` exposes narrow mutation surfaces only (`currentToneTrack()`, `currentToneCatalog()`,
  `rock-hero-common/core/include/rock_hero/common/core/session/session.h:59-77`); broader song
  fields are read-only via `song()` (line 38). Metadata editing needs one more narrow accessor.
- Undo is memento-based `IEdit` objects pushed to `EditorUndoHistory`
  (`rock-hero-editor/core/src/controller/editor_undo_history.h:96-120, 280, 337`);
  `NoNetMutation` (line 67) already models "draft equals current, record nothing".
- Controller intents follow `on<Thing>Requested(payload)` naming
  (`i_editor_controller.h:42-63`); prompts/dialogs are driven by view-state structs
  (`rock-hero-editor/core/include/rock_hero/editor/core/controller/editor_view_state.h:59-108`).
  Separate-window precedent:
  `rock-hero-editor/ui/src/audio_device/audio_device_settings_window.cpp`.
- No metadata edit UI exists anywhere in `rock-hero-editor/` (rg for `SongMetadata` hits only
  tests and the GP importer); no importer sets `year` (rg `year` over
  `rock-hero-editor/core/src/project/` — zero hits).
- Package-relative reference validation precedents to reuse: safe-path checks and
  "missing or unsafe" errors for audio/tone/chart refs (`rock_song_package_read.cpp:131, 260,
  549, 696, 726`), `validateToneDocumentOnDisk` on the write side
  (`rock_song_package_write.cpp:428-429`), `relativeWorkspacePath` (`package/workspace_paths.h`).
- Timing precedent for the preview fields: `startOffset` is plain seconds, omitted when zero
  (`rock_song_package_write.cpp:275-281`); package timing keeps a fixed 3-decimal grid
  (`formatTimingValue`, lines 167-170).

**Shared-UI home for the art codec**
- `rock-hero-common/ui` is a placeholder library linking only `rock_hero::common::core`
  (`rock-hero-common/ui/CMakeLists.txt:1-16`); its README reserves it for shared UI services,
  sanctions adding JUCE GUI dependencies when real shared code requires them, and says to remove
  the include-root `.gitkeep` with the first real public header. A `rock_hero::juce_graphics`
  wrapper target already exists (consumed at `rock-hero-editor/ui/CMakeLists.txt:79`).

**JUCE image facilities (verified via juce-tracktion-expert against the vendored JUCE 8 source;
paths below are relative to `external/tracktion_engine/modules/juce/modules/juce_graphics/`)**
- Built-in codec registry is exactly PNG, JPEG, GIF — a hard-coded private static with no runtime
  registration API (`images/juce_ImageFileFormat.cpp:38-76`). WebP/HEIC: absent from the entire
  vendored tree — a fundamental constraint, not an accident.
- Encode support is PNG and JPEG only; `GIFImageFormat::writeImageToStream` is a stub
  (`image_formats/juce_GIFLoader.cpp:458-462`). JPEG default quality is 0.85
  (`image_formats/juce_JPEGLoader.cpp:483-486`), settable via `JPEGImageFormat::setQuality`
  (lines 301-304).
- **JPEG alpha pitfall**: the JPEG writer silently drops alpha with no background compositing
  (`image_formats/juce_JPEGLoader.cpp:513-522`) — ARGB sources must be flattened onto a solid
  background before encoding or transparent regions produce undefined color.
- **Downscale pitfall**: `juce::Image::rescaled` (`images/juce_Image.cpp:746-759`) on the software
  path does plain 2x2 bilinear even at high quality (`native/juce_RenderingHelpers.h:1000-1010`) —
  downscaling by more than ~2x skips source pixels and aliases. Correct procedure: successive
  halvings until within 2x of target, then one final exact rescale. `rescaled` preserves the
  source `ImageType` (`images/juce_Image.cpp:751`), so `juce::SoftwareImageType` images process
  headless and off the message thread (consistent with the project's established headless
  pixel-test approach).
- Aspect-preserving draw: `juce::Graphics::drawImageWithin` + `juce::RectanglePlacement`
  (`contexts/juce_GraphicsContext.h:588-591`). On Windows the vendored libpng/libjpeg are always
  used — deterministic decode, no OS-codec extras.

**Corpus**: 39 local `.rock` packages (converted commercial content — local-only, never committed,
never in CI); all carry `formatVersion 1` and no art/sort/preview fields.

The working tree currently carries in-flight editor-core tone-handler and UI changes
(`docs/plans/in-progress/tone-track-tempo-map-plan.md` is active work); this plan's editor-core phases
must rebase over that work when it lands and must not touch tone files meanwhile.

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## 5. Dependencies

Upstream (blocking):

- `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` Phase 0 (Q1 bump rule, Q4 per-document
  ladders) and Phase 2 (migration ladder + `SongReadResult.migrated`) — Phase 1 here is the first
  real `song.json` schema change and must ride that ladder, not precede it.

Downstream (consumers; recorded in their Dependencies sections too):

- `docs/plans/roadmap/26-game-startup-menus-library.md` — Phase 3 (album-art images; shares this plan's
  JUCE image findings and Phase 3 codec), Phase 7 (sort columns), Phase 9 (preview snippet).
- `docs/plans/roadmap/11-derived-difficulty-calculator.md` — editor display of the derived rating rides
  with this dialog later (that plan's non-goals).
- `docs/plans/roadmap/42-chart-validation.md` — its editor pre-export content gate plugs into the Phase 5
  publish-blocker seam defined here.
- `docs/plans/roadmap/40-chart-editing.md` — cites this plan (its decision 9) for the save==publish
  export-gate resolution.

External decisions: Q1–Q6 below, aggregated in `docs/plans/roadmap/00-roadmap.md` Decisions-needed.

## 6. Decisions already made

Restated with sources; a fresh session must not re-litigate these.

- **Save == publish; validation normalizes, never rejects, on the save path** — established
  invariant restated in `docs/plans/roadmap/40-chart-editing.md` (decision 9), mechanically visible in
  code: save and publish share one writer (`project_io.cpp:183`, `project.cpp:634`); repairs
  happen as load normalization surfaced as unsaved changes (`project_handlers.cpp:268`). This
  plan may add a *publish-only* gate (Q1) but must not make the shared writer reject.
- **Descriptors are derived, never authored** — `docs/design/architecture.md` "Song Data Model"
  ("a value *derived* from playable chart data, not authored data") and
  `docs/plans/roadmap/11-derived-difficulty-calculator.md`. This plan authors only relational and
  presentation facts.
- **Undo is RockHero-owned full-state mementos** —
  `docs/plans/completed/editor-undo/editor-undo-plan.md`; the editor-core `IEdit`/`EditorUndoHistory`
  mechanism in the inventory is the only integration point for new edits.
- **FLAC is the enforced package audio format** — `docs/design/architecture.md` "Technology
  Stack"; untouched here.
- **Format changes route through plan 10** —
  `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` Non-goals and Dependencies name this
  plan as the first real bump; its rollback notes state the first real migration step ships here.
- **Package timing precision is a fixed 3-decimal grid** — `docs/design/architecture.md` "Song
  Data Model"; the preview fields reuse it (seconds, `{:.3f}`), matching the `startOffset`
  precedent rather than inventing a new time spelling.
- **The game never rewrites user packages** — `docs/plans/roadmap/26-game-startup-menus-library.md`
  constraints; all authoring here is editor-side, the game only reads.

## 7. Open questions for the user

Mirrored into `docs/plans/roadmap/00-roadmap.md` Decisions-needed. Phase 0 presents these and STOPS.

- **Q1 — required metadata vs save==publish.** How does "required for export" coexist with
  normalize-don't-reject saves?
  (A) *Split publish validation from save* (recommended). The shared writer stays
  validation-free; a pure editor-core readiness check runs only in the publish action path
  (before `Project::publish`), returning typed blockers the UI renders with an "open Song
  Information" affordance. Save/Save As remain unconditional. Publish and save are already
  distinct actions with distinct paths (inventory), so the split is a gate placement, not an
  architecture change; plan 42's content validation later appends to the same list.
  (B) Placeholder-with-warning: publish always succeeds, auto-filling "Unknown Artist"/year 0
  with a warning. Rejects the product goal — the game library fills with junk rows and nothing
  ever forces completion.
  (C) Enforce in the writer (save also rejects). Violates the established invariant and bricks
  saving work-in-progress imports (every GP import lacks year today).
- **Q2 — which fields hard-block publish.**
  (A) *All five: title, artist, album, year, album art* (recommended — matches the product goal;
  the game keeps a fallback tile for pre-existing v1 packages anyway, per
  `docs/plans/roadmap/26-game-startup-menus-library.md` Phase 3).
  (B) Title/artist/year hard; album and art soft (warning, publish proceeds). Choose this only
  if requiring art is judged too hostile for quick personal exports.
- **Q3 — canonical art policy.**
  (A) *Transcode at import to one JPEG master, quality 0.85, max dimension 1024, aspect
  preserved, downscale-only, alpha flattened onto solid black* (recommended; the
  juce-tracktion-expert findings above drive every term: JPEG-only photographic payload, the
  alpha-drop pitfall, the >2x bilinear aliasing pitfall). Package entry `art/cover.jpg`,
  referenced as `metadata.art`. Predictable package size and one decode shape for the game.
  (B) Store the user's original file (PNG/JPEG/GIF passthrough), constraints checked only.
  Preserves originals but ships multi-megabyte PNGs and forces the game to handle three formats.
- **Q4 — sort-field storage semantics.**
  (A) *Store only explicit overrides* (recommended): `sortTitle`/`sortArtist`/`sortAlbum` are
  absent unless the charter enables that field's toggle; the dialog pre-fills an auto-derived
  suggestion (leading English article stripped: "The ", "A ", "An ") when toggled on; consumers
  fall back to the raw field when absent (exactly the fallback plan 26 Phase 7 already specifies).
  (B) Always write derived sort fields. Duplicates derivable data, drifts when title edits
  forget the sort twin, and violates the derived-over-authored spirit for the common case.
- **Q5 — where the shared art codec lives.**
  (A) *`rock-hero-common/ui`, feature folder `art/`* (recommended): the codec needs
  `juce_graphics`, which is beyond `common/core`'s narrow `juce_core` permission
  (`docs/design/architecture.md` "JUCE utility dependency in core modules"), and both products
  need it (editor import-time processing now, plan 26's album-art adapter next) — the placement
  procedure (`docs/design/architectural-principles.md` "Placement Procedure for New Files")
  lands exactly here, and the library's README reserves it for this moment. This turns the
  placeholder library real and adds `rock_hero::juce_graphics` to the shared build graph.
  (B) Duplicate ~60 lines in editor/ui and game — violates constraint (a)'s extract-first rule.
  (C) Extend `common/core`'s JUCE permission to `juce_graphics` — drags a rendering stack into
  the headless domain library; rejected shape.
- **Q6 — chart "version" field.** Should packages carry an authored chart/package version number
  (e.g. "v2 of my chart of Song X")?
  (A) *No authored version; resolve identity via plan 10's chart-identity hash* (recommended).
  Any content edit changes the hash, which is what leaderboards
  (`docs/plans/roadmap/29-online-leaderboards.md`) and score records key on; an authored counter is
  authored metadata that drifts, double-counts, or forgets — exactly what constraint (d) exists
  to prevent. `formatVersion` (schema) already exists and is unrelated to content revisions.
  (B) An authored integer the charter bumps manually. Human-maintained, unverifiable, and
  redundant beside the hash.

## 8. Phased implementation

### Phase 0 — decision gate

Present Q1–Q6 with the inventory evidence. **STOP — present findings and get sign-off.** No
later phase starts before this gate closes and plan 10's Phase 0/Phase 2 have landed. No code
changes.

**Exit criteria**: the user has answered Q1–Q6 and signed off, and plan 10's Phase 0 and
Phase 2 have landed.
**Verification**: none — no code changes in this phase.

### Phase 1 — song.json metadata extensions *(assumes plan 10 Q1-A and Q4-A; Q3-A, Q4-A here)*

- **Scope** (`rock-hero-common/core` only):
  1. `SongMetadata` gains `std::string art;` (package-relative image path, empty = none),
     `std::string sort_title, sort_artist, sort_album;` (empty = no override), and
     `std::optional<PreviewWindow> preview;` where `PreviewWindow { double start_seconds; double
     length_seconds; }` — spelled in JSON as `"art"`, `"sortTitle"`, `"sortArtist"`,
     `"sortAlbum"`, `"previewStart"`, `"previewLength"` inside the existing `metadata` object.
  2. Writer: emit each new field only when present/non-empty (`songDocumentContents`,
     `rock_song_package_write.cpp:339-385`); preview values at the 3-decimal timing grid
     (`formatTimingValue`); bump the emitted `formatVersion` to 2 (line 345). Write-side art
     validation mirrors `validateToneDocumentOnDisk` (`rock_song_package_write.cpp:428`): a
     non-empty `art` ref must be canonical-safe and exist in the workspace or the save fails
     before side effects — the same *structural* contract as tone/audio refs, no completeness
     rule, so save==publish is preserved.
  3. Reader: parse the new optional fields in `readMetadata`
     (`rock_song_package_read.cpp:169-183`); validate a present `art` ref with the existing
     safe-path checks. Normalization (never rejection): a dangling `art` ref (safe path, file
     absent) clears the ref; a preview with negative start or non-positive length drops the
     pair — both surface through plan 10's `SongReadResult.migrated`-style signal so the editor
     marks unsaved changes. Rationale for the asymmetry against audio/tone refs: those targets
     are required for the package to *function*; art and preview are optional presentation, and
     hard-failing a whole package over decoration contradicts the normalize precedent.
  4. Register the v1→v2 migration step in plan 10's ladder (the first real step): pure
     default-fill — v1 documents parse as v2 with all new fields absent. Opening a v1 package
     thereafter reports `migrated == true`, the editor marks unsaved changes (plan 10's signal),
     and re-saving writes v2.
  5. If plan 10's Q1 instead resolves to (B), this phase shrinks: fields land under
     `formatVersion 1` with no ladder step; everything else is identical.
- **Files**: `song/song.h`, `package/rock_song_package_write.cpp`,
  `package/rock_song_package_read.cpp`, plan 10's migration registry TU.
- **Public-header impact**: new fields on `SongMetadata` (existing public header); no new headers.
- **Testing** (`rock-hero-common/core/tests/test_rock_song_package.cpp`): round-trip all new
  fields; absent fields omit their keys byte-identically to today's output modulo the version
  digit; unsafe art path (`../escape.jpg`) rejected on read AND write; dangling art ref
  normalized to absent with the migrated/normalized signal set; invalid preview dropped likewise;
  v1 fixture parses via the ladder with `migrated == true`.
  **Local corpus check** (never CI): all 39 packages open, migrate in memory, save to a temp
  workspace, reopen — metadata intact, art/sort/preview absent, zero failures.
- **Exit criteria**: tests green; corpus 39/39; a v2 package with art round-trips.
- **Verification** (code + behavior changed → build + tests):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — editor-core Song Information workflow and undo

- **Scope**:
  1. `Session` gains one narrow mutation surface: `SongMetadata* metadata() noexcept` (null when
     no song is loaded), following the `currentToneTrack()`/`currentToneCatalog()` pattern
     (`session.h:59-77`).
  2. New editor-core feature folder `song_information/`: a `SongInformationDraft` value (all
     dialog fields), a pure `normalizeSongInformationDraft` (trim whitespace; year clamped to 0
     or 1000–9999; sort overrides trimmed, cleared when equal to the auto-derivation; preview
     validated as in Phase 1), and a pure `suggestedSortValue(const std::string&)` helper
     implementing the Q4-A article-stripping rule (also consumed by the dialog's auto-fill).
  3. `SongInformationEdit final : IEdit` — full before/after `SongMetadata` memento (constraint
     (f)); apply writes through `Session::metadata()`. Draft equal to current state returns
     `EditorUndoFailureCode::NoNetMutation` (`editor_undo_history.h:67`) and records nothing.
  4. Controller intents on `IEditorController`: `onSongInformationRequested()` (opens the dialog
     via view state), `onSongInformationSubmitted(SongInformationDraft draft)`,
     `onSongInformationDismissed()`. View state gains an `std::optional<SongInformationViewState>`
     (current values + album-art image file path + publish-blocker summary once Phase 5 lands),
     following the prompt-struct pattern (`editor_view_state.h:59-108`). Handlers live in a new
     `song_information_handlers.cpp` per the multi-TU controller convention.
  5. Art file staging (byte-level only; no image decoding in core):
     `onAlbumArtSelected(std::vector<std::byte> canonical_jpeg)` writes `art/cover.jpg` into the
     project workspace and sets `metadata.art`; `onAlbumArtCleared()` removes the ref and file.
     Both are mementos carrying old/new file bytes (a ≤1024px q0.85 JPEG is a few hundred KB).
     Core treats the bytes as opaque, like tone documents; codec work is Phase 3's, at the UI
     boundary.
- **Files**: `session/session.{h,cpp}` (common/core),
  `rock-hero-editor/core/{include,src}/.../song_information/*`, `controller/i_editor_controller.h`,
  `controller/editor_view_state.h`, new `song_information_handlers.cpp`, `editor_action_id.h`
  (dialog open availability: requires a loaded project).
- **Public-header impact**: one `Session` accessor; the draft/view-state/intent surface in
  editor-core public headers (its views need them).
- **Testing** (new `rock-hero-editor/core/tests/test_editor_controller_song_information.cpp`):
  submit updates session metadata, marks dirty, undo restores the prior struct and redo reapplies
  (via `operator==`); unchanged submit records no undo entry; draft normalization table cases;
  art select/clear round-trips bytes through undo; save-after-edit persists via
  `test_project.cpp` (metadata write already covered at lines 761-780 — extend with
  art/sort/preview).
- **Exit criteria**: all listed behaviors proven headless; no UI yet.
- **Verification**:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — shared album-art codec in rock-hero-common/ui *(assumes Q3-A, Q5-A)*

- **Scope**: the first real code in `rock-hero-common/ui`, feature folder `art/`:
  `album_art_codec.h/.cpp` exposing pure functions over `juce::Image`/bytes —
  `decodeAlbumArt(span<const byte>) -> expected<juce::Image, AlbumArtError>` (PNG/JPEG/GIF via
  `juce::ImageFileFormat`; anything else → typed `UnsupportedImageFormat` naming the supported
  formats), `prepareMasterArt(juce::Image, int max_dimension = 1024)` (flatten alpha onto solid
  black, aspect-preserving downscale-only via successive halvings then one exact `rescaled` —
  the verified anti-aliasing procedure), and
  `encodeAlbumArtJpeg(const juce::Image&, float quality = 0.85f) -> MemoryBlock`. All operations
  use `juce::SoftwareImageType` so they run headless and off the message thread (verified,
  inventory). CMake: link `rock_hero::juce_graphics`; remove the include-root `.gitkeep` per the
  library README; new test target `rock_hero_common_ui_tests` linking `rock_hero::common::ui`
  (the production library, matching the project's test-target convention).
- **Editor consumption**: the Song Information dialog (Phase 4) calls the codec on the chooser
  result and passes canonical JPEG bytes to `onAlbumArtSelected` — media conversion stays in UI,
  policy (file placement, undo) stays in core (`docs/design/architectural-principles.md`,
  "Humble Object, But With the Right Scope"). Plan 26's `IAlbumArtGenerator` adapter is the
  codec's second consumer (its "verify JUCE image decode formats" checkpoint is discharged by
  this plan's verified findings).
- **Public-header impact**: first public header of `rock-hero-common/ui`
  (`include/rock_hero/common/ui/art/album_art_codec.h`); typed `AlbumArtError` beside it.
- **Testing** (`rock-hero-common/ui/tests/test_album_art_codec.cpp`, self-authored tiny
  fixtures): decode succeeds for minimal PNG/JPEG/GIF bytes and fails typed for a text buffer;
  alpha PNG flattens to opaque (corner pixel check); a 1537x1023 synthetic gradient downscales
  to ≤1024 preserving aspect within one pixel; already-small images pass through un-upscaled;
  encoded output starts with JPEG SOI bytes and re-decodes to the same dimensions.
- **Exit criteria**: tests green headless (no display); clang-tidy clean on the new TU.
- **Verification** (new target + link → configure warranted; new lint surface):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```

### Phase 4 — Song Information dialog (editor/ui)

- **Scope**: a `song_information/` feature in `rock-hero-editor/ui`: modal-style window following
  the `audio_device_settings_window.cpp` precedent, opened from a new File-menu item "Song
  Information..." (`editor_view.cpp:663-671`) and driven by Phase 2's view state. Contents:
  title/artist/album text fields; digits-only year field; art panel (aspect-preserving preview via
  `juce::Graphics::drawImageWithin` + `RectanglePlacement` — verified facility, "Choose Image..."
  chooser filtered to `*.png;*.jpg;*.jpeg;*.gif`, "Remove"); per-field sort toggles revealing an
  editor pre-filled from `suggestedSortValue` (Q4-A); preview start/length numeric fields
  (seconds). Decode/transcode failures (including the WebP/HEIC rejection) render inline in the
  dialog and never reach the controller. OK submits one draft → one undo entry; Cancel dismisses
  without side effects. Theme via the existing `EditorTheme` seam (`editor_theme.cpp`).
- **Files**: `rock-hero-editor/ui/{include,src}/.../song_information/*`,
  `main_window/editor_view.cpp` (menu item + window ownership).
- **Public-header impact**: editor/ui headers only (product-private).
- **Testing**: per `docs/design/architectural-principles.md` "Selective UI Wiring Tests" — one
  wiring test: constructing the dialog from a populated view state shows the stored values and
  OK emits the composed draft intent (fake controller); field/normalization logic is already
  covered headless in Phase 2. Manual checklist for chooser interactions recorded in-session.
- **Exit criteria**: dialog edits every field end-to-end against a real project; undo/redo from
  the menu reverts and reapplies edits including art.
- **Verification** (code changed → build; wiring behavior → tests):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 5 — publish readiness gate *(assumes Q1-A, Q2 outcome)*

- **Scope**: pure editor-core function in the `project/` feature:
  `publishBlockers(const common::core::Song&) -> std::vector<PublishBlocker>` where
  `PublishBlocker { PublishBlockerCode code; std::string message; }` — codes per Q2's required
  set (MissingTitle, MissingArtist, MissingAlbum, MissingYear, MissingArt). The publish action
  path (`project_handlers.cpp`, the `PublishProject` flow) runs it before starting the busy
  publish; a non-empty list cancels the operation and surfaces a view-state prompt listing every
  blocker with an "Open Song Information" affordance. Save and Save As remain untouched — the
  Q1-A split. The blocker list is deliberately generic so `docs/plans/roadmap/42-chart-validation.md`
  can append content blockers to the same vector without changing the gate seam.
- **Files**: `rock-hero-editor/core/{include,src}/.../project/publish_readiness.{h,cpp}` (header
  public — the view renders blockers), `project_handlers.cpp`, `editor_view_state.h`,
  `rock-hero-editor/ui` prompt presentation in `editor_view.cpp`.
- **Public-header impact**: `publish_readiness.h` + view-state addition.
- **Testing**: pure table tests for `publishBlockers` (each missing field, all present, year 0
  vs valid); controller test proving a blocked publish never calls the injected publish function
  (`publish_call_count == 0`, exact pattern of `test_editor_controller_busy.cpp:452-467`) and a
  complete song publishes exactly as today; blocker prompt appears in derived view state.
- **Exit criteria**: publishing an incomplete GP import is blocked with the field list; filling
  the dialog then publishing succeeds; saves never gated.
- **Verification**:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## 9. Final acceptance phase

Per constraint (h), the one sanctioned bundle, as separate invocations from the repo root, plus
formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: the Phase 1 local corpus table (39/39) recorded; one end-to-end
pass (import GP → fill Song Information incl. art → publish → reopen the published `.rock`,
metadata and art intact); and the Q1/Q2 outcomes recorded in `docs/plans/roadmap/00-roadmap.md`.

## 10. Rollback/abort notes

- **Phase 1 is the risky one**: once any package is saved at `formatVersion 2`, older builds
  hard-reject it — plan 10's intended data-loss firewall, but it means this phase lands only
  after plan 10's ladder is merged and signed off. Revert is clean while v2 packages exist only
  in local temp workspaces; the corpus stays v1 on disk until deliberately re-saved. Keep the
  migration step and field additions in one commit so a revert restores a coherent v1-only
  writer.
- **Phase 2/5 ride on editor-core files that currently have in-flight tone work in the working
  tree** (inventory); start them only from a clean tree after that work lands, and rebase rather
  than merge around `editor_view_state.h`/`i_editor_controller.h` hotspots.
- **Phase 3** turns the placeholder `rock-hero-common/ui` library real and adds
  `rock_hero::juce_graphics` to the shared graph; keep it an isolated commit so a revert restores
  the placeholder exactly (re-adding `.gitkeep` per the library README). If the codec proves
  wrong-shaped for plan 26's album-art adapter, fix it here — plan 26 must not fork a second
  image path.
- **Phase 5** is independently revertible: removing the gate returns publish to today's ungated
  behavior without touching the format or the dialog. Do not weaken it by auto-filling
  placeholders as a "temporary" fallback — that is rejected option Q1-B by the back door.
- Art undo mementos hold file bytes in memory; if repeated art swaps ever make history memory a
  real problem, cap art-edit history depth as a follow-up rather than weakening full-state
  restore (constraint (f); `docs/plans/completed/editor-undo/editor-undo-plan.md`).
