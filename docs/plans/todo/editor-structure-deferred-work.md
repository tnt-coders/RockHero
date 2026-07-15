# Editor Structure — Deferred Work

Status: deferred work register. Nothing here is scheduled. Each item lists the trigger that should
bring it back into active scope.

## Scope

These items came out of the editor runtime/readability planning but are deliberately **not** part of
the active `editor-runtime-extraction-plan.md`. They are real and likely worthwhile eventually, but
doing them now would be premature: they are either gated on a prerequisite that does not exist yet,
or they touch modules outside the current editor focus, or they should only happen after the
extracted shape has proven itself.

The former `SignalChainWorkflow` item was completed as part of
`docs/plans/completed/signal-chain-plugin-editing-plan.md`.

Shared principles and the role vocabulary live in
`docs/plans/completed/editor-runtime-extraction-plan.md`; this document does not repeat them.

---

## Deferred Runtime Work

### Framework-Isolation Part 2 — Message-Thread Scheduling

Three sites in `EditorController::Impl` use JUCE message-loop primitives directly:

- `makeBusyProjectOperationProgress()` uses `juce::MessageManager` to post a busy-state transition
  from the worker thread to the message thread, then blocks the worker on `AnalysisPaintGate` until
  the message thread has painted.
- `startLiveRigLoadStage()` uses `juce::Timer::callAfterDelay` for a 500 ms cosmetic hold so the
  100% live-rig progress state is visible before the busy overlay clears.
- `startLiveRigLoadStage()` branches on `juce::MessageManager::getInstanceWithoutCreating() ==
  nullptr` to detect the test environment and skip the timer-based delay.

**Why deferred.** The fix is more invasive than the audio-device boundary and the practical benefit
is lower. `JuceEditorTaskRunner` already marshals completions through
`juce::MessageManager::callAsync` by design; extending it (or adding a sibling scheduler) to cover
these sites touches async choreography that is currently correct and well-tested. The paint-gate
rendezvous is tightly coupled to the busy-token lifecycle and the view's paint-fence contract;
abstracting the post behind a port adds indirection over that coupling rather than removing it. The
test-environment branch is inelegant but not a source of bugs.

**Revisit when** any of these is true:

- a second `editor/core` consumer needs to post to the message thread outside the task runner,
  making a shared dispatch port genuinely reusable;
- the test-environment branch becomes a source of bugs or false passes because the synchronous test
  path and the async production path diverge in a way that matters;
- a new runtime (e.g. a headless CI host) must replace JUCE's message loop but cannot because
  `editor/core` calls it directly.

**Shape if implemented.** Add `postToMessageThread(std::function<void()>)` and
`callAfterDelay(std::chrono::milliseconds, std::function<void()>)` to `IEditorTaskRunner` (or a
narrower `IMessageThreadScheduler`). Production implements them via `juce::MessageManager` and
`juce::Timer`; the inline test runner runs them synchronously/immediately, removing the
`MessageManager == nullptr` branch. Replace the three direct calls and drop the `juce_events`
include from `editor_controller.cpp` — which would make that file free of direct JUCE includes.

### Project Lifecycle Slices

**Why deferred.** Do not start with one broad `ProjectWorkflow` / `ProjectLifecycleController` — that
risks a second oversized coordinator, which the whole effort is trying to avoid.

**Revisit when** project lifecycle stays noisy after the active extractions. Then introduce small
slices around proven decision points — interrupted restore prompts, post-write deferred-action
continuation, save-before-close/exit — not one broad type.

### Optional `EditorStateProjector`

**Why deferred.** Premature until multiple workflows expose clean snapshots.

**Revisit when** view-state projection becomes duplicated or hard to read after the extractions. It
should be a near-pure projection helper, not a second controller.

---

## Deferred Readability Work

The runtime extraction plan closed without a standalone readability pass. Treat the remaining
taxonomy and naming ideas as opportunistic cleanup: do the local rename or folder grouping only when
a feature already touches the same area, or when the current shape creates a concrete navigation
problem. Do not move public headers before the include-path churn is clearly worthwhile, and never
move a monolithic file into a role folder and call it cleaner.

### Editor Naming And Folder Cleanup

**Partially superseded (2026-07-04).** The folder-grouping half of this item is now active work in
`docs/plans/in-progress/project-structure-cleanup-plan.md`, which groups `editor/core` and `editor/ui`
by feature (grouping "by role" was explicitly rejected by the rules adopted in
`docs/design/architectural-principles.md`). The naming reevaluations (`InputCalibrationPrompt`,
renaming the launcher-like `AudioDeviceSettingsWindow` when touched) stay deferred and
opportunistic.

**Revisit when** a feature changes the affected type. Keep behavior changes separate from pure
taxonomy moves.

### `common/audio` Public Ports And Values

**Superseded (2026-07-04).** The role-based grouping sketched here (`ports/`, `values/`,
`workflows/`, `errors/`) was rejected by the feature-folder rules adopted in
`docs/design/architectural-principles.md` — role folders answer "what kind", which the suffix
already carries. The public grouping now happens by feature (`device/`, `transport/`, `song/`,
`plugin/`, `live_rig/`, `input/`) as Phase 4 of
`docs/plans/in-progress/project-structure-cleanup-plan.md`, with `engine.h` staying at the public root
as the composition-facing facade.

### `common/audio` Private Tracktion Grouping

**Scheduled (2026-07-04).** This is now Phase 4 of
`docs/plans/in-progress/project-structure-cleanup-plan.md`, gated exactly as stated here: `src/tracktion/`
is created only as the engine Part A seam split extracts real units from `engine.cpp`, never by
moving the monolith wholesale.

### `common/ui` And `game/*` Modules

No grouping work until those modules grow and are being actively developed. `game/*` in particular
should not be reorganized before there is real game code to organize.

**Revisit when** game development begins (explicitly after the editor focus, per current direction).

---

## Durable Documentation

**Done for structure (2026-07-04).** With user confirmation, the structural rules were folded into
`docs/design/` as Phase 0 of `docs/plans/in-progress/project-structure-cleanup-plan.md`:

- `docs/design/coding-conventions.md` — the two-tier controller rule, role subsets, projection
  modules, and the view/component suffix policy;
- `docs/design/architecture.md` — the feature-folder convention note;
- `docs/design/architectural-principles.md` — the two-axis rule, feature-folder and root/shared
  placement rules, the new-file placement procedure, and the multi-TU coordination-object rule.

Any future durable-doc updates should follow the same confirm-first flow per `CLAUDE.md`.
