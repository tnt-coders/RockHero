# Editor Engine And Undo Master Plan v2

Status: in-progress master plan. Supersedes `editor-engine-undo-master-plan.md` (v1), which has
been removed. This document coordinates the active implementation order across
`remaining-god-object-decomposition-plan.md`, `editor-undo-plan.md`,
`test-fixture-opportunities-plan.md`, and the completed `editor-logging-plan.md`. It does not
replace those documents; it defines the preferred sequence so the project does structural risk
reduction and feasibility validation before undo/redo adds rollback, replay, dirty-state, and
history behavior to the same seams.

## What Changed From v1

- **Added the Tracktion/JUCE behavior spike as an explicit early phase (Phase 2).** v1 omitted it,
  even though `editor-undo-plan.md` makes it a Stage 0 prerequisite. It is the true long pole:
  rollback feasibility, the safe undo-quarantine mechanism, and plugin-parameter observation are all
  unknown until it runs, and Phase 4 (audio undo-readiness) and Phase 8 (parameter entries) cannot
  be finalized without its findings.
- **Tightened Phase 0 logging reconciliation.** The durable file-backed logger that
  `editor-undo-plan.md` names as its only startup prerequisite already exists (the Quill-backed
  `Logger` facade plus `JuceQuillBridge`). `editor-logging-plan.md` was moved to completed with a
  result note, not merely "revised", and the undo prerequisite is treated as already satisfied.
- **Recorded the Part B (ISP) reconciliation.** `remaining-god-object-decomposition-plan.md` left
  the `IEditorController` segmentation as "modest, maybe not worth finishing". This plan supplies the
  decisive reason to do the minimal version: undo introduces `onUndoRequested`/`onRedoRequested` and
  later edit intents, and segmenting first gives them a clean home instead of swelling the 35-method
  aggregate. Part B is therefore in scope at minimal size (Phase 6).
- **Noted phase independence/parallelism.** The Engine seam split (Phase 3), the spike (Phase 2),
  and the pure undo model (Phase 5) are largely independent. The pure `EditorUndoHistory` is the
  cheapest derisking and may begin as early as after Phase 1.
- **Reconciled folder guidance with the decomposition plan.** Both documents now say: start flat
  under `src/`, and earn a `src/tracktion/` subfolder only if a real private cluster emerges. The
  decomposition plan was updated to drop its pre-created `tracktion/` layout.
- **Added a cross-agent build note** (see Ordering Principles): verification steps describe the
  required passing state, not that any particular agent runs the build.

## Goal

Reach user-visible editor undo/redo with the least avoidable risk.

The preferred path is:

1. Validate undo feasibility against real Tracktion/JUCE behavior with a small spike.
2. Do the behavior-preserving `Engine` implementation-seam split (independent of the spike).
3. Make the audio boundary undo-ready, backed by the spike findings.
4. Build undo/redo in headless editor-core, then wire user-visible Undo/Redo only after the selected
   edit scope is fully covered.

This order spends a small amount of time validating feasibility and reducing adapter complexity
before adding rollback, replay, dirty-state, and history behavior that must call through that
adapter.

## Source Plans

- `docs/in-progress/remaining-god-object-decomposition-plan.md` owns the `Engine` implementation
  split and the minimal `IEditorController` segmentation.
- `docs/in-progress/editor-undo-plan.md` owns undo/redo semantics, the Tracktion spike, failure
  policy, rollback proof, dirty state, UI wiring, and tests.
- `docs/in-progress/test-fixture-opportunities-plan.md` owns optional test-harness cleanup.
- `docs/completed/editor-logging-plan.md` (retired in Phase 0) recorded the durable logging
  direction; its intent is implemented by the Quill-backed `Logger` facade, with the actual outcome
  noted at the top of that document.

## Ordering Principles

- Validate feasibility (spike) before committing to behavior-changing undo-readiness work.
- Prefer behavior-preserving adapter seams before behavior-changing undo work.
- Keep every step independently buildable and reviewable.
- Do not split `Engine` state. One `Engine::Impl` continues to own the single Tracktion `Edit`.
- Start engine implementation files flat under `src/`. Introduce a `src/tracktion/` subfolder only
  when a genuine private cluster emerges and the flat folder is demonstrably hard to scan. Do not add
  a `rock_hero::common::audio::tracktion` namespace for private implementation files.
