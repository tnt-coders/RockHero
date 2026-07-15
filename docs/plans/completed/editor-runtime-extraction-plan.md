# Editor Runtime Extraction Plan

Status: completed. Runtime stages 0-5 and the focused action-policy follow-up are implemented.
The remaining Stage 6 naming/folder readability ideas were evaluated and deferred rather than
treated as completion blockers.

## Scope

This was the part of the former master plan judged worth doing now, given that the editor is the
active development focus for the foreseeable future before any game work begins. It covers the
`EditorController` runtime decomposition. The editor-scoped readability work that naturally fell
out of that decomposition is now tracked as deferred cleanup.

Related documents hold the rest:

- `docs/plans/todo/editor-structure-deferred-work.md` - work that is real but should wait for a proven
  need or for attention to move outside the editor (codebase-wide folder reorg,
  framework-isolation Part 2, speculative runtime types, deferred editor readability cleanup,
  durable design-doc updates).
- `docs/plans/completed/editor-structure-dropped-candidates.md` - process steps from the master plan that
  are not worth formalizing on a solo, early-stage project.
- `docs/plans/completed/editor-action-policy-cleanup-plan.md` - completed Stage 5/6 follow-up for
  pure action metadata and availability-policy cleanup.

This document is self-contained: it does not depend on the deleted master plan.

## Purpose

`EditorController` keeps editor policy out of JUCE components — which is correct — but it has become
the owner of nearly every editor workflow at once (3,752 implementation lines, 46 member fields,
~5,595 lines of characterization tests). Every new editor feature gravitates into it because that
is where everything already lives.

The fix is to move cohesive headless policy into focused, directly testable `editor/core` types,
then make the resulting roles visible through editor-scoped folders and names. Doing the runtime
extraction first means the readability pass organizes the *real* end-state types rather than
churning the layout twice.

## Why Now

The editor is where development is happening next, so the maintainability investment is paid back by
the very work that follows it. Extraction cost also scales with codebase size: extracting from 46
fields today is cheaper than after the next round of editor features adds more state and
cross-cluster interaction.

The more important reason is the **pattern**. Without the extractions, the controller's
gravitational pull only strengthens as it grows — each new editor feature adds state and policy that
entangles with existing clusters. The first extractions establish the convention — *workflows return
decisions and effects; the root facade executes them* — so the upcoming editor features follow that
shape instead of growing the controller further.

The architectural-principles document makes automated testability a structural requirement, not a
secondary quality attribute. Each extraction delivers on that directly: a workflow becomes testable
with simple inputs and explicit outputs, without the full controller harness.

## Evidence Base (From The Test Split)

The completed editor test-file split is the empirical justification for the runtime stages. The
split itself was test-only, but it exposed runtime pressure that was previously hard to see:

- Several **narrow** behavior test files still need the **same broad** controller construction —
  transport/live-input fakes, song-audio/audio-device fakes, plugin-host/live-rig fakes, project
  operation callbacks, settings and task-runner services, and view-state capture. The breadth of
  setup needed to exercise one responsibility is a proxy for how many policy clusters the controller
  sits at.
- The busy tests alone span open, import, save, save-as, publish, live-rig loading, stale
  completions, paint fencing, close/exit supersession, and audio-device-open scheduling. Busy state
  is editor-wide operation policy, not a feature detail — which is exactly why it is the first
  extraction.
- The controller owns multiple distinct state machines (project/restore, prompt replay, busy/async,
  plugin catalog, signal-chain projection, output gain, input calibration, audio-device window
  transitions, transport, view-state projection). These interact but are not one machine.

This signal confirms the extraction order below rather than motivating a separate refactor.

## Guiding Principles

These come from `docs/design/architectural-principles.md` and
`docs/design/coding-conventions.md`. They constrain every stage.

- **Testability is structural.** Most behavior should be testable without a message loop, audio
  device, GPU, Tracktion runtime, or plugin scanning. An extracted type that cannot be tested
  without the full controller fixture has the wrong boundary.
