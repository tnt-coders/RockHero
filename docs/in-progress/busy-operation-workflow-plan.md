# Busy-Operation Workflow Plan (Editor)

Status: in-progress plan.

This plan supersedes the narrower `audio-device-settings-busy-overlay-ordering` note. That note
identified one symptom; this plan addresses the design smell underneath it. It also pulls
**Framework-Isolation Part 2 — Message-Thread Scheduling** out of
`docs/todo/editor-structure-deferred-work.md` into active scope, because the busy lifecycle is the
consumer that justifies the scheduling boundary.

It continues the runtime work begun in `editor-runtime-extraction-plan.md`, Stage 1 (Extract
`BusyOperationState`). Stage 1 extracted the pure state; this plan extracts the orchestration that
still surrounds it.

## Origin

Applying audio-device settings from the modal settings window can briefly show the editor-wide
busy overlay *after* the settings dialog has already reappeared on the failure path. The cause is
ordering: `EditorController::Impl::onAudioDeviceChangeRequested()` runs the dispatched operation —
which includes the settings controller reopening its dialog on failure — and only afterward calls
`finishBusyOperation()`:

```text
captured_change()        // apply fails -> setApplying(false) reopens dialog
finishBusyOperation()    // editor busy overlay clears AFTER the dialog is back
```

The user-visible expectation is the reverse on failure: apply fails, the editor busy overlay
clears, then the settings dialog reappears with its inline error.

## Root Design Problem

The pure busy *state* is already cohesive: `BusyOperationState` (`busy_operation_state.h`) owns the
active operation, the invalidation token, the live-rig progress payload, and the `viewState()`
projection, and explicitly does not submit work, wait for paint, call view APIs, or own ports.

The busy *orchestration* around it is not cohesive. It is spread across:

- ~10 thin forwarding methods on `EditorController::Impl` — `beginBusy`,
  `transitionBusyOperation`, `transitionBusyOperationAfterPaint`, `finishBusyOperation`,
  `supersedeBusyOperation`, `endBusy`, `beginLiveRigLoadProgress`, `setLiveRigLoadProgress`,
  `updateLiveRigLoadProgress`, `runAfterBusyOverlayPainted` — most of which are "mutate
  `m_busy_state`, then `updateView()`";
- `PaintGate`, the worker/message-thread rendezvous used by the paint-fenced transition;
- the paint fence itself, which lives in `EditorView::runAfterBusyOverlayPainted` and is reached
  through the view;
- ad-hoc message-thread scheduling inlined at each site (`juce::MessageManager::callAsync`,
  `juce::Timer::callAfterDelay`, and `juce::MessageManager::getInstanceWithoutCreating()`
  test-environment branches);
- the lifecycle spine re-typed inline in every operation handler (open / import / plugin-load /
  audio-device / live-rig all hand-roll `begin -> updateView -> runAfterBusyOverlayPainted ->
  token check -> work -> finish`).

There is no single owner of the rule *"run blocking work under the busy overlay, then continue
only after the overlay has cleared."* The audio-device ordering bug exists precisely because that
rule has no home: it got split across the dispatcher callable and `onAudioDeviceChangeRequested()`,
and the two halves sequenced wrong.

## Decision

Extract `BusyOperationWorkflow` in `editor/core`. It owns the existing `BusyOperationState`
instance plus the scheduling dependency and internal presentation handoff the orchestration needs,
and exposes the busy lifecycle as one cohesive API. The audio-device ordering becomes a property of
that API rather than something each call site must sequence by hand.

The name uses the project's sanctioned `Workflow` role (headless policy that accepts inputs and
performs/returns effects), per `editor-runtime-extraction-plan.md` ("Naming And Role Vocabulary").
It is deliberately **not** a `Manager`, `Coordinator`, or `Service`, which that section bans. It
sits beside `BusyOperationState` the way the other extracted runtime types sit beside their state.

`EditorController` stays the root facade and the only `IEditorController` implementation. It holds
a `BusyOperationWorkflow` member and delegates lifecycle to it, exactly as it already delegates to
`BusyOperationState`, `DeferredProjectActionState`, `PluginCatalogWorkflow`, and
`InputCalibrationWorkflow`.

## Scope

In scope:

- busy-operation lifecycle orchestration (the ~10 thin methods + `PaintGate`);
- a message-thread scheduling boundary (the three deferred Framework-Isolation Part 2 sites);
- private busy-presentation callback seams wired from the attached view;
- the explicit "clear busy, then continue" ordering seam;
- audio-device apply/cancel ordering around the editor busy overlay.

## Non-Goals

- Do **not** let `BusyOperationWorkflow` become a second oversized coordinator. It owns the busy
  lifecycle and the scheduling/presentation plumbing for it — nothing else. It must not learn
  project, plugin, audio-device, or live-rig *operation semantics*; handlers pass their work and
  continuations in as callables.
- Do not move JUCE dialog/window behavior out of `editor/ui`. The settings-window owner keeps
  dialog visibility, modality, inline errors, and close behavior.
- Do not add a generic JUCE wrapper layer beyond the narrow scheduler port this plan introduces.
- Do not pull in the rest of the deferred editor-structure work (`SignalChainWorkflow`, project
  lifecycle slices, `EditorStateProjector`, non-editor readability moves).

## New Boundary And Callback Seam

`IMessageThreadScheduler` is a project-owned interface in `editor/core`, consistent with the
ports-and-adapters rule in `architectural-principles.md` ("Ports and Adapters", "Keep Threading at
the Boundary"). The goal is to make the workflow runnable in tests without a JUCE message loop,
removing the `MessageManager == nullptr` branches.

### `IMessageThreadScheduler`

```cpp
[[nodiscard]] virtual bool postToMessageThread(std::function<void()> work) = 0;
[[nodiscard]] virtual bool callAfterDelay(
    std::chrono::milliseconds delay, std::function<void()> work) = 0;
```

Introduced as its own port rather than folded into `IEditorTaskRunner`. `IEditorTaskRunner`'s
contract is *"run slow work off the message thread, marshal completion back"* — a worker-thread
abstraction. `postToMessageThread` / `callAfterDelay` are message-thread-only primitives with no
worker involved; merging them would blur a currently-clean interface. (This is the "narrower
`IMessageThreadScheduler`" alternative the deferred-work doc already anticipated.)

- Production: a JUCE-backed implementation using `juce::MessageManager::callAsync` and
  `juce::Timer::callAfterDelay`, composed in `app/`. The missing-`MessageManager` case is
  test-only and is answered by the fake, so a `getInstanceWithoutCreating()` branch must not
  reappear in the workflow or the production implementation.
- Tests: a deterministic fake that runs posts/delays synchronously or through an explicit pump,
  matching the existing inline task-runner shape.

The scheduler intentionally does **not** expose `isMessageThread()` or wrap JUCE's
`isThisTheMessageThread()`. Thread context should be encoded in the workflow entrypoint being
called: message-thread handlers use message-thread workflow methods, and worker-progress handlers
use worker-specific workflow methods. The workflow should not preserve the current ambiguity where
a method asks at runtime where it came from before deciding how to behave.

`postToMessageThread` and `callAfterDelay` return whether the work was accepted for delivery. The
workflow must treat `false` as a non-fatal scheduling failure: release any waiters, skip cosmetic
paint/delay waiting, and continue in the least surprising way rather than hanging an operation. For
the live-rig completion hold, a failed delay schedule means running the completion immediately.
This preserves the current `callAsync` failure handling around `PaintGate`.

For `transitionAfterPaintAndWaitFromWorker()` tests, the scheduler/presentation fakes must let the
posted transition and presentation callback resolve while the worker path is waiting. A fake that
only queues work on the same test thread will deadlock because the test cannot pump the queue while
the worker is blocked in the wait. Use an inline fake for these tests, or an explicit worker/test
pump that runs independently of the waiting call.

### Busy presentation handoff

Do **not** add a separate busy-paint interface. The single production implementation is still the
view's existing `runAfterBusyOverlayPainted(...)`, and naming a one-method interface around this
mechanism makes the design read worse than the behavior it represents.

Instead, `BusyOperationWorkflow` stores a paired late-bound presentation handoff:

```cpp
using Continuation = std::function<void()>;
using PresentationFence = std::function<void(Continuation)>;

void attachPresentation(PresentationFence ready, PresentationFence cleared);
void detachPresentation();
```

These callbacks are implementation seams, not public architecture concepts. `EditorController`
wires them from `attachView()` to the current view's `runAfterBusyOverlayPainted(...)` and
`runAfterBusyOverlayRemoved(...)`; workflow tests can install fake callbacks that run immediately
or wait for explicit "paint" signals. If no callbacks are attached, the workflow runs
continuations immediately so startup/shutdown paths cannot wait for an impossible paint.

This work also adds a `detachView()` clear path, which closes a **pre-existing latent dangling
pointer**, not just a risk introduced by the new callback. `Editor` declares `m_controller` before
`m_view` (`editor.h`), so the view `unique_ptr` is destroyed *before* the controller, and the
controller's raw `m_view` is never nulled today — there is already a teardown window where
`m_view` dangles. The clear path must therefore retire **both** bindings: the existing `m_view`
and the workflow callbacks. Ownership of the call is explicit: `Editor` teardown must invoke
`EditorController::detachView()` *before* it resets `m_view`. "Clear before the view is destroyed"
is not enough on its own; the call site and ordering are the contract. `detachView()` must clear
both the existing `m_view` pointer and the workflow's busy-presentation callbacks.

The clear operation must reset the stored `std::function` objects, not merely make them return
early. The callbacks installed from `attachView()` capture the current view so they can call
`runAfterBusyOverlayPainted(...)`; the captured view pointer's lifetime is therefore the
`std::function` lifetime. Resetting the functions releases those captures and prevents the dangling
view problem from moving from `m_view` into the workflow callbacks.

### View refresh

The "mutate state, then refresh the view" rule moves *into* the workflow so call sites stop
pairing every lifecycle call with `updateView()`. The workflow takes an injected
`std::function<void()>` refresh callback, wired by the controller to `updateView()`. The controller
still owns view derivation and continues to read `m_busy.viewState()` inside `deriveViewState()`;
the workflow only signals when a re-derive is needed. A `std::function` keeps this the lightest
seam; promote to a tiny port only if a second signal appears.

### Async liveness

Moving the message-thread posting into the workflow must not drop the liveness guard that exists
today. Every posted/delayed continuation is currently wrapped in `safeCallback(...)`, which guards
on the controller Impl's `m_alive` `std::weak_ptr` so a queued `callAsync` cannot fire into a
destroyed controller. The production `IMessageThreadScheduler` is composed in `app/` and
**outlives** the editor, so a queued post can fire after `Editor` — and therefore the workflow —
is gone.

The workflow must own a private `std::shared_ptr<bool>` liveness token and capture
`std::weak_ptr<bool>` in every callback it hands to the scheduler or busy-presentation callbacks,
per
`architectural-principles.md` ("Keep Threading at the Boundary"): a weak liveness guard for
project-owned non-component owners whose callbacks may outlive them. This is specifically about the
workflow's own dispatch; continuations passed *in* by callers (e.g. the settings controller's
`alive` guard, or `EditorController::safeCallback` wrappers) still carry their own guards and are
not the workflow's responsibility. The implementation must not silently re-expose a raw, unguarded
post.

## `BusyOperationWorkflow` API

The workflow owns `BusyOperationState m_state`, references `IMessageThreadScheduler`, stores the
late-bound busy-presentation callbacks, stores the refresh callback, owns a private
`std::shared_ptr<bool>` liveness token for weak-guarding its own posted callbacks, and absorbs
`PaintGate`. It exposes the lifecycle as composable primitives plus one ordered
convenience seam:

Primitives (each refreshes the view after mutating state, except where noted):

- `begin(BusyOperation) -> token` — refreshes internally. Note this is a consolidation: today's
  `beginBusy()` does **not** refresh, and every call site follows it with an explicit
  `updateView()`. Migrated callers must drop that now-redundant refresh (see Stage 5).
- `isCurrent(token)`, `currentToken()`
- `transition(BusyOperation, token)`
- `transitionAfterPaintAndWaitFromWorker(BusyOperation, token)` — owns the busy-presentation
  callback + `PaintGate` rendezvous internally and is for worker-thread progress only
- `finish()` — clears and refreshes
- `supersede()` — clears without refreshing (caller owns the next push)
- `beginLiveRigProgress()`, `setLiveRigProgress(message, fraction)`,
  `updateLiveRigProgress(progress)`
- `attachPresentation(ready, cleared)` / `detachPresentation()` — late-bind and clear the
  view-owned presentation handoff as one lifecycle pair
- `postToMessageThread(callback)`, `callAfterDelay(delay, callback)` — delegate to the scheduler

Ordered seam (the missing rule, made explicit and given the human-facing name callers should use):

- `runMessageThreadBusyOperation(BusyOperation op, std::function<void()> work,
  std::function<void()> after_cleared = {})`

Semantics:

```text
token = begin(op)           // busy state set, view refreshed
run after busy presentation is ready:
    if !isCurrent(token): return
    work()                  // blocking message-thread work, overlay already painted once
    if !isCurrent(token): return
    finish()                // busy cleared, view refreshed
    if after_cleared: after_cleared()
```

`finish()` runs before `after_cleared()`, so the overlay-cleared state is published before any
post-operation UI. The busy-presentation readiness callback stays private to the workflow; callers
ask for the outcome they want through `runMessageThreadBusyOperation(...)`, not for a paint gate
or readiness hook. The second token check is required: `work()` may re-enter controller behavior,
trigger shutdown, or supersede the busy operation. In that case the workflow must not clear a
newer operation or run a stale continuation. This is the shape the audio-device and plugin-load
handlers already want; the live-rig and project-open/import handlers keep using the lower-level
primitives because they span worker-thread phases and determinate progress.

### Paint-Gate Threading Contract

`transitionAfterPaintAndWaitFromWorker()` preserves the current paint-gate behavior while
making the thread contract explicit. It is not a "maybe message thread, maybe worker" method.
Message-thread callers use `transition()` directly. Worker-progress callers use this method when
they need the next expensive worker step to wait briefly until the new busy state has had a chance
to paint. The behavior:

- it is used by project-operation progress callbacks, which normally run on a worker thread;
- otherwise it posts the transition to the message thread, waits for the busy presentation to be
  ready, and then releases the worker;
- waiting has the existing short timeout so a hidden, minimized, or tearing-down view cannot turn a
  cosmetic paint fence into a stuck project operation;
- if posting fails or no busy-presentation callback is attached, the gate releases immediately.

These rules are part of the workflow contract, not caller folklore. The method name and call sites
carry the thread assumption; the scheduler does not expose an `isMessageThread()` query to paper
over unclear ownership, so this remains a documented precondition rather than a runtime assertion.
Calling this method from the message thread would self-post the transition and then block the
message thread waiting for a presentation callback that cannot run, causing an avoidable stall and
missed paint until the timeout releases the gate. Unit tests should cover the timeout/post-failure
paths with fakes rather than depending on JUCE's real message loop.

## Audio-Device Ordering Fix

With the seam in place, the fix is mechanical.

Change the dispatcher contract so the settings controller hands the editor both halves explicitly:

```cpp
using AudioDeviceSettingsDispatcher =
    std::function<void(std::function<void()> work, std::function<void()> after_cleared)>;
```

`AudioDeviceSettingsController::onOkRequested()` / `onCancelRequested()` split their current single
callable:

- `work`: run the blocking `m_settings.apply()` / `m_settings.cancel()` and capture the outcome
  (via an `alive`-guarded shared outcome flag, since both halves run on the message thread);
- `after_cleared`: `updateView()`, then on success `finishAndClose()`, on failure
  `m_view->setApplying(false)` so the dialog reappears with its inline error — now strictly after
  the editor busy overlay has cleared.

`EditorController::Impl::onAudioDeviceChangeRequested(work, after_cleared)` keeps the
calibration-prompt and empty-callable guards, then delegates to
`m_busy.runMessageThreadBusyOperation(BusyOperation::OpeningAudioDevice, work, after_cleared)`.
The editor never learns settings-window presentation details; it only guarantees ordering.

`editor_view.cpp`'s dispatcher lambda forwards both callables to `onAudioDeviceChangeRequested`.

Resulting sequence on failure:

```text
settings controller hides dialog
editor shows busy
busy overlay paints
apply runs (blocking)
editor clears busy        <- overlay gone
settings dialog reopens with inline error
```

The previous `callAsync`-in-`setApplying(false)` fallback is no longer needed: ordering is now a
property of `runMessageThreadBusyOperation`, not an implicit reliance on stack unwinding.

## Implementation Stages

1. Add `IMessageThreadScheduler` + JUCE production impl + deterministic test fake; compose the
   production impl in `app/`. Preserve scheduling failure as a boolean result.
2. Create `BusyOperationWorkflow` owning `BusyOperationState` + scheduler + refresh callback +
   storage for the late-bound busy-presentation callbacks + a private `std::shared_ptr<bool>`
   liveness token; move `PaintGate` and the lifecycle methods off `EditorController::Impl`
   into it, carrying the `safeCallback`-equivalent weak guard into the workflow's own
   scheduler/presentation posts. Wire `EditorController` to hold and delegate to it.
3. Wire the busy-presentation callbacks from `EditorController::attachView()` to the current
   view's `runAfterBusyOverlayPainted(...)` and `runAfterBusyOverlayRemoved(...)`, and add an
   explicit `detachView()` clear path that retires both the existing `m_view` binding and the
   workflow callbacks through `detachPresentation()`. Invoke `detachView()` from `Editor` teardown
   *before* `m_view` is reset, since `Editor` destroys the view before the controller.
4. Add `runMessageThreadBusyOperation` and change the audio-device dispatcher contract; update
   `onAudioDeviceChangeRequested`, `AudioDeviceSettingsController::onOkRequested()` /
   `onCancelRequested()`, the dispatcher type, and the `editor_view.cpp` wiring.
5. Migrate the remaining handlers (open / import / plugin-load / live-rig) to the workflow
   primitives. Drop the now-redundant explicit `updateView()` calls that today trail `beginBusy()`,
   since `begin()` refreshes internally; leave only the refreshes the workflow does not perform.
   Replace the three direct JUCE scheduling sites (`makeBusyProjectOperationProgress`, the live-rig
   `callAfterDelay` hold, and the `getInstanceWithoutCreating()` test branch) with the scheduler.
   Drop the now-unused `juce_events` include from `editor_controller.cpp`.
6. Tests: add `BusyOperationWorkflow` unit tests with fake scheduler and presentation callbacks
   (deterministic, no `MessageManager` branch); update `test_editor_controller_busy.cpp` and
   `test_audio_device_settings_controller.cpp` for the new ordering and dispatcher contract;
   update the editor-controller test harness and `immediate_editor_task_runner` as needed.

## Testing

- `BusyOperationWorkflow` gets focused unit tests over its primitives and
  `runMessageThreadBusyOperation` ordering, using the scheduler fake and a fake busy-presentation
  callback. These are the "pure/headless" and "adapter" tiers the architecture favors, not UI
  tests.
- Settings-controller tests assert apply/cancel route work through the dispatcher and that success
  closes / failure re-shows after the busy clears, against a fake dispatcher.
- No new tests should require a JUCE message loop; the fakes remove the `MessageManager == nullptr`
  branch entirely.

## Documentation Impact (pending confirmation)

Per the `CLAUDE.md` documentation-maintenance rule, confirm before editing `docs/design/`. Once the
shape lands and proves out:

- `editor-structure-deferred-work.md` — remove "Framework-Isolation Part 2 — Message-Thread
  Scheduling"; it is implemented here.
- `editor-runtime-extraction-plan.md` — extend the desired-end-state diagram to show
  `BusyOperationWorkflow` and `IMessageThreadScheduler` under the facade.
- `docs/design/architecture.md` / `architectural-principles.md` — only if the team agrees the
  scheduler port is a durable editor boundary worth recording.

## Verification Notes

After implementing:

- Failed audio-device apply clears the editor busy overlay *before* the settings dialog reappears.
- Successful apply still closes the settings dialog; focus returns to it on reopen after failure.
- Cancel failure, if reproducible, follows the same visual ordering.
- Superseding or destroying the controller during `runMessageThreadBusyOperation` work does not
  clear a newer busy operation and does not run the stale post-busy continuation.
- Paint-gated worker progress never blocks the message thread, times out when the busy presentation
  cannot paint, and releases immediately when message-thread posting fails.
- Destroying the view before the controller clears the busy-presentation callbacks and the
  `m_view` binding through `detachView()`, leaving no stale view pointer in the controller or
  workflow.
- A scheduled workflow post or delay that fires after the editor is destroyed is dropped by the
  workflow's liveness token rather than touching freed state.
- `EditorController` no longer calls JUCE message-thread scheduling primitives directly for the
  busy sites; `editor_controller.cpp` drops its `juce_events` include.
- The workflow's tests are deterministic with no `MessageManager == nullptr` branches.
- Focused clang-tidy still passes for `editor_controller.cpp`, `editor_view.cpp`, and
  `audio_device_settings_window.cpp`.
