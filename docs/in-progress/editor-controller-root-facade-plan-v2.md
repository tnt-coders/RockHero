# EditorController Root Facade Plan v2

Status: in-progress plan. The active scope remains Phases 1 through 6: framework isolation, four
private workflow/state extractions, and a checkpoint before any broader controller work.

## Changes From v1

### Message-Thread Scheduling Reassessment Moved To The Checkpoint

v1 placed a message-thread scheduling reassessment (Phase 3) between BusyOperationState and
DeferredEditorActionState. This breaks extraction momentum for a question that cannot be answered
well until all four extractions are done and the controller's remaining JUCE usage is visible in
context. v2 folds that reassessment into the Phase 6 checkpoint.

### Speculative Future Phases Trimmed

v1 defined detailed phases for SignalChainWorkflow (Phase 9), ProjectLifecycleWorkflow slices
(Phase 10), and EditorStateProjector (Phase 11). These are gated on prerequisites that do not
exist yet and cannot be validated against current code. v2 retains them as future placeholders
without full phase definitions.

### Active Scope Tightened

v2 defines the active scope as Phases 1 through 6: framework isolation, four extractions, and one
checkpoint. Everything after the checkpoint is deferred until a concrete need arrives.

### Cost-Benefit Framing Added

v1 did not address when or why to start. v2 explains the cost-benefit reasoning: the project will
grow significantly, extraction cost scales with codebase size, and the extractions establish a
pattern that future features should follow.

### Private-First Header Convention Confirmed

v2 references the established convention for same-library test access to private headers via the
test target's `src/` include path.

---

## Purpose

`EditorController` should become a streamlined root facade for editor UI intents, not the owner of
every editor workflow state machine.

The current controller keeps editor policy out of JUCE components, which is correct. But it has
grown to 3,752 implementation lines with 46 member fields spanning busy coordination, deferred
prompt replay, plugin catalog management, input calibration policy, live-rig restore/capture,
project lifecycle, transport commands, settings persistence, and view-state projection.

That is too much for one class. As the project grows, every new editor feature gravitates into
`EditorController` because that is where everything lives. The extractions in this plan serve two
goals:

1. Move cohesive headless workflow policy into focused `editor/core` types that are directly
   testable with small fixtures.
2. Establish the pattern: workflows return decisions and effects, and the controller executes
   them so future features follow that pattern instead of growing the controller further.

## Why Now

Extraction cost scales with codebase size. The controller currently has 46 fields and 5,595 lines
of characterization tests. Extracting from that is cheaper than extracting after the next round of
features adds more state, more interactions between clusters, and more test scenarios that need
updating.

The more important reason is the pattern. Without the extractions, the controller's gravitational
pull only gets stronger as it gets bigger. Each new feature adds state and policy that entangles
with existing clusters. The first four extractions establish the convention for how editor workflows
should be structured. Future features follow that convention instead of growing the root controller.

The architectural principles document states that automated testability is a structural requirement,
not a secondary quality attribute. The extractions deliver on that: each workflow becomes directly
testable with simple inputs and explicit outputs, without requiring the full controller harness.

## Desired End State

`EditorController` remains the public `IEditorController` implementation. After the active phases,
it should be the root facade that:

- receives UI intents from `IEditorController`;
- owns or references app-composed ports;
- subscribes to transport and audio-device listener surfaces;
- delegates workflow decisions to extracted types;
- executes requested effects against project-owned ports;
- owns async callback liveness and task-runner coordination;
- publishes `EditorViewState` to `IEditorView`;
- reports transient errors through `IEditorView`.

Extracted workflow and state objects own editor policy that can be expressed without JUCE components,
Tracktion objects, audio devices, or real plugin scanning. They accept explicit inputs and return
explicit snapshots, decisions, or requested effects.

```text
EditorView
  -> IEditorController
      -> EditorController root facade
          -> BusyOperationState
          -> DeferredEditorActionState
          -> PluginCatalogWorkflow
          -> InputCalibrationWorkflow
      -> common/audio, editor/core Project, EditorSettings, IEditorTaskRunner
```

## Relationship To The Framework-Isolation Plan

Implement `docs/in-progress/editor-core-framework-isolation-plan.md` Part 1 first.