- **Separate state from side effects.** Logic produces explicit state plus *requested* effects
  (`SeekTransport`, `PersistInputCalibration`, `ShowUnsavedChangesPrompt`, …). The root facade
  executes them. Workflows do not call ports, settings, views, dialogs, Tracktion, or JUCE
  primitives directly.
- **Ports and adapters, framework at the edge.** `editor/core` is headless workflow code. JUCE GUI,
  JUCE audio-device, and Tracktion usage belong in adapters or the UI/app tier. Narrow `juce_core`
  utility use is acceptable only when it stays headless and testable.
- **One owner per state.** After extracting a cluster, `EditorController` keeps no mirrored copy. It
  may cache the latest full `EditorViewState`, but queries extracted types for their snapshots.
- **Private-first headers.** New extracted types start as private headers in
  `rock-hero-editor/core/src`. Same-library tests reach them through the test target's private
  `src/` include path. Promote to `include/rock_hero/editor/core/` only when a real external
  consumer needs the type. Stage 6 may group these private files under `src/workflows/`, but it must
  not move them into public include folders without a demonstrated public consumer.
- **Folders before namespaces.** Prefer folders for visual grouping. Do not add role namespaces
  (`view_state`, `ports`, `controllers`) just because a role folder exists.

## Naming And Role Vocabulary

Names describe practical responsibility. Use `State` for a small state machine or snapshot owner;
`Workflow` for headless policy that accepts inputs and returns decisions/effects; `Controller` only
for public view-facing controller contracts. Avoid `Manager`, `Handler`, `Helper`, and broad
`Service` names.

The four extracted runtime types from Stages 1–4 — `BusyOperationState`, `DeferredProjectActionState`,
`PluginCatalogWorkflow`, `InputCalibrationWorkflow` — already conform. The editor-relevant slice of
the role vocabulary used in Stage 6:

| Role | Naming shape | Folder hint |
| --- | --- | --- |
| Controller | `<Feature>Controller` | `controllers/` |
| Controller interface | `I<Feature>Controller` | `ports/` or `controllers/` |
| View state | `<Feature>ViewState` | `view_state/` |
| Workflow / session | `<Feature>Workflow` / `<Feature>Session` | `workflows/` |
| Settings store | `<Feature>Settings` | `settings/` |
| Task runner | `<Feature>TaskRunner` | `tasks/` |
| Snapshot/request/result/progress | `<Thing>Snapshot`, etc. | near owning API |
| Error | `<Subject>ErrorCode`, `<Subject>Error` | `errors/` if many |

## Desired End State

`EditorController` remains the public `IEditorController` implementation and the only root controller
for the editor view. After the runtime stages it is the root facade that:

- receives UI intents from `IEditorController`;
- owns or references app-composed ports;
- subscribes to transport and audio-device listener surfaces;
- delegates workflow decisions to extracted types;
- executes requested effects against project-owned ports;
- owns async callback liveness and task-runner coordination;
- publishes `EditorViewState` to `IEditorView` and reports transient errors through it.

```text
EditorView
  -> IEditorController
      -> EditorController root facade
          -> BusyOperationState
          -> DeferredProjectActionState
          -> PluginCatalogWorkflow
          -> InputCalibrationWorkflow
      -> common/audio, editor/core Project, EditorSettings, IEditorTaskRunner
```

The deferred Stage 6 candidate would make that structure more visible from the folder tree: ports,
view-state, controllers, and workflows would be visually separable in `editor/core`, and `editor/ui`
source would be grouped by shell/feature/widget.

---

# Rollout

The stages were ordered so each one settled before the next. Stages 0-5 were runtime structure.
Stage 6 was evaluated as an editor-scoped readability pass and deferred because it was not needed to
ship the runtime extraction. Pause after each future extraction: if a boundary proves wrong, fix or
re-absorb it rather than layering workarounds.

## Stage 0: Framework Isolation — Audio Device Serialization

The clean boundary fix that must precede extraction. It removes direct JUCE XML parsing and raw
`deviceManager()` serialization from the controller.

