# Plugin Preset Undo Options Plan

Status: investigation plan. This document exists because live testing with Archetype Nolly and
Gateway contradicted the earlier assumption that full plugin chunks are always the right undo
primitive for live plugin edits.

## Problem Statement

RockHero can now undo many plugin-owned parameter changes without crashing by replaying captured
normalized parameter values. That restores the audible state for Archetype Nolly preset changes,
but Nolly's preset dropdown still displays the selected preset name with a dirty marker after undo.

Trying to restore the opaque plugin chunk made the application freeze again and still did not reset
the preset dropdown text. **Important correction (source-verified):** the freeze is *not* in
`setStateInformation` itself. That call completes quickly (~80 ms in the parameter-undo logs) and is
dispatched **inline on the message thread** by Tracktion's `callBlocking`
(`tracktion_AsyncFunctionUtils.h:171-177`) - there is no self-deadlock in the restore call. The
freeze is in the editor render that runs immediately after the restore: `EditorView::setState`
(`rock-hero-editor/ui/src/editor_view.cpp:806`) reads the live audio engine (`refreshAudioMeters`)
and deadlocks with the live audio thread that the full-state load just destabilized. So full-chunk
restore is fragile because of what it triggers downstream, not because the chunk call is broken. See
`plugin-parameter-undo-open-problems.md` for the full freeze analysis. Temporarily hiding/reopening
the plugin window also made behavior worse and should not be pursued further.

### Plugin state is a spectrum, and no single primitive covers all of it

Live testing makes the real shape clear. Plugin state falls into bands, and each band needs a
different undo primitive:

1. **Exposed parameters** (knobs): restorable by replaying captured parameter values through
   `setPluginParameterValue`. No freeze. This is the current working baseline.
2. **Host-visible programs** (a VST3 program list): restorable by `setCurrentProgram(programNum)` -
   a light call Tracktion already makes on restore (see Source-Backed Constraints). No freeze, *if*
   the plugin represents the preset as a program.
3. **Opaque, file-backed, private state** (Nolly's private preset-browser label; Neural Amp
   Modeler / Gateway **Model** `.nam` and **IR** `.wav` selections): not a parameter and not an
   enumerable program. It lives only in the opaque chunk and is reachable only by full-chunk restore
   (Option B) or recreate-from-state (Option F) - the band that hits the freeze.

Option D below covers band 2 only. The **program-exposure probe (2026-06-15) measured Archetype
Nolly at `getNumPrograms() == 0`** (see Option D), and Gateway's Model/IR are file paths, not
programs - so **none of our reference plugins expose presets as band-2 host programs.** Option D
therefore has no beneficiaries today and is shelved; the realistic preset-undo path is band-3
full-chunk restore.

