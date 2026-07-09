# Plugin-state idle churn — follow-up (instantiation/restore re-announce as phantom undo edits)

Status: **implemented and live-verified working; not yet committed.** One product decision is open
(the intent signal — see "Open decision"). This is the concrete follow-up to
[plugin-state-idle-churn.md](plugin-state-idle-churn.md); it supersedes that plan's premise of
continuous idle drift.

## The exact problem

A hosted amp-sim VST3 (reproduced with Archetype Cory Wong X) caused **phantom "Edit \<plugin\>"
undo entries the user never made**, and in one case those phantom entries **destroyed the redo
stack**:

1. Insert a plugin, make a few real edits (building an undo stack), then undo all of them. The redo
   stack is intact and visible in the history inspector.
2. The moment the plugin insert is redone (Ctrl+Y recreates the plugin), a spurious `Edit` is
   pushed. Because a push discards the redo branch ahead of the cursor, **every redo entry after the
   insert is wiped**.
3. Symmetrically, undoing *back down* to the just-inserted state (the last undo before the one that
   removes the plugin) pushed a spurious `Edit` that wiped the rest of the redo list.
4. A related symptom: with the plugin window open you could not undo the insert — each Ctrl+Z
   flushed/undid the spurious pending edit instead of reaching the insert.

The user, watching the history inspector directly, corrected the original plan's premise: the
plugin does **not** churn continuously at idle. The spurious edit is **instantiation/restore-time**
only — it fires once when the plugin is (re)created or its state is restored.

## Root cause (source-verified via juce-tracktion-expert)

- RockHero hosts VST3 through JUCE's **headless** format, so plugin instantiation and
  `setStateInformation` restore run **synchronously** on the message thread and complete *inside*
  `createNewPlugin`, before the dirty tracker even attaches. So the phantom edit is **not** the
  synchronous load.
- It is a **later, asynchronous self-report** from the plugin: Tracktion's `Selectable::changed()`
  is delivered through a coalesced `AsyncUpdater`, and the plugin's own `restartComponent` /
  parameter re-flush (`valueChangedByPlugin` -> `parameterChanged`, fired even for unchanged values)
  lands after the tracker is listening. The plugin's `getStateInformation` chunk drifts slightly
  from the freshly-captured baseline, so `before != after` and the old logic emitted an edit.
- **There is no host-visible "the plugin is done re-announcing" signal** — `restartComponent` is
  unbounded by the VST3 spec and JUCE never synthesizes a completion event. Therefore **any timing
  window is fundamentally a race** (the pre-existing 100 ms `PostRestoreAbsorbWindow` lost that race
  on a heavy plugin). Correctness cannot be defined by timing; it must be defined by **user
  intent**.

## The fix implemented

Define a plugin-state edit by **user intent**, the only discriminator the framework guarantees
separates a real edit from the plugin's unbounded async self-chatter. A settled transaction is
emitted **only when `before != after` AND the transaction carried user intent**; otherwise the new
state is folded into the baseline and nothing is recorded.

