# Plugin Re-Announce Undo Pollution

Status: **complete** — resolved by removing post-restore suppression and accepting coherent
REAPER-like extra undo entries when plugins re-announce after restore.
Last updated: 2026-06-16.

## The exact issue we are trying to resolve

When the user undoes (or redoes) a plugin state change on a live Tracktion `ExternalPlugin`
(specifically Neural DSP **Archetype Nolly**), RockHero restores the prior opaque plugin state via
full-chunk `setStateInformation`. After that restore, the plugin **asynchronously re-announces all
of its parameters** over roughly 850 ms – 2.5 s, in multiple bursts with gaps between them.

Our plugin-edit observer cannot tell that re-announce apart from a genuine user edit. The resolved
policy is to record later plugin dirtiness as normal full-state transactions rather than suppressing
it and risking missed user edits. This can produce an extra coherent undo entry after restore, which
matches the observed REAPER-like tradeoff. Earlier symptoms were:

- The preset/dirty label shows `*` (dirty) after a clean undo.
- The undo stack gains a bogus entry that, if undone, does nothing meaningful (or corrupts the
  user's mental model of the stack).

The rejected mitigation was a **restore-settle quiet debounce** after each host-driven restore. It
absorbed late re-announce bursts, but it also created a period where genuine user edits could be
folded into the refreshed baseline. That is worse than an extra undo entry.

**Resolved policy:** only defer undo capture while RockHero is synchronously applying a host-owned
state restore/load. After that call returns, all later plugin dirtiness is treated as a normal
full-state transaction. This can create an extra coherent undo entry for a plugin's post-restore
settling, but it should not drop immediate user edits.

Reference point: **REAPER hosts this same plugin with no apparent post-undo settle window**. A
follow-up test showed that immediately turning a knob after undoing a preset load required two
subsequent Ctrl+Z presses, implying REAPER may tolerate an extra post-restore plugin-state entry
rather than suppressing the tail completely.

## System context

- Tracktion Engine / JUCE audio is isolated to `rock-hero-common/audio`. Everything below lives in
  `rock-hero-common/audio/src/engine.cpp` unless noted.
- Plugin undo restore is **full-chunk** (`get/setStateInformation`) by decision — granular
  before/after value replay is rejected because it cannot reproduce opaque non-parameter state,
  dependent parameters, or controller sync. "Memento" = full-chunk memento. This is non-negotiable
  (`project_undo_fidelity_nonnegotiable`).
- The editor observes plugin edits through two trackers, both gated on
  `shouldDeferPluginUndoCapture()`:
  - `PluginParameterDirtyTracker` — listens to `AutomatableParameter::Listener` dirty signals and
    pokes the state tracker.
  - `PluginDirtyStateTracker` — `SelectableListener` + `juce::Timer` quiet debounce; captures
    before/after `PluginInstanceState` and emits a `PluginStateEdit` when `before != after`.
- `PluginInstanceState` is captured by `flushPluginStateToValueTree()` then serializing
  `ExternalPlugin::state` (the Tracktion `ValueTree`) to XML. The before/after comparison is a diff
  of that whole-tree XML (`engine.cpp` ~line 1442; capture lambda ~line 3131).
- Restore entry point: `Engine::setPluginState` (`engine.cpp` ~line 4725) →
  `ExternalPlugin::restorePluginStateFromValueTree` → `setStateInformation`.

## What we know about the problem (proven, not guessed)

All findings below are from source-grounded reading of vendored Tracktion plus on-device logging
(a debugger cannot be attached — Neural DSP plugins refuse to load with one attached, and builds
are user-run).

### The Tracktion plugin-state tree has two independent payloads

`ExternalPlugin::flushPluginStateToValueTree`
(`external/tracktion_engine/.../tracktion_ExternalPlugin.cpp`):

- `IDs::state` (line ~1031) — the **opaque chunk** from `getStateInformation`, base64-encoded. This
  is exactly what we restore (line ~1090 reads it back into `setStateInformation`).
- `IDs::parameters` (written by `AutomatableEditItem::saveChangedParametersToState`,
  `tracktion_AutomatableEditItem.cpp` line ~322) — a binary blob of per-parameter explicit values,
  written only for params where `getCurrentValue() != getCurrentExplicitValue()`.

### Tracktion's parameter-change bridge cannot distinguish cause

`ProcessorChangedManager::audioProcessorParameterChanged`
(`tracktion_ExternalPlugin.cpp` line ~42) just sets a flag and triggers an async update. It does
**not** distinguish user-originated from programmatic changes, and does not even override the
gesture callbacks. So the live-parameter layer carries no causality signal.

### Nolly fires parameter gestures even on its programmatic re-announce

The post-restore re-announce logs `Plugin edit started from parameter gesture` /
`Plugin parameter gesture completed` for ~50 parameters in tight ~90 ms bursts — i.e. Nolly
brackets its programmatic re-announce in `beginEdit`/`endEdit`-style gestures, which per the VST3
spec are supposed to mean "user edit." So **gestures do not separate user edits from the
re-announce** for this plugin. (A real in-plugin preset load also changes ~all parameters at once,
so "burst of many params" cannot be used to mean "ignore" either — preset loads are exactly what we
must record.)

### The decisive measurement: every passive state layer moves during the re-announce

We instrumented `setPluginState` to capture `IDs::state` and `IDs::parameters` right after the
restore and again 2.5 s later (after the re-announce settled). Result for an undo of a Nolly preset
load:

```
[chunk] state_identical=false restored_len=9653 settled_len=9657 |
        params_identical=true restored_len=0 settled_len=0 | instance_id=1019
```

Interpretation:

- **`IDs::parameters` is empty** (`len=0` both times) — Nolly writes nothing there. It is **not**
  the pollution source. (This disproved our working hypothesis that the param-mirror blob churns.)
- **The opaque chunk is NOT stable**: `IDs::state` grew **9653 → 9657 bytes** over 2.5 s with zero
  user interaction. Nolly's `getStateInformation` is **not idempotent across a load+settle** — some
  internal runtime/DSP state inside the opaque blob changes after the load settles.

### Conclusion: passive observation cannot work for this plugin

No Tracktion layer separates "our restore re-announcing" from "the user editing":

- Full-tree XML diff → trips (chunk moved).
- Chunk-only (`IDs::state`) diff → **also trips** (9653 → 9657).
- `IDs::parameters` diff → useless (always empty for Nolly).
- Gesture-presence → useless (Nolly fires gestures on the re-announce).
- Gesture value-delta → useless (re-announce gestures are no-delta; and a user in-plugin preset
  load is also no-delta — value is applied before the gesture).

The only information no passive layer has is **causality**: *we* know exactly when *we* caused a
change, because we are the one calling `setStateInformation`. The re-announce is always a
consequence of our restore and never happens spontaneously. The genuinely hard part is only the
**async tail** that continues after our synchronous restore call returns.

## Things we have tried (and why each failed)

1. **VST3 host-program restore (Option D).** Probe showed Nolly reports `getNumPrograms() == 0`;
   presets are band-3 opaque state. Only full-chunk restore reaches them. Dead for our plugins.

2. **Granular before/after parameter-value replay.** Rejected on fidelity grounds — cannot
   reproduce opaque non-parameter state, dependent parameters, or controller sync
   (`project_undo_fidelity_nonnegotiable`).

3. **Fixed-duration suppression window**. Suppresses observation for a hardcoded interval after the
   restore. Defects: fragile if re-announce outlasts it, and drops user edits made during the
   window.

4. **Gesture value-delta detection** (reverted). Recorded an edit only when a gesture's begin/end
   parameter values differed. **Dropped preset loads** — both the re-announce and a real in-plugin
   preset load fire no-delta gestures (the plugin applies the value before announcing the gesture),
   so begin == end in both cases. Proven by log: an undone preset load that should have been
   recorded was instead lost, and Ctrl+Z removed the whole plugin instead of undoing the preset.

5. **Focus-session capture (#1 architecture).** Treat all parameter changes within one
   plugin-window focus session as a single undo entry. **Rejected by user**: (a) a keyboard-shortcut
   edit is not a focus event, and (b) per-focus-session granularity (a whole session collapsing to
   one undo) is unacceptable.

6. **Compare re-announced parameter values to the restored state S** (drift measurement). The
   intent was: re-announce sets params *to* S, user edits set them to something *else*. The
   measurement was confounded (the post-restore baseline was read before Nolly's async param update,
   so it captured pre-update values, not S) and then superseded by the chunk measurement above,
   which shows the chunk itself is unstable — so "compare to S" cannot be byte-clean regardless.

7. **Diff the opaque chunk only, REAPER-style** (this session's hypothesis). Killed by the `[chunk]`
   measurement: the chunk is not stable through a load+settle (9653 → 9657), so a chunk-only diff
   records the re-announce as a spurious edit too.

### Related, already-resolved finding (separate bug)

The earlier **preset-undo freeze** was a different problem and is fixed: `audioMeterSnapshot()`'s
per-frame master-meter `addClient` deadlocked against the audio-graph rebuild that
`setStateInformation` triggers (`forceFullReinitialise` → `stop` + `restartPlayback`). Fixed by
giving the master meter its own stable structural `LevelMeterPlugin`. See
`project_preset_undo_freeze_resolved`. The full-chunk restore mechanism itself is correct and fast
(~90 ms).

## Current implementation: no post-restore suppression tail

The current code implements the REAPER-like tradeoff:

- `Engine::setPluginState` defers undo capture only around the synchronous
  `restorePluginStateFromValueTree` / `setStateInformation` call.
- After the call returns, plugin observers are rebuilt with the immediate restored chunk as their
  baseline.
- Any later re-announce, preset normalization, MIDI input, keybind, mouse edit, or plugin-internal
  state change is captured by the normal `PluginDirtyStateTracker` full-state transaction path.

This deliberately chooses **never drop later edits** over **never create a post-restore entry**. If
Nolly's re-announce changes the opaque chunk after undo/redo, that change can become an extra undo
entry. The entry should still be coherent because it is a full before/after plugin-state memento.