**Scope decision (corrected 2026-06-15): preset undo is REQUIRED, not a stretch goal.** An earlier
note here scoped band-3 out of "parity" on the belief that REAPER does not undo preset loads. That
belief was wrong: **REAPER *does* undo plugin preset loads**, so preset undo is a real gap we must
close, not parity we already have. Because Nolly's preset is band-3 opaque state (`num_programs ==
0`), the **only** mechanism that can restore it is full-chunk restore - which currently hits the
freeze. The freeze is therefore the blocking problem for required functionality and must be solved,
not scoped around.

The one genuine carve-out remains **Neural Amp Modeler / Gateway Model (`.nam`) and IR (`.wav`)
loads**: live testing confirmed REAPER itself does not undo those specific file-backed selections,
so they stay a separate, lower-priority case. (A working full-chunk restore may recover them for
free if the loaded-file reference lives in the opaque chunk - to be determined once the freeze is
solved.) Every ordinary preset load is in scope and gated only on the freeze. The realistic target
is: band 1 (parameter edits, already working) + band-3 preset loads via a freeze-free full-chunk
restore.

The target behavior is REAPER-like:

- loading a plugin preset should become one undoable operation
- undo should restore the prior audible state
- undo should restore plugin-owned UI/controller state, including the preset dropdown text
- redo should restore the loaded preset
- automation playback must not create undo entries
- ordinary knob edits should remain stable and should not reload the full plugin state

## Source-Backed Constraints

The VST3 contract separates parameter edit notification, processor/component state, and
edit-controller state.

- `IComponentHandler::beginEdit`, `performEdit`, and `endEdit` are the plugin-to-host parameter edit
  callbacks. Steinberg documents these as UI-thread callbacks used by the edit controller to tell
  the host about parameter edits.
- `IComponentHandler::restartComponent` tells the host to react to broader changes. In particular,
  `kParamValuesChanged` means multiple parameter values changed, such as after a program change,
  and the host should invalidate cached values and ask the edit controller for current values.
- `IEditController` owns controller-side state and GUI-facing parameter behavior through
  `setComponentState`, `setState`, `getState`, `getParamNormalized`, and `setParamNormalized`.
- JUCE's VST3 wrapper captures and restores both `IComponent` and `IEditController` state inside
  `AudioProcessor::getStateInformation` / `setStateInformation`.
- Tracktion's `ExternalPlugin::restorePluginStateFromValueTree` forwards the saved chunk to
  `AudioProcessor::setStateInformation`, but RockHero live testing shows that path is not reliable
  for Nolly preset undo.
- **Tracktion already restores the VST3 program selection.** In
  `tracktion_ExternalPlugin.cpp:1083-1084`, `restorePluginStateFromValueTree` calls
  `setCurrentProgram (v.getProperty (IDs::programNum), false)` when `getNumPrograms() > 1`, before
  applying the opaque chunk. So `programNum` is part of the captured state, and a plugin whose preset
  is a host program can have that selection restored by a **direct `setCurrentProgram(...)`** call -
  far lighter than a full `setStateInformation`, and a candidate path that avoids the chunk-restore
  freeze. This only helps plugins that expose presets as programs; arbitrary file-backed selections
  (NAM Model/IR) are not enumerable programs and never change `programNum`.
- **The full-chunk restore call is not the freeze.** `setStateInformation` is dispatched via
  `callBlocking` (`tracktion_AsyncFunctionUtils.h:171-177`), which runs the function inline when
  invoked on the message thread (no posted-message round trip, no self-deadlock). The restore itself
  completes; the freeze is the downstream editor render reading the live audio engine.

Relevant references:

- Steinberg `IComponentHandler`:
  https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponentHandler.html
- Steinberg `IEditController`:
  https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IEditController.html
- Steinberg `RestartFlags`:
  https://steinbergmedia.github.io/vst3_doc/vstinterfaces/ivsteditcontroller_8h.html
- Local JUCE VST3 restore path:
  `external/tracktion_engine/modules/juce/modules/juce_audio_processors_headless/format_types/juce_VST3PluginFormatImpl.h`
- Local Tracktion restore path:
  `external/tracktion_engine/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp`

## Working Hypotheses

1. REAPER is probably not undoing Nolly preset loads by blindly calling the equivalent of
   `setStateInformation` on the live processor/editor.
2. REAPER may be recording the plugin-originated edit-controller transaction stream and replaying
   through host/plugin parameter-edit mechanisms.
3. Nolly's preset dropdown text may be controller/UI state that is not fully synchronized by
   Tracktion/JUCE when RockHero replays the saved chunk in our current path.
4. Parameter snapshots are useful but incomplete: they can restore exposed values, but they cannot
   guarantee hidden preset labels, file selections, IR choices, or other private plugin state.

## Options

### Option A: Keep Parameter Snapshot Replay Only

Keep the current safe behavior: preset bursts are captured as parameter snapshots and undo replays
changed parameter values through `IPluginHost::setPluginParameterValue`.

Pros:

- Already avoids the Nolly freeze/crash behavior seen with full-state restore.
- Keeps ordinary knob undo simple and stable.
- Avoids special handling of plugin windows.
- Easy to test with current fakes.

Cons:

- Does not restore hidden plugin state such as Nolly's preset dropdown label.
- May not restore file/IR selections if they are not exposed as parameters.
- Does not meet the REAPER-level UX target.

Use only as a temporary fallback, not as the final solution.

### Option B: Full Chunk Restore With More Guards

Continue trying to make `setStateInformation` safe by adding guards around playback, plugin
windows, graph rebuilds, observer suppression, delayed refreshes, or processor restart.

Pros:

- Theoretically restores component and edit-controller state in one memento.
- Aligns with project persistence and plugin recreate concepts.
- Generic if it works.

Cons:

- Already failed live with Nolly: freezing returned and the preset text still did not reset.
- Hiding/reopening the plugin editor made behavior worse and is unlike REAPER.
- High risk of plugin-specific timing bugs and fragile host behavior.
- Could regress ordinary knob undo.

Recommendation: do not continue down this path unless a focused spike proves a specific missing
Tracktion/JUCE call changes Nolly behavior without freezing.

### Option C: Capture Plugin-Originated Edit Transactions

Treat plugin UI edits as the primary source of truth. For VST3, the meaningful stream is
`beginEdit` / `performEdit` / `endEdit`, plus group edit and restart notifications where exposed
through JUCE/Tracktion. RockHero would capture those as one editor `IEdit` transaction and replay
through parameter-edit APIs that notify the plugin's edit controller correctly.

Pros:

- Closest to the VST3 host/plugin contract.
- Likely closest to how mature DAWs integrate plugin UI undo.
- Avoids full processor state reload for normal live edits.
- Can group preset loads into one undo step when the plugin emits many edits.

Cons:

- Tracktion/JUCE may not expose enough low-level callback detail through public APIs.
- May require adapting or extending the JUCE/Tracktion boundary.
- Still may not capture hidden non-parameter state unless the plugin represents it through
  controller state or parameter/program notifications.
- Needs strong automation suppression so playback automation does not enter project undo.

Validation questions:

- Can RockHero observe plugin-originated `beginEdit` / `performEdit` / `endEdit` separately from
  automation and host-applied undo replay?
- Does Nolly emit a grouped parameter transaction or only many independent parameter edits on
  preset load?
- Does Gateway IR/cab loading emit parameter edits, restart flags, dirty flags, or only opaque
  state changes?
- Does replaying through `setParamNormalized`/Tracktion parameter APIs update the plugin editor's
  own preset UI, or only exposed controls?

### Option D: Host-Level VST3 Program/Unit Integration

Investigate whether affected plugins expose preset selection through VST3 programs, program-list
units, or a program-change parameter. If they do, capture the program number alongside the parameter
snapshot and restore it on undo with a **direct `setCurrentProgram(oldProgram)`** call.

Source-grounded note: this is partly built already. Tracktion captures `programNum` in the saved
state and restores it via `setCurrentProgram` (`tracktion_ExternalPlugin.cpp:1083-1084`). The new
work is small: capture the program number at edit time and call `setCurrentProgram` on undo/redo
*without* going through the full chunk. Because `setCurrentProgram` is far lighter than
`setStateInformation`, it is the most promising **freeze-free** path to resetting a preset
**dropdown** - if (and only if) the plugin represents its preset as a program.

Pros:

- Reuses an existing Tracktion path (`setCurrentProgram`); small, semantic, and avoids the
  full-chunk restore that triggers the freeze.
- Could explain REAPER behavior if REAPER uses VST3 program/unit APIs.
- May fix preset labels for plugins that expose preset selection through standard program APIs.

Cons:

- Covers band 2 only (see the spectrum above). Many modern plugins do not expose browser presets as
  host programs.
- **Neural Amp Modeler / Gateway Model and IR loads are band 3, not programs.** A `.nam` model or
  `.wav` IR is an arbitrary file path, not an enumerable program, so `getNumPrograms()` will not
  reflect it and `programNum` will not change. Option D **cannot** undo Model/IR loads. They remain
  opaque/private state requiring a chunk-restore path.
- Neural DSP plugins may use private preset browsers instead of host program lists, in which case
  even Nolly's preset selection is band 3, not band 2.
- Tracktion's current public wrapper may not expose enough unit/program-list detail beyond
  `programNum`.

**Program-exposure probe result (2026-06-15): Archetype Nolly reports `getNumPrograms() == 0`.**
A temporary probe (`logProgramProbe` in `engine.cpp`) logged the program list at plugin attach, at
every gesture start, and during the open-time parameter burst. Every reading was identical:
`num_programs=0 current_program=0 current_program_name=` (both Nolly instances, 1017 and 1018).
With zero programs there is nothing for `setCurrentProgram` to restore, so **Option D cannot restore
Nolly's preset regardless of any preset switch** - Nolly's preset is measured band-3 opaque state,
the same bucket as NAM Model/IR. This means Option D's only remaining beneficiary would be a plugin
that *does* expose `getNumPrograms() > 1`; none of our reference plugins (Nolly, Gateway) do.
Band 1 (single exposed parameters) was confirmed working in the same session: turning one knob
produced a clean coalesced `Edit Crunch Amp Mid` undo entry.

Remaining validation question:

- Does Gateway / Neural Amp Modeler report `getNumPrograms() > 1`, and does loading a Model or IR
  change `programNum`? Expected: no (file-backed, band 3), now strongly implied by the Nolly result.
  Confirm only if we want a second data point before formally shelving Option D.

**Related defect surfaced by the probe (separate issue): opening a plugin pushes a no-op undo
entry.** When `OpenPlugin` runs, Nolly announces all ~hundreds of parameters via gesture begin/end.
Every one logs `gesture yielded no delta; tracking state` (before == after), yet the burst-promotion
heuristic still promotes them to a state edit and pushes `Edit Archetype Nolly X` onto the undo
stack. Merely opening a plugin therefore pollutes the stack with a no-op entry. This is a defect in
the burst-promotion path, not in the preset design: the promotion should not emit an entry when
every captured parameter has zero delta (and arguably when the resulting full-chunk before/after are
equal). Track and fix independently of the preset-undo work.

### Option E: Plugin-Provided Undo/Redo Forwarding

When a plugin editor has focus, allow the plugin to handle Ctrl+Z/Ctrl+Y first, then synchronize
RockHero's undo state afterward.

Pros:

- If Nolly owns a correct internal undo stack, this may naturally update its preset dropdown.
- Avoids reconstructing plugin-private semantics in RockHero.

Cons:

- Conflicts with RockHero's requirement for one project undo stack.
- Undo labels, dirty state, persistence, and redo ordering become ambiguous.
- Plugin internal undo may not exist, may not cover presets, and may be inconsistent across
  plugins.
- Would not integrate with project-level undo history unless carefully bridged.

Recommendation: investigate only as a diagnostic comparison, not as the primary architecture.

### Option F: Recreate Plugin Instance From Captured State

For preset undo only, remove/recreate the plugin instance from the captured prior state instead of
mutating the existing live instance.

Pros:

- Similar to project load/recreate semantics.
- May avoid live `setStateInformation` editor-controller synchronization bugs.
- Could restore hidden state because the plugin initializes from the prior state.

Cons:

- Heavyweight and may interrupt audio.
- Must preserve instance IDs and chain position.
- May close/reopen plugin editors, which is worse UX and already felt wrong.
- Expensive for repeated preset undo/redo.
- Still may not fix Nolly if the initial state chunk does not contain the preset browser label.

Recommendation: keep as a last-resort fallback for plugin state that cannot be replayed otherwise,
not for ordinary preset undo unless proven necessary.

### Option G: Plugin-Specific Compatibility Layer

Add compatibility handling for known problematic plugins such as Neural DSP or Gateway.

Pros:

- Could produce the exact UX for critical plugins faster.
- Allows plugin-specific file/IR/preset behavior if no generic API exists.

Cons:

- Bad long-term architecture.
- Requires ongoing maintenance and user reports for every plugin family.
- Violates the goal of generic VST3-host behavior.

Recommendation: avoid unless a small compatibility shim is needed after the generic host behavior
is correct.

## Recommended Investigation Sequence

- **Program-exposure probe - DONE (2026-06-15).** Result: **Archetype Nolly reports
  `getNumPrograms() == 0`** at attach, at every gesture, and during the open-time burst (both
  instances). Nolly exposes no host program list, so Option D / `setCurrentProgram` cannot restore
  its preset. Gateway Model/IR are file paths, not programs. Conclusion: **none of our reference
  plugins are band 2; preset undo requires full-chunk restore (band 3), so the freeze must be
  solved.** Option D is shelved (no beneficiaries).

- **Freeze-mechanism probe - NEXT, and reframed.** The earlier hypothesis here ("the freeze is the
  post-restore editor render, not `setStateInformation`") is **refuted**: granular parameter undo
  runs through the *identical* post-undo render path (`completeUndoTransition` → `updateView` →
  `EditorView::setState` → `refreshAudioMeters`, since neither path takes the
  `instantiatesPlugin` busy branch) and does **not** freeze. Same render, different outcome ⇒ the
  render is not the cause. The freeze tracks the **full-chunk restore mechanism** itself:
  `restorePluginStateFromValueTree` → `setStateInformation` (heavy plugin re-init / suspend-resume),
  **plus**, when the plugin window is open, Tracktion calling `recreatePluginWindowContentAsync`
  (`engine.cpp` `recreateEditorAsync`: `setEditor(nullptr)` + `callAfterDelay(50, …)`) to rebuild the
  editor. Two cheap experiments isolate which:
  1. Full-chunk restore with the **plugin window closed** (no `recreatePluginWindowContentAsync`
     fires) - does it still freeze? Isolates the editor rebuild from the state re-init.
  2. Full-chunk restore with **monitoring / the audio device off** - does it still freeze? Isolates
     live audio-thread contention during `setStateInformation`.
  Whichever still freezes localizes the cause; if neither does, the combination is the culprit and a
  scoped suspend/defer around the restore is the fix.

1. Freeze the current safe fallback.
   Keep parameter snapshot replay as the non-crashing baseline. Do not reintroduce live full-state
   restore into preset undo until the freeze-mechanism probe proves it is safe.

2. Instrument what Nolly and Gateway actually emit.
   Add temporary diagnostic logging at the Tracktion/JUCE boundary for:
   - parameter gesture begin/end
   - `parameterChanged`
   - `currentValueChanged`
   - processor changed / host display changed details
   - VST3 restart flags if they are observable
   - current program number/name before and after preset selection
   - opaque component/controller state hash before and after preset selection

3. Determine whether preset load is parameter-representable.
   For each tested plugin, classify the preset/file operation as:
   - parameter-only
   - parameter plus program change
   - parameter plus controller state
   - opaque-only
   - unknown/private

4. Spike Option C.
   Try to capture plugin-originated edit transactions closer to the VST3/JUCE edit stream. The
   spike should answer whether RockHero can distinguish user plugin UI edits from automation and
   host replay without relying on broad `currentValueChanged` capture.

5. Spike Option D if program data changes.
   If Nolly or Gateway exposes meaningful program/unit changes, add a narrow captured memento for
   program selection and test whether replay updates the preset dropdown.

6. Only then revisit opaque state.
   If hidden state is not reachable through parameters, edit transactions, or program APIs, test
   recreate-from-state as a controlled fallback. Do not mutate a live Nolly instance with
   `setStateInformation` from undo unless a new specific reason appears.

## Acceptance Tests

Manual tests:

- Archetype Nolly:
  - insert plugin
  - open editor
  - load a preset
  - press Ctrl+Z
  - verify sound, visible controls, and preset dropdown return to the prior state
  - press Ctrl+Y and verify the loaded preset returns
- Gateway:
  - change a knob
  - undo/redo
  - load amp/cab/IR or other file-backed state
  - undo/redo and verify both sound and UI state
- Automation:
  - play transport with automation changing plugin parameters
  - verify no project undo entries are created
- Mixed edits:
  - plugin insert, preset load, knob edit, undo repeatedly
  - verify undo order is knob edit, preset load, insert

Automated tests:

- plugin-originated grouped parameter edits become one editor `IEdit`
- host-applied undo replay does not create another undo entry
- transport playback suppresses automation-generated parameter observation
- parameter-only state edit uses parameter replay and does not call full state restore
- opaque-only state edit is either rejected/faulted or routed through an explicit fallback path,
  never silently treated as restored

## Decision Gate

Do not choose a final implementation until the instrumentation answers these questions:

1. Does Nolly expose the preset label/selection through any host-visible program or parameter path?
2. Does Nolly emit restart/component-change notifications around preset load?
3. Does replaying the same emitted edit transaction update Nolly's own editor UI?
4. Is Gateway file/IR loading exposed as parameters, state, programs, or private data?
5. Can automation be separated cleanly from plugin UI edits at the chosen capture point?

Question 1 is now answered for Nolly: **no.** The program-exposure probe measured
`getNumPrograms() == 0`, so there is no program/unit path and **Option D is shelved** (no
beneficiaries among our plugins). That removes the freeze-free shortcut and means **required preset
undo can only come from a freeze-free full-chunk restore (band 3)** - so the live decision gate is
now the freeze-mechanism probe, not the program probe.

If question 3 is yes (replaying an emitted edit transaction updates Nolly's own editor UI), Option C
is still worth pursuing as a lighter complement. Otherwise the path is: solve the full-chunk-restore
freeze (see the reframed freeze-mechanism probe), then use full-chunk restore for preset undo. The
only state still accepted as not-undoable is **NAM Model/IR specifically** - the one case REAPER also
does not handle - and even that may come along for free once full-chunk restore is freeze-free.