- Do not add public audio header grouping before undo; it is navigation churn, not undo readiness.
- Do not expose partial-coverage undo. A live Undo command that skips the user's latest tone edit is
  worse than no Undo command.
- Treat tests as gates, not cleanup. Each slice must leave focused tests green before continuing.
- **Build/verification ownership is agent-dependent.** Verification steps below state the test
  targets that must build and pass. They do not require a specific agent to run them. In this repo,
  Claude does not invoke `cmake`/`ctest` (it disrupts CLion include paths); the user runs builds, or
  the Codex `.codex/skills/rockhero-build/` workflow is used. Read every "build and run" line as
  "this must pass before continuing", not "run it yourself".
- Update `docs/design/` only after implementation proves durable and the user confirms the design
  should become a durable rule.

## Phase Dependency Map

```text
Phase 0  Planning / logging reconciliation / doc alignment
Phase 1  Focused baseline tests
   |
   +--> Phase 2  Tracktion/JUCE behavior spike      (independent)
   +--> Phase 3  Engine implementation-seam split   (independent, behavior-preserving)
   +--> Phase 5  Pure undo model (EditorUndoHistory) (independent, cheapest derisk)
   |
Phase 4  Audio boundary undo readiness   (needs Phase 2 findings; easier after Phase 3)
Phase 6  Minimal controller interface segmentation (needs nothing; do before Phase 7)
Phase 7  Controller action gate / hidden undo shell (needs Phases 4, 5, 6)
Phase 8  First-pass undo entries + dirty-state migration (needs Phase 7; parameter work needs Phase 2)
Phase 9  UI wiring and shortcuts          (needs Phase 8 complete for the scoped categories)
Phase 10 Full verification and doc closure
```

## Phase 0 - Planning, Logging Reconciliation, And Doc Alignment

Purpose: remove ambiguity and stale prerequisites before structural changes.

Deliverables:

- Keep `documentation-conventions-audit-plan.md` in `docs/completed/`; it is already implemented.
- **Retire `editor-logging-plan.md`.** Its intent — a durable file-backed editor logger installed by
  the app composition root before the audio engine and window — is already implemented by the
  Quill-backed `Logger` facade and the `JuceQuillBridge`, which routes `juce::Logger::writeToLog`
  into the same durable log. **Done:** the plan was moved to `docs/completed/editor-logging-plan.md`
  with an Implementation Result note recording that the Quill facade is its effective outcome, so
  undo stages can cite "the durable logger exists" rather than an open plan.
- Confirm the undo logging prerequisite is satisfied and that no diagnostics port is introduced: the
  undo plan logs through the existing Quill `Logger` facade (`RH_LOG_*`) directly, the way
  `editor_controller.cpp` already does. There is no `IEditorDiagnostics` interface; the durable,
  category-based, structured logging it would have wrapped is already provided by Quill (with
  `JuceQuillBridge` routing `juce::Logger::writeToLog` into the same log). No separate logging
  prerequisite phase is needed.
- Keep `test-fixture-opportunities-plan.md` in progress. It is opportunistic, not a prerequisite.
- Keep `editor-undo-plan.md` in progress. It is not implemented.
- Keep `remaining-god-object-decomposition-plan.md` in progress and ensure its folder guidance reads
  flat-first (already reconciled). Use this master plan as the sequencing guide.

Verification:

- `git status --short` shows only intentional planning-doc changes.
- No code build is required for this phase unless planning edits touch CMake or source files.

Exit criteria:

- The active docs no longer disagree about the logging prerequisite, and `editor-logging-plan.md` is
  no longer cited as open.
- This master plan (v2) is the single ordering source for the next implementation run.

## Phase 1 - Focused Baseline Tests

Purpose: establish a clean test baseline before moving code around.

Do not broaden this into a full refactor. Run enough tests to catch the modules that will move.

Targets that must build and pass:

- `rock_hero_common_audio_tests`.
- `rock_hero_editor_core_tests`.
- `rock_hero_editor_ui_tests` if Part B or UI wiring will be touched next.

