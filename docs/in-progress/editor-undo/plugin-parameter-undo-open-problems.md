# Plugin Parameter Undo — Open Problems & Investigation Log

Status: **unresolved.** This documents two distinct, currently-blocking problems with undo/redo of
plugin-parameter edits (knobs turned inside a plugin's own editor window), the evidence gathered,
and every fix attempt so far (all unsuccessful, all discarded). Written so the next session does not
repeat the dead ends.

## Current mechanism (the thing under investigation)

- **Capture:** a per-plugin observer (`PluginParameterMementoTracker` in
  `rock-hero-common/audio/src/engine.cpp`) listens to a plugin's Tracktion `AutomatableParameter`s.
  On a parameter change it captures the plugin's **full state chunk** (`PluginInstanceState`, via
  `flushPluginStateToValueTree()` + serialize) as `before`/`after`.
- **Undo payload:** `PluginParameterEdit` (editor-core `IEdit`) stores the full before/after chunks.
- **Restore:** undo/redo calls `IPluginHost::setPluginState` →
  `external_plugin->restorePluginStateFromValueTree(...)` → the VST3's `setStateInformation` (a full
  state reload).
- **Ctrl+Z routing:** the plugin's editor window is a Tracktion-owned `PluginWindow`
  (`engine.cpp`). `PluginWindow::keyPressed` detects Undo/Redo shortcuts and forwards them via
  `PluginWindowCommandObserver` → controller `onUndoRequested()`.

Test environment: live **NeuralDSP USB Audio Device** open with input monitoring on; the live rig is
`guitar input -> plugin -> output`, so the plugin is being processed on the audio thread in real
time.

---

## Problem 1 — App freezes completely on parameter undo (Archetype Nolly)

### Symptom
Open Nolly's editor window, turn a knob (e.g. "Rhythm Amp Treble"), press `Ctrl+Z` → the **entire
app hard-freezes** (must force-quit).

### What the log proves
From a freeze repro:

```
... Plugin parameter edit started    instance_id=1020 label_hint=Rhythm Amp Treble   (55.584)
... Plugin parameter edit completed  instance_id=1020 label_hint=Rhythm Amp Treble   (54.382)
... undo.push entry_pushed label=Edit Rhythm Amp Treble
... Action requested action=Undo                                                     (55.644)
... Action started   action=Undo
... undo.begin  status=pending                                                       (55.644)
... undo.commit  status=applied                                                      (55.724)
[LOG ENDS — no "Action dispatch completed action=Undo"]
```

- **The undo succeeds.** `setPluginState` runs entirely between `undo.begin` (55.644) and
  `undo.commit` (55.724) — **~80 ms, fast, no freeze in the restore itself.**
- **The freeze is *after* the commit.** There is no `Action dispatch completed action=Undo` line.
- In the code, the only thing between the `undo.commit` log and that missing line is
  `updateView()` → `deriveViewState()` → `m_view->setState()`:
  - `deriveViewState()` (`editor_controller.cpp:4064`) only reads cached snapshots and the undo
    history — it does **not** call into the live plugin. Ruled out.
  - `EditorView::setState()` (`rock-hero-editor/ui/src/editor_view.cpp:806`) does the editor UI
    render, including **`refreshAudioMeters()`** (reads the live audio engine/meter source) and a
    synchronous `repaint()`.

So the freeze is in **`EditorView::setState` — the editor UI render that runs right after the
restore.**

### Facts established / hypotheses ruled out
- The restore (`setStateInformation`) itself is fast (~80 ms) — **not** the bottleneck.
- **Window closed still freezes** → it is **not** the plugin-editor recreation
  (`recreatePluginWindowContentAsync` → `PluginWindow::recreateEditorAsync`, which only runs for an
  open window). Ruled out.
- The earlier **B3 / Step-2 spike** restored a Nolly value successfully **with the window open** —
  so full-state restore + open window is not inherently the trigger. The difference: the spike had
  no editor UI polling the live audio engine after the restore.
- The **Gateway** plugin (see Problem 2) never ran its undo, and **did not freeze** → the freeze
  requires the actual restore/render to execute.

### Leading hypothesis (UNCONFIRMED)
A **message-thread ↔ audio-thread deadlock**: the full `setStateInformation` destabilizes the
plugin's live processing on the audio thread; the subsequent editor render (`refreshAudioMeters` /
`repaint`, which read the live audio engine) blocks waiting on the stuck audio thread. Fits every
fact, including why a headless/spike context never hit it.

A debugger stack would confirm this in one shot, **but NeuralDSP plugins refuse to load with a
debugger attached**, so that path is closed.

