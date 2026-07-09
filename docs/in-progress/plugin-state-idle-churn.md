# Plugin-state idle churn and phantom undo entries

Status: **diagnosed and design-revised, not yet implemented.** Two source-verification passes
(juce-tracktion-expert, both 2026-07-08): the first traced the root cause; the second verified the
fix design's framework assumptions at the JUCE/Tracktion source level and forced the amendments
marked **[revised]** below. The live-diagnostic gate before coding still applies — the decisive
signals are plugin behavior and cannot be proven from source.

This surfaced as a "side observation" while fixing the automation-point gesture bug (commit
`1880237e` deferred mid-gesture state pushes). That fix makes the churn *harmless to gestures*, but
the churn itself — and the undo pollution it causes — is a separate, still-open problem.

## Symptom

With an amp-sim VST3 loaded in a tone's chain (reproduced with Archetype Cory Wong X), the editor
emits a storm of full view-state rebuilds in bursts, **even at idle** — repeating roughly once per
second with the user touching nothing. Each burst runs `listAutomatableParameters` and rebuilds the
entire editor view. Representative log slice after a single insert:

```
Plugin edit pending changed pending=true
Plugin state edit started  instance_id="1021" label_hint="Archetype Cory Wong X" absorbing=false
Plugin state edit completed instance_id="1021"
Undo transition event ... type="entry_pushed" label="Edit Archetype Cory Wong X"
Plugin edit pending changed pending=false
  ... (this cycle repeats ~1/sec indefinitely)
```

## Root cause (source-traced)

The churn is **plugin-initiated, not Tracktion-polled.** There is no timer in Tracktion that
periodically re-serializes plugin state.

1. **The VST3 fires host-change callbacks at idle.** Amp sims routinely push a meter / tuner /
   analyzer read-out through a parameter or `updateHostDisplay()` about once per second. Tracktion's
   `ProcessorChangedManager` catches both
   `audioProcessorParameterChanged` and `audioProcessorChanged`
   (`external/tracktion_engine/.../plugins/external/tracktion_ExternalPlugin.cpp:42-64`), each
   calling `triggerAsyncUpdate()`.
2. **Tracktion forwards it with no equality gate — but the two callbacks take different paths.**
   **[revised]** `handleAsyncUpdate` (`tracktion_ExternalPlugin.cpp:143-155`) distinguishes them:
   - `paramChanged` (from `audioProcessorParameterChanged`) only calls
     `edit.pluginChanged(plugin)` — the 500 ms save-dirty poke. Bare parameter drift reaches our
     tracker separately, through `ExternalAutomatableParameter`'s own listener on the JUCE
     parameter (async value path, `tracktion_ExternalAutomatableParameter.h:289-295, 320-332` ->
     `valueChangedByPlugin` -> our `PluginParameterDirtyTracker::parameterChanged`,
     `plugin_dirty_tracking.cpp:84`).
   - `processorChanged` (from `audioProcessorChanged`, i.e. the plugin called `restartComponent`
     or `setDirty`) runs the full `updateFromPlugin` fan-out: `plugin.changed()` (`:128`) ->
     `Selectable` listeners -> our `PluginDirtyStateTracker::selectableObjectChanged`
     (`plugin_dirty_tracking.cpp:234`), plus `plugin.refreshParameterValues()` (`:126`) ->
     `Listener::parameterChanged` on **every** automatable parameter (`:824-829`) — Tracktion's
     `setParameter` notifies unconditionally, even for unchanged values
     (`tracktion_AutomatableParameter.cpp:1464-1468`). This is the same-millisecond N-parameter
     burst, and it means a bare `parameterChanged` carries **zero intent information**.
   - Consequence: the observed `Selectable` churn in the log **proves this plugin calls
     `restartComponent` or `setDirty` ~1/sec** — pure output-parameter drift would never touch the
     Selectable path. The 500 ms `PluginChangeTimer` (`model/edit/tracktion_Edit.cpp:527-543`)
     only sets the save-dirty flag via `markAsChanged()`; it does **not** re-serialize and is not
     part of the loop.