Exit criteria:

- The relevant test targets build and pass.
- Any pre-existing unrelated failures are documented before structural work begins.

## Phase 2 - Tracktion/JUCE Behavior Spike

Purpose: validate that undo is feasible against real framework behavior before any undo-readiness
code is written. This is `editor-undo-plan.md` Stage 0, pulled forward and made explicit. It is
small, independent, and can run in parallel with the Phase 3 engine seam split.

This phase is investigative. It may add temporary logging and throwaway probes; it does not ship
undo behavior.

Spike questions (from `editor-undo-plan.md`):

1. Does `edit.getUndoManager().canUndo()` change after insert, move, remove, output gain, plugin
   parameter changes, and plugin-state capture/restore?
2. Which undo-quarantine mechanism is safe: clear Tracktion undo history after owned mutations,
   inhibit Tracktion transaction creation, or leave it inaccessible and bounded? Does clearing affect
   playback graph rebuilds, plugin-state flush, dirty/change timers, or later plugin operations?
3. Is rollback feasible for the undo-wired mutations — can a plugin be instantiated and loaded before
   insertion, and can a failed operation prove restoration of the exact pre-state?
4. For one real VST3 plugin, what is the parameter-callback pattern: gesture begin/end, raw
   `parameterChanged`, or both? Does undo through `setPluginParameterValues` update an open plugin
   editor window?
5. Can `insertPluginState` preserve Tracktion item/runtime ids, or must changed-id restore be
   supported (the design assumes changed-id support regardless)?

Verification:

- The spike's findings are written into `editor-undo-plan.md`'s Open Questions / Spikes section or a
  short result note, resolving the quarantine mechanism and the parameter-observation approach.

Exit criteria:

- Quarantine mechanism chosen.
- Rollback feasibility confirmed for each operation intended for first-pass undo, or that operation
  is explicitly excluded until feasible.
- Parameter-callback reality characterized enough to design observation (Phase 8).

## Phase 3 - Engine Implementation-Seam Split

Purpose: make the Tracktion adapter reviewable before undo/redo depends on rollback and replay. This
is the high-value part of `remaining-god-object-decomposition-plan.md`. It is behavior-preserving and
independent of the Phase 2 spike, so the two can proceed in parallel.

### Phase 3A - Extract `Engine::Impl`

Move the `Engine::Impl` declaration into a private implementation header. Keep the implementation
flat under `src/` at first; do not pre-create a folder.

Rules:

- `Engine::Impl` is declared once.
- `Engine` remains the only public facade.
- Public audio port headers do not move.
- No new namespace level is introduced.
- Behavior stays byte-for-byte equivalent at the public contract level.

Verification:

- `rock_hero_common_audio_tests` builds and passes.
- Search for stale includes and accidental public Tracktion leakage.

### Phase 3B - Extract Low-Coupling Helpers

Extract helper clusters that do not require splitting `Engine` state:

- path and encoding helpers;
- plugin identity and scan helpers;
- tone document read/write helpers;
- plugin move/index helpers if they are not already isolated enough.

Folder guidance:

- Keep new files flat under `rock-hero-common/audio/src` unless the extracted files make the flat
  folder meaningfully harder to scan.
- Only if a real private Tracktion cluster emerges, introduce `src/tracktion/` as a pure
  file-organization tool, one move at a time. Earn the folder; do not pre-create it.
- Keep namespace `rock_hero::common::audio` even if a `tracktion/` folder is later introduced.

Verification:

- Add direct helper tests only when the helper has real policy worth testing (e.g. plugin path
  normalization, tone-document resolution).
- `rock_hero_common_audio_tests` builds and passes.

### Phase 3C - Split Undo-Relevant Port Implementations

Split the public method definitions undo/redo will stress most, prioritized to match the audio
boundary that Phase 4 changes:

1. plugin host operations: scan, known catalog, insert, move, remove, open plugin window;
2. live rig operations: capture, load, output gain, live-rig step machine;
3. device/meter code only if it blocks clean review of live-rig or plugin-host changes.

Transport and song-audio can move now if cheap, but they are not required before undo.

Rules:

