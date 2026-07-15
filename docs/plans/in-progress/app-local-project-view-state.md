# App-local project view state (selected arrangement + settings storage simplification)

Status: in progress. Alternatives were reviewed (independent from-scratch design draft plus an
adversarial critique, reconciled 2026-07-07); the decisions below are final and the phase plan is
implementable top to bottom from the current tree state. Implementation state is at the bottom.

## Goal

Two related threads on branch `tone-capture-scope`:

1. **Move `selectedArrangement` out of the `.rhp` project file into per-user app settings**, joining
   the other per-project *view/resume* state (cursor, grid note value, timeline zoom) that already
   lives in `EditorSettings`.
2. **Simplify how per-project settings are stored** — the `KeyedRecordStore` + per-family `Codec`
   pattern is heavier than the data warrants for these values.

A third, already-finished change rides on the same branch and is independent of the above:
the fresh-open arrangement default now walks **Lead -> Rhythm -> Bass** before falling back to the
first stored arrangement (`getSelectedArrangementIndex` in `project_handlers.cpp`), with a
regression test. That work is complete; it is only mentioned here so the branch state is clear.

## Background / current design

- `EditorSettings` wraps one `juce::PropertiesFile` (`storeAsXML`) at
  `C:\Users\<user>\AppData\Roaming\Rock Hero\Rock Hero Editor.settings` on Windows. One file holds
  every editor setting.
- Settings come in two shapes:
  - **App-wide scalars** (`waveformVisible`, `audioDeviceState`, `lastOpenProject`, ...) — plain
    `getValue`/`setValue`; getters return `std::optional<T>`, setters return
    `std::expected<void, EditorSettingsError>` (the error is a `saveIfNeeded` disk-write failure).
  - **Per-project-path records** (cursor, grid note value, timeline zoom) — stored as an XML *list*
    under one property per family, via the `KeyedRecordStore<Codec>` template plus a ~60-line
    `Codec` per family; getters return `std::expected<std::optional<T>, EditorSettingsError>`.
    `KeyedRecordStore` is also used by a **fifth** family, input calibration.
- `selectedArrangement` currently lives in the `.rhp`'s `project.json` under an `editorState`
  object. It is the **only** field of `ProjectEditorState`, and it is threaded through
  `Project::save/saveAs`, the injected `SaveFunction`/`SaveAsFunction` contracts, `project_io`,
  and the test harness.

## Alternatives considered

### Where `selectedArrangement` lives

- **In-package `project.json` `editorState` (status quo) — rejected.** It contradicts the settled
  view-state policy (the cursor helper's own comment: view state lives "outside the .rhp package"
  so view changes never dirty content). Because it is only captured at save time, a switch without
  a following save is silently lost — worse resume behavior than the settings families. It also
  makes two saves of identical musical content byte-unstable, and a shared package carries the
  previous author's view state as noise. One optional string forces an entire threading axis
  through save contracts and the test harness.
- **Sidecar file next to the `.rhp` — rejected.** Two-file identity (rename orphans the sidecar,
  copy sometimes carries it), litters user folders, and needs a new IO path and error taxonomy for
  one string. No precedent in the codebase.
- **Separate member inside the `.rhp` zip — rejected.** Persisting view state would rewrite a
  FLAC-bearing archive, and it could only land at save anyway (the editor works on the extracted
  workspace). All of the status quo's costs, none removed.
- **App settings keyed by a minted project UUID — rejected.** Minting a UUID into `project.json`
  dirties every existing package on open (a bug class this project already fought), and copied
  packages share the UUID so diverging copies fight over one record — worse than path keying for
  the copy case. It would also split identity semantics from the cursor/grid/zoom trio. Revisit
  only if per-project state ever becomes precious; view state is not.
- **Do not persist; always derive — rejected.** A real regression for multi-arrangement charting
  (working on Bass across sessions would reopen to Lead every time). The derived default already
  exists as the fallback.
- **Chosen: app settings keyed by normalized project path**, exactly like cursor/grid/zoom.
  Accepted tradeoff (the same one the trio already pays): the record does not travel to another
  machine and is orphaned (harmlessly) by rename/move; the failure mode is the guitar-forward
  default.

### How per-project scalars are stored

- **Keep `KeyedRecordStore` and factor the duplicated method bodies — rejected.** This preserves
  typed corruption errors and per-record format versions, but source inspection shows the store's
  robustness is thinner than it looks: individually corrupt records are *already* silently dropped
  at load and pruned on the next save; the preserve-and-report path only fires for whole-blob
  corruption; and whole-*file* corruption defeats every layout (a `PropertiesFile` that fails to
  parse loads empty and is rewritten on the first set — taking calibration with it either way). So
  the machinery mainly guards convenience scalars against bugs and hand-edits, at the cost of
  ~60-line codecs plus near-duplicate method bodies per family.