**Problem.** `restoreAudioDeviceState()` (editor_controller.cpp ~line 3147) reads an XML string from
`EditorSettings`, parses it with `juce::parseXML`, and calls
`m_audio_devices->deviceManager().initialise(1, 2, xml.get(), true)`.
`persistAudioDeviceState()` (~line 3171) calls `deviceManager().createStateXml()` and writes the
string back. This pulls `juce_core` XML parsing and `juce_audio_devices` mutation into `editor/core`
and reaches past `IAudioDeviceConfiguration` to the raw manager.

**Change.** Add two methods to `IAudioDeviceConfiguration`:

```cpp
[[nodiscard]] virtual bool restoreSerializedDeviceState(const std::string& serialized_state) = 0;
[[nodiscard]] virtual std::optional<std::string> serializedDeviceState() const = 0;
```

The production adapter in `Engine` handles XML parsing and `deviceManager()` calls internally.
`restoreSerializedDeviceState` returns `false` only when the serialized value cannot be parsed,
preserving the current cleanup path for bad persisted XML without exposing JUCE XML to editor core.
The controller bodies simplify to settings-read → port-call → clear-on-failure (restore) and
settings-write from the port result (persist).

**Steps.**

1. Add the two methods to `IAudioDeviceConfiguration`.
2. Implement both in `Engine`, moving the XML parsing and `deviceManager()` calls out of the
   controller. Invalid XML returns `false`.
3. Update `ConfigurableAudioDeviceConfiguration` and any other `IAudioDeviceConfiguration` test
   helper or concrete implementor used by editor-controller and audio-device settings tests. Test
   helpers may store-and-return the string or no-op.
4. Replace the controller bodies with the simplified versions.
5. Remove the now-unneeded `juce_audio_devices` include and `juce::parseXML` / `juce::XmlElement` /
   `juce::String` serialization usage from `editor_controller.cpp`. (`juce_events` for the message
   thread stays until the Stage 5 Part 2 decision.)
6. Verify `editor/core` no longer references `deviceManager()`. The accessor stays on the port for
   `editor/ui` to hand JUCE's `AudioDeviceSelectorComponent` its manager.

**Exit criteria.**

- `EditorController` no longer parses audio-device XML or calls `deviceManager()` for persistence;
- invalid persisted device state is still cleared from settings;
- restore/persist behavior is covered through project-owned ports;
- existing characterization tests pass unchanged (the new methods are additive).

## Stage 1: Extract `BusyOperationState`

First extraction — smallest and most self-contained. Add under `rock-hero-editor/core/src`.

**Owns:** the active `BusyOperation`, the current busy token, and the optional live-rig progress
payload.

**Responsibilities:** begin an operation; transition the current token to another operation; reject
stale tokens; finish or supersede operations; expose a `BusyViewState` snapshot.

**Does not own:** `IEditorView`; busy-overlay paint fencing; task-runner submission; callback
liveness guards.

**Steps.**

1. Add `BusyOperationState` with direct tests (simple values, no JUCE window, no audio device).
2. Move busy operation, token, stale-token, transition, finish, supersede, and progress-snapshot
   logic into it.
3. Keep task-runner submission, paint fencing, and callback liveness in `EditorController`.
4. Update `EditorController` to call `BusyOperationState` for busy decisions and view snapshots.
5. Keep the broad controller tests for async busy behavior and paint-fence behavior.

**Exit criteria.** Controller no longer owns busy token/progress fields directly; stale completions
are still dropped; paint-fence behavior unchanged; direct busy-state tests need no JUCE window or
audio device.

## Stage 2: Extract `DeferredProjectActionState`

**Owns:** the deferred project action; unsaved-changes prompt visibility; Save As prompt visibility.

**Responsibilities:** remember a blocked project action; decide whether to prompt, save first,
discard, cancel, or replay; expose prompt snapshots for view-state projection; clear prompt state
after completion.

**Does not own:** project IO; filesystem dialog choice; settings persistence; transport or audio
calls.

**Steps.**

1. Add `DeferredProjectActionState` with direct state-machine tests.
2. Move deferred-action storage and prompt-visibility fields into it.
3. Express prompt decisions as return values: cancel, save first, discard and replay, or continue
   waiting.
