# Editor Busy Overlay Plan

## Goal

Add one generalized editor-wide busy mechanism for operations that can visibly pause the app:
opening/importing projects, saving, publishing, loading plugins, and changing audio devices.

The user experience should be consistent:

- show that work is in progress
- disable unrelated commands while the work is active
- keep the current editor contents visible underneath
- prevent duplicate actions and conflicting mutations
- surface failures through the existing error path after the busy state clears

This should be implemented as controller-derived state and a reusable view overlay, not as
operation-specific loading flags scattered across individual panels.

Implement the mechanism as small vertical slices. The first slice should prove the shared state,
overlay, controller gating, and background task plumbing only for project open/import. Later slices
should reuse that foundation for save/publish, plugin loading, and audio-device apply one at a
time.

## Design Direction

Model busy state in `rock-hero-editor/core` as part of `EditorViewState`.

Suggested core types:

```cpp
enum class BusyOperation : std::uint8_t
{
    OpeningProject,
    ImportingProject,
    SavingProject,
    SavingProjectAs,
    PublishingProject,
    ChangingAudioDevice,
    LoadingPlugin,
};

struct BusyViewState
{
    BusyOperation operation{BusyOperation::OpeningProject};
    std::string message;
    bool cancel_enabled{false};
};
```

`EditorViewState` should then contain:

```cpp
std::optional<BusyViewState> busy;
```

`BusyOperation` is the stable semantic identity used by controller policy, tests, telemetry, and
special-case behavior such as audio-device apply handling. `message` is the canonical user-facing
text rendered by the view. The view should render `message` and should not derive its own display
copy from the enum.

Use a core helper such as `busyMessage(BusyOperation)` as the central source of default copy.
`beginBusy(BusyOperation)` should fill `m_busy->message` from that helper. Slice 1 does not need
per-call message overrides; if a later operation needs dynamic copy, add an explicit
`setBusyMessage()` or overloaded `beginBusy()` then.

The controller remains the owner of workflow policy. The JUCE view renders the state, blocks input
when `busy` is set, and emits no special workflow decisions except future cancellation requests if
we add cancellable operations later.

## Settled Decisions

- Do not add a minimum busy-overlay display duration in the first pass. Fast operations should not
  be made artificially slower unless real flicker shows up during testing.
- Keep the task runner editor-only. Game startup loading has different lifetime and threading
  constraints, so designing a shared runner now would be speculative.
- During audio-device apply, keep the settings dialog open. Disable its Apply button and show
  local progress in the dialog while the editor-wide busy overlay blocks the rest of the app.
- The plugin-loading slice means "Tracktion accepted the plugin into the edit." Warmed and
  seamless-ready plugin state belongs to the tone system work, not this busy-overlay feature.

## Busy Semantics

When `busy` is present:

- File commands are disabled.
- Transport controls are disabled.
- Waveform seek gestures are ignored.
- Signal-chain add/remove/reorder actions are disabled.
- The audio-device button is disabled unless the current busy operation is the device dialog
  itself and we intentionally allow that dialog to remain interactive.
- Keyboard shortcuts that trigger editor commands are ignored.
- Existing modal prompts should not be opened for new operations.
- Existing content remains visible so the app does not look like it has unloaded the project.

The controller should derive these disabled command flags from `busy` rather than requiring every
caller to remember to set all individual booleans manually.

Intents listed in the ignore list are dropped when `isBusy()` is already true at intent receipt.
Intents that themselves begin a busy operation do so on entry when `isBusy()` is false.

## View Overlay

Add a reusable `BusyOverlay` component in `rock-hero-editor/ui`.

The overlay should:

- be a normal top-level child of `EditorView`, not a JUCE modal component
- cover `EditorView::getLocalBounds()`
- stay in front of the other editor child components
- draw a translucent dim layer over the current editor contents
- show a compact centered progress surface with an indeterminate spinner and message
- intercept mouse input with `setInterceptsMouseClicks(true, true)`
- grab keyboard focus on busy entry and consume `keyPressed()`
- be hidden and non-intercepting when `EditorViewState::busy` is empty

Keep it presentation-only. It should not know what opening, saving, publishing, plugin loading, or
device changing means. It only renders the message and optional cancel affordance supplied by
core state.

Controller-level command gates are still required even with an intercepting overlay. Some intents,
such as native window close requests, do not originate from child-component input.

## Paint Fence

Use a paint-callback-triggered fence for message-thread-only work that must block after the busy
state is pushed.

Concrete shape:

1. Controller begins busy and pushes `EditorViewState`.
2. Controller asks the view to run a callback after the busy overlay has painted once.
3. `EditorView` stores the callback until `BusyOverlay::paint()` renders the busy state.
4. After that first paint, `EditorView` posts the callback with `juce::MessageManager::callAsync()`.
5. The blocking message-thread work starts from that callback.

