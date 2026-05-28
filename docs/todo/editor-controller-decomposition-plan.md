# EditorController Decomposition Plan

## Status

Deferred. Do not start this refactor preemptively.

This document is a consolidated backlog plan for possible future `EditorController` decomposition.
The current recommendation is to prefer narrower cleanup first, especially framework-isolation work
that removes direct JUCE device serialization from editor core without creating broad new workflow
types.

## Purpose

`EditorController` is the editor application's root workflow facade. It implements
`IEditorController`, owns non-owning references to app-composed ports, subscribes to transport and
audio-device listener surfaces, executes side effects, pushes `EditorViewState`, reports transient
errors through `IEditorView`, and schedules work through the task runner and busy paint fence.

This plan identifies future extraction candidates only for cases where `EditorController::Impl`
becomes harder to change safely. The goal is not to split by size. The goal is to extract cohesive
state, policy, and workflow units that make the project simpler in practice.

## When To Start

Start only when at least one of these is true:

- A new feature requires adding state or policy to a cluster, and the cluster's current entanglement
  with unrelated fields makes the change hard to reason about or test.
- A bug in one cluster is hard to reproduce or fix because the test harness must construct the full
  controller to exercise a small state machine.
- A contributor or review finds that the implementation is too large to hold in working memory when
  modifying one workflow.
- The characterization test file becomes hard to navigate because too many unrelated scenarios
  share the same fixture.

These are judgment calls. The refactor should be motivated by real friction, not line count.

## Why Not Now

The current controller is large, but the case for broad decomposition is not strong enough yet:

- Existing characterization tests cover important behavior through the public `IEditorController`
  API.
- Responsibility clusters are large but still recognizable.
- The editor feature set is still moving, so premature extraction could freeze boundaries before
  interaction patterns settle.
- The tightest couplings cross proposed boundaries, especially project lifecycle to live-rig
  restore/capture and busy coordination to async work.
- Extracted types would need effect and callback contracts with the root facade. Those contracts are
  easier to design after the workflows prove stable.

Keep this plan as a ready reference. Re-read current code and design docs before using it.

## Design Rules

### Root Facade Rule

`EditorController` remains the public root facade.

Extracted workflow or state objects should not:

- call `IEditorView`;
- own JUCE components, dialogs, or windows;
- own Tracktion objects;
- push view state directly;
- schedule task-runner work directly unless that is their explicit responsibility.

The root facade should continue to execute side effects against project-owned ports and translate
workflow decisions into UI updates, task submissions, persistence, and audio operations.

### Simplification Gate

Do not add a new type unless at least one of these is true:

- it exposes a state machine that can be tested with a small fixture;
- it removes a meaningful field cluster from `EditorController::Impl`;
- it centralizes policy currently duplicated or about to be duplicated;
- it returns requested effects that make side effects explicit and testable;
- it clarifies a stable project concept whose name is already used in view state or ports.

Do not add a type if it only forwards calls, needs references to most of the root controller, or
requires broader mutable access than the code it replaces.

### Naming Rule

Use names that describe practical responsibility:

- `Controller` only for types with a public view-facing controller contract.
- `State` for small state machines and snapshots.
- `Workflow` for headless policy that accepts explicit inputs and returns explicit decisions or
  requested effects.

Avoid vague names such as `Manager`, `Handler`, `Helper`, or `Service` unless the project role is
more precise than the alternatives above.

### Threading And Side-Effect Rule

Extracted editor workflows should run on the message thread unless documented otherwise. They are
not real-time audio-thread participants.

Plugin catalog scanning may run on a worker thread through `IEditorTaskRunner`, but Tracktion graph
mutation, plugin add/remove/open, live-rig restore/capture, and audio-device mutation remain
message-thread operations behind project-owned ports.

### Audio Authority Rule For Plugin Chain Order

`common/audio` is the authority for the runtime plugin chain after mutations. Editor core may hold
a rendered snapshot, but future insert/reorder/move operations must return an authoritative
post-mutation chain from the audio boundary.

Editor core must not infer final chain order by editing local `PluginViewState` vectors once
insert/reorder/move exists.

## Extraction Candidates

### Tier 1: Clear Boundaries

#### `BusyOperationState`

Likely first extraction if busy-state behavior becomes painful. This is the smallest cohesive state
machine.

Responsibilities:

- store the active `BusyOperation`;
- issue and advance busy tokens;
- answer whether a token is current;
- transition between busy operations without changing token identity;
- supersede and finish operations;
- hold optional progress/message override state when needed;
- produce `BusyViewState` from current state.

Non-responsibilities:

- no `IEditorView`;
- no JUCE paint fence;
- no task-runner dispatch;
- no callback liveness ownership;
- no direct view update.

Expected tests:

- beginning an operation creates a current token;
- finishing invalidates old tokens;
- superseding invalidates old completions;
- transition preserves the token;
- stale progress updates are ignored;
- `BusyViewState` matches operation, message, presentation, and progress.

#### `DeferredEditorActionState`