Part 1 moves audio-device state serialization behind `IAudioDeviceConfiguration`. This is a clean
boundary fix that removes direct JUCE XML parsing and raw `deviceManager()` calls from the
controller before workflow extraction begins.

Part 2 (message-thread scheduling) should be deferred until the Phase 6 checkpoint, when the
controller's remaining JUCE usage is visible in context after all four extractions.

## Design Rules

### Root Facade Rule

`EditorController` remains the only public root controller for the editor view. Extracted types do
not implement `IEditorController`, do not call `IEditorView`, and do not push view state directly.

### Effect-Returning Workflow Rule

Extracted workflows return requested effects instead of executing side effects directly.

Examples:

- request `EnableLiveInputMonitoring`;
- request `PersistInputCalibration`;
- request `ShowUnsavedChangesPrompt`;
- request `ReplayDeferredAction`;
- request `RefreshPluginBrowser`;
- request `StartBusyOperation`.

The root facade translates those requested effects into calls on `EditorSettings`,
`IEditorView`, `IEditorTaskRunner`, `common/audio` ports, `Project`, or transport.

### One Owner Per State Rule

After extracting a cluster, `EditorController` must not keep mirrored copies of the extracted state.
It may cache the latest full `EditorViewState`, but it should query extracted workflows for their
snapshots when projecting state.

### Private-First Header Rule

New extracted types start as private headers in `rock-hero-editor/core/src`. Same-library tests
access them through the test target's private `src/` include path. Promote a header to
`include/rock_hero/editor/core/` only when a real external consumer needs the type.

### No Broad Project Controller Rule

Do not add a large `ProjectWorkflowController` or `ProjectLifecycleController` as the first project
lifecycle extraction. That would likely create a second oversized coordinator. Project lifecycle
should be split only after smaller extractions prove a clean command/effect model.

### Naming Rule

Use names that describe practical responsibility:

- `State` for a small state machine or snapshot owner;
- `Workflow` for headless policy that accepts inputs and returns decisions/effects;
- `Controller` only for public view-facing controller contracts.

Avoid vague names such as `Manager`, `Handler`, `Helper`, or `Service`.

## Active Phases

### Phase 1: Framework Isolation

Implement `editor-core-framework-isolation-plan.md` Part 1.

Steps:

1. Add `restoreSerializedDeviceState` and `serializedDeviceState` to
   `IAudioDeviceConfiguration`.
2. Implement both in the audio adapter that owns the JUCE device manager.
3. Replace controller-side XML parsing and raw device-manager serialization with port calls.
4. Preserve the current invalid-state cleanup behavior: if a stored serialized value cannot be
   parsed into device-manager state, the controller clears it from `EditorSettings` based on the
   port result.
5. Update fakes and tests.
6. Remove unneeded JUCE audio-device and XML includes from `editor_controller.cpp`.

Exit criteria:

- `EditorController` no longer directly parses audio-device XML;
- `EditorController` no longer calls `deviceManager()` for persistence;
- invalid persisted audio-device state is still cleared from settings;
- tests cover restore/persist behavior through project-owned ports.

### Phase 2: Extract `BusyOperationState`

First extraction. Smallest and most self-contained.

Add `BusyOperationState` under `rock-hero-editor/core/src`.

Owns:

- active `BusyOperation`;
- current busy token;
- optional live-rig progress payload.

Responsibilities:

- begin a busy operation;
- transition a current token to another operation;
- reject stale tokens;
- finish or supersede operations;
- expose a `BusyViewState` snapshot.

Explicitly does not own:

- `IEditorView`;
- busy overlay paint fencing;
- task-runner submission;
- callback liveness guards.

Steps:

1. Add `BusyOperationState` with direct tests.
2. Move busy operation, token, stale-token, transition, finish, supersede, and progress snapshot
   logic into it.
3. Keep task-runner submission, view paint fencing, and callback liveness in `EditorController`.
4. Update `EditorController` to call `BusyOperationState` for busy decisions and view snapshots.
5. Keep broad controller tests for async busy behavior and paint-fence behavior.

Exit criteria:

- `EditorController` no longer owns busy token/progress fields directly;
- stale completions are still dropped;
- busy paint fence behavior is unchanged;
- direct busy-state tests do not require JUCE windows or audio devices.

### Phase 3: Extract `DeferredEditorActionState`

Owns:

- deferred project action;
- unsaved-changes prompt visibility;
- Save As prompt visibility.