4. Keep project IO, save execution, and view updates in `EditorController`.
5. Preserve controller tests for open/import/close/exit prompt flows.

**Exit criteria.** Prompt-replay logic is testable without the full controller; controller no longer
owns prompt booleans directly; deferred-action behavior is still observable through
`IEditorController`.

## Stage 3: Extract `PluginCatalogWorkflow` (Conditional)

**Owns:** known plugin candidates; browser visibility.

**Responsibilities:** sort and store catalog candidates; open/close browser state; validate selected
candidate IDs; expose `PluginBrowserViewState` input data.

**Does not own:** `IPluginHost`; plugin instantiation; plugin removal; live-rig persistence; output
gain; calibration gates.

**Condition.** Proceed only if the extraction deletes meaningful controller fields and branches. If,
after Stages 1–2, the catalog collapses to simple storage plus pass-through, leave it in
`EditorController` and record that decision at the Stage 5 checkpoint.

**Steps.**

1. Add `PluginCatalogWorkflow` with direct tests (sorting, missing selection, browser state,
   snapshot output).
2. Move catalog storage, sorting, browser open/close, and selected-candidate lookup into it.
3. Keep plugin scanning task submission in `EditorController`.
4. Keep `IPluginHost` mutation calls in `EditorController` — do **not** create a signal-chain
   workflow yet (see `docs/plans/todo/editor-structure-deferred-work.md`).

**Exit criteria.** Catalog/browser state is local to one type; add/remove/open behavior is
unchanged; no premature signal-chain workflow.

## Stage 4: Extract `InputCalibrationWorkflow`

This is the **clearest example of a workflow currently misplaced inside the root controller** — it
needs route identity, previous calibration state, live-input gain, monitoring flags, prompt
visibility, settings-window state, and live-rig readiness. It is nonetheless sequenced **last**
because it is the most complex extraction: the simpler `State`/effect patterns from Stages 1–3
should be proven first (strongest by boundary clarity, last by risk).

**Owns:** persisted app-local calibration state; committed input device identity; prompt visibility;
settings-window route-transition state; active measurement rollback state.

**Likely supporting types:** `InputCalibrationSnapshot` (view-state projection input);
`InputCalibrationEffect` (requested live-input side effects); a route/rollback state type.

**Responsibilities:** decide whether calibration matches the current input identity; clear
calibration when committed input identity changes; preserve calibration across output-only,
sample-rate, and buffer-size changes; decide prompt visibility from project audio readiness and
input availability; request live-input gate effects; prepare measurement and rollback state; commit
successful calibration and request persistence.

**Does not own:** JUCE calibration window; audio-device settings window; `ILiveInput`;
`EditorSettings`; plugin catalog or signal-chain mutation; Tracktion objects.

**Steps.**

1. Add characterization tests for route changes, prompt visibility, measurement start/cancel/
   success, manual gain, settings-window open/close, and live-input gate behavior.
2. Add `InputCalibrationWorkflow` and supporting snapshot/effect value types.
3. Move calibration state fields into the workflow.
4. Convert live-input side effects into requested effects.
5. Keep `ILiveInput`, `EditorSettings`, and window ownership in `EditorController` and UI code.
6. Update `deriveViewState()` to use the workflow snapshot.
7. Add direct workflow tests with no JUCE windows and no audio device; keep integration-style
   controller tests for the full calibration flow.

**Exit criteria.** Calibration policy is directly testable; the controller executes live-input
effects but owns no calibration policy fields; settings-window route-transition rules are explicit;
prompt visibility is derived from workflow state plus root-provided facts.

## Stage 5: Runtime Checkpoint

Pause and assess before any further runtime work.

**Questions to answer.**

- Did the four extractions delete fields and logic from `EditorController`, or only add forwarding?
- Are side effects still executed in one obvious place?
- Do direct workflow tests make controller tests smaller or more focused?
- Are any extracted interfaces growing because the boundary is wrong?
- Is `EditorController` easier to scan by intent category?
- Does the effect-returning pattern hold up, or do some workflows need direct port access?
- (If Stage 3 was skipped) is leaving the plugin catalog in the controller still the right call?
- **Framework-isolation Part 2 (message-thread scheduling):** is the remaining JUCE usage in
  `editor_controller.cpp` limited enough to leave alone? The conditions and shape for doing it are
  in `docs/plans/todo/editor-structure-deferred-work.md`. Default expectation: leave it alone unless one
  of those conditions is met.