3. **The serialized bytes genuinely differ each cycle.** Our tracker already has a byte-equality
   gate — `settlePendingEdit` only emits inside `if (before != *after && m_emit_edit)`
   (`plugin_dirty_tracking.cpp:342-356`). It fires every cycle, so `before != after` is *true* every
   cycle. Our capture is the full `ExternalPlugin::state` ValueTree including the opaque base64 VST3
   chunk from `getStateInformation` (`tracktion_ExternalPlugin.cpp:1025-1062`; capture at
   `rock-hero-common/audio/src/engine/engine_plugin_host.cpp:1262-1265`). The plugin embeds volatile
   data (meter/tuner value, timestamp, or RNG seed) into that chunk and/or drifts a read-only meter
   parameter, so the bytes are never stable at idle.

**Consequence for the fix:** a byte-equality gate cannot help — the bytes are honestly different.

**Fundamental vs. accidental:**
- *Fundamental (framework constraint):* Tracktion forwards every plugin-initiated change to
  `Selectable` + `AutomatableParameter` listeners with no de-duplication, and exposes no filtering
  hook (`ProcessorChangedManager` has only `AsyncUpdater` coalescing; `EngineBehaviour` has no
  plugin-change-filtering virtual). RockHero-side gating is the only place this can live.
- *Accidental (this plugin):* that *this* VST3 drifts its chunk at idle is plugin behavior, not a
  Tracktion guarantee. A well-behaved plugin is quiet. The fix must assume some plugins misbehave.

## This is a correctness bug, not just efficiency

Every idle cycle pushes a real `label="Edit Archetype Cory Wong X"` entry onto the **product undo
stack**. Path, all confirmed from source:
- `settlePendingEdit` emits because `before != after` (`plugin_dirty_tracking.cpp:342`).
- `Engine::Impl::emitPluginStateEdit` forwards unless save/load capture is deferred
  (`engine_plugin_host.cpp:684-695`); idle is not deferred.
- `EditorController::Impl::onPluginStateEditCompleted`
  (`rock-hero-editor/core/src/signal_chain/signal_chain_handlers.cpp:199-231`) has a redundant
  `if (edit.before == edit.after)` drop (`:210`) that **never trips** (bytes differ), then calls
  `pushUndoEntry(...)` (`:229`).

So after a tone sits untouched for a minute, Ctrl+Z is buried under dozens of phantom "Edit" entries,
and one Ctrl+Z "undoes" nothing the user did (it restores a stale meter-bearing chunk). Treat this on
par with a failing test.

## Fix design (ranked)

**Fix 1 — Coalesce the plugin-edit-path `updateView()` calls onto a single async tick
(editor-core). Low risk when scoped; do regardless.** **[revised]**
`EditorController::Impl::updateView()` (`editor_controller.cpp:2087-2094`) is fully synchronous
(`deriveViewState()` + `m_view->setState()`) and is called ~3x/cycle by the plugin-edit observation
path (pending=true, completed, pending=false via `signal_chain_handlers.cpp:192-196` and `:230`).
Two constraints the original design missed:
- **Scope.** `updateView()` has ~105 call sites across 7 editor-core files, and controller tests
  assert synchronous view semantics (`FakeEditorView` counts `setState` calls and checks
  `pushed_states` sequences, e.g. busy-progress steps). Coalesce **only the plugin-edit observation
  path**, not `updateView()` globally.
- **Layering.** Editor-core must not call `juce::Timer` directly; the existing
  `IMessageThreadScheduler` port is the seam (tests keep `ImmediateMessageThreadScheduler`, so
  their synchronous assertions are untouched). The production implementation must use
  `juce::Timer::callAfterDelay(1, ...)` semantics, **not** `callAsync` — on Windows
  `callAsync`/PostMessage outranks `WM_PAINT` and starves paints (see memory
  `callAsync-starves-paints`). If the port needs a delayed-post variant, add it there.

Batches the storm but does **not** fix undo pollution; necessary but not sufficient.

**Fix 2 — Intent-gate edit emission (adapter, `rock-hero-common/audio`). The real fix.**
A byte-equality gate is useless (bytes differ). Gate on *evidence of user intent* instead. Treat a
settled transaction as a real edit only if, during it, either:
- **(a) a parameter gesture occurred** — `PluginParameterDirtyTracker` already receives
  `parameterChangeGestureBegin/End` (`plugin_dirty_tracking.cpp:65-75`); idle meter drift arrives
  as bare `parameterChanged` / `selectableObjectChanged` with no surrounding gesture. **[revised]
  This is the strong signal, not a guaranteed one.** Source-verified caveats: gestures are not
  mandated by the plugin APIs (Tracktion's own comment,
  `tracktion_ExternalAutomatableParameter.h:322-327` — a plugin may call `performEdit` without
  `beginEdit/endEdit`, and JUCE never synthesizes gestures), and Tracktion silently drops gestures
  delivered off the message thread (`tracktion_ExternalAutomatableParameter.h:299-300`). Both
  failure modes degrade a real GUI knob move to bare `parameterChanged` — indistinguishable from
  drift. A GUI knob move implies the window is open, so criterion (b) covers those plugins;
  (a) and (b) are load-bearing **together**.