The callback is single-shot. The view must clear the stored callback before or while posting it so
subsequent overlay paints, including spinner animation paints, cannot re-fire the same work.

This is stricter than chaining `callAsync()` or using `Timer::callAfterDelay(0)`, because it waits
for the overlay's paint path to actually run before starting the blocking work. The follow-up
`callAsync()` keeps the blocking work out of the paint call itself.

This fence is only for operations that cannot move off the message thread. Background project IO
should not use it because the overlay can animate while the worker thread runs.

## Operation Lifecycle

Every operation that can take visible time should follow the same high-level lifecycle:

1. Validate that no busy operation is active.
2. Set `m_busy` in `EditorController`.
3. Derive and push `EditorViewState` so the overlay can repaint.
4. Start or schedule the work.
5. Complete the operation and capture success or failure.
6. Clear `m_busy`.
7. Derive and push final state.
8. Report any captured error through `IEditorView::showError()`.

If the operation completion is stale, the lifecycle stops before clearing busy, pushing final
state, or reporting errors. The newer operation owns the current busy state.

The key rule is that the view must receive and paint the busy state before the expensive work
blocks the UI thread. Posting work with `juce::MessageManager::callAsync()` after a state push is
not a repaint guarantee: JUCE `repaint()` invalidates the component and a later message-thread task
can still run before the paint occurs. For work that cannot move off the message thread, use the
paint-callback-triggered fence before starting the blocking work, and accept that the
indeterminate spinner will freeze while the message thread is blocked.

## Async Execution Direction

Long term, use one small editor task runner boundary rather than ad hoc `std::thread` usage inside
controller methods.

Suggested responsibilities:

- Run filesystem-heavy work off the message thread.
- Marshal completion back to the message thread.
- Keep cancellation support optional for the first pass.
- Keep task results typed with existing project-owned error domains.
- Avoid allowing background tasks to mutate `Session`, `Project`, Tracktion, or JUCE components
  directly.

Project package open/import/save/publish can move most of their filesystem work to a background
task. The final commit to `EditorController` state, `Session`, and audio activation should happen
on the message thread. `IAudio::prepareSong()` also belongs in that message-thread commit stage
because the current Tracktion-backed implementation validates audio files through Tracktion
runtime types rather than pure filesystem IO.

Plugin loading and audio device changes are more constrained because Tracktion/JUCE graph and
device mutations are message-thread-sensitive. The existing plugin-host methods are explicitly
message-thread operations, including `IPluginHost::scanPluginFile()`. Do not move those calls to a
background task without adding a new adapter-owned API that is designed and verified for
background scanning.

For unavoidable message-thread operations, a busy overlay can communicate that work is starting,
but it cannot keep animating while the message thread is blocked. Prefer moving true IO work to
the task runner before relying on a synchronous fallback.

Tracktion graph mutations should preserve the current adapter policy: stop and release the
playback context, mutate plugin/clip/tone graph state, dispatch pending device updates when
needed, reallocate playback context, and rebind instrument monitoring. Busy state should span this
whole stop/mutate/dispatch/reallocate/rebind cycle rather than exposing per-step UI transitions.

## Controller Changes

Add controller-owned state:

```cpp
std::optional<BusyViewState> m_busy;
std::uint64_t m_busy_generation{0};
```

Add helpers:

```cpp
[[nodiscard]] bool isBusy() const noexcept;
[[nodiscard]] std::uint64_t beginBusy(BusyOperation operation);
void endBusy();
```

`beginBusy()` increments `m_busy_generation`, stores `m_busy`, and returns the generation token.
It also fills `m_busy->message` with `busyMessage(operation)` and leaves
`m_busy->cancel_enabled` false unless a later cancellable operation explicitly opts in.

Every operation captures the token returned by `beginBusy()` and compares it with the current
controller generation before committing state, regardless of whether completion happens on a
background task, a message-thread continuation, or a paint-fence callback. A mismatched generation
means the completion is stale and must be discarded. A stale completion does not call `endBusy()`;
the live busy state belongs to the operation that superseded it.

Update intent handlers to ignore new requests while busy:

- `onOpenRequested`
- `onImportRequested`
- `onSaveRequested`
- `onSaveAsRequested`
- `onPublishRequested`
- `onCloseRequested`
- `onExitRequested`
- `onPlayPausePressed`
- `onStopPressed`
- `onWaveformClicked`
- `onAddPluginRequested`

`deriveViewState()` should gate command booleans with `!isBusy()` rather than relying only on
project-loaded and transport state.

## Project Loading First Slice

The first deliverable should cover only project open/import. This gives the app an immediate
loading display for the most obvious slow path without forcing plugin readiness or audio-device
command routing into the same change.