If an extracted type is awkward, fix or re-absorb it before continuing.

**Exit criteria.** No boundary debt carried forward; remaining controller responsibilities are
intentionally root-facade responsibilities; the effect-returning pattern is confirmed as the
convention for future editor workflow types.

**Checkpoint result, May 29, 2026.** The Stage 05 cleanup pass confirmed the runtime extraction
shape and found no additional extraction that should block Stage 6:

- Stages 1-4 deleted real state ownership from `EditorController`. `BusyOperationState` owns busy
  token/progress state, `DeferredProjectActionState` owns prompt/replay state,
  `PluginCatalogWorkflow` owns browser catalog state, and `InputCalibrationWorkflow` owns
  calibration policy state.
- `EditorController` remains the correct root facade for side-effect execution: it owns app-composed
  ports, task-runner submission, callback liveness, project IO completion, live-rig/audio-device
  mutations, settings persistence, view publication, and transient error reporting.
- The cleanup removed the unused cooperative busy-policy branch and narrowed project-write metadata
  helpers to `EditorAction::ProjectWriteAction`, so non-write actions are no longer representable at
  those helper call sites.
- No broad `ProjectWorkflow`, `EditorActionController`, action router, or `SignalChainWorkflow`
  should be introduced before Stage 6. The signal-chain workflow remains deferred until the audio
  boundary exposes a stronger authoritative chain model.
- A pure `EditorController::Impl` declaration/definition reorder is deferred to Stage 6. It is a
  readability/taxonomy change, not remaining runtime boundary debt.
- The focused follow-up moved pure action availability into private `editor_action_availability`
  code. `EditorController` now collects `ActionConditions` and still owns view-state construction;
  no broad action router or `EditorStateProjector` was introduced.
- Framework-isolation Part 2 remains deferred. The remaining direct JUCE message-thread scheduling
  sites are known and documented in `docs/plans/todo/editor-structure-deferred-work.md`.

## Stage 6: Editor-Scoped Readability

This candidate stage was evaluated but not implemented as standalone shipping work. The remaining
ideas are tracked in `docs/plans/todo/editor-structure-deferred-work.md` and should be handled when a
feature naturally touches the same area or the current tree creates a concrete navigation problem.
Behavior should not change during these taxonomy moves.

The focused action-policy cleanup in `docs/plans/completed/editor-action-policy-cleanup-plan.md` has
been applied. Pure action metadata and availability policy no longer live as loose controller
helpers, while side-effect dispatch stays in the root facade.

### 6a. Editor Naming Fixes

Apply names with high readability value and low design risk; avoid `Port` postfixes; reserve `Ports`
for bundles. Editor-relevant items:

| Current name | Issue | Action |
| --- | --- | --- |
| `InputCalibrationPrompt` | Name no longer captures workflow state | Replace during Stage 4. |
| `PluginHandle` / `LiveRigPlugin` | Overlap loaded-chain entry concepts | Fold into `PluginChainEntry`. |
| `AudioDeviceSettingsWindow` | The type is a launcher, not the actual window | Rename when touched. |
| `JuceEditorTaskRunner` | Concrete message-thread marshalling | Keep, document behavior. |

Do not rename established, already-clear editor types: `TransportControls`, `SignalChainPanel`,
`ArrangementView`, `BusyOverlay`, `PluginBrowserWindow`, `EditorViewState`, and other `*ViewState`
types.

### 6b. `editor/core` Folders (After Stage 4)

Group public headers and private source once the extracted types exist, one move at a time, updating
includes mechanically and Doxygen `\file` comments, without mixing in behavior changes:

- public: `controllers/` (controller contracts and implementations), `view_state/` (render-state
  DTOs), `workflows/` (the Stage 1–4 extracted types if/when any become public), plus `ports/`,
  `project/`, `import/`, `settings/`, `tasks/`, `commands/` as the obvious homes;