- One port seam per commit or review slice.
- Move matching `Impl` helper methods beside the port they serve when that improves locality.
- Do not split state ownership across helper classes.
- Do not change failure semantics while moving code.

Verification after each seam:

- `rock_hero_common_audio_tests` builds and passes.
- `rg` finds no stale file names, stale helper names, or Tracktion includes outside allowed files.

Exit criteria:

- Undo-relevant adapter code is no longer buried in one ~4,000-line source file.
- `Engine` public contracts are unchanged.
- The common audio tests pass.
- Optional public audio header grouping remains deferred.

## Phase 4 - Audio Boundary Undo Readiness

Purpose: prove the audio-side operations undo needs can satisfy the undo failure contract. Gated on
the Phase 2 spike, and easier to land after Phase 3 has split the plugin-host/live-rig seams.

This phase is behavior-changing where the existing boundary is missing required data or rollback
guarantees. Keep changes narrow and test-backed. The fixed contracts are in `editor-undo-plan.md`'s
Audio Boundary and Rollback Proof Requirements sections.

Work items:

- Add the Tracktion-free `IPluginHost` surface the undo plan specifies: `insertPlugin` returning an
  explicit `inserted_instance_id`; `capturePluginState` / `insertPluginState` with a complete
  opaque `PluginInstanceState`; `setPluginParameterValues`; and the pending-parameter observer/status
  methods. The implementation spelling may differ; the semantics are required.
- For each mutating operation the first undo pass uses (insert, remove, move, output gain, plugin
  state capture/restore, and plugin parameters if in the first visible release), write the rollback
  proof from `editor-undo-plan.md` before exposing it to user-visible history.
- Add or adjust return payloads only where undo needs authoritative post-state, such as a restored
  plugin instance id after recreation.
- Preserve typed errors; do not convert recoverable failures to strings. Report a rollback-contract
  violation through a distinct invariant path, not as an ordinary `PluginHostError`.
- Keep Tracktion undo-manager behavior quarantined inside the adapter, using the mechanism chosen in
  Phase 2.

Verification:

- Focused common-audio/adapter tests for each rollback proof: preflight failures, failures repaired
  to exact pre-state, and rollback-contract violations carrying diagnostic context.
- `rock_hero_common_audio_tests` builds and passes.

Exit criteria:

- Every audio operation selected for first-pass undo has a documented, tested unchanged-on-error or
  rollback story.
- Operations that cannot meet the contract are explicitly excluded from user-visible undo until
  fixed.

## Phase 5 - Pure Undo Model

Purpose: build the undo state machine where it is easiest to test. This is `editor-undo-plan.md`
Stage 1. It depends on nothing in Phases 2-4 and is the cheapest derisking, so it may begin as early
as after Phase 1 and run in parallel with the spike and the engine split.

Implement `EditorUndoHistory` and related value types in `rock-hero-editor/core` without touching
JUCE, Tracktion, UI components, or the audio adapter.

Work items:

- Add `EditorUndoHistory` and `EditorUndoEntry` (variant of insert/remove/move/placement/
  display-type/parameter-batch/output-gain payloads) and their payload structs.
- Add transition results/events for push, undo, redo, mark-clean, clean-revision eviction,
  invalidation, and id remapping.
- Add clean-revision tracking and bounded history depth (start at 100).
- Add `remapInstanceId(old_id, new_id)` covering every payload, including nested placement ids and
  parameter batches, before any plugin recreation path depends on it.
- Keep diagnostics out of the pure history type; it returns events the controller logs later.

Verification:

- Direct unit tests in `rock-hero-editor/core/tests`: push, undo-to-redo, redo-to-undo,
  push-after-undo clears redo, clean marker, clean-marker eviction marks clean unreachable, reset,
  two-phase non-commit on failure, and id remapping.

Exit criteria:

- The pure undo model is fully covered without constructing `EditorController`.
- No user-visible Undo/Redo is wired yet.

## Phase 6 - Minimal Controller Interface Segmentation