Project loading slice scope:

- add `BusyViewState` and `EditorViewState::busy`
- add `BusyOverlay` and editor-wide input blocking
- add controller busy gates for all existing intents
- add default messages for `OpeningProject` and `ImportingProject`
- add the editor-only task runner
- move only open/import package IO behind the task runner
- add `m_busy_generation` stale-completion handling
- commit loaded `Project`, `Session`, transport seek, `IAudio::prepareSong()`, and arrangement
  activation on the message thread
- clear busy before reporting load/import errors

This slice should not change save, publish, plugin loading, or audio-device apply behavior except
that those intents are ignored while an open/import operation is busy.

When moving project IO to background work, avoid sharing mutable `Project` or `Session` objects
across threads. Prefer creating a temporary project context inside the background task, returning a
loaded result object, and committing it on the message thread only after success.

Each project IO task must capture the busy generation returned by `beginBusy()`. Completion
handlers must discard results whose generation no longer matches the controller, even if the
background work itself succeeded.

Known project-loading slice limitation: after background project IO completes,
`IAudio::prepareSong()` can still freeze the spinner because it must run during the message-thread
commit stage with the current Tracktion-backed audio adapter. That freeze is acceptable for the
first busy-overlay pass and should be revisited only if large projects make the commit stage
noticeably slow.

Known Slice 1 UX limitation: close, exit, and cancellation are ignored while project open/import is
busy. If an open/import stalls, the user has no clean in-app escape hatch in this slice. Accept
that limitation for the first implementation and revisit it when adding real cancellation or a
deliberate terminate-and-discard path.

## Save And Publish Slice

After project open/import is stable, reuse the same busy state and task runner for write
operations:

- `Save`
- `Save As`
- `Publish`

This slice should move package write IO behind the task runner, reuse `m_busy_generation`, and
keep final controller state commits on the message thread. It should not introduce cancellation or
determinate progress yet.

## Plugin Loading Slice

`onAddPluginRequested()` should become a busy operation around the current scan/add flow.

First pass:

- set busy to `LoadingPlugin`
- push `"Loading plugin..."`
- wait for the paint-callback-triggered overlay fence
- run the current scan/add flow on the message thread
- clear busy after the add command succeeds or fails

The current `IPluginHost::scanPluginFile()` and `IPluginHost::addPlugin()` calls must remain on the
message thread. A background plugin scan requires a new audio-adapter API that is explicitly safe
for that execution context.

`addPlugin()` success means the plugin was accepted into the Tracktion edit, not necessarily that
the external plugin is fully initialized and ready for seamless switching. Future signal-chain or
tone state should distinguish pending, ready, and failed plugin instances.

This plan explicitly defers warmed/seamless-ready plugin state to the tone system plan in
`docs/in-progress/tone-rack-plan.md`. Seamless tone switching should wait for a readiness signal
or adapter callback from that future tone work rather than assuming that `addPlugin()` completion
means the plugin is fully warmed.

Possible later split:

- keep actual plugin instance creation and graph rebuild on the message thread
- add a plugin warm-up step before reporting the chain as ready for seamless tone switching

Do not mark the project dirty for runtime-only plugin changes until tone persistence exists.

## Audio Device Apply Slice

Audio-device changes need special handling because the current settings UI hosts the device manager
directly. The controller receives `onAudioDeviceConfigurationChanged()` after the device has
already changed, which is too late to show a busy overlay before the slow work starts.

The current notification path is useful for persistence and state refresh, but it is post-facto.
It does not cover slow driver open/restart work and should not be considered a complete busy
solution.

Recommended direction:

- route device changes through a project-owned audio-device command surface instead of exposing
  direct mutation through the dialog
- let the UI emit "apply device setup" intents to the controller
- have the controller set `ChangingAudioDevice` before applying the setup through the audio
  backend
- keep the settings dialog open, disable its Apply button, and show local apply progress while the
  editor-wide busy overlay blocks the rest of the app
- pause or stop transport before applying device setup until live reconfiguration is proven safe
- keep the settings component as presentation of available devices, inputs, outputs, sample rates,
  and buffer sizes

This command-surface work should stay isolated in its own slice. It is the only way to guarantee
the overlay appears before a slow driver reconfiguration begins.

## Error Handling

Busy state should clear before showing the final error dialog. That keeps the app from displaying
an error dialog above a stale "Loading..." overlay.

The required order is: capture the result, clear busy, push final state, then call
`IEditorView::showError()` if the result failed.

Use the existing `IEditorView::showError()` one-shot path for failures. Do not put error text in
`BusyViewState`; busy state describes active work, not completed failure.

## Cancellation

The first pass should set `cancel_enabled = false` for every operation.

Cancellation becomes useful later for operations that can cooperatively stop:

- long package import
- long project save or publish
- plugin scan across multiple paths

Do not add a Cancel button until the underlying operation can actually respect it. A fake cancel
button would create ambiguous state and more test burden.

## Testing Strategy

Keep tests aligned with the vertical slices so each slice can be implemented, reviewed, and
validated without taking on the whole busy-overlay design at once.

Project-loading slice tests:

- open/import sets `busy` with the expected operation and message before work starts
- while open/import is busy, other intents do not call underlying services
- successful open/import clears busy and publishes the final derived view state
- failed open/import clears busy before reporting the existing error
- unsaved-change prompts are not newly opened while busy
- stale open/import completions are ignored when their busy generation no longer matches
- stale open/import completions do not call `endBusy()`
- close, exit, and cancellation are unavailable during the first project-loading slice
- `IAudio::prepareSong()` remains in the message-thread commit stage

Project-loading UI tests:

- `EditorView` shows the overlay when `state.busy` is present
- the overlay renders `BusyViewState::message`
- menu items are disabled while busy
- transport, waveform, and signal-chain actions are disabled or ignored while busy
- overlay hides and stops intercepting input when `state.busy` clears

Save and publish slice tests:

- save/save-as/publish set the expected busy operation and message
- package write completions commit on the message thread
- success clears busy and updates dirty/project-path state correctly
- failure clears busy before reporting the existing save or publish error
- save-as prompt continuation still works after the busy operation completes
- stale write completions are ignored

Plugin-loading slice tests:

- add-plugin sets `LoadingPlugin` and waits for the overlay paint fence before scan/add starts
- current scan/add calls remain on the message thread
- stale paint-fence callbacks do not scan, add, commit state, or call `endBusy()`
- success means the plugin was accepted into the Tracktion edit
- failure clears busy before reporting the existing plugin-load error
- project dirty state does not change for runtime-only plugin changes

Audio-device apply slice tests:

- applying a device setup sets `ChangingAudioDevice` before backend mutation begins
- the settings dialog Apply action is disabled while apply is active
- editor-wide commands behind the dialog are blocked while apply is active
- transport is paused or stopped before device setup changes
- failure clears busy before reporting the existing device error

## Implementation Slices

### Slice 1: Project Open/Import Loading Display

- Add `busy_view_state.h`.
- Include `BusyViewState` in `EditorViewState`.
- Add controller `m_busy` helpers and `m_busy_generation`.
- Add default messages for `OpeningProject` and `ImportingProject`.
- Populate busy messages inside `beginBusy(BusyOperation)` through the default message helper.
- Gate derived command booleans while busy.
- Add `BusyOverlay` to `EditorView`.
- Add the editor-only task runner.
- Move only open/import package IO through the task runner.
- Recheck the busy generation before committing task results.
- Commit loaded project/session/audio state on the message thread.
- Add controller and UI tests for the project-loading slice.

This slice is the first implementation target. It should leave save, publish, plugin loading, and
audio-device apply behavior otherwise unchanged.

### Slice 2: Save/Save As/Publish Busy State

- Add default messages for `SavingProject`, `SavingProjectAs`, and `PublishingProject`.
- Move package write IO through the same editor task runner.
- Reuse the same generation-token stale-completion handling.
- Keep prompt handling and final state commits on the message thread.
- Add save/save-as/publish controller tests.

### Slice 3: Plugin Loading Busy State

- Add the `LoadingPlugin` default message.
- Wrap the current add-plugin flow in busy begin/end.
- Use the paint-callback-triggered fence before starting scan/add.
- Recheck the busy generation before starting or committing the fenced work.
- Keep `IPluginHost::scanPluginFile()` and `IPluginHost::addPlugin()` on the message thread.
- Preserve the contract that success means Tracktion accepted the plugin into the edit.
- Add focused controller/UI tests for the fence and busy clearing behavior.

### Slice 4: Audio Device Apply Busy State

- Replace direct device-manager mutation from the settings dialog with controller-owned apply
  intents.
- Add audio-device command methods to the project-owned audio-device boundary.
- Show `ChangingAudioDevice` before applying setup.
- Keep the settings dialog open with its Apply button disabled and a local progress indicator.
- Pause or stop transport before device setup changes until live reconfiguration is proven safe.
- Keep the existing direct `juce::AudioDeviceManager` access only if still needed for read-only
  enumeration or transitional compatibility.

### Slice 5: Optional Progress And Cancellation

- Add determinate progress only after package operations can report meaningful progress.
- Add cancellation only for operations that can cooperatively stop.
- Extend `BusyViewState` with any needed progress fields only after a real operation needs them.

## Open Questions

- Should progress become determinate for project import/export once archive code can report file
  counts?
- Should plugin loading report separate scan, create, and warm-up phases once tone persistence and
  seamless tone switching are implemented?
