# Watch: plugin-state idle churn (never confirmed; remedies ready if it appears)

Status: **watch item, no action needed.** Closed out of `docs/in-progress/` on 2026-07-08 together
with the phantom-undo-edit fix (`Gated plugin-state undo capture on user intent`, `edb485bd`, then
simplified to a gesture-only gate).

## What was suspected, and what was actually true

An amp-sim VST3 (Archetype Cory Wong X) was suspected of continuously re-serializing a drifting
state chunk at idle (~1/sec), polluting the undo stack. After the undo history inspector gave
direct visibility into the stack, the continuous churn was **never observed**. The real defect was
narrower: the plugin's asynchronous **instantiation/restore re-announce** settled as a phantom
"Edit \<plugin\>" entry, which could truncate the redo stack. That is fixed structurally: a settled
plugin-state transaction is emitted only when it carried a parameter gesture
(`rock-hero-common/audio/src/tracktion/plugin_dirty_tracking.{h,cpp}`); everything else folds into
the baseline.

## What to watch for

- **True idle churn** (a plugin whose state chunk drifts at idle): would now appear as repeating
  `Folded plugin state change (no user intent)` log lines at idle. It can no longer pollute undo,
  but each folded settle still runs a full state capture (`flushPluginStateToValueTree` →
  `suspendProcessing` → `getStateInformation`) once per 750 ms debounce cycle — a performance
  concern, not a correctness one. Remedies designed in the retired plan (verified against
  Tracktion/JUCE source 2026-07-08, so re-verify only lightly):
  - Skip the capture entirely on a no-intent settle — intent is known before capturing, and keeping
    the stale baseline is harmless (a later real edit's `before` restores pre-drift volatile state).
  - Coalesce the plugin-edit-path `updateView()` calls through the `IMessageThreadScheduler` port
    (production impl must use `callAfterDelay(1, ...)` semantics, never `callAsync` — PostMessage
    starves `WM_PAINT` on Windows).
- **Missing undo entries for in-plugin preset loads**: the accepted cost of the gesture-only gate.
  A gesture-less user action inside the plugin folds silently (its state still persists with the
  tone). If a user reports "I loaded a preset in the plugin and can't undo it", the retired plan's
  corroborated window-open signal (window visibility plus a `juce::AudioProcessorListener`
  `ChangeDetails` heuristic — JUCE collapses `restartComponent` to
  `{programChanged, parameterInfoChanged}`) is the researched starting point.
- **Automation points that "move by themselves"** (tangential): Tracktion's `setParameterValue`
  non-automation branch moves a *single-point* automation curve to follow a plugin-initiated value
  change while the transport is idle (`tracktion_AutomatableParameter.cpp:1439-1441`). Unlikely
  (meter params are not the ones users automate) but adjacent to this subsystem.