- **One grouped record per project (`projectViewState:<path>` holding all families) — rejected,
  narrowly.** One key per project and per-project corruption isolation are real, but the families
  write at different times (read-modify-write churn), one format-version root couples schemas that
  evolve independently (the grid retirement is the existence proof), and it migrates three live
  families for no functional gain.
- **Per-project files under AppData — rejected.** A second persistence mechanism (directory
  management, hashed names, orphan cleanup) for what is currently four scalars per project.
- **Chosen: flat keys — one top-level property per `(family, project)`**, key =
  `"<family>:" + normalizedPath` via the existing `projectSettingsKeyFor` normalizer, value = one
  string. Each value is independent, so the corrupt-shared-blob failure mode disappears rather
  than being traded away; for convenience data, "one corrupt value -> that one value resets" is
  the correct behavior. Honest losses, all accepted for these families: the per-family
  missing-vs-corrupt distinction, per-record `formatVersion` (retire by renaming the key prefix —
  the move already used for the grid), and blob-level refuse-to-clobber.
- **Multi-scalar rule (binding on future families):** one key per `(family, project)`; a family
  with several scalars encodes **one composite token value**, following the grid's `"num/den"`
  precedent. The upcoming loop-selection family (plan 47, two grid-position tokens) stores one key
  with one composite value — never two keys whose writes could tear.

### Related policy calls

- **Dead-key cleanup: none.** The repo precedent is to orphan retired keys, not migrate or delete
  them (`projectGridSpacings` from the grid retirement; the obsolete flat calibration keys, with a
  test asserting they are ignored). The old list keys — `projectCursorPositions`,
  `projectGridNoteValues`, `projectTimelineZooms`, and `projectSelectedArrangements` (written by
  the superseded codec on this branch) — are simply ignored. Existing cursor/zoom/grid records
  reset once; trivial for convenience state on a dev machine.
- **No migration of old in-package selections.** Old `.rhp` files' `editorState` is ignored
  (tolerated, not honored) — a one-time benign fresh-open default, matching the tone-format
  precedent of dropping legacy spellings pre-release.
- **Persist timing: on successful switch, not at close/save.** Unlike the cursor (continuous —
  must batch to lifecycle points), an arrangement switch is a rare, discrete, already-expensive
  operation, and the value cannot drift between switches, so close/save hooks add nothing.
  Switch-time persistence buys crash recovery and sidesteps the import-displacement path that
  destroys the old project without a close persist.

## Key decisions

1. **`selectedArrangement` moves to app settings**, keyed by normalized project path (see
   alternatives). The value is the arrangement **UUID string** (indices are unstable; the song
   model and resolver already speak ids). The settings layer never validates the id against the
   song — existence resolution stays the controller's concern.
2. **The whole `ProjectEditorState` scaffolding is removed.** `project.json` remains a
   `{ "formatVersion": 1 }` manifest (still validates a workspace). `formatVersion` stays 1 —
   verified both directions: the old reader tolerates a missing `editorState`, the new reader
   ignores a legacy one; bumping would gratuitously break old binaries.
3. **The four per-project scalar families flatten to flat keys** (see alternatives), each family
   collapsing from a codec plus two ~25-line methods to two ~6-line methods. Implementation
   guards: an **empty project path is a no-op/nullopt** (the normalizer returns an empty key —
   never let all pathless writes share one key); doubles are written via JUCE's round-trip
   serialization and read with a strict parse (corrupt -> nullopt); keys embed full paths as XML
   attribute values (backslashes/colons/spaces are auto-escaped; note: non-ASCII path characters
   are emitted as numeric character references — round-trips correctly, just not human-readable).
   Multi-instance behavior is unchanged from the list store: whole-file last-writer-wins, atomic
   temp-file writes.
4. **Input calibration keeps `KeyedRecordStore` + `InputCalibrationCodec` untouched.** It is
   genuinely different: keyed by structured physical-route identity, has a real `remove` op, and
   is measured (not reconstructible) data whose silent reset would be harmful. Two storage
   patterns coexist deliberately; a future structured per-project family (e.g. plugin-window
   bounds, see the docs note in Phase 4) may one day add a third shape — that is a decision for
   that plan, not this one.
