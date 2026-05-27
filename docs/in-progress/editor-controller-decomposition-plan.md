# EditorController Decomposition Plan

## Purpose

`EditorController` is the editor application's root workflow coordinator, but it now owns enough
separate workflows that the class is hard to navigate and the test fixture has grown with it. The
goal is to extract controller logic by responsibility while preserving the current public
`IEditorController` API as the main behavior seam for editor-view tests and app wiring.

This plan also resolves the related view/controller/view-state consistency question. Not every
view should get a controller by default. Controllers should appear where workflow policy, side
effects, persistence, or cross-service coordination need a headless public seam.

## Current Shape

- `EditorView` renders `EditorViewState` and emits intents through `IEditorController`.
- `EditorController` handles project workflow, audio preparation, plugin workflow, transport
  commands, busy-state policy, settings persistence, audio-device status projection, and deferred
  action replay.
- `AudioDeviceSettingsView` has a dedicated controller because that window has its own editor
  workflow and testable state mapping.
- Leaf widgets such as transport controls and arrangement rows are currently passive views. That is
  acceptable while they only render derived state and emit local intents upward.

## Design Rule

Use this rule rather than "every view has a controller":

- Product workflow views get a core-owned view state and a core-owned controller or service when
  they make decisions, coordinate side effects, or own meaningful state transitions.
- Passive leaf widgets may keep UI-owned view state and listener callbacks when they only render
  already-derived state.
- If a leaf widget starts owning workflow policy, move that policy into `rock-hero-editor/core`
  behind a controller/service and keep the widget passive.

This keeps navigation predictable without manufacturing controllers that only forward button
clicks.

## Planned Classes

### `EditorController`

Root facade that continues to implement `IEditorController` for the editor view and app shell. It
should route user intents to focused collaborators, aggregate their emitted state, and remain the
composition point for editor-wide dependencies.

### `ProjectWorkflowController`

Core class for open, import, save, save-as, publish, close, exit, startup restore, unsaved-changes
prompts, selected-arrangement restoration, and deferred action replay. It owns project lifecycle
state and emits requested side effects rather than directly becoming UI code.

### `PluginWorkflowController`

Core class for plugin catalog scan state, plugin browser opening, append/remove operations, plugin
window requests, and dirty-tone behavior. It owns signal-chain workflow policy while the
signal-chain view remains passive.

### `BusyOperationCoordinator`

Core class for busy tokens, stale-completion dropping, operation superseding, and delayed work that
must wait until a busy overlay has painted. It should have deterministic tests that do not require
JUCE components.

### `InputCalibrationController`

Core class for input calibration gate policy, calibration prompt visibility, audio-device
settings-window lifecycle tracking, and input device identity comparison. It owns the state that
decides whether calibration is required, when to clear it, and when to show or dismiss the
calibration prompt.

Planned state:

- calibration gain (optional app-local value)
- calibration prompt visibility
- input device identity snapshot captured before the settings window opens
- settings window open/closed flag

Planned responsibilities:

- load and persist calibration from shared user-audio settings
- clear calibration when the input device identity changes
- preserve calibration when only non-input fields change (buffer size, sample rate, output device)
- apply the monitoring gate through the `ILiveInput` port
- decide whether the calibration prompt should be visible
- distinguish temporary settings-window device closure from committed route changes

This class should be extracted after `BusyOperationCoordinator` and
`ProjectWorkflowController` are stable, because the calibration prompt and busy overlay interact
during audio-device apply flows. Until extraction, the calibration helpers live as a logical group
inside `EditorController::Impl`.

### `EditorStateProjector`

Small core helper only if needed after the larger extractions. Its job would be to assemble
`EditorViewState` from project, transport, plugin, and audio-device snapshots. Do not create this
first; create it only if state projection remains duplicated or noisy after extracting workflow
controllers.

## View State Placement

- Keep `EditorViewState`, `ArrangementViewState`, `PluginBrowserViewState`,
  `SignalChainViewState`, and `TransportViewState` in `rock-hero-editor/core` because they express
  editor workflow state.
- Keep transport controls as a passive UI widget that consumes `core::TransportViewState` and emits
  listener callbacks upward.
- Add UI-owned view state only when it is strictly local widget rendering state and does not belong
  to a product-core workflow projection.

## Refactor Steps

1. Add characterization tests through `IEditorController` for any workflow behavior that is not
   already covered before moving it.
2. Extract `BusyOperationCoordinator` first because stale completions and busy overlay timing cut
   across multiple workflows and can be tested deterministically.
3. Extract `ProjectWorkflowController` next. Keep persistence and prompt behavior observable
   through the existing public controller tests.
4. Extract `PluginWorkflowController` after project workflow is stable. Keep plugin browser and
   signal-chain tests focused on public editor-controller behavior until the extracted controller
   has a useful public API of its own.
5. Extract `InputCalibrationController` after busy and project workflow are stable. The
   calibration prompt and busy overlay interact during audio-device apply flows, so busy
   coordination should be settled first. Keep calibration gate tests focused on public
   editor-controller behavior until the extracted controller has its own public seam.
6. Reduce `EditorController` to routing, state aggregation, and dependency wiring.
7. Update durable design docs only after the extracted shape has settled.

## Testing Strategy

- Prefer tests through `IEditorController` while the public editor behavior is unchanged.
- Add direct tests for extracted core classes only when they expose meaningful public behavior,
  such as busy-token transitions or project workflow decisions.
- Do not add tests for private helper methods or one-off forwarding classes.
- Keep fakes local until the same stable fake behavior is needed by multiple modules.

## Non-Goals

- Do not split classes purely by line count.
- Do not create a controller for every passive view.
- Do not move UI component ownership or JUCE window lifetime into `editor/core`.
- Do not remove placeholder static libraries; they are intentional scaffolding for compiled
  modules.
