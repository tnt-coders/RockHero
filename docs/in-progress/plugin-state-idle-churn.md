# Plugin-state idle churn and phantom undo entries

Status: **diagnosed, not yet implemented.** Root cause is source-traced (juce-tracktion-expert pass,
2026-07-08); the fix design below is agreed in shape but implementation is deferred to a later
session. Do a short live-diagnostic pass (see "Validate before coding") before committing to the
gesture gate, since the decisive signals cannot be proven from source alone.

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
   (`external/tracktion_engine/.../plugins/external/tracktion_ExternalPlugin.cpp:30-64`), each
   calling `triggerAsyncUpdate()`.
2. **Tracktion forwards it with no equality gate.** `handleAsyncUpdate`
   (`tracktion_ExternalPlugin.cpp:143-155`) runs `updateFromPlugin`, which fans one notification out
   to **selectable + N parameters**:
   - `plugin.changed()` (`:128`) -> `Selectable` listeners -> our
     `PluginDirtyStateTracker::selectableObjectChanged`
     (`rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.cpp:234`).
   - `plugin.refreshParameterValues()` (`:126`) -> `valueChangedByPlugin()` on every automatable
     parameter (`:824-829`) -> our `PluginParameterDirtyTracker::parameterChanged`
     (`plugin_dirty_tracking.cpp:84`), once per parameter. This is the same-millisecond burst.
   - The 500 ms `PluginChangeTimer` (`model/edit/tracktion_Edit.cpp:527-543`) only sets the
     save-dirty flag via `markAsChanged()`; it does **not** re-serialize and is not part of the loop.
3. **The serialized bytes genuinely differ each cycle.** Our tracker already has a byte-equality
   gate — `settlePendingEdit` only emits inside `if (before != *after && m_emit_edit)`
   (`plugin_dirty_tracking.cpp:342-356`). It fires every cycle, so `before != after` is *true* every
   cycle. Our capture is the full `ExternalPlugin::state` ValueTree including the opaque base64 VST3
   chunk from `getStateInformation` (`tracktion_ExternalPlugin.cpp:1049-1056`; capture at
   `rock-hero-common/audio/src/engine/engine_plugin_host.cpp:1262-1265`). The plugin embeds volatile
   data (meter/tuner value, timestamp, or RNG seed) into that chunk and/or drifts a read-only meter
   parameter, so the bytes are never stable at idle.

**Consequence for the fix:** a byte-equality gate cannot help — the bytes are honestly different.

**Fundamental vs. accidental:**
- *Fundamental (framework constraint):* Tracktion forwards every plugin-initiated change to
  `Selectable` + `AutomatableParameter` listeners with no de-duplication. We cannot make Tracktion
  suppress a plugin that dirties itself at idle.
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

**Fix 1 — Coalesce `updateView()` onto a single async tick (editor-core). Low risk; do regardless.**
`EditorController::Impl::updateView()` (`editor_controller.cpp:2087-2094`) is fully synchronous
(`deriveViewState()` + `m_view->setState()`) and is called ~3x/cycle (pending=true, completed,
pending=false via `signal_chain_handlers.cpp:192-196` and `:230`). Replace direct calls with a
coalesced single-shot on the message thread (mark-dirty + one rebuild per tick). Use
`juce::Timer::callAfterDelay(1, ...)` **not** `callAsync` — on Windows `callAsync`/PostMessage
outranks `WM_PAINT` and starves paints (see memory `callAsync-starves-paints`). Batches the storm but
does **not** fix undo pollution; necessary but not sufficient.

**Fix 2 — Intent-gate edit emission (adapter, `rock-hero-common/audio`). The real fix.**
A byte-equality gate is useless (bytes differ). Gate on *evidence of user intent* instead. Treat a
settled transaction as a real edit only if, during it, either:
- **a parameter gesture occurred** — `PluginParameterDirtyTracker` already receives
  `parameterChangeGestureBegin/End` (`plugin_dirty_tracking.cpp:65-75`), which fire only for genuine
  user knob/slider moves; idle meter drift arrives as bare `parameterChanged` (`:84`) /
  `selectableObjectChanged` with no surrounding gesture; **or**