5. **Per-project getters change to plain `std::optional<T>`**, matching the app-wide scalar
   convention (`waveformVisible()`), because flat-key getters cannot fail — corrupt values read as
   absent *by design*. Savers keep `std::expected<void, EditorSettingsError>` (disk-write failures
   are real). The four `InvalidProject*History` error codes (Cursor, GridNoteValue, TimelineZoom,
   SelectedArrangement) and their default messages are deleted; `InvalidInputCalibrationHistory`
   stays. Call sites lose the double-optional dance.
6. **Persistence points:** in the `SelectArrangement` handler after the session commit succeeds
   (best-effort, keyed by `m_project_file` when non-empty), and in
   `applyProjectWriteSuccess(SaveProjectAs)` beside the existing grid/zoom writes (first moment an
   imported project has a path). Restore happens in `completeOpenProject` via a best-effort
   settings read passed into `loadSessionSong`; imports keep passing `nullopt`. The open-time
   fallback is **never written back** as if user-chosen — no laundering a derived default into
   stored state.
7. **A dangling persisted UUID falls back to the Lead -> Rhythm -> Bass walk**, not raw index 0.
   Today `getSelectedArrangementIndex` only runs the walk for `nullopt`; a stale id semantically
   *is* "no usable choice", and staleness becomes reachable once write-time validation
   (`selectedArrangementForSave`) is deleted. Silent (log at most). This dispositions the TODO at
   `project.cpp:406` about surfacing that mismatch — the fallback becomes the deliberate best
   default, and the TODO is deleted with its surrounding code.

## Semantics changes (explicit)

- Close-with-discard now remembers the last switched arrangement (it is view state persisted at
  switch time), where the `.rhp` field would have reverted to the last save. Consistent with
  "where I left off"; intended.
- Arrangement switching never touches dirty state — already the intent, now actually true (the
  choice is no longer lost unless a save happens to follow).
- Old packages' saved selections are abandoned (see policy calls); saved cursor/zoom/grid records
  under the old list keys reset once.

## Implementation plan

Each phase ends with a compiling tree and green touched tests. Phase 1 must come first: the tree
does not currently compile (`ProjectEditorState` is already removed from `project.h` but still
referenced widely).

### Phase 1 — Complete the `ProjectEditorState` tear-out

1. `project.cpp`: remove `m_editor_state` (init/move/close resets), `editorState()`, the
   `editor_state` params on `save`/`saveAs`, the load-time `editorState` read, and the
   stale-selection TODO (`:406`, dispositioned by decision 7).
2. `project_io.h`/`.cpp`: `readProjectDocument` validates `formatVersion` only and returns
   `std::expected<void, ProjectError>`; drop `editorState` parsing, `selectedArrangementForSave`,
   and the `arrangement_ids` threading from `writeProjectDocument`/`writeProjectFiles`; remove
   `readOptionalString` if now unused. `project.json` is written as `{ "formatVersion": 1 }`.
3. `editor_controller.h`: `SaveFunction`/`SaveAsFunction` shrink to `(Project&, const Song&)`;
   `editor_controller.cpp` wrappers follow.
4. `editor_controller_impl.h`: remove the `projectEditorStateForSave` decl and
   `ProjectWriteTaskState::editor_state`.
5. `project_handlers.cpp`: `completeOpenProject` passes `std::nullopt` to `loadSessionSong` (the
   settings read arrives in Phase 3); remove `projectEditorStateForSave` and the worker save
   call's `editor_state` argument. Restructure `getSelectedArrangementIndex` so the not-found
   branch reuses the Lead -> Rhythm -> Bass walk (decision 7).
6. `rock-hero-common/audio/src/tracktion/plugin_window.cpp:481`: update the stale comment that
   references `ProjectEditorState` plumbing.
7. Tests: `editor_controller_test_harness.h` drops the `editor_state` save/saveAs params and the
   `last_save_editor_state`/`last_save_as_editor_state` members; lifecycle tests drop the
   `ProjectEditorState` literals (settings-based assertions arrive in Phase 3); `test_project.cpp`
   drops the `editorState` round-trip and simplifies `projectDocumentEntry`; add a
   stale-id-falls-back-to-walk regression test.
8. Verify: build + editor-core tests. (Intermediate behavior: selection is not persisted anywhere;
   fresh opens use the default walk. Acceptable on the branch.)

### Phase 2 — Flatten per-project settings storage

1. `i_editor_settings.h`: the four per-project getters become plain `std::optional<T>`
   (`TimePosition`, `Fraction`, `double`, `std::string`); savers keep their signatures.
   `editor_settings.h`, `NullEditorSettings`, and all call sites follow (the cursor/grid/zoom
   restore helpers in `project_handlers.cpp` lose the error-arm logging).