Good candidate if unsaved-change and Save As prompt replay becomes difficult to change. It is small
but currently testable only through the full controller harness.

Responsibilities:

- store the deferred project action;
- track unsaved-changes prompt visibility;
- track Save As prompt visibility;
- accept user prompt decisions;
- return replay, save-first, discard, cancel, or no-op decisions.

Non-responsibilities:

- no project IO;
- no filesystem dialogs;
- no view mutation;
- no audio activation;
- no live-rig capture/restore.

Expected tests:

- unsaved replacement requests show the prompt;
- Save keeps the deferred action until save succeeds;
- failed save preserves or drops deferred action according to current behavior;
- Discard replays the correct action;
- Cancel clears prompts without changing the project.

### Tier 2: Cohesive But Entangled

#### `InputCalibrationWorkflow`

The calibration behavior is cohesive, but it touches audio-device settings-window lifecycle,
project audio readiness, persistence, and `ILiveInput` effects. Extract only after the effect
contract is obvious.

Responsibilities:

- own app-local calibration state supplied by settings;
- track committed input device identity;
- track settings-window open/closed route-transition state;
- decide calibration status for view state;
- decide prompt visibility;
- prepare measurement state and rollback state;
- request live-input gate effects explicitly;
- request persistence explicitly or expose the value to persist.

Non-responsibilities:

- no JUCE input calibration window;
- no audio-device settings window ownership;
- no plugin catalog or plugin chain mutation;
- no Tracktion calls.

Expected tests:

- calibration is cleared when committed input identity changes;
- calibration is preserved for output-only, sample-rate, or buffer-size changes;
- prompt visibility follows project audio readiness and input route availability;
- starting measurement disables normal monitoring and records rollback state;
- cancel/dismiss restores previous route state;
- success stores gain, enables monitoring, and requests persistence.

### Tier 3: Optional

#### `PluginCatalogWorkflow`

Extract only if catalog/browser state remains noisy after higher-value extractions. This may be
mechanically simple but might not remove enough cognitive load to justify a new type.

Responsibilities:

- store known plugin candidates for the browser;
- sort candidates consistently;
- track browser visibility;
- validate selected candidate IDs against current catalog;
- expose a browser snapshot for `PluginBrowserViewState`.

Non-responsibilities:

- no `IPluginHost` calls;
- no plugin instantiation;
- no live-rig persistence;
- no output gain;
- no calibration gating;
- no busy scheduling.

Expected tests:

- catalog sorting remains stable;
- selected missing candidate reports a selection error;
- opening browser refreshes snapshot from supplied candidates;
- closing browser clears only visibility, not catalog state.

## Future Placeholders

These types are directional markers, not commitments:

- `SignalChainWorkflow`: only after `common/audio` exposes insert/reorder/move operations with
  authoritative post-mutation snapshots.
- `ProjectLifecycleWorkflow`: only after smaller extractions show that project
  open/import/save/close decisions can be expressed as commands and effects without giving the
  workflow object access to every port.
- `EditorStateProjector`: only after multiple extracted workflows expose clean snapshots and state
  projection becomes duplicated or difficult to read.

## Refactor Sequence

1. Wait for a trigger condition.
2. Add or confirm characterization coverage for the specific cluster being extracted.
3. Extract the chosen candidate minimally: state, transitions, projection, and effect requests.
4. Leave orchestration, port calls, scheduling, and view updates in the root facade.
5. Let the extraction settle through at least one feature addition or bug fix.
6. Adjust or re-absorb a bad boundary before starting another extraction.
7. Repeat only if remaining friction still justifies another type.
8. Update durable `docs/design/` documents only after the extracted shape settles.

## Characterization Coverage To Preserve

Before each extraction, preserve current behavior through public `IEditorController` tests where
possible. Add direct tests only when an extracted type exposes meaningful public behavior.

Important behavior to keep covered:

- busy routing disables ordinary commands while preserving allowed superseding behavior;
- stale busy completions are dropped;
- close/exit supersede project open, import, save, plugin load, and live-rig restore safely;
- busy overlay paint fencing still runs blocking message-thread work after a visible busy push;
- plugin browser open, scan, add, remove, open-window, and error behavior;
- plugin add/remove marks tone dirty only when live-rig persistence is available;
- live-rig restore reports progress and commits plugin/output snapshots correctly;
- save captures live rig before project IO leaves the message thread;
- input calibration survives non-input route changes and clears on input identity changes;
- calibration prompt and audio-device settings window interactions remain unchanged.

## Non-Goals

- Do not split classes by line count.
- Do not create controllers for passive views.
- Do not move UI component ownership, JUCE dialogs, file choosers, or window lifetime into
  `editor/core`.
- Do not move Tracktion objects or plugin graph mutation outside `common/audio`.
- Do not introduce broad wrappers around JUCE or Tracktion for mocking.
- Do not harden today's append-only linear plugin chain into a long-term public editor model.
- Do not add `EditorStateProjector` before projection duplication or snapshot clarity justifies it.