Two intent signals (currently OR'd — see "Open decision"):

- **(i) A parameter gesture** (`parameterChangeGestureBegin`/`End`). Only the plugin's own editor
  GUI raises these, in response to the user grabbing a control. Confirmed to fire reliably for
  Archetype on every knob turn.
- **(ii) The plugin editor window being open** when a change arrives — corroboration for edits that
  produce no gesture (e.g. an in-plugin preset load). Sampled on every dirty signal (the window may
  close before the debounce fires).

### The `expect_self_report` refinement (the key correctness piece)

Window-open alone is not enough, because the editor window is often left open *while the user
undoes* — so a restore re-announce would arrive with the window open and be misread as intent. The
fix: a transaction-scoped `m_expect_self_report` flag.

- Set true when the tracker is constructed (fresh insert) and on every `resetBaseline` (undo/redo
  restore). While set, **window-open does not imply intent** — the open window is incidental to the
  plugin settling after *our* operation.
- A **gesture always overrides it** (real interaction resumed) and clears it.
- Cleared when the first transaction after construction/restore settles, so ordinary later edits
  take intent from the open window again.

This is deterministic — no timing — and exactly distinguishes "the plugin reacting to our
operation" from "the user editing." Rapid undos each `resetBaseline` (re-arming the flag) and drop
the previous in-flight transaction, which is why only the *last* undo's re-announce used to survive
to settle and emit; it is now folded.

### What was removed

- The entire `PostRestoreAbsorbWindow` class and its 100 ms timing window.
- The `absorb_initial_reannounce` plumbing: the parameter on `refreshPluginEditObservers` and
  `endPluginUndoCaptureDeferral`, and the `ScopedPluginUndoCaptureDeferral` constructor flag /
  `m_absorb_reannounce` member (only one caller passed `true`).

The 750 ms quiet debounce stays, but purely as an event coalescer — it is no longer a correctness
boundary.

### Files changed

- `rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.h` / `.cpp` — the tracker rewrite:
  `PluginParameterDirtyTracker` gains a `MarkUserIntent` channel (gestures) distinct from
  `MarkDirty` (plain changes); `PluginDirtyStateTracker` replaces the absorb window with
  `m_user_intent` + `m_expect_self_report` and an injected `IsEditorWindowOpen` callback; emits only
  on `before != after && intent`.
- `rock-hero-common/audio/src/engine/engine_plugin_host.cpp` — injects the window-open callback
  (`plugin.windowState->isWindowShowing()`), wires the gesture channel to `markUserIntent`, and
  drops the absorb parameter.
- `rock-hero-common/audio/src/engine/engine_impl.h` — retires `absorb_reannounce` from the deferral
  API and RAII guard.
- `rock-hero-common/audio/src/engine/engine_live_rig.cpp` — the one `ScopedPluginUndoCaptureDeferral`
  call site.

Editor-core is unchanged: `emitPluginStateEdit` and the undo push are correct — they simply stop
being fed non-intent transactions.

## Live verification (2026-07-08, from the editor log)

- Insert with window closed: `expect_self_report=true` -> `Folded plugin self-report (no user
  intent)` -> no `Edit` entry.
- Undo chain with the window open: restore re-announces log `window_open=true
  expect_self_report=true` -> `Folded`; the user undid cleanly back through `Insert Archetype Cory
  Wong X (slot 1)`. Redo stack preserved.
- Knob turn: `Plugin user intent (gesture)` fires, then `Plugin state edit completed` -> a real
  `Edit Archetype Cory Wong X (slot 1)` entry. Real edits still recorded.

## Open decision (blocking commit)

Because gestures fire reliably for this plugin, a strictly simpler and more robust option exists:

- **Gesture-only** (recommended): drop signal (ii) entirely and, with it, the `IsEditorWindowOpen`
  callback, the engine window-wiring, **and** the `m_expect_self_report` flag (with no window-intent
  the re-announces are folded automatically for lack of a gesture). Provably free of every
  window/timing/self-report edge case and much smaller. Cost: an in-plugin preset load fires no
  gesture, so it would not get its own undo entry (the state is still persisted with the tone).
- **Keep gesture OR window-open** (the implemented build): also captures in-plugin preset loads as
  undo entries, but retains a latent per-plugin assumption — a plugin that self-mutates while its
  window is open could still create a phantom edit (Archetype does not, so it works today).

Awaiting the user's choice before committing. The diagnostic log lines
(`started ... window_open ... expect_self_report`, `Folded plugin self-report`, `Plugin user intent
(gesture)`, `Plugin state edit completed`) were added to make the gate observable and should be
kept or trimmed as part of the commit.

## Key source references

- Tracker: `rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.{h,cpp}`.
- Engine wiring: `rock-hero-common/audio/src/engine/engine_plugin_host.cpp`
  (`refreshPluginEditObservers`, `refreshRestoredPluginEditObserver`, `emitPluginStateEdit`).
- Framework (read-only): headless synchronous instantiation
  (`external/tracktion_engine/modules/juce/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatHeadless.cpp:119-122`);
  async coalesced `Selectable::changed()`
  (`external/tracktion_engine/modules/tracktion_engine/selection/tracktion_SelectionManager.cpp:108-167`);
  unconditional `parameterChanged`
  (`external/tracktion_engine/modules/tracktion_engine/model/automation/tracktion_AutomatableParameter.cpp:1459-1468`);
  window visibility (`plugin.windowState->isWindowShowing()`).