Responsibilities:

- remember a blocked project action;
- decide whether to prompt, save first, discard, cancel, or replay;
- expose prompt snapshots for view-state projection;
- clear prompt state after completion.

Explicitly does not own:

- project IO;
- filesystem dialog choice;
- settings persistence;
- transport or audio calls.

Steps:

1. Add `DeferredEditorActionState` with direct state-machine tests.
2. Move deferred action storage and prompt visibility fields into it.
3. Express prompt decisions as return values: cancel, save first, discard and replay, or continue
   waiting.
4. Keep project IO, save execution, and view updates in `EditorController`.
5. Preserve controller tests for open/import/close/exit prompt flows.

Exit criteria:

- prompt replay logic is testable without constructing the full controller;
- `EditorController` no longer owns prompt booleans directly;
- deferred action behavior remains observable through `IEditorController`.

### Phase 4: Extract `PluginCatalogWorkflow`

Owns:

- known plugin candidates;
- browser visibility.

Responsibilities:

- sort and store catalog candidates;
- open/close browser state;
- validate selected candidate IDs;
- expose `PluginBrowserViewState` input data.

Explicitly does not own:

- `IPluginHost`;
- plugin instantiation;
- plugin removal;
- live-rig persistence;
- output gain;
- calibration gates.

Proceed with this phase only if the extraction deletes meaningful controller fields and branches.
If the catalog shape collapses to simple storage plus pass-through after Phases 2 and 3, leave it in
`EditorController` and record that decision at the checkpoint.

Steps:

1. Add `PluginCatalogWorkflow` with direct tests.
2. Move catalog storage, sorting, browser open/close, and selected-candidate lookup into it.
3. Keep plugin scanning task submission in `EditorController`.
4. Keep `IPluginHost` mutation calls in `EditorController` until signal-chain workflow is ready.
5. Add direct tests for sorting, missing selection, browser state, and snapshot output.

Exit criteria:

- plugin catalog/browser state is local to one type;
- plugin add/remove/open behavior is unchanged;
- future signal-chain workflow is not prematurely created.

### Phase 5: Extract `InputCalibrationWorkflow`

Most complex extraction. Do it last so the simpler state/effect patterns are proven first.

Owns:

- persisted app-local calibration state;
- committed input device identity;
- prompt visibility;
- settings-window route-transition state;
- active measurement rollback state.

Likely supporting types:

- `InputCalibrationSnapshot` for view-state projection input;
- `InputCalibrationEffect` for requested live-input side effects;
- `InputCalibrationRouteState` or a renamed replacement for measurement rollback data.

Responsibilities:

- decide whether calibration matches the current input identity;
- clear calibration when committed input identity changes;
- preserve calibration across output-only, sample-rate, and buffer-size changes;
- decide prompt visibility from project audio readiness and input availability;
- request live-input gate effects;
- prepare measurement state and rollback state;
- commit successful calibration and request persistence.

Explicitly does not own:

- JUCE calibration window;
- audio-device settings window;
- `ILiveInput`;
- `EditorSettings`;
- plugin catalog or signal-chain mutation;
- Tracktion objects.

Steps:

1. Add characterization tests for calibration route changes, prompt visibility, measurement start,
   cancel, success, manual gain, settings-window open/close, and live-input gate behavior.
2. Add `InputCalibrationWorkflow` and supporting snapshot/effect value types.
3. Move calibration state fields into the workflow.
4. Convert live-input side effects into requested effects.
5. Keep `ILiveInput`, `EditorSettings`, and window ownership in `EditorController` and UI code.
6. Update `deriveViewState()` to use the workflow snapshot.
7. Add direct workflow tests with no JUCE windows and no audio device.
8. Keep integration-style controller tests for the full calibration flow.

Exit criteria:

- calibration policy is directly testable;
- controller executes live-input effects but does not own calibration policy fields;
- settings-window route-transition rules are explicit;
- prompt visibility is derived from workflow state and root-provided facts.

### Phase 6: Checkpoint

Pause and assess the controller shape before any further work.

Questions to answer:

- Did the four extractions delete fields and logic from `EditorController`, or only add forwarding?
- Are side effects still executed in one obvious place?
- Do direct workflow tests make controller tests smaller or more focused?
- Are any extracted interfaces growing because the boundary is wrong?
- Is `EditorController` easier to scan by intent category?
- Should message-thread scheduling (framework-isolation Part 2) be addressed now, or is the
  remaining JUCE usage limited enough to leave alone?