- private `src/`: `controllers/`, `project/`, `import/`, `tasks/`, `view_state/`, `commands/`, and a
  `workflows/` home for the extracted types.

Open questions to settle here: are controller interfaces clearer under `controllers/` or `ports/`;
is `IEditorView` a UI port owned by core or a root view-state contract; does `JuceEditorTaskRunner`
stay public under `tasks/`.

### 6c. `editor/ui` Source Folders (Independent)

The `editor/ui` source tree already mixes shell, windows, views, controls, panels, and shared
widgets, so this grouping is immediately useful and can happen any time during this work:
`shell/`, `timeline/`, `transport/`, `signal_chain/`, `plugin_browser/`, `audio_devices/`,
`input_calibration/`, `shared/`. Do not move a monolithic file into a role folder and call it
cleaner — the folder should correspond to a real ownership boundary.

**Verification for each readability slice.** `rg` finds no stale include paths or type names; CMake
source lists reference the new paths; public headers keep valid Doxygen file comments; affected
tests build and pass; the resulting tree is easier to scan.

---

# Cross-Cutting Constraints And Watch Items

- **Concrete `EditorView` test harness.** The UI test split produced a shared `EditorView` harness
  in `rock_hero::editor::ui_testing` that includes private view implementation headers through the
  UI test target's private `src/` include path. This is the sanctioned private-first convention
  applied to same-library UI tests, so it is **acceptable** — the warning sign is *not* that it
  includes private headers. The real watch condition is the harness beginning to **recreate
  controller workflow setup**, which would mean the view is reaching into policy that belongs in the
  controller/workflows. (A non-UI test needing concrete `EditorView` would separately violate
  layering.) The UI side does not justify a runtime refactor by itself.
- **Effect-returning discipline.** No extracted type calls `IEditorView`, owns JUCE components, or
  reaches into Tracktion/JUCE adapters directly.
- **Framework regression check.** After each runtime stage, confirm extracted workflow files include
  no JUCE GUI, JUCE audio-device, or Tracktion headers (narrow `juce_core` utility use only).
- **Measure success by deleted ownership.** Each extraction must delete member fields and policy
  branches from `EditorController`. A type that leaves the original state mirrored in the root facade
  has failed — stop and narrow or abandon it.

# Definition Of Done

The completed runtime scope is complete when:

- audio-device state serialization is behind `IAudioDeviceConfiguration`;
- busy, deferred-action, plugin-catalog (if Stage 3 proceeded), and input-calibration state no longer
  live as loose fields on `EditorController::Impl`;
- each extracted workflow/state type has direct tests and a private header, and constructs only the
  dependencies relevant to its responsibility;
- `EditorController` still handles intent routing, side-effect execution, async liveness, and
  view-state publication, and reads as a root facade;
- no broad replacement controller was created;
- the Stage 5 checkpoint confirmed the pattern;
- pure action metadata is no longer represented as loose anonymous helpers in `EditorController`;
- remaining editor naming/folder cleanup is tracked as deferred, opportunistic work.

# Non-Goals

- Do not split code by line count, or create one controller per view.
- Do not refactor runtime code as part of the completed test-file split.
- Do not create a workflow type merely because a test file exists with a matching name.
- Do not move JUCE dialogs, windows, or component ownership into `editor/core`, or move concrete
  JUCE view behavior into editor core.
- Do not move Tracktion graph mutation out of `common/audio`, or harden the append-only linear
  plugin chain as the permanent model.
- Do not add broad framework wrappers only for mocking.
- Do not rename every type with a role suffix, add `Port` to individual interfaces, or introduce
  role namespaces as a substitute for folders.
- Do not reorganize non-editor module folders here (see
  `docs/plans/todo/editor-structure-deferred-work.md`).
- Do not change behavior during pure taxonomy moves.
- Do not split `Engine` or `EditorController` just to satisfy folder shape.
- Do not update durable `docs/design/` documents from this plan; that is deferred until the shape
  has proven itself.