### Likely fix
**Granular parameter undo.** Capture `{instance_id, parameter_id, before_value, after_value}` and
restore via `AutomatableParameter::setParameter`. Changing one value does **not** reload full state,
does not disturb the audio graph, and does not recreate the editor — so the audio thread never
stalls and the render never blocks. This is the design the discussion repeatedly converged on; the
only reason it was deferred was a (now-disproven) assumption that full-chunk restore would be fast
enough and that granular couldn't keep the processor/UI in sync (it can, *because* `setParameter`
goes through the host).

---

## Problem 2 — Plugin-dependent `Ctrl+Z` forwarding (some plugins eat the keystroke)

### Symptom
With the **Gateway** plugin: turn a knob, press `Ctrl+Z` → **nothing happens** (no reset, no
freeze).

### What the log proves
```
... Plugin parameter edit started   instance_id=1017 label_hint=Bass
... Plugin parameter edit completed  instance_id=1017 label_hint=Bass
... undo.push entry_pushed label=Edit Bass
[LOG ENDS — no "Action requested action=Undo"]
```
The edit **was** captured and pushed to history, but there is **no `Action requested action=Undo`** —
the keystroke **never reached Rock Hero**. Gateway's own GUI consumed `Ctrl+Z`; Nolly lets it bubble
up to `PluginWindow::keyPressed`.

### Implication
The `PluginWindowCommandObserver` / `keyPressed`-forwarding approach is **unreliable** — it depends
on whether each plugin's editor passes the keystroke through. A robust path is needed (e.g. an
application/global command target, or intercepting the shortcut at the host window level before the
plugin sees it). Secondary to Problem 1, but real.

---

## What was tried (all unsuccessful, all discarded)

1. **`callAsync` deferral of the plugin-window command.**
   Hypothesis: re-entrancy crash from running undo synchronously inside the plugin window's
   `keyPressed`. Result: freeze unchanged — the log shows the freeze is *after* the restore in
   `setState`, not re-entrancy. The deferral moved the undo out of `keyPressed` and it still froze.

2. **`refreshBaseline()` no-op in the observer's suppressed branches.**
   Hypothesis: a per-parameter full-state re-serialization storm during the restore (the observer
   captured a fresh full chunk on every parameter callback while suppression was on). Result: freeze
   unchanged. Also discovered the suppression flag is structurally incapable of catching the
   restore's notifications, because `currentValueChanged` is delivered **asynchronously**
   (`tracktion_AutomatableParameter.cpp:1454`, `parameterChangedCaller.triggerAsyncUpdate()`) —
   it fires *after* the synchronous suppression scope ends.

3. **Full gesture-only observer rewrite.**
   Removed the non-gesture/debounce/baseline/`juce::Timer`/suppression machinery so the tracker only
   reacts to `parameterChangeGestureBegin`/`End` and ignores raw value changes. Hypothesis: the
   observer reacting to the restore's async value-change flood. Result: **freeze unchanged** — this
   **definitively rules out the observer** as the cause. Also a regression: it drops undo for
   plugins that change values without begin/end gestures (Tracktion confirms these are optional and
   flow through `ExternalAutomatableParameter::valueChangedByPlugin()`, line ~331). **Discarded.**

Net result of the three attempts: they conclusively eliminated the observer and the re-entrancy as
causes, and pointed the evidence at the **restore → editor-render → live-audio** interaction.

---

## Diagnostics still open (debugger-free, Neural-DSP-compatible)

1. **Audio-disable test (fastest).** Disable the audio device / stop monitoring so the plugin is not
   processed live, then turn a Nolly knob and `Ctrl+Z`.
   - No freeze → confirms the message↔audio deadlock → granular is the fix.
   - Still freezes → not the audio thread; move to (3).
2. **Gateway-via-main-editor.** Trigger Gateway's undo from a path Gateway does not eat (focus the
   main window / Edit ▸ Undo). If Gateway *also* freezes → the freeze is general to full-state
   restore on any live plugin; if only Nolly freezes → it is tied to Nolly's heavy state reload.
3. **`setState` step logging.** Add `RH_LOG` lines between each operation in `EditorView::setState`
   (before/after `refreshAudioMeters`, the signal-chain panel, `repaint`, …). The last line before
   the freeze pinpoints the exact blocking step. Works with Neural DSP loaded.

---

## Recommended direction

1. **Implement granular parameter undo** (the clean design): value-based payload +
   `IPluginHost::setPluginParameterValue` + a gesture-bracketed observer that captures the changed
   parameter's value rather than the full chunk. It removes the full-state restore that is the prime
   suspect for Problem 1 and is the correct model regardless. (Confirm with the audio-disable test
   first if cheap; the disproven assumptions that originally rejected it are documented above.)
2. **Make `Ctrl+Z` forwarding robust** for Problem 2 — do not rely on the plugin passing the
   keystroke through.