- **(b) the plugin editor window was open — corroborated, not bare.** **[revised]** The bare
  boolean re-opens the storm: an amp sim's GUI left open at idle keeps drifting ~1/sec, and there
  is provably no framework signal that distinguishes "window open + idle drift" from "window open +
  preset load" (verified by exhaustion of the host-side listener surfaces). Amp sims are exactly
  the plugins users leave open. So criterion (b) must be "window open **and** a corroborating
  signal". The corroborator: Tracktion discards the `juce::ChangeDetails` flags
  (`ProcessorChangedManager::audioProcessorChanged` never reads its argument,
  `tracktion_ExternalPlugin.cpp:51-64`), but RockHero can attach its own
  `juce::AudioProcessorListener` via the public `ExternalPlugin::getAudioPluginInstance()` and see
  them. JUCE's VST3 host collapses every plugin `restartComponent` — the spec-correct preset-load
  route — to `{programChanged, parameterInfoChanged}`
  (`juce_VST3PluginFormatImpl.h:3527-3564`), and `setDirty` to `{nonParameterStateChanged}`
  (`:3519-3525`); a spec-correct preset load additionally produces a same-tick
  `parameterValueChanged` burst across many distinct parameter indices (`resetParameters`,
  `:3556-3557`), while drift repeats the same one or two indices. Whether this plugin's idle
  signature differs from its preset-load signature is a plugin fact — live-diagnostic item 5
  decides the final gate. Do **not** build the ChangeDetails listener until the diagnostics show
  it is needed. Window visibility itself is trivially readable:
  `plugin->windowState->isWindowShowing()` (`tracktion_PluginWindowState.cpp:93-96`; we already
  use it at `engine_plugin_host.cpp:1559`). Sample it on dirty signals during the transaction, not
  only at settle (the window may close before the debounce fires).

Otherwise the settle is a **no-intent fold**: keep the existing baseline, emit nothing. Two
implementation traps here, both **[revised]**:
- **Do not reuse the post-restore absorb path verbatim.** That path re-arms the 100 ms absorb
  window (`plugin_dirty_tracking.cpp:332`). Re-arming after every ~1/sec idle settle creates a
  perpetually recurring window in which a quick real knob flick is silently swallowed (a
  transaction that begins inside the window and settles without further signals is absorbed). The
  no-intent fold must **never** call `arm()`.
- **Skip the capture entirely on a no-intent settle.** Today every settle runs
  `flushPluginStateToValueTree()` -> `suspendProcessing(true)` -> `getStateInformation` -> resume
  (`tracktion_ExternalPlugin.cpp:1048-1051`) — once per second at idle. The intent gate already
  decided the transaction is not a real edit, so there is nothing to compare: keep the old
  baseline (the skipped drift is volatile data; a later real edit's `before` simply restores
  pre-drift volatile state, which is harmless). This removes the 1/sec processing suspension and
  makes Fix 3's feedback loop impossible by construction: a capture-induced re-announce opens a
  new transaction with no intent, which is folded without capturing.

Plumbing note: `PluginParameterDirtyTracker` currently erases the gesture/plain distinction (both
funnel into `markDirty()`, `plugin_dirty_tracking.cpp:65-88`). Fix 2 needs a second channel
(e.g. `markDirtyFromGesture()`) so the state tracker can flag intent per transaction; the flag
resets at settle. Contained in the adapter.

Residual risk: a plugin that changes its chunk with the window closed and no gesture (rare:
scripted/MIDI-learn) is silently folded — acceptable, and far better than the storm.