2. `editor_settings.cpp`: add one private flat-key helper (`"<family>:" +
   projectSettingsKeyFor(path)`, empty path guarded per decision 3); rewrite the four get/save
   pairs as flat-key get/put — cursor/zoom as round-trip doubles with strict parse, grid as
   `"num/den"`, arrangement as the raw UUID string. Delete the four per-project `Codec`s, their
   `State` structs, and their `using ...Store` aliases (including the superseded
   `ProjectSelectedArrangementCodec`). `KeyedRecordStore`, `hasCurrentXmlFormat`, and the
   calibration codec stay.
3. `editor_settings_error.h`/`.cpp`: delete the four `InvalidProject*History` codes and their
   default messages.
4. No cleanup or migration code — old list keys are orphaned (see policy calls).
5. `test_editor_settings.cpp`: replace list-parsing/dedup/blob-corruption tests with flat-key
   round-trips (numeric-exact for doubles); keep a same-path-different-spelling normalization
   test; add a path-with-spaces+unicode round-trip and a corrupt-value-reads-as-absent test;
   retire the preserve-on-corruption tests (that behavior is deliberately gone).
6. Verify: build + settings tests + touched controller tests.

### Phase 3 — Route `selectedArrangement` through settings

1. `completeOpenProject`: best-effort read `m_settings.projectSelectedArrangementFor(state->file)`
   and pass it into `loadSessionSong`; imports keep `nullopt`.
2. `SelectArrangement` success point: after the session commit succeeds, persist the new id via
   `recordSettingsResultBestEffort` when `m_project_file` is non-empty.
3. `applyProjectWriteSuccess(SaveProjectAs)`: persist the currently displayed arrangement id
   beside the existing grid/zoom writes. No close/save hooks (see policy calls).
4. Lifecycle tests: persisted-at-switch, persisted-at-save-as, restored-at-open,
   dangling-id-falls-back-to-walk, and no-write-back-at-open.
5. Verify: build + lifecycle tests.

### Phase 4 — Docs + final verification

1. `docs/plans/roadmap/47-editor-loop-selection.md`: update the Q3 inventory (`editorState` is gone;
   selection is app-local per-path like cursor/grid/zoom) and record the flat-key convention its
   loop family must follow (one key, one composite token value — see the multi-scalar rule).
2. `docs/plans/todo/plugin-window-persistence.md`: add a staleness note — `ProjectEditorState` no longer
   exists; per-arrangement window bounds are machine-local and would target an app-settings home
   with a structured shape to be designed when that plan is picked up.
3. `docs/design/architecture.md`: verified — it only describes `project.json` generically ("stores
   `project.json` at the root"); no change needed.
4. Final: full build, touched test suites, whole-project clang-tidy, pre-commit — as separate
   invocations per the agent-build rules.

## Implementation state

**COMPLETE (uncommitted).** All four phases landed; the tree builds, all four test suites pass,
whole-project clang-tidy is clean, and pre-commit (clang-format + conventions) passes.

- **Phase 1 — `ProjectEditorState` tear-out:** struct/overloads/`editorState()`/`m_editor_state`
  removed from `Project`; `project_io` `readProjectDocument` validates `formatVersion` only and
  `project.json` is written as `{ "formatVersion": 1 }`; `SaveFunction`/`SaveAsFunction` and the
  worker save call shrank; `getSelectedArrangementIndex` restructured so a missing OR stale id
  reuses the Lead->Rhythm->Bass walk (`defaultArrangementIndex`); `:406` TODO and the
  `plugin_window.cpp` comment updated; harness + `test_project` + lifecycle tests updated.
- **Phase 2 — flat-key settings:** the four per-project families store one flat key each
  (`"<family>:" + normalizedPath`); `KeyedRecordStore` + the four codecs/State/Store + the four
  `InvalidProject*History` error codes deleted (calibration keeps `KeyedRecordStore`); getters are
  plain `std::optional<T>`; `test_editor_settings` rewritten (round-trips, normalization, unicode
  path, corrupt-as-absent, legacy-list-ignored).
- **Phase 3 — selection through settings:** restored in `completeOpenProject`, persisted on
  successful `SelectArrangement` and in `applyProjectWriteSuccess(SaveProjectAs)`; five lifecycle
  tests (restore, stale->walk, no-write-back, persist-on-switch, persist-on-save-as).
- **Phase 4 — docs + verify:** plan 47 Q3 inventory + multi-scalar rule updated;
  plugin-window-persistence staleness note added; architecture.md verified unchanged; full verify
  bundle green.

Not yet committed — awaiting review.