Purpose: give undo's new intents a clean home and keep `IEditorController` from becoming a larger
unsegmented append point. This is Part B of `remaining-god-object-decomposition-plan.md`, and this
plan is the reason it is worth doing at minimal size (it resolves that plan's open question).

Work items:

- Define feature sub-interfaces for the existing editor intent groups (project, transport,
  signal-chain, plugin browser, input calibration, audio device, busy), and re-express
  `IEditorController` as their aggregate. No method bodies change.
- Narrow `InputCalibrationWindow` to the calibration intent slice.
- Reserve a place for the upcoming Undo/Redo (and future edit) intents — a small `IEditIntents` or
  equivalent slice — so Phase 7 adds them there rather than onto the monolithic aggregate.
- Stop if segmentation does not reduce real coupling; record that outcome rather than carrying
  half-done churn.

Rules:

- `EditorController` remains the only root controller implementation.
- `EditorView` may keep the aggregate because it is the hub. No per-view controllers are introduced.
- No JUCE or Tracktion types enter editor-core controller contracts.

Verification:

- `rock_hero_editor_core_tests` builds and passes.
- `rock_hero_editor_ui_tests` builds and passes if UI constructor types change.

Exit criteria:

- `InputCalibrationWindow` no longer depends on all editor intents.
- New undo/edit intents have a clear home before undo wiring begins.

## Phase 7 - Controller Action Gate And Hidden Undo Shell

Purpose: connect undo history to editor workflow at the single command boundary. Needs Phases 4, 5,
and 6. This phase creates the hidden control path, but it does not make undo history the dirty-state
authority yet.

Work items:

- Add Undo and Redo action ids and the `onUndoRequested`/`onRedoRequested` intents (on the Phase 6
  edit-intent slice), but keep user-visible UI disabled until the selected edit scope is complete.
- Add the action-dispatch chokepoint behavior from `editor-undo-plan.md`, including the single
  pending-parameter flush point (active once observation exists in Phase 8).
- Log action lifecycle plus the history transition events returned by `EditorUndoHistory` at the
  action gate via the Quill `Logger` facade (`RH_LOG_*` on `editor.controller`), not inside the
  history type. Do not add an `IEditorDiagnostics` port — Quill already provides durable structured
  logging (see the undo plan's Logging Surface section).
- Preserve the current `m_has_unsaved_changes` dirty-state behavior until Phase 8 converts every
  selected edit category to history-backed pushes. Existing Save, Save As, Close, Open, Import, and
  Restore prompts must continue to consult the old dirty source in this intermediate state.
- Add faulted-session state only when rollback-contract handling lands (Phase 8 remove memento), via
  the centralized availability check the busy state already uses.

Verification:

- Editor-core controller tests for action availability and hidden Undo/Redo routing.
- Regression tests proving existing dirty prompts and Save/Save As behavior still follow
  `m_has_unsaved_changes` plus `m_save_requires_destination` until Phase 8 migrates them.

Exit criteria:

- Controller state can report hidden undo availability, hidden redo availability, and (once Phase 8
  lands) faulted state from headless data.
- Existing project dirty-state behavior is unchanged.
- Undo/Redo are not exposed as partial user-facing commands.

## Phase 8 - First-Pass Undo Entries

Purpose: implement undo entries from lowest risk to highest while keeping them hidden until the whole
selected scope is covered. Needs Phase 7; the parameter work needs the Phase 2 spike findings. This
is also where dirty-state ownership migrates from `m_has_unsaved_changes` to undo-history
clean-revision tracking, because only this phase creates the history entries that can keep dirty
state accurate.

Recommended order (matches `editor-undo-plan.md` reversal strategy):

1. signal-chain block placement and display-type changes (pure editor-core, no audio change);
2. plugin moves, once the rollback proof is solid;
3. parameter boundary: last-known cache, observation scoped to user plugins, gesture grouping,
   non-gesture debounce with the self-animating guard, explicit flush with drop/cache semantics at
   the dispatch chokepoint, pending-state notifications, and ingestion suppression;
4. output gain, after drag begin/commit gesture boundaries are clear;
5. plugin insert (insert undo = remove; insert redo busy-fenced, remaps to returned id);
6. plugin remove memento (capture/restore, visual-state capture/apply, id remapping) — and add
   rollback-contract handling here: the `Error` diagnostic, entering the faulted state, and the
   report-to-developer + reopen/close message.
7. dirty-state migration: once all selected edit categories push history entries, replace
   `m_has_unsaved_changes` as the edit-dirty source with undo-history clean-revision tracking while
   preserving `m_save_requires_destination` as the separate project-destination reason.

Important gate:

- If native plugin parameter edits are part of the user's current tone-editing surface and cannot be
  observed and replayed reliably (per the Phase 2 spike), do not ship visible global Undo/Redo yet.

Verification:

- Controller tests per edit type: entry push, undo apply, redo apply, failure leaves history and
  model unchanged, id remapping after recreation, dirty-state transitions.
- Tests proving Save and Save As mark the current history revision clean.
- Tests proving Close, Open, Import, and Restore consult the migrated history dirty state.
- Audio fake tests for rollback and replay-request ordering; common-audio tests for any real adapter
  behavior added.

Exit criteria:

- The selected first-pass edit scope has complete undo and redo behavior matching the failure policy
  in `editor-undo-plan.md`.
- Dirty state is history-based for all selected edit categories, with `m_save_requires_destination`
  preserved separately.
- No partial visible undo is possible.

## Phase 9 - UI Wiring And Shortcuts

Purpose: expose Undo/Redo only after the headless behavior is complete.

Work items:

- Add `undo_enabled` and `redo_enabled` to `EditorViewState`.
- Add the Edit menu beside File, route selections through controller intents, and handle `Ctrl+Z` /
  `Ctrl+Y` in `keyPressed`.
- Disable Undo/Redo while busy, while faulted, and while any blocking editor mode prohibits edits.
- On a rollback-contract violation, show the report-to-developer message and disable all editing plus
  Save/Save As, leaving Open/Import/Restore/Close/Exit; the app keeps running.
- Add UI tests for menu state and shortcut forwarding.

Verification:

- `rock_hero_editor_core_tests` and `rock_hero_editor_ui_tests` build and pass.

Exit criteria:

- User-visible Undo/Redo cannot be triggered outside the supported state.
- UI only renders controller-derived availability; it does not own undo policy.

## Phase 10 - Full Verification And Documentation Closure

Purpose: close the feature and avoid stale planning docs.

Verification:

- Build and run common audio, editor core, and editor UI tests; run broader `ctest` when feasible.
- Run clang-format/pre-commit before committing; run clang-tidy on touched code that warrants it.
- Consider the docs target if public headers changed significantly.

Documentation actions:

- Move completed implementation plans from `docs/in-progress/` to `docs/completed/`.
- Leave deferred optional public audio header grouping in `docs/todo/` unless it was actually done.
- Update `docs/design/` only after the user confirms the implemented shape is durable.

Exit criteria:

- Undo/Redo is visible only for a complete, tested edit scope.
- Planning docs accurately describe what shipped and what remains deferred.

## Deferred Until After First Visible Undo

Do not include these in the main path unless they become blockers:

- public `common/audio` header grouping under `ports/`, `values/`, `errors/`, or `workflows/`;
- broad private folder taxonomy beyond what the `Engine` split naturally earns;
- a new Tracktion-specific namespace;
- game-app logging parity;
- chart/note undo before chart editing exists;
- broad project workflow extraction;
- per-view controller implementations.

## Preferred Next Concrete Step

Phases 0 and 1 are complete:

- Phase 0: `editor-logging-plan.md` is retired to completed and the active docs no longer disagree
  about the logging prerequisite.
- Phase 1: the test baseline is green — `rock_hero_common_audio_tests`,
  `rock_hero_editor_core_tests`, and `rock_hero_editor_ui_tests` all build and pass, with no
  pre-existing unrelated failures to document.

The next implementation sequence is:

1. Start **Phase 2 (the Tracktion spike)** as the next real work — it is the cheapest,
   highest-information action and gates the whole feature. The **Phase 3A `Engine::Impl` extraction**
   can proceed in parallel, since it is behavior-preserving and independent of the spike.

Do not start Phase 4 (audio undo readiness) until the Phase 2 spike has chosen the quarantine
mechanism and confirmed rollback feasibility for the operations targeted by first-pass undo.