**Fix 3 — Break self-induced feedback: framework-side REFUTED; keep only if live diagnostics
contradict.** **[revised]** Source-verified: `juce::AudioProcessor::suspendProcessing` only takes
the callback lock and sets a bool — no listener callbacks (`juce_AudioProcessor.cpp:583-587`) —
and the VST3 host's `getStateInformation` fires nothing host-side
(`juce_VST3PluginFormatImpl.h:2815-2833`). Any suspend/resume feedback is therefore 100%
plugin-initiated. With Fix 2's skip-capture fold, idle settles no longer capture at all, so even a
plugin-initiated re-announce loop starves. Diagnostic item 3 stays as a cross-check; expect it to
be moot.

**Fix 4 — Keep the editor-side drop gate as belt-and-suspenders.** The `edit.before == edit.after`
drop at `signal_chain_handlers.cpp:210` is dead code for this bug; leave it (cheap) but do not rely
on it — real filtering happens in Fix 2.

**Residual UI issue — the Undo affordance still flickers after Fixes 1+2.** **[revised]**
`undo_available` is `m_undo_history.canUndo() || m_plugin_host.hasPendingPluginEdits()`
(`editor_controller.cpp:1821`), and idle churn still opens/settles pending transactions (intent is
only knowable at settle). With an empty history the Undo affordance flickers ~1/sec. Cleanest
option: have the tracker expose "pending **with intent evidence so far**" (a gesture has already
arrived mid-transaction) and use that for `undo_available`; the flush-before-action gate at
`signal_chain_handlers.cpp:182` can keep raw pending. Decide during implementation; the flicker is
cosmetic, so shipping Fixes 1+2 first is acceptable.

**Ruled out — the debounce timer is fine.** `g_plugin_dirty_transaction_quiet_debounce`
(`plugin_dirty_tracking.cpp:16`; `startTimer` `:290`; `timerCallback` `:361`) is a correct one-shot
that re-arms only on a fresh dirty signal (`beginPendingEdit` `:244`). It re-arms "forever" only
because the *input* never stops. Fixing the timer would treat the symptom.

**Ruled out — automation playback is not a churn source.** **[revised]** Source-verified:
automation evaluation dispatches the async `Listener::currentValueChanged` (a no-op in our tracker,
`plugin_dirty_tracking.cpp:80-82`) and never fires `Listener::parameterChanged` or gestures
(`tracktion_AutomatableParameter.cpp:1384-1457`; `Listener::parameterChanged` fires only from
`setParameter(..., sendNotification)`, `:1459-1477`). No machinery needed.

**Recommended order:** Fix 1 (scoped, via the scheduler port) + Fix 2 (correctness, with the
amendments above). Fix 3 expected moot. Undo-affordance flicker as a follow-up decision.

## Watch item (tangential, not part of this fix)

Tracktion's `setParameterValue` non-automation branch **moves a single-point automation curve's
point** to follow a plugin-initiated value change when the transport is idle
(`tracktion_AutomatableParameter.cpp:1439-1441`). A drifting parameter that has a one-point tone
automation curve could silently edit the user's automation. Unlikely (meter params are not the
ones users automate) but real, and adjacent to this subsystem — keep in mind if automation points
ever "move by themselves".

## Validate before coding (live diagnostics — our side of the boundary)

These cannot be derived from source; add temporary logging, reproduce at idle + with a real knob
turn + with an in-plugin preset load, then remove:
1. **Which callback drives it** — count/distinguish `selectableObjectChanged` vs `parameterChanged`
   arrivals, and for parameters, which parameter index. Sharpened by the fan-out correction above:
   `selectableObjectChanged` at idle proves the plugin calls `restartComponent`/`setDirty` ~1/sec.
   If parameter arrivals are always the same read-only meter/tuner index, the burst-breadth
   heuristic in Fix 2(b) is viable.
2. **Chunk vs. parameter drift** — log byte-length + cheap hash of `edit.before` vs `edit.after`, and
   whether the diff is inside `IDs::state` (base64 chunk) or elsewhere.
3. **Self-feedback (Fix 3 cross-check)** — count `selectableObjectChanged` arrivals in the ~50 ms
   after each `flushPluginStateToValueTree` in `capturePluginState`. Expected moot under the
   skip-capture fold; a spike would mean the plugin re-announces on suspend/resume.
4. **Gesture accompaniment (validates Fix 2a)** — log gesture begin/end counts within each
   transaction. Expect zero at idle, non-zero on a real knob turn. If a knob turn shows zero, this
   plugin is gesture-less and criterion (b) is doing all the work.
