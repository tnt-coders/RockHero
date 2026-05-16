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
- Close and Exit are always honored, even during busy. The action router routes both through the
  supersede path so the user always has a clean in-app escape hatch. Supersede invalidates
  completion and clears UI busy state; it does not interrupt a worker thread that is already
  running.
- Add a small controller-side action router before adding more busy operations. Disabled
  `*_enabled` booleans and `BusyOverlay` remain the presentation layer, but controller action
  routing is the authoritative execution gate for direct calls, shortcuts, and delayed UI
  callbacks.

## Busy Semantics

When `busy` is present, the controller action router applies each action's busy policy.
`deriveViewState()` projects that same routing policy into the view-state booleans:

- `open_enabled`, `import_enabled`, `save_enabled`, `save_as_enabled`, `publish_enabled` are
  false because those commands use `BlockedByBusy`.
- `play_pause_enabled`, `stop_enabled`, and `signal_chain.add_plugin_enabled` are false for the
  same reason.
- `close_enabled` is true while busy because Close uses `SupersedesBusy`, even during the first
  open before a project has committed.

The view reflects command availability and intercepts normal input, but the controller owns the
rule for whether a command may start. This keeps direct controller calls, menu shortcuts, delayed
file chooser continuations, and rendered UI affordances aligned.

User-visible behavior while busy:

- File commands are disabled (Open, Import, Save, Save As, Publish; Close remains available).
- Transport controls are disabled.
- Waveform seek gestures are ignored.
- Signal-chain add/remove/reorder actions are disabled.
- The audio-device button is disabled unless the current busy operation is the device dialog
  itself and we intentionally allow that dialog to remain interactive.
- Keyboard shortcuts that trigger editor commands are ignored through `BusyOverlay`'s focus and
  `keyPressed()` consumption.
- Existing modal prompts are not opened for new operations. Prompts already onscreen before busy
  began stay onscreen until resolved.
- Existing content remains visible so the app does not look like it has unloaded the project.

## Action Routing Policy

Use a small private action router in `EditorController`, not scattered `if (isBusy()) return;`
checks and not view-only gating.

The router should be the single controller-side entry point for actions that can mutate
editor state, schedule work, or disrupt an in-flight operation. That includes direct controller
calls from tests, menu shortcuts, file chooser continuations, and normal component intents.

Use one private action value that carries the action id and any payload:

```cpp
class EditorAction
{
public:
    enum class Id
    {
        OpenProject,
        ImportProject,
        SaveProject,
        SaveProjectAs,
        PublishProject,
        CloseProject,
        ExitApplication,
        PlayPause,
        Stop,
        SeekWaveform,
        AddPlugin,
        ApplyAudioDevice,
    };

    [[nodiscard]] static EditorAction openProject(std::filesystem::path file);
    [[nodiscard]] static EditorAction saveProject();
    [[nodiscard]] Id id() const noexcept;
};

enum class ActionBusyPolicy
{
    // Normal mutating actions are blocked until the active busy operation finishes.
    BlockedByBusy,

    // Superseding actions intentionally invalidate the active busy operation before running.
    SupersedesBusy,

    // Cooperative actions may run during busy without clearing or invalidating it.
    AllowedWhileBusy,
};
```

This does not need to become a public framework yet. Keep it inside `EditorController` until a
second module needs the same command vocabulary.

Action policy should answer two related questions from one source:

- Is the action available in the current non-busy editor state?
- What happens to this action when `busy` is present?

`deriveViewState()` should project the same policy into `*_enabled` booleans using
`EditorAction::Id`. `runAction(EditorAction)` should use the ID for policy, then route the action
body internally only when the policy allows it. This prevents UI enablement and
controller execution from drifting apart.

Busy policy by action:

- Normal mutating commands use `BlockedByBusy`: Open, Import, Save, Save As, Publish, Play/Pause,
  Stop, waveform seek, Add Plugin, and Apply Audio Device.
- Close and Exit use `SupersedesBusy`: they call the supersede path that clears busy state and
  advances the generation token.
- `AllowedWhileBusy` should be rare and explicit. Use it only for passive observations or future
  cancel/progress commands that are designed to operate during busy state.