- Does the effect-returning pattern hold up, or do some workflows need direct port access?

If an extracted type is awkward, fix or re-absorb it before continuing.

Exit criteria:

- no boundary debt is carried into future work;
- remaining controller responsibilities are intentionally root-facade responsibilities;
- the pattern is confirmed as the convention for future editor workflow types.

## Deferred Work

These are directional markers, not commitments. Do not start any of them until the stated
prerequisites are met and the Phase 6 checkpoint has confirmed the extraction pattern.

### Future `SignalChainWorkflow`

Do not create until `common/audio` exposes chain mutation operations beyond append/remove with
authoritative post-mutation snapshots, and plugin addressing can grow beyond a flat vector.

Would own the user-visible plugin chain snapshot and request append, insert, remove, move, and
reorder mutations. The root facade would execute mutations against `common/audio` and update the
workflow with the returned authoritative snapshot.

### Future Project Lifecycle Slices

Do not start with one broad project workflow type. If project lifecycle remains noisy after the
active phases, introduce smaller slices around proven decision points: interrupted restore prompts,
post-write deferred action continuation, save-before-close/exit decisions.

### Optional `EditorStateProjector`

Add only if projection becomes duplicated or difficult to read after multiple workflows expose clean
snapshots. Should be a pure or nearly pure projection helper, not a second controller.

### Framework-Isolation Part 2

Reassess at the Phase 6 checkpoint. Add a message-thread dispatch abstraction only if the remaining
direct `juce::MessageManager` and `juce::Timer` usage in `EditorController` is causing real
problems: test divergence, boundary leakage into extracted types, or a second consumer needing the
same dispatch pattern.

## How To Ensure The End Result Is Clean

### Measure Simplicity By Deleted Ownership

Each extraction should delete member fields and policy branches from `EditorController`. A new type
that leaves all original state mirrored in the root facade has failed.

### Keep Side Effects Centralized

The root facade executes side effects. Workflow objects do not call ports, settings, views, file
dialogs, Tracktion, or JUCE UI primitives directly.

### Require Direct Tests For Extracted Policy

Every extracted workflow/state type should have direct tests that construct it with simple values.
If a type cannot be tested without the full controller fixture, its boundary is probably wrong.

### Preserve Public Controller Characterization Tests

Do not delete broad `IEditorController` tests because direct workflow tests exist. Direct tests
cover policy. Controller tests cover wiring, side-effect execution, async behavior, and view-state
publication.

### Keep New Headers Private By Default

Start new workflow headers in `src/`. Same-library tests use the `src/` include path.

### Pause After Each Extraction

Let each boundary settle before extracting the next cluster. If a boundary proves wrong, adjust it
or re-absorb the type rather than layering workarounds on top.

Each completed extraction must remove direct owner fields or policy branches from
`EditorController` and add focused direct tests for the extracted type. If a phase mostly adds
forwarding without reducing controller responsibility, stop and either narrow or abandon that
extraction.

### Prevent Framework Regression

After each phase, check that extracted workflow files do not include JUCE GUI, JUCE audio-device,
or Tracktion headers. Narrow `juce_core` utility use is acceptable only when it remains headless and
testable.

## Definition Of Done For Active Phases

The active scope is complete when:

- audio-device state serialization is behind `IAudioDeviceConfiguration`;
- busy, deferred action, plugin catalog, and input calibration state no longer live directly as
  loose fields on `EditorController::Impl`;
- extracted workflow/state types have direct tests and private headers;
- `EditorController` still owns side-effect execution and async liveness;
- no broad replacement controller has been created;
- the root facade reads as intent routing, side-effect execution, workflow coordination, and
  view-state publication;
- the Phase 6 checkpoint has confirmed the pattern.

## Non-Goals

- Do not split code by line count.
- Do not create one controller per view.
- Do not move JUCE dialogs, windows, or component ownership into `editor/core`.
- Do not move Tracktion graph mutation out of `common/audio`.
- Do not add broad framework wrappers only for mocking.
- Do not create a large project workflow controller as a first step.
- Do not harden the current append-only linear plugin chain as the permanent model.
- Do not update durable `docs/design/` documents until the extracted shape has proven itself.