5. **ChangeDetails signatures (decides Fix 2b's corroborator)** **[new]** — attach a temporary
   `juce::AudioProcessorListener` to `getAudioPluginInstance()` and log the `ChangeDetails` flags
   plus calling thread for: idle churn, a knob turn, and an in-plugin preset load. If idle churn is
   `{nonParameterStateChanged}`-only (setDirty) and preset loads are
   `{programChanged, parameterInfoChanged}` (restartComponent), the flags cleanly close the
   window-open hole for this plugin; keep the mapping a heuristic layered on the window-open gate,
   not a replacement (the flag semantics are plugin-defined). Also note which thread `setDirty`
   arrives on — that path is not marshalled to the message thread.

## Proven from source vs. needs live confirmation

Proven: Tracktion forwards with no equality filter and no filtering hook; the
`paramChanged`/`processorChanged` split and both fan-out paths; our tracker only emits on differing
bytes so bytes genuinely differ; idle cycles push real undo entries (correctness bug);
`updateView()` is synchronous and ~3x/cycle; Tracktion does not poll state on a timer; the gesture
chain works when the plugin cooperates but gestures are neither mandated nor thread-safe against
Tracktion's message-thread guard; `restartComponent`/`setDirty` flag collapse in the JUCE VST3
host; no framework-side suspend/resume feedback; automation playback never reaches the tracker.
Needs live confirmation: the five "Validate" items above — which idle signature this plugin emits,
whether its knob moves carry gestures, and what its preset loads look like.

## Key files

Tracktion (vendored, read-only):
- `external/tracktion_engine/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp`
  — `ProcessorChangedManager` (`:25-158`), `handleAsyncUpdate` (`:143-155`), `updateFromPlugin`
  (`:80-130`), `refreshParameterValues` (`:824-829`), `flushPluginStateToValueTree` (`:1025-1062`).
- `external/tracktion_engine/modules/tracktion_engine/plugins/external/tracktion_ExternalAutomatableParameter.h`
  — gesture forwarding + message-thread drop (`:297-318`), async value path (`:289-295, 320-332`),
  "gestures not mandated" comment (`:322-327`).
- `external/tracktion_engine/modules/tracktion_engine/model/automation/tracktion_AutomatableParameter.cpp`
  — listener dispatch (`:1384-1477`), single-point-curve move (`:1439-1441`).
- `external/tracktion_engine/modules/tracktion_engine/plugins/tracktion_PluginWindowState.cpp` —
  `isWindowShowing` (`:93-96`).
- `external/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.cpp` —
  `Edit::pluginChanged` (`:2431-2438`), `PluginChangeTimer` (`:527-543`).

JUCE (vendored; note the VST3 hosting implementation lives in the headless module in this fork):
- `external/tracktion_engine/modules/juce/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h`
  — `beginEdit/performEdit/endEdit` (`:3461-3506`), `setDirty` (`:3519-3525`),
  `restartComponent` handling + flag collapse (`:3508-3517, 3527-3564`), `resetParameters`
  (`:2879-2887`), `getStateInformation` (`:2815-2833`).
- `external/tracktion_engine/modules/juce/modules/juce_audio_processors_headless/processors/juce_AudioProcessor.cpp`
  — `suspendProcessing` (`:583-587`), listener delivery (`:424-436`).

RockHero:
- `rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.cpp` — trackers, debounce, absorb
  window, emit gate; Fix 2's home.
- `rock-hero-common/audio/src/engine/engine_plugin_host.cpp` — `capturePluginState` (`:1231-1266`),
  `emitPluginStateEdit` (`:684-695`), tracker wiring (`:735-810`), window-visibility precedent
  (`:1559`).
- `rock-hero-editor/core/src/signal_chain/signal_chain_handlers.cpp` — `onPluginEditPendingChanged`
  (`:192-196`), `onPluginStateEditCompleted` (`:199-231`), flush-before-action gate (`:180-189`).
- `rock-hero-editor/core/src/controller/editor_controller.cpp` — `updateView` (`:2087-2094`),
  `undo_available` derivation (`:1821`), observer wiring (`:1022-1039`).
- `rock-hero-editor/core/include/rock_hero/editor/core/tasks/i_message_thread_scheduler.h` — the
  port Fix 1's coalescing must go through (tests use `ImmediateMessageThreadScheduler`).