The router gives us the useful part of `isBusy()` checks without duplicating them everywhere:
the checks live in one action policy table/helper, and intent handlers become thin wrappers that
build a named `EditorAction`.

Do not introduce a separate `EditorCommand`, `EditorCommandDispatcher`, or duplicated command-state
snapshot until the controller has enough behavior to justify the extra object model. The current
simple shape is easier to read: `runAction(EditorAction::openProject(std::move(file)))`,
`canRunAction(id)`, `actionAvailableWhenIdle(id)`, and `actionBusyPolicy(id)`.

The router should not infer which actions belong on a background thread. It runs accepted action
bodies on the controller/message thread. Operation-specific helpers such as `openProject()`
or a future `runBusyTask(...)` helper own the split between background work and message-thread
commit, because project IO, plugin loading, Tracktion mutation, and audio-device changes have
different thread-affinity rules.

File chooser and prompt continuations must also re-enter through the action router before starting work.
For example, a file chooser opened before busy began might complete after another operation has
started; that continuation should be blocked by the router instead of relying on the overlay.

Deferred project replacement uses a separate private `ProjectCommand` value object with the same
factory style:

```cpp
requestProjectCommand(ProjectCommand::open(std::move(file)));
requestProjectCommand(ProjectCommand::close());
```

Prompt view state should expose only `ProjectCommandId`, because the view needs the identity for
copy and not the path payload.

## Close and Exit Supersede

Close and Exit are intentionally not blocked while busy. The action router detects their
`SupersedesBusy` policy, calls `supersedeBusyOperation()` before running the action body, and
then proceeds through normal close or exit behavior. The supersede path advances
`m_busy_generation` as well as clearing `m_busy`, so any in-flight worker's eventual completion
sees a generation mismatch and discards itself rather than committing on top of changed state.

- **Close during busy:** supersedes the in-flight operation and returns the editor to whatever
  project state existed before the open or import began.
- **Exit during busy:** supersedes the in-flight operation, then proceeds with normal exit. The
  editor task runner's destructor joins outstanding workers before the app shuts down, so exit may
  still wait for a worker even though the worker's result is no longer wanted.

Do not describe this as true cancellation. True cancellation requires a cancellation token or a
runner/API that can cooperatively interrupt long work. Until then, Close and Exit are best
understood as "discard the result and clear the UI state."

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