- **the plugin editor window was open** — the only way to change opaque chunk state with no param
  gesture (e.g. loading a preset inside the plugin's own browser).

Otherwise fold the new bytes into the baseline and emit nothing (reuse the existing absorb behavior
at `plugin_dirty_tracking.cpp:327-339`). This kills idle churn *and* undo pollution while preserving
knob-turn edits and in-plugin preset loads. Belongs in the adapter (it is Tracktion/JUCE signal
interpretation, not editor policy). Residual risk: a plugin that changes its chunk with the window
closed and no gesture (rare: scripted/MIDI-learn) is silently folded into baseline — acceptable, and
far better than the storm.

**Fix 3 — Break self-induced feedback (adapter). Only if "Validate" step confirms it.**
Our own capture calls `flushPluginStateToValueTree()`, which does
`suspendProcessing(true) ... getStateInformation ... suspendProcessing(false)`
(`tracktion_ExternalPlugin.cpp:1048-1051`). Some plugins re-announce `audioProcessorChanged` on
suspend/resume, which would re-arm our tracker and make the loop partly self-sustaining. If
confirmed, extend the existing 100 ms `PostRestoreAbsorbWindow` (`plugin_dirty_tracking.cpp:23`,
`90-119`) to briefly cover the moment right after each capture. Skip if not confirmed.

**Fix 4 — Keep the editor-side drop gate as belt-and-suspenders.** The `edit.before == edit.after`
drop at `signal_chain_handlers.cpp:210` is dead code for this bug; leave it (cheap) but do not rely
on it — real filtering happens in Fix 2.

**Ruled out — the debounce timer is fine.** `g_plugin_dirty_transaction_quiet_debounce`
(`plugin_dirty_tracking.cpp:16`; `startTimer` `:290`; `timerCallback` `:361`) is a correct one-shot
that re-arms only on a fresh dirty signal (`beginPendingEdit` `:244`). It re-arms "forever" only
because the *input* never stops. Fixing the timer would treat the symptom.

**Recommended order:** Fix 1 (immediate relief) + Fix 2 (correctness). Fix 3 only if confirmed.

## Validate before coding (live diagnostics — our side of the boundary)

These cannot be derived from source; add temporary logging, reproduce at idle + with a real knob
turn, then remove:
1. **Which callback drives it** — count/distinguish `selectableObjectChanged` vs `parameterChanged`
   arrivals, and for parameters, which parameter index. If always the same read-only meter/tuner
   parameter, Fix 2's gesture gate is guaranteed to work.
2. **Chunk vs. parameter drift** — log byte-length + cheap hash of `edit.before` vs `edit.after`, and
   whether the diff is inside `IDs::state` (base64 chunk) or elsewhere.
3. **Self-feedback (Fix 3 trigger)** — count `selectableObjectChanged` arrivals in the ~50 ms after
   each `flushPluginStateToValueTree` in `capturePluginState`. A spike => suspend/resume is
   re-announcing.
4. **Gesture accompaniment (validates Fix 2)** — log gesture begin/end counts within each
   transaction. Expect zero at idle, non-zero on a real knob turn.

## Proven from source vs. needs live confirmation

Proven: Tracktion forwards with no equality filter, one notification -> selectable + N params; our
tracker only emits on differing bytes so bytes genuinely differ; idle cycles push real undo entries
(correctness bug); `updateView()` is synchronous and ~3x/cycle; Tracktion does not poll state on a
timer. Needs live confirmation: the four "Validate" items above.

## Key files

Tracktion (vendored, read-only):
- `external/tracktion_engine/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp`
  — `ProcessorChangedManager` (`:30-64`), `handleAsyncUpdate` (`:143-155`), `updateFromPlugin`
  (`:80-129`), `refreshParameterValues` (`:824-829`), `flushPluginStateToValueTree` (`:1025-1059`).
- `external/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.cpp` —
  `Edit::pluginChanged` (`:2431-2438`), `PluginChangeTimer` (`:527-543`).

RockHero:
- `rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.cpp` — trackers, debounce, absorb
  window, emit gate.
- `rock-hero-common/audio/src/engine/engine_plugin_host.cpp` — `capturePluginState` (`:1231-1266`),
  `emitPluginStateEdit` (`:684-695`), tracker wiring (`:735-856`).
- `rock-hero-editor/core/src/signal_chain/signal_chain_handlers.cpp` — `onPluginEditPendingChanged`
  (`:192-196`), `onPluginStateEditCompleted` (`:199-231`).
- `rock-hero-editor/core/src/controller/editor_controller.cpp` — `updateView` (`:2087-2094`),
  observer wiring (`:1022-1039`).