The overlay handles input from the EditorView child component tree. Intents that bypass that
tree, such as native window close (`onCloseRequested`) and Alt+F4 (`onExitRequested`), are not
gated by the overlay; they are handled by [Close and Exit Supersede](#close-and-exit-supersede)
above rather than by controller-level `isBusy()` guards.

## Paint Fence

Use a paint-callback-triggered fence for message-thread-only work that must block after the busy
state is pushed.

Concrete shape:

1. Controller begins busy and pushes `EditorViewState`.
2. Controller asks the view to run a callback after the busy overlay has painted once.
3. `EditorView` stores the callback until `BusyOverlay::paint()` renders the busy state.
4. After that first paint, `EditorView` posts the callback with
   `juce::MessageManager::callAsync()`.
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

1. Route the action and validate that action policy allows it to start.
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

The Slice 1 `JuceEditorTaskRunner` shape is intentionally transitional. It owns one worker and
joins any prior worker from `submit()`. That is only acceptable while the action router
guarantees that normal busy-blocked actions cannot submit overlapping background work. Before any
slice allows concurrent submissions, real cancellation, or command queuing, replace this with a
runner that queues work or rejects overlapping submissions without blocking the message thread.

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
void finishBusyOperation();
void supersedeBusyOperation();
void endBusy();
```

Add private action-routing helpers:

```cpp
void runAction(EditorAction action);
void performAction(EditorAction action);
[[nodiscard]] bool prepareAction(EditorAction::Id action);
[[nodiscard]] bool canRunAction(EditorAction::Id action) const;
[[nodiscard]] bool actionAvailableWhenIdle(EditorAction::Id action) const;
[[nodiscard]] static ActionBusyPolicy actionBusyPolicy(EditorAction::Id action) noexcept;
```

Intent handlers should construct a named `EditorAction` and pass it to `runAction(...)` directly.
Do not add a dispatcher class until the policy grows beyond this controller. The simple helper is
enough to keep controller execution and view-state availability aligned.

`beginBusy()` increments `m_busy_generation`, stores `m_busy`, and returns the generation token.
It also fills `m_busy->message` with `busyMessage(operation)` and leaves
`m_busy->cancel_enabled` false unless a later cancellable operation explicitly opts in.

`finishBusyOperation()` clears `m_busy` and advances `m_busy_generation` after the matching
completion commits or fails. `supersedeBusyOperation()` does the same generation advance before a
superseding action body runs. Both call the same low-level primitive because both make older
in-flight completions stale; the separate names keep normal completion and command takeover clear.

Every operation captures the token returned by `beginBusy()` and compares it with the current
controller generation before committing state, regardless of whether completion happens on a
background task, a message-thread continuation, or a paint-fence callback. A mismatched generation
means the completion is stale and must be discarded. A stale completion does not finish busy state;
the live busy state belongs to the operation that superseded it.

Intent handlers should not each hand-roll `isBusy()` checks. They should construct an
`EditorAction` and pass it through the private action router. `onCloseRequested`
and `onExitRequested` remain reachable during busy because their action policy is
`SupersedesBusy`; normal actions are blocked while busy by the same action policy that feeds
the view-state booleans.

`deriveViewState()` should call the same action-availability helper used by routing when filling
command booleans. Do not maintain a separate busy override block in the view-state path; that
would let UI enablement and direct command execution drift apart.

The action router calls `supersedeBusyOperation()` before running Close and Exit bodies when busy is
active. `closeProject()` should not call the low-level busy primitive directly.

## Project Loading First Slice

The first deliverable should cover only project open/import. This gives the app an immediate
loading display for the most obvious slow path without forcing plugin readiness or audio-device
command routing into the same change.

Project loading slice scope:

- add `BusyViewState` and `EditorViewState::busy`
- add `BusyOverlay` and editor-wide input blocking
- add the private action router and route open/import entry points through it
- derive command booleans from the same action policy used by direct controller calls
- wire routing to supersede busy state before running Close and Exit bodies
- add default messages for `OpeningProject` and `ImportingProject`
- add the editor-only task runner
- move only open/import package IO behind the task runner
- add `m_busy_generation` stale-completion handling
- commit loaded `Project`, `Session`, transport seek, `IAudio::prepareSong()`, and arrangement
  activation on the message thread
- clear busy and push the final non-busy state before reporting load/import errors

This slice should not change save, publish, plugin loading, or audio-device apply behavior. Their
commands are blocked while busy through action policy; Close and Exit remain available and
supersede the in-flight operation.

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
noticeably slow. Close and Exit during this freeze still work in principle because the override
block leaves `close_enabled` true and the BusyOverlay does not consume window-level inputs, but
the visible UI cannot redraw until the message thread is unblocked.

Slice 1 does not include real cooperative cancellation; that lands in Slice 5. Close and Exit are
the only escape hatches and they work by supersede (discarding the worker's result on
completion) rather than by interrupting the worker thread. For background IO this is fine because
the worker finishes quickly and its result is discarded; for the prepareSong freeze it means the
user may need to wait for prepareSong to complete before Close or Exit becomes visually
responsive.

## Startup Restore After Async Open

Startup restore needs a small follow-up because project open is now asynchronous.

`restoreLastOpenProject()` must not call `currentProjectFile()` immediately after scheduling
`onOpenRequested()`. At that point, the open request has only begun; the current project may still
be empty or may still be the previous session.

Recommended shape:

- If the saved project path no longer exists, clear `lastOpenProject` immediately and do not start
  an open.
- If the saved path exists, start a restore-specific open request and leave `lastOpenProject`
  untouched while the async work is pending.
- Let the open completion decide restore cleanup. On successful restore, normal project
  persistence keeps the setting correct. On failed restore, clear the setting only after the
  failure has completed if the intended policy is still "do not show the same failed restore every
  launch."
- Keep manual Open behavior separate from startup restore policy. A failed manual Open should not
  implicitly clear the user's saved startup project unless the user actually opened a different
  project.

This likely means adding either a private `openProjectFromStartupRestore()` helper or a small
operation-source flag captured by the open task state.

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

- first instrument and optimize the existing path before assuming a busy overlay is necessary for
  normal device switches; REAPER's behavior suggests a typical switch should not feel long
- time the JUCE calls (`setCurrentAudioDeviceType()`, `setAudioDeviceSetup()`), settings-dialog
  refresh work (`scanForDevices()`, staged `createDevice()`), and Tracktion route rebuild work
  (`dispatchPendingUpdates()`, `ensureContextAllocated()`, input rebinding)
- avoid repeated device scans and staged device creation during ordinary combo refreshes when
  cached data or current-device capabilities are sufficient
- avoid opening an intermediate default device when switching device type if the final setup is
  already known
- reduce Tracktion context rebuild work to the minimum needed after a committed route change
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

The required order is: capture the result, finish busy, push final state, then call
`IEditorView::showError()` if the result failed.

In controller code, that should look like `finishBusyOperation()`, then `deriveAndPush()`, then
`reportError(...)`. Do not call `reportError()` between finishing busy state and
`deriveAndPush()`: native dialogs can run a modal message loop, and the user should not see or
interact with the error while the last pushed editor state still says the app is busy.

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

Close and Exit during busy are supersede operations, not cancellation. They invalidate the pending
completion and clear the view state, but a running worker can still run to completion. With the
Slice 1 runner, app shutdown may wait in the runner destructor for that worker to finish.

## Testing Strategy

Keep tests aligned with the vertical slices so each slice can be implemented, reviewed, and
validated without taking on the whole busy-overlay design at once.

Project-loading slice tests:

- open/import sets `busy` with the expected operation and message before work starts
- while busy, the derived `EditorViewState` reports `open_enabled`, `import_enabled`,
  `save_enabled`, `save_as_enabled`, `publish_enabled`, `play_pause_enabled`, `stop_enabled`, and
  `signal_chain.add_plugin_enabled` as false
- while busy, `close_enabled` is true so Close can supersede the in-flight operation
- successful open/import clears busy and publishes the final derived view state
- failed open/import clears busy and publishes the non-busy state before reporting the existing
  error
- the fake view records a non-busy last state at the moment `showError()` is called
- stale open/import completions are ignored when their busy generation no longer matches
- stale open/import completions do not finish busy state
- Close during busy supersedes the in-flight operation through routing, the worker's completion
  sees a generation mismatch, and the loaded result is not committed
- Exit during busy supersedes the in-flight operation through the same path
- `IAudio::prepareSong()` remains in the message-thread commit stage
- startup restore does not clear `lastOpenProject` immediately after scheduling an async open
- startup restore clears missing-file settings synchronously without starting an open
- startup restore success or failure performs any setting cleanup from the async completion path,
  not from the scheduling path

Action-routing tests:

- normal actions are blocked by the router while busy even when the controller method is
  called directly
- Close and Exit are routed as superseding actions while busy
- file chooser and prompt continuations re-enter through routing and cannot start a normal
  mutating command after another busy operation has begun
- the action router and `deriveViewState()` agree on command availability for representative loaded,
  unloaded, dirty, clean, and busy states
- overlapping task submissions cannot be reached through normal action routing while busy

Project-loading UI tests:

- `EditorView` shows the overlay when `state.busy` is present
- the overlay renders `BusyViewState::message`
- menu items reflect the action policy while busy, with the File > Close item remaining enabled
- transport, waveform, and signal-chain actions are disabled or ignored while busy
- overlay hides and stops intercepting input when `state.busy` clears

Save and publish slice tests:

- save/save-as/publish set the expected busy operation and message
- package write completions commit on the message thread
- success clears busy and updates dirty/project-path state correctly
- failure clears busy and publishes the non-busy state before reporting the existing save or
  publish error
- save-as prompt continuation still works after the busy operation completes
- stale write completions are ignored

Plugin-loading slice tests:

- add-plugin sets `LoadingPlugin` and waits for the overlay paint fence before scan/add starts
- current scan/add calls remain on the message thread
- stale paint-fence callbacks do not scan, add, commit state, or finish busy state
- success means the plugin was accepted into the Tracktion edit
- failure clears busy and publishes the non-busy state before reporting the existing plugin-load
  error
- project dirty state does not change for runtime-only plugin changes

Audio-device apply slice tests:

- applying a device setup sets `ChangingAudioDevice` before backend mutation begins
- the settings dialog Apply action is disabled while apply is active
- editor-wide commands behind the dialog are blocked while apply is active
- transport is paused or stopped before device setup changes
- failure clears busy and publishes the non-busy state before reporting the existing device error

## Implementation Slices

### Slice 1: Project Open/Import Loading Display

- Add `busy_view_state.h`.
- Include `BusyViewState` in `EditorViewState`.
- Add controller `m_busy` helpers and `m_busy_generation`. The shared busy-end primitive advances
  the generation.
- Add default messages for `OpeningProject` and `ImportingProject`.
- Populate busy messages inside `beginBusy(BusyOperation)` through the default message helper.
- Add the private action router and route open/import entry points through it.
- Fill `deriveViewState()` command booleans from action policy.
- Wire routing to call `supersedeBusyOperation()` before Close and Exit bodies when busy.
- Add `BusyOverlay` to `EditorView`.
- Add the editor-only task runner; its destructor joins outstanding workers before allowing the
  app to shut down.
- Move only open/import package IO through the task runner.
- Recheck the busy generation before committing task results.
- Commit loaded project/session/audio state on the message thread.
- Push final non-busy state before reporting open/import errors.
- Add controller and UI tests for the project-loading slice, including Close-during-busy and
  Exit-during-busy supersede tests.

This slice is the first implementation target. It should leave save, publish, plugin loading, and
audio-device apply behavior otherwise unchanged.

### Slice 1A: Hardening Before More Busy Operations

Do this cleanup before moving on to save/publish, plugin loading, or audio-device apply:

- Replace view-only busy gating assumptions with the private action router.
- Route current controller entry points and delayed file/prompt continuations through routing.
- Fix startup restore so `restoreLastOpenProject()` does not inspect `currentProjectFile()`
  immediately after scheduling async open.
- Make every open/import failure path call `finishBusyOperation()`, `deriveAndPush()`, then
  `reportError()`.
- Keep the current single-worker `JuceEditorTaskRunner` only under the explicit invariant that
  action routing prevents overlapping normal submissions.
- Update comments and tests to say Close and Exit supersede in-flight work; they do not cancel or
  interrupt the worker.

This hardening slice is small but important. It makes the first busy operation trustworthy before
the same mechanism is reused by more operations.

### Slice 2: Save/Save As/Publish Busy State

- Add default messages for `SavingProject`, `SavingProjectAs`, and `PublishingProject`.
- Route Save, Save As, Publish, and any Save As prompt continuation through action routing.
- Move package write IO through the same editor task runner.
- Reuse the same generation-token stale-completion handling.
- Keep prompt handling and final state commits on the message thread.
- Push final non-busy state before reporting save/publish errors.
- Add save/save-as/publish controller tests.

### Slice 3: Plugin Loading Busy State

- Add the `LoadingPlugin` default message.
- Wrap the current add-plugin flow in busy begin/end.
- Route Add Plugin and the file chooser continuation through action routing.
- Use the paint-callback-triggered fence before starting scan/add.
- Recheck the busy generation before starting or committing the fenced work.
- Keep `IPluginHost::scanPluginFile()` and `IPluginHost::addPlugin()` on the message thread.
- Preserve the contract that success means Tracktion accepted the plugin into the edit.
- Push final non-busy state before reporting plugin-load errors.
- Add focused controller/UI tests for the fence and busy clearing behavior.

### Slice 4: Audio Device Apply Busy State

- Replace direct device-manager mutation from the settings dialog with controller-owned apply
  intents.
- Route audio-device apply through action routing.
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
