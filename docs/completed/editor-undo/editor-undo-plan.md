# Editor Undo/Redo Plan

Status: completed planning and implementation record. Consolidated from the v9 review draft and
supersedes the earlier numbered drafts that existed during plan review. This version records the
faulted-session failure policy, diagnostics ownership, undo-specific diagnostics, and
faulted-session persistence decisions used by the implemented editor undo system.

> **Parameter-undo correction (2026-06-16): plugin edits use full-state mementos.** The granular
> parameter-value workaround was rejected after the freeze root cause was isolated to UI meter reads
> touching Tracktion's playback context during plugin state restore. Normal plugin UI edits now use
> the same full opaque `PluginStateEdit` memento path as preset/file-load edits and replay through
> `IPluginHost::setPluginState`. This preserves plugin-owned non-parameter state such as preset
> labels, dirty markers, loaded files/IRs, and hidden processor metadata. The removed granular API
> (`PluginParameterEdit`, `PluginParameterSnapshot`, and `setPluginParameterValue`) should not be
> reintroduced without a new design review for a distinct non-undo use case such as automation.

> **Phase M decided (2026-06-11): RockHero mementos; Tracktion is a backend.** The inverse-mechanism
> question is now settled in favor of the memento design this document already describes, so the memento
> capture/restore and Tracktion-undo *quarantine* sections are the chosen mechanism, not "one
> candidate". RockHero owns one project undo stack; tone entries restore absolute captured state through
> audio-adapter primitives; Tracktion's undo manager is never consumed as product history; adapter-local
> rollback uses `UndoManager::undoCurrentTransactionOnly()`, never raw `Edit::undo()`. Editor metadata
> stays in editor-core; stay on B2-lite. Rationale: `editor-engine-undo-master-plan-v3.md` (Phase M
> Decision) and `undo-ownership-analysis.md` (Decision + the tree-vs-runtime and automation-curve
> proofs). Plugin edit sections now use the full-chunk capture/restore payload
> (`PluginStateEdit` carries before/after `PluginInstanceState`; replay via `setPluginState`).
> For corrected Tracktion mechanics, `undo-ownership-analysis.md` remains authoritative.

> **Non-negotiable constraint — plugin-state reproduction fidelity (2026-06-11).** Undo/redo of a
> plugin edit must restore the plugin's exact prior state. Plugin-parameter undo therefore uses
> **coherent full-state restore** (the plugin's own chunk via `get/setStateInformation`), the same
> primitive as remove-undo — the parameter sections below now reflect this. The gesture/debounce/
> self-animating *observation* detects *when* a settled user edit occurred; what is captured and
> replayed is the **full plugin chunk** (before/after), not a parameter vector. Granular value replay
> is rejected because it cannot reproduce non-parameter internal state (IRs, presets, oversampling,
> hidden DSP), dependent/stepped parameters, or controller/processor sync. See
> `undo-ownership-analysis.md` ("Verified: VST3 parameter undo is whole-chunk" and "Fidelity is
> non-negotiable").

> **Representation decided (2026-06-14): undo entries are `IEdit` objects, and instance ids are
> preserved on recreate (no remap).** The undo stack stores polymorphic editor-core `IEdit` entries
> (`undo(EditorEditContext&)` / `redo(EditorEditContext&)` / `label()`), not a `std::variant` of
> passive payloads. Each edit co-locates its data with its own forward/inverse logic; memento-style
> edits (remove, plugin-parameter) hold their before/after chunks as members. This supersedes the
> variant/payload framing in "Core Model" below. Rationale: per-operation cohesion, isolated
> unit-testability (an edit plus a fake context, no full controller), and alignment with the
> `common::audio::IEdit` reservation in `architecture.md` — though the concrete interface is
> **editor-core**, not the `common::audio` placeholder, because edits restore editor-only visual
> state (block placement, display-type) that `common/audio` must not depend on. `EditorEditContext`
> injects the signal-chain model, `IPluginHost`, `ILiveRig`, and an output-gain setter at apply
> time, so edits stay pure value objects with no controller back-pointers. The id-preserving
> recreate outcome is reported by the edit's own `undo`/`redo` result, so a failed id check is a
> normal non-commit failure rather than a history-wide rewrite trigger.
>
> **Instance-id remapping is removed.** Recreate (remove-undo, insert-redo) preserves the original
> `instance_id` instead of accepting a new one, so no stored entry is ever rewritten. A focused
> spike (2026-06-14) confirmed Tracktion's `EditItemID::readOrCreateNewID` returns any `id` present
> in the inserted state verbatim and allocates a fresh id only when none is present; re-inserting a
> captured plugin state with its `id` intact restores the original `itemID` after the prior instance
> was removed and released. The engine's deliberate `removeProperty(IDs::id)` in the general
> `insertPluginState` path is what forced new ids. This collapses
> `EditorUndoHistory::remapInstanceId`, the "Instance-Id Remapping" section (now
> "Instance-Id Preservation"), and the old
> `original_instance_id`/`restored_instance_id` rewrite contract for the undo path.
>
> **Documented fallback (not taken):** had id preservation proved unsafe, remap would live on an
> `IPluginEdit` capability sub-interface implemented only by plugin-id-bearing edits (the history
> sweeps `dynamic_cast<IPluginEdit*>` entries) — not on the base `IEdit`, since non-plugin edits
> (output gain, future chart/tempo edits) carry no id. The spike passed, so this is recorded only as
> the contingency.

## Consolidated Review Updates

- Kept `EditorUndoHistory` free of any logging dependency; history returns transition results and
  events, and `EditorController` logs those events at the action boundary through the Quill `Logger`
  facade (`RH_LOG_*`).
- Clarified that the only pre-undo logging prerequisite is the durable file-backed logger, which
  already exists as the Quill-backed `Logger` facade (plus `JuceQuillBridge`). Action logs, undo
  logs, and the rollback diagnostic are added incrementally with the undo stages that use them, via
  `RH_LOG_*` directly. There is no separate `IEditorDiagnostics` interface; Quill already provides
  durable, category-based, structured logging.
- Tightened faulted-session persistence: normal Save and Save As are blocked while faulted because
  the live backend is untrusted. A future safe-copy command may be added only if it serializes a
  known-good editor project model and never reads refreshed backend state as persistence input.

## Failure Philosophy

Apps such as REAPER appear not to clear undo history during normal use because their ordinary edit
paths are built to keep the project state known. They either preflight before mutation, apply
changes through transactional model primitives, roll back failed operations, or repair the project
model into a known state before the user can continue.

RockHero should aim for the same user experience. The recoverable undo contract should never need
to clear history because a command failed. If a command cannot be made transactional or repaired
back to its pre-action state, it is not ready to be included in user-visible undo.

Expected failures must not clear history:

- no project is open;
- an action is unavailable while busy;
- a plugin instance id is stale before mutation starts;
- a parameter id cannot be resolved before setting anything;
- a plugin candidate cannot be found;
- plugin creation or loading fails before insertion;
- an async completion is stale before apply.

Those failures leave the history pointer unchanged.

Every undoable mutating port must obey one of these shapes:

- preflight rejects before side effects;
- mutation succeeds and returns an authoritative post-state;
- mutation starts, then repair/rollback restores the exact pre-action state before returning an
  error.

A rollback-contract violation is when an adapter started a mutation and then could prove neither
success nor rollback. It is a **critical internal error that should be impossible** if every wired
port truly preflights or rolls back; the live rig is now in a state the editor cannot trust. When it
happens, the controller does not terminate the app; it faults the session:

- log an `Error` record through the Quill `Logger` facade (`RH_LOG_ERROR`, persisted to the editor
  log file) with enough context to debug the broken operation;
- enter a `faulted` session state that blocks all content editing, Undo/Redo, Save, and Save As, so
  the user cannot build on or persist the untrusted state. This is centralized in the existing
  action-availability check (the same place the busy state already blocks editing), so it is one flag
  and one branch, not a mode threaded through every gate. Only the non-saving project-lifecycle
  escape actions stay available: Open/Import/Restore, Close, and Exit;
- refresh the editor's diagnostic view from the backend and mark the project dirty without treating
  that readback as trusted persistence state;
- show a message making clear this is an unexpected internal error that should not happen, asking the
  user to report it to the developer and attach the editor log file, and telling them to reopen the
  project to rebuild a clean live rig from the last saved trusted state.

The `faulted` flag clears when a project is loaded or closed (reopening rebuilds a trusted session).
A port without a proven preflight-or-rollback path must not be wired into user-visible undo, and the
design goal is that this path is unreachable through ordinary user operations. The simple rule is:
recoverable errors preserve state; a rollback-contract violation faults the session, tells the user
to report the bug, and routes them to reopen or close rather than saving from untrusted state.

### Trusted Project State After Faults

The non-corruption guarantee comes from keeping persistence tied to a known-good editor project
model, not to whatever the live backend happens to contain after a rollback-contract violation:

- each undoable command starts from the current known-good project model;
- the known-good model is updated only after the command and all required adapter side effects
  succeed and return validated post-state;
- rollback-contract violations do not promote backend readbacks into the known-good model;
- normal Save and Save As serialize only the known-good model and are disabled while the session is
  faulted, because the user-visible session may no longer match the known-good model;
- if preserving unsaved work after a fault later becomes important, add a separately named safe-copy
  command that serializes only the known-good model, does not clear the faulted state or dirty flag,
  and never serializes refreshed backend state. That command is not part of the first undo pass.

## Goal

Add editor undo/redo for project/tone edits that affect saved live-rig state, with undo policy in
`rock-hero-editor/core` and Tracktion/JUCE detail isolated in `rock-hero-common/audio`.

First scope:

- plugin insert;
- plugin remove;
- plugin move;
- signal-chain block placement;
- plugin display-type override;
- plugin parameter changes from plugin editor windows (user-inserted plugins only);
- output gain from the editor's output-gain control.

Out of scope:

- input gain, because it is a per-user input-config value (like input calibration), not project
  tone state;
- input calibration, because it is per-device user settings and live-route state rather than
  project tone history;
- chart/note undo, because chart editing does not exist yet.

## Logging

The only app-startup logging prerequisite is a durable file-backed logger. It already exists: the
Quill-backed `Logger` facade in `rock-hero-common/core`, installed by the editor app composition
root before the audio engine and editor window, with `JuceQuillBridge` routing
`juce::Logger::writeToLog` into the same durable log. That is enough to debug undo development.
Everything structured below is built incrementally inside the undo implementation stages, each stage
adding the logging for the behavior it introduces. Do not create a separate logging prerequisite
stage before the undo work needs it. Keep it narrow: do not implement the deferred
`docs/todo/core-domain-logging-targets-plan.md` target split here.

### Logging Surface

Log through the project's existing Quill `Logger` facade directly, the same way
`editor_controller.cpp` already does (`RH_LOG_WARNING("editor.controller", ...)`). Do **not** add a
separate `IEditorDiagnostics` port: Quill already provides durable, category-based, structured
(fmt-style) logging, so wrapping it would be an abstraction over an abstraction and a second logging
vocabulary, and it would contradict the direct-logging practice already in editor-core.

- Use the `editor.controller` category (and finer `editor.controller.*` subcategories if useful) at
  the controller action boundary. Log at the boundary, not inside low-level domain objects.
- `EditorUndoHistory` stays pure: it depends on no logger and returns transition results/events that
  the controller logs via `RH_LOG_*`.
- Use `RH_LOG_INFO` for normal lifecycle, `RH_LOG_WARNING` for unusual recoverable paths, and
  `RH_LOG_ERROR` for recoverable failures and rollback-contract violations.
- Keep message text human-readable but do not make exact log text a behavioral contract. The tested
  behaviors are state transitions (faulted session, availability, dirty state), not log lines.
- The one place emission itself is load-bearing is the rollback-contract diagnostic (the bug-report
  payload). If its emission must be proven, assert the faulted-state behavior and, only if needed,
  capture the record with a focused Quill test sink rather than introducing a standing port.

### Action And Undo Events

Logged by the controller at the action gate (one chokepoint covers all actions), filled out as each
stage lands. For undo history, the controller logs the transition result/events returned by the pure
history type:

- action lifecycle: request received; availability rejection with action id and reason; started with
  busy/open-state context; completed; recoverable failure with typed code/message; canceled; stale
  async completion ignored;
- undo-specific: undo/redo requested; entry pushed with type and affected ids; pending parameter
  edit started/completed/flushed/dropped; rollback attempt started/completed; plugin recreated with
  preserved id; clean revision marked or made unreachable by eviction.

Use `RH_LOG_INFO` for normal lifecycle, `RH_LOG_WARNING` for unusual recoverable paths, and
`RH_LOG_ERROR` for recoverable failures and rollback-contract violations.

### Rollback-Contract Diagnostic

A rollback-contract violation is a critical error handled by faulting the session (see Failure
Philosophy), not by aborting, so it needs no special synchronous-write-before-termination path. It is
a single structured `RH_LOG_ERROR` record on the normal log, written before the session is faulted —
this is the diagnostic the user is asked to send to the developer — including: operation
name; undo/redo direction and entry type; target plugin/candidate ids, chain index, parameter ids,
and output-gain values as relevant; busy operation and token; message-thread check result; typed
adapter error code/message; whether rollback was attempted and its failure detail; and pre/post chain
snapshots or compact readbacks. Log plugin-state chunk sizes and hashes only, never raw bytes.

## Ownership Decision

Undo policy lives in a headless editor-core history type driven by `EditorController`.
`common/audio` provides Tracktion-free apply/capture operations and parameter-edit events, but it
does not own the app undo stack.

Tracktion's undo manager is useful evidence, not the ownership answer:

- RockHero has two authority domains. Plugin runtime order and plugin state are backend
  authoritative, while the project/session/timeline model is editor-core authoritative. A single
  `Ctrl+Z` history must linearly order edits from both domains.
- Some Tracktion plugin-list and plugin-state `ValueTree` changes already enter
  `Edit::getUndoManager()` internally. That stack is not intentionally shaped around RockHero
  commands.
- Native plugin knob edits are observable, but not reliably represented as undoable Tracktion
  transactions.
- Tracktion transaction grouping is not RockHero command grouping. RockHero needs entries such as
  "move plugin plus placement", "remove plugin plus visual metadata", and future chart/session
  edits that Tracktion cannot define.

The split is:

- **RockHero editor-core history:** user-visible undo/redo availability, command labels, clean
  revision, typed failure policy, `Ctrl+Z` / `Ctrl+Y`.
- **Tracktion/JUCE adapter:** plugin mutation primitives, opaque plugin state capture/restore,
  parameter observation, message-thread enforcement, and adapter-local cleanup for Tracktion's
  internal undo history.

### Inverse mechanism: RockHero mementos (Phase M decision)

Phase M is settled: RockHero owns one product undo stack, and tone entries restore absolute captured
state through RockHero-owned mementos. Tracktion remains the backend for plugin instantiation, opaque
state capture/restore, plugin-list mutation, parameter access, and graph rebuilds, but Tracktion's
undo manager is not consumed as product history.

The cleaned-base Tracktion measurements still matter because they corrected earlier assumptions:

- For a single isolated command wrapped in one `beginNewTransaction`, `Edit::undo()` is clean and
  id-preserving (spike: insert undone to empty chain, redone with the same id 1011).
- Transaction boundaries are controllable; the earlier claim that every ValueTree write necessarily
  creates an ungroupable user transaction was wrong.
- Third-party VST3 parameter undo is whole-chunk either way. The chosen RockHero memento path stores
  before/after full plugin chunks for fidelity, not a granular parameter vector.

Those facts made Tracktion delegation viable for tone-only structural edits, but Phase M chose
mementos because product undo is wider than Tracktion's `Edit`: editor metadata and future
tablature/chart edits must share the same visible `Ctrl+Z` order. Delegating tone entries to
`Edit::undo()/redo()` would keep two ordered histories synchronized by convention. Mementos keep one
source of truth: each entry restores its own captured state through project-owned contracts.

B2-full is therefore not part of the undo path. It remains a possible future routing refactor only if
the project later chooses a Tracktion-owned tone-edit architecture; the memento implementation uses
the current synchronous B2-lite/B2-lite+ adapter paths.

## Tracktion Undo Quarantine

> *Live as of the 2026-06-11 Phase M decision (mementos chosen). Tracktion's undo manager is internal
> adapter state, never product history. Note (Tracktion specialist correction): for adapter-local
> rollback prefer `UndoManager::undoCurrentTransactionOnly()` over raw `Edit::undo()`, which can pop the
> previous transaction if the scoped operation produced no undoable action or the bracketing is wrong.*

RockHero does not ignore Tracktion's undo manager; it quarantines it as internal adapter state and
owns the product undo history itself. The adapter still uses Tracktion for plugin instantiation,
state capture/restore, list mutation, parameter access, and graph rebuilds.

Rules:

- Do not route RockHero Undo/Redo actions to `tracktion::Edit::undo()` or
  `tracktion::Edit::redo()`.
- Do not expose Tracktion undo/redo through menus, shortcuts, plugin windows, or future
  control-surface paths.
- Do not derive RockHero `canUndo()` / `canRedo()` from `Edit::getUndoManager()`.
- Do not assume Tracktion's internal undo history is empty after RockHero plugin operations.
- Suppress RockHero plugin-edit ingestion while RockHero is applying an edit, undo, or redo.

Preferred implementation shape:

- Keep quarantine adapter-local. The editor should not need a Tracktion-shaped `clearUndoManager`
  port.
- Prefer adapter operations that avoid recording Tracktion undo actions in the first place, such as
  using `nullptr` undo managers for memento restores where Tracktion APIs allow it.
- Leave Tracktion's internal stack inaccessible by default. Tracktion already bounds stored undo units;
  because RockHero never consumes that stack, accumulated internal history is not user-visible state.
- Use Tracktion undo only for bounded adapter-local rollback when that is the safest way to reverse a
  just-opened internal transaction. In that case prefer `undoCurrentTransactionOnly()` or an
  equivalent current-transaction rollback primitive, never raw `Edit::undo()`.
- Clearing Tracktion undo history is allowed only as adapter-local maintenance after a mutation has
  completed and state has been refreshed, and only if the adapter has a concrete memory or correctness
  reason to do so. It is not the product undo mechanism and should not become an editor-core port.

The key invariant is that RockHero never consumes Tracktion's internal stack as user history.

Spike status for quarantine and parameter observation:

1. Log whether `edit.getUndoManager().canUndo()` changes after insert, move, remove, output gain,
   plugin parameter changes, plugin-state capture, and plugin-state restore.
2. Verify whether clearing Tracktion undo history after those operations affects playback graph
   rebuilds, plugin state flush, dirty/change timers, or subsequent plugin operations.
3. Verify one real VST3 plugin's knob-change callback pattern: gesture begin/end, raw
   `parameterChanged`, or both.

Spike outcome (see Stage 0 Spike Status): items 1-2 ran on 2026-06-10 and validated that clearing
Tracktion undo history after owned mutations is mechanically safe in the tested cases. Phase M still
does not require active clearing; the selected mechanism is memento restore through RockHero ports
with Tracktion undo kept private and unconsumed. Item 3 ran on 2026-06-12 against one well-behaved
real VST3 and confirmed the gesture path; no-gesture/self-animating plugin behavior remains a
targeted follow-up probe for when a suitable plugin is available.

## History Failure Policy

RockHero uses two-phase undo/redo with an unchanged-on-error contract for all side-effecting ports
used by undo history.

Failure classes:

| Failure class | Examples | History result |
|---|---|---|
| Preflight failure before mutation | no project, target missing, invalid parameter id, busy state | leave history pointer unchanged |
| Port failure after no net mutation | adapter rejected before mutation, or repaired back to pre-state | leave history pointer unchanged |
| Successful apply | requested editor/backend changes completed and returned authoritative post-state | commit undo/redo pointer transition |
| Rollback-contract violation (critical, should never happen) | adapter mutated then could prove neither success nor rollback | log Error diagnostic, enter faulted state (block all editing + Save/Save As), ask user to report the bug, route to reopen/close |

The controller does not carry a normal "clear history because state is unknown" branch. Unknown
state is outside the recoverable undo contract. If a port cannot keep the unchanged-on-error
guarantee, that operation should not be wired into user-visible undo yet.

Use an explicit two-phase model:

1. Flush pending plugin parameter edits at the action-dispatch chokepoint (see Recording Boundary).
2. Peek the next undo/redo entry.
3. Preflight all cheap invariants before mutation.
4. Execute against editor state and project-owned ports.
5. Update the pending entry if capture changed it (recreate preserves the id, so there is no id
   remap to apply).
6. Commit the stack transition only after success.
7. If an adapter reports a rollback-contract violation, log the Error diagnostic, enter the faulted
   state (block all editing and saving), refresh a diagnostic backend view only, and route the user
   to report the bug and reopen/close rather than trying to keep editing or saving over untrusted
   state.

For busy-fenced instantiate directions, keep the transition pending until the operation completes
and passes the normal busy-token/liveness checks.

## Parameter Edit Observation

There are two parameter-like sources, but they are not identical:

1. **External plugin parameters.** Changes made in a user-inserted plugin's own editor arrive
   through Tracktion's `AutomatableParameter` callbacks.
2. **Output gain.** The editor output-gain control is a RockHero editor command routed through
   `ILiveRig::setOutputGain`. The gain plugin stores a plain `gainDb` property rather than an
   `AutomatableParameter`, so output gain is never part of parameter observation; it shares the
   before/after history shape but has its own command path.

### Observed Plugins

Attach parameter observation only to user-inserted signal-chain plugins. Those are the edits
RockHero wants to make undoable, so the observer is wired to their `AutomatableParameter`s and to
nothing else. Structural live-rig plugins are simply not attached, so there is no "exclusion" guard
to maintain and no double-record to prevent:

- The output-gain plugin stores gain as a plain `gainDb` property (a `CachedValue<float>`), not an
  `AutomatableParameter`, so it emits no parameter callbacks at all. Output gain is edited through
  RockHero's own slider and recorded as a dedicated dB command (see Output Gain Rules).
- Input gain is per-user config and out of scope (not undoable).
- Meters and other internal plugins are not user tone edits and are not attached.

### External Plugin Parameter Rules

The adapter observes a user-inserted plugin's `AutomatableParameter` callbacks **only to detect when a
settled, discrete user parameter edit has occurred.** The undoable payload is **not** a set of
parameter values — it is the plugin's **full opaque state chunk** captured before and after the settled
edit (`PluginInstanceState`, the same representation and `get/setStateInformation` primitive as
remove-undo). Editor-core stores the before/after states opaquely and replays them through
`IPluginHost::setPluginState`. This is required by the non-negotiable fidelity rule: granular value
replay cannot reproduce non-parameter internal state (IRs, presets, oversampling, hidden DSP),
dependent/stepped parameters, or controller/processor sync; the full chunk can. See
`undo-ownership-analysis.md` ("Fidelity is non-negotiable").

Because the payload is the whole chunk, **one settled edit captures every parameter that moved in that
gesture/window at once.** There is no per-parameter batch, no before/after value vector, and no
parameter-id bookkeeping in the restore path; parameter identity remains an adapter-internal detail
used only to track gesture/debounce state during detection.

The detection layer does, however, know which parameter(s) settled, so the emitted edit carries a
**display-only `label_hint`** (the changed parameter name(s), e.g. "Gain") for the history label and
undo confirmation. This is visibility metadata only — never read for restore, identity, or remapping
(see *Undo Visibility → Plugin-parameter edits*). When several parameters moved in one gesture, the
hint may name the primary one or summarize ("Amp Sim — 3 parameters").

The adapter maintains a **last-settled state baseline** (one captured `PluginInstanceState`) for every
observed plugin, used as the `before` source for the conservative non-gesture path and refreshed so
dropped/automation motion never becomes a later spurious `before`:

- initialize the baseline when a user plugin enters the chain, after project load/restore, after
  `recreatePluginStatePreservingId`, after candidate insert, and after any full plugin-state
  refresh;
- set the baseline to the freshly captured `after` state on each emitted edit;
- refresh the baseline to the current captured state on a dropped/uncertain settle, before clearing
  pending state;
- set the baseline to the applied state after a host-driven `setPluginState` while recording is
  suppressed (undo/redo apply);
- discard the baseline when a plugin leaves the chain.

Detection uses the parameter callbacks (this logic is unchanged from the prior design; only the
captured/replayed payload is the chunk):

Gesture path (the reliable source):

- On `parameterChangeGestureBegin`, capture the current chunk as `before` (a fresh pre-edit snapshot,
  preferred over the baseline for accuracy).
- On `parameterChangeGestureEnd`, capture the current chunk as `after`; if it differs from `before`,
  emit one edit `{instance_id, before, after}` and set the baseline to `after`. (Spike-confirmed:
  the whole drag — including a multi-second mid-drag pause — is one open gesture, so `gestureEnd` is
  the correct settle boundary; a time-based debounce would wrongly split a paused drag.)

Non-gesture fallback (best effort, must stay conservative):

- A `parameterChanged` with no open gesture (and recording not suppressed) starts or extends a short
  debounce window for that plugin instance. **Suppress this path while a gesture is open for the
  parameter and for a brief window after its `gestureEnd`:** spikes show a lone post-gesture
  `parameterChanged` (same value, no `currentValueChanged` pair) can arrive just after `gestureEnd` as
  a value-confirmation, and it must not be treated as a fresh edit (the net-change-vs-baseline guard
  also drops it). The pre-edit chunk cannot be reconstructed after the first
  callback, so the non-gesture `before` is the **baseline** (one parameter-step of slop at the very
  first change is the accepted best-effort limit, identical to the prior design's cache fallback).
- The window fires only when motion settles (no further change for that plugin within the window); on
  settle, capture the current chunk as `after` and emit one edit if it differs from the baseline.
- Explicit flush is a classification point, not a bypass around the self-animating guard: it emits a
  chunk edit only for plugins it can classify as a settled/discrete user edit, and drops
  continuous/high-rate/meter-like/uncertain motion — refreshing that plugin's baseline to the current
  state and clearing pending state without history.
- **Guard against self-animating parameters.** Plugins move their own parameters (LFOs, envelopes,
  MIDI-learn sweeps, meters). That motion must not become history:
  - a plugin whose parameters change continuously never settles, so no chunk is ever captured for it;
  - additionally drop plugins changing at a high sustained rate, or whose moving parameters the adapter
    can identify as non-user-writable / meter-like;
  - prefer dropping an uncertain non-gesture settle over recording a spurious one. The gesture path
    remains the trustworthy source; the non-gesture path exists only so discrete randomize/preset
    actions are not silently lost.

Replay: undo/redo restore the captured chunk via `IPluginHost::setPluginState`, which drives the live
processor through `setStateInformation` (the setter) rather than by reverting a ValueTree property, so
the audio follows by construction. An already-open plugin editor window **does** visibly refresh on
`setStateInformation` (Step 2 spike, confirmed 2026-06-12), so the on-screen control snaps to the
restored value automatically (see Undo Visibility for the policy on revealing the affected plugin).

Suppression:

- Ignore parameter callbacks while RockHero is applying an edit, undo, redo, plugin restore, project
  load, project close, or any other host-driven state application.
- Ignore automation-originated callbacks if Tracktion exposes enough context to distinguish them.
- If automation origin cannot be distinguished reliably, prefer suppressing only during known
  RockHero applies and record user-origin plugin editor changes.

### Output Gain Rules

Output gain remains an editor command, and is the only path by which output gain becomes undoable:

- slider drag begin captures `before`;
- drag/end commit records one entry if the gain net-changed;
- keyboard/text commits record one immediate before/after entry;
- undo/redo replay through `ILiveRig::setOutputGain`, with recording suppressed.

The gain plugin exposes a plain `gainDb` property rather than an `AutomatableParameter`, so it
produces no parameter callbacks; output gain is recorded only by this command path, producing
exactly one history entry.

## Edit Reversal Strategy

Reverse every edit with an inverse command except remove. Remove needs a captured memento because
reversing it reconstructs a destroyed plugin instance.

| Edit | Undo | Redo | Captured | Sync/async |
|---|---|---|---|---|
| Insert | `removePlugin(id)` | `insertPlugin(candidate, index)` + placement | block placement only | undo sync, redo busy-fenced |
| Remove | recreate captured plugin at index | `removePlugin(id)` | full plugin state + visual state | undo busy-fenced, redo sync |
| Move | `movePlugin(id, from)` + placement | `movePlugin(id, to)` + placement | before/after index + placement | sync |
| Block placement | restore before vector | restore after vector | before/after placement vectors | sync |
| Display-type override | restore before | restore after | id + before/after override | sync |
| Plugin parameter edit | restore captured prior plugin state (full chunk) | restore captured post-edit plugin state (full chunk) | instance id + before/after full plugin state | sync |
| Output gain | `setOutputGain(before)` | `setOutputGain(after)` | before/after gain | sync |

Notes:

- Move stays a compound inverse. The handler reorders through `IPluginHost::movePlugin` and then
  restores block placement.
- The Tracktion adapter currently re-inserts the same `tracktion::Plugin::Ptr` during a move, so
  the instance, parameters, and runtime id should survive. Tests still validate this at the
  project-owned boundary.
- Insert is also a compound inverse, but only on the editor-visual side. The insert flow assigns a
  visual block, so the insert entry must store block placement and insert-redo must restore it after
  applying the snapshot.
- Candidate insert returns the inserted runtime id explicitly, which the insert edit stores.
  Insert-redo recreates the plugin under that same id (preserved, not remapped).
- Only insert-redo and remove-undo instantiate a plugin. Those directions run on the message thread
  behind `BusyOperation::LoadingPlugin` via `runAfterBusyPresentationReady`, matching plugin insert.

## Audio Boundary

Add Tracktion-free surface to `IPluginHost`, next to the existing instance operations. Names may
change in implementation; the contracts are fixed.

```cpp
struct PluginInsertResult
{
    PluginChainSnapshot snapshot;
    std::string inserted_instance_id;
};

struct PluginInstanceState
{
    std::vector<std::byte> opaque_data;
};

struct PluginStateEdit
{
    std::string instance_id;
    PluginInstanceState before;
    PluginInstanceState after;
    std::string label_hint;      // display-only plugin or edit label.
};

struct PluginEditObserver
{
    std::function<void(bool)> pending_changed;
};

struct PluginStateEditObserver
{
    std::function<void(PluginStateEdit)> edit_completed;
};

[[nodiscard]] virtual std::expected<PluginInsertResult, PluginHostError> insertPlugin(
    const PluginCandidate& plugin_candidate, std::size_t chain_index) = 0;

[[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
    const std::string& instance_id, std::size_t destination_index) = 0;

[[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
    const std::string& instance_id) = 0;

[[nodiscard]] virtual std::expected<PluginInstanceState, PluginHostError>
capturePluginState(const std::string& instance_id) = 0;

[[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError>
recreatePluginStatePreservingId(
    const PluginInstanceState& state, std::size_t chain_index) = 0;

// Restores a full opaque state chunk to an EXISTING instance, driving the live processor via
// setStateInformation (the setter). Used by plugin edit undo/redo. Distinct from
// recreatePluginStatePreservingId, which recreates a removed plugin under its prior id.
[[nodiscard]] virtual std::expected<void, PluginHostError> setPluginState(
    const std::string& instance_id, const PluginInstanceState& state) = 0;

virtual void flushPendingPluginEdits() = 0;
[[nodiscard]] virtual bool hasPendingPluginEdits() const = 0;
virtual void setPluginEditObserver(PluginEditObserver observer) = 0;
virtual void setPluginStateEditObserver(PluginStateEditObserver observer) = 0;
```

The implementation can choose a different spelling, but the semantics are required:

- `PluginInstanceState` is a complete public value type that editor-core can store in history. Its
  bytes are opaque to editor-core.
- Every mutating operation has an unchanged-on-error contract. If it returns `std::unexpected`, the
  adapter either made no side effects or repaired all side effects back to the pre-call state.
- If an adapter cannot repair after partial mutation, that is a rollback-contract violation and
  should be reported through a separate invariant path with diagnostic context, not as an ordinary
  recoverable `PluginHostError`.
- `insertPlugin` returns `inserted_instance_id`; callers do not infer it from snapshot position.
- `recreatePluginStatePreservingId` is the undo-only recreate path. It succeeds only when the
  restored live plugin keeps the runtime id encoded in the captured state; a changed id is a restore
  failure after rollback, not a remap. It returns the updated `PluginChainSnapshot` like
  `movePlugin`/`removePlugin`; the preserved id is guaranteed by the contract, so it is not echoed
  back in the result.
- The general new-instance state-insert path must remain separate from undo. If retained for future
  duplication, name and contract it so callers know it strips or allocates runtime ids.
- Parameter-change observers are attached only to user-inserted plugins' automatable parameters.
  They exist solely to tell the plugin-state tracker that a settled user edit occurred; completed
  undo entries are before/after full-state chunks, not parameter values. Structural live-rig plugins
  are not attached (the gain plugin has no automatable parameter, and meters/input gain are not user
  tone edits).
- `setPluginState` restores an opaque chunk to an existing instance through the live setter
  (`setStateInformation`), so the running processor follows; it must not be implemented as a raw
  ValueTree property revert (which would not reload the processor) and must not enter Tracktion's undo
  manager.
- Capture, id-preserving recreate, state restore (`setPluginState`), and pending-plugin flush are
  message-thread operations.
- `PluginEditObserver` reports aggregate pending-state changes so editor-core can keep command
  availability in sync with gesture/debounce state. `PluginStateEditObserver` reports completed
  before/after-chunk edits with a display-only `label_hint`; the label is never used for restore,
  identity, or remapping.
- `flushPendingPluginEdits` synchronously emits settled/discrete pending edits (each a before/after
  chunk), drops continuous or uncertain pending edits, refreshes the per-plugin state baseline,
  clears pending state, and sends a pending-state notification when the aggregate pending state
  changes.
- The controller fences the two instantiating directions behind `BusyOperation::LoadingPlugin`; the
  audio layer does not own editor busy state.
- The boundary stays on `IPluginHost`, not `ILiveRig`, because it reverses per-plugin host
  operations. Output gain remains on `ILiveRig`.

## Rollback Proof Requirements

Every mutating port used by undo history needs a written, test-backed rollback proof before the
operation is exposed through user-visible Undo/Redo. The proof must name:

- all preflight checks that happen before mutation;
- the exact pre-state captured before mutation starts;
- the first point where side effects may occur;
- the rollback or repair path for each later failure;
- the readback or snapshot that proves the pre-state was restored before returning an ordinary
  recoverable error.

If the adapter cannot prove either success or rollback, it must report a rollback-contract violation
(a distinct error kind) instead of returning a normal `PluginHostError`, with enough context for the
controller's diagnostic: operation name, target ids, typed error detail, whether rollback was
attempted, and any available pre/post chain snapshots or readbacks. This path should be unreachable
through ordinary user actions; it exists to capture diagnostics and fault the session if a port breaks
its contract.

Initial per-port proof shape:

- `insertPlugin`: preflight candidate existence, chain index, and plugin cap before side effects.
  Instantiate and load the plugin before insertion when Tracktion allows it. Capture the pre-chain
  snapshot before insertion or graph rebuild. If insertion, metadata refresh, or graph rebuild fails
  after mutation, remove the inserted plugin, restore the pre-chain order/state, refresh, and return
  an ordinary error only after the pre-chain snapshot is proven restored.
- `movePlugin`: preflight instance id and destination. Capture the exact pre-chain order and any
  adapter-owned metadata affected by rebuild before moving. If the move or rebuild fails, restore
  the previous order/state and verify by snapshot. A best-effort "move it back" attempt is not
  enough unless it proves the exact pre-state.
- `removePlugin`: preflight instance id. Capture the plugin memento and pre-chain state before
  removal. If removal, metadata refresh, or graph rebuild fails after mutation, reinsert/restore the
  captured plugin, restore the pre-chain order/state, refresh, and return an ordinary error only
  after the original state is proven restored.
- `recreatePluginStatePreservingId`: preflight memento validity, destination index, plugin cap, and
  expected runtime id. Capture the pre-chain state before restore begins. If state restore,
  insertion, id verification, metadata refresh, or graph rebuild fails, remove any partial plugin
  and restore the pre-chain snapshot before returning an ordinary error.
- `setPluginState`: preflight the target instance exists before applying. Capture the instance's
  current chunk as the pre-state. Apply the requested chunk via `setStateInformation`. If the apply or
  a required readback fails, restore the captured pre-state chunk and verify before returning an
  ordinary error.
- `ILiveRig::setOutputGain`: keep the command undoable only while the implementation is
  unchanged-on-error. If output gain later gains a recoverable failure path, it must follow the same
  preflight/capture/apply/rollback/readback shape before remaining in the undo scope.

Pure editor-core state edits, such as placement and display override changes, should compute and
validate the replacement state before committing it. They should not need a rollback-contract path
because the state transition can remain a single in-memory commit.

## Editor Visual State On Recreate

Remove-undo must restore editor-owned metadata that the audio memento must not own:

```cpp
struct PluginVisualEditState
{
    std::string instance_id;
    std::size_t block_index{};
    std::optional<PluginDisplayType> display_type_override;
};
```

Rules:

- Capture the target plugin's `PluginVisualEditState` before removing it.
- After successful recreate, the restored plugin keeps its original `instance_id` (preserved, not
  remapped), so the captured `PluginVisualEditState` keys still match.
- Apply the restored chain snapshot first, because backend order/metadata are authoritative.
- Apply block placement and display override after the snapshot refresh using the preserved id.

## Instance-Id Preservation

Recreate preserves the plugin's original runtime `instance_id`, so no entry is ever rewritten and
editor state that keys on `instance_id` stays valid across undo/redo.

A spike (2026-06-14) confirmed Tracktion's `EditItemID::readOrCreateNewID` returns any `id` present
in the inserted state verbatim and allocates a fresh id only when none is present. Re-inserting a
captured plugin state with its `id` intact restores the original `itemID` after the prior instance
was removed and released (the plugin cache does not merge a stale instance). The general
`Engine::insertPluginState` strips the id on purpose — it must support inserting a captured state as
a new instance for non-undo callers (e.g. future duplication) — so undo uses a dedicated
id-preserving recreate path, valid because the removed id is provably free at recreate time.

Both instantiating directions therefore restore the original id:

- remove-undo recreates the captured plugin with its original `instance_id`;
- insert-redo recreates the inserted plugin with the id the insert edit stored.

The undo path no longer has an `original_instance_id`/`restored_instance_id` mapping. Tests must
cover an id-preserving recreate for both remove-undo and insert-redo against a fake `IPluginHost`,
asserting the restored id equals the original and that later undo/redo of sibling edits referencing
that id still resolve. When `recreatePluginStatePreservingId` lands in the concrete `Engine`
adapter, add an adapter-level test that captures, removes, and recreates through the project-owned
port; the raw Tracktion regression test only pins the vendor primitive. Because the design now
depends on this Tracktion behavior, keep that lower-level regression test so a future engine upgrade
cannot silently reintroduce the remap problem.

**Fallback (not taken):** if id preservation were unsafe, remapping would return — but on an
`IPluginEdit` capability sub-interface implemented only by plugin-id-bearing edits (the history
sweeps `dynamic_cast<IPluginEdit*>` after a recreate), never on the base `IEdit`, because non-plugin
edits (output gain, future chart/tempo edits) carry no instance id. The spike passed, so this stays a
contingency.

## Recording Boundary

Recording happens only at user-intent boundaries:

- plugin insert/remove/move handlers;
- placement and display override handlers;
- output-gain gesture/commit handlers;
- plugin parameter edit observer after the adapter completes gesture, debounce, or explicit flush
  grouping.

Pending plugin parameter edits are flushed at a single chokepoint: the controller's action-dispatch
boundary (`prepareAction` / `runAction`), and before save, close, project load/restore, undo, and
redo. Individual edit handlers do not each flush. The chokepoint guarantees that a plugin-editor
tweak made immediately before any command is ordered correctly in history and that a new handler
cannot forget to flush. The flush applies the same classification rules as the debounce path: a
settled/discrete pending edit may become a history entry (its before/after chunk); continuous or
uncertain motion is dropped, its plugin's state baseline is refreshed, and its pending state is
cleared without history. If a flush emits an entry, that entry becomes the newest history entry, and
the command (including an undo/redo request) then proceeds against it.

Low-level apply paths do not record RockHero history. However, some low-level Tracktion operations
may still record into Tracktion's internal undo manager; quarantine covers that separately.

While executing any edit, undo, redo, project load, project close, or restore:

- set a controller/audio-adapter suppression guard;
- ignore emitted parameter edit batches;
- do not push history;
- do not clear redo except when committing a new user edit.

Undo/redo execution uses primitives directly with normal dirty-state recomputation after the stack
transition. Applying a snapshot, placement, override, parameter value, or output gain during
undo/redo must not create a new entry.

## Dirty State

Undo history is the source of truth for edit dirtiness. `m_save_requires_destination` stays the
separate project-lifecycle reason a project needs a destination.

```text
hasUnsavedChanges() == project.has_value() && (edit_dirty || m_save_requires_destination)
```

`edit_dirty` is a pure function of the current history position versus the clean revision,
recomputed after every transition.

Rules:

- Open/restore a saved project: clear history, mark current revision clean.
- Import a song: clear history; `m_save_requires_destination` still forces Save As.
- Successful undoable edit: push one entry, clear redo, recompute dirty.
- Successful Save / Save As: flush pending parameter edits, save, then mark current revision clean.
- Publish does not mark clean.
- Close: flush pending parameter edits before unsaved-change checks, then clear undo/redo if close
  proceeds.
- A rollback-contract violation logs an `Error` diagnostic, enters the faulted state, and marks the
  project dirty; it is not a normal dirty/clean transition. Save and Save As remain disabled while
  faulted so the dirty flag warns that the visible session is not trusted, but does not permit
  persistence from that session.

Bounded depth starts at 100 entries. If pushing past the cap evicts the clean-revision entry, the
project can no longer return to clean by undoing; mark the clean revision unreachable and report
dirty for all positions thereafter.

Migrate the existing `m_has_unsaved_changes` edit sites before enabling Undo/Redo. The Edit menu and
shortcuts must not ship while dirty state still comes from the old flag.

## Core Model

Private editor-core types near the existing workflow classes:

- `EditorUndoHistory`: undo stack, redo stack, clean-revision marker, bounded depth, `canUndo()`,
  `canRedo()`, labels, pending two-phase transitions, and failure/invalidation handling. It stores
  `std::unique_ptr<IEdit>` entries and never inspects their concrete type. (No `remapInstanceId`:
  recreate preserves instance ids — see the representation callout and "Instance-Id Preservation".)
- `IEdit`: the editor-core polymorphic edit interface — `undo(EditorEditContext&)`,
  `redo(EditorEditContext&)`, and `label()`, each apply method returning a typed success/failure
  (including restored id verification for recreate operations). Concrete edits exist for insert,
  remove, move, placement, display-type, plugin-state, and output-gain. Each co-locates its data
  with its own forward/inverse logic; plugin-state edits hold their before/after full plugin chunks
  as members.
- `EditorEditContext`: the apply-time seam handed to every `undo`/`redo` — references to the
  signal-chain model, `IPluginHost`, `ILiveRig`, and an output-gain setter. Edits stay pure value
  objects and never store controller back-pointers.

The history type must be directly testable without `EditorController` and without diagnostics
dependencies. It returns transition results/events that the controller can log, but it does not know
where or whether those events are logged.

## Controller Integration

Public intents:

- `onUndoRequested()`;
- `onRedoRequested()`.

Private actions:

- `EditorAction::Undo`;
- `EditorAction::Redo`.

Availability:

- require an open project;
- require no active busy operation;
- block while the input calibration prompt owns the signal-chain flow;
- when the session is `faulted` (after a rollback-contract violation), block all editing, Undo/Redo,
  Save, and Save As in the same centralized check the busy state uses; leave only
  Open/Import/Restore, Close, and Exit available until a project is loaded or closed;
- undo is available when the session is not faulted and `EditorUndoHistory::canUndo()` or
  `IPluginHost::hasPendingPluginEdits()` is true;
- redo requires a non-faulted session and `canRedo()`;
- close and exit remain the only normal actions that supersede busy work.

Execution:

- the controller subscribes to plugin edit observers and refreshes view state whenever pending state
  changes or a completed batch is emitted;
- pending plugin edits are flushed once at the action-dispatch chokepoint before any command;
- synchronous directions run on the message thread;
- insert-redo and remove-undo run behind `BusyOperation::LoadingPlugin`;
- plugin rebuild undo/redo is not cancellable under the current scan-only cancellation policy;
- stale async completions are rejected with existing busy-token/liveness rules before apply;
- preflight, no-net-mutation, and repaired failures leave history unchanged;
- a rollback-contract violation logs an `Error` diagnostic, enters the faulted state (blocks all
  editing and saving), and prompts the user to report the bug and reopen/close; the app keeps
  running.

Rollback-contract handling (see Failure Philosophy and Logging):

- log one structured `Error` diagnostic with the current action, undo/redo direction, target ids,
  adapter error detail, rollback attempt, busy state, thread-check result, and available snapshots;
- enter the faulted state so the availability check blocks all editing, Undo/Redo, Save, and
  Save As;
- refresh a diagnostic backend view and mark the project dirty without making backend readback
  eligible for persistence;
- show a message that this is an unexpected internal error to report to the developer (with the editor
  log file to attach), leaving reopen/close as the recovery. No abort, no termination hook, no
  synchronous-write-before-exit path.

## Undo Visibility (no silent off-screen undo)

Because there is one unified `Ctrl+Z` across tone, future chart/tablature, and editor-metadata edits,
undo must never silently revert something in a panel or window the user cannot see. Three layers, all
required, address this; each is easier *because* entries are RockHero-owned (the history knows what an
entry is and where it lives — a raw `Edit::undo()` would not):

- **Scope discipline (foundation).** Only user-intent edits are recorded (see Recording Boundary), so
  `Ctrl+Z` can only ever revert something the user deliberately did. No automatic/engine activity is on
  the stack.
- **Labels + transient confirmation.** Every entry carries a human-readable label ("Move Plugin",
  "Amp Sim — Gain"). The Edit menu shows "Undo <label>"/"Redo <label>", and on apply the controller
  surfaces a brief non-modal confirmation ("Undone: <label>"). For plugin-parameter edits the label
  names the actual parameter(s) that moved — see *Plugin-parameter edits* below for how the observation
  layer supplies this. Exact text is not a behavioral contract.
- **Reveal-on-undo (the key one).** Applying undo/redo emits a navigation/selection intent alongside
  the state change (per `architectural-principles.md` "separate state from side effects"): switch to
  the affected domain's panel, select and flash the affected signal-chain block, scroll the timeline to
  the reverted automation point. The undo drives focus to its own target. Each `IEdit` therefore
  exposes enough identity (domain + affected object id) to produce that reveal intent — alongside
  `label()`, e.g. a `revealTarget()` accessor; `EditorView` honors it without owning undo policy.

### Plugin-parameter edits (the closed-window case)

A parameter changed inside a plugin's own floating VST3 editor window, then undone with that window
closed, is the case most at risk of feeling "hidden". The decision: **do not force the plugin's own
GUI open on undo.** Force-opening overrides the user's deliberate close, scales horribly (an undo
chain across several plugins would pop several heavyweight third-party windows), is not how DAWs
behave, and is not what "no hidden undo" requires. "No hidden undo" means the user always knows an
edit was reverted and what it was — not that every third-party plugin GUI must auto-open.

That guarantee is delivered through four channels we control, which together make the closed-window
case non-hidden:

- **Audible.** This is a real-time guitar rig; undoing a tone parameter is heard immediately, not
  buried in a hidden document field.
- **The label names the exact parameter.** The restore payload is the full chunk (for fidelity), but
  the *observation* layer already knows which `AutomatableParameter`(s) fired the settled gesture. The
  parameter-edit observer therefore emits a **display-only label hint** (the changed parameter
  name(s), e.g. "Amp Sim — Gain") alongside the before/after chunks. This is display metadata only — it
  is never used for restore, identity, or remapping — so it does not reintroduce per-parameter
  bookkeeping into the restore path.
- **Reveal-on-undo flashes the affected block** in the signal chain, so the user's eye goes to the
  right plugin.
- **If the window is open, it updates live.** `setPluginState` drives `setStateInformation`, so an
  open editor reflects the restore — **spike-confirmed (2026-06-12):** the on-screen control snapped
  back to the restored value.

Residual (accepted): with the window closed and no RockHero-owned view of that parameter, the user
hears the change and reads "Amp Sim — Gain" but does not *watch* the specific knob rotate unless they
reopen the plugin. This matches every DAW and is closed further by the named label.

Future direction (not required for the first pass): if RockHero grows its **own** selected-block
parameter view (knobs RockHero renders, not the plugin's GUI), reveal-on-undo can show the restored
values there — fully in UI we control, no plugin window needed. That is the ideal end state; record it
as the direction, do not build it before the view exists.

`RecordingEditorController` and UI tests assert the reveal/label intents (including the parameter
label hint) alongside the Undo/Redo requests.

## UI Integration

`EditorViewState` gains:

- `undo_enabled`;
- `redo_enabled`;
- optional labels only if the menu shows command-specific text.

`EditorView`:

- add an Edit menu beside File;
- route Undo/Redo selections to the controller;
- handle `Ctrl+Z` for undo and `Ctrl+Y` for redo in `keyPressed`;
- keep File menu behavior unchanged.

Plugin-window parameter edits need no new UI. Output gain needs drag begin/commit boundaries so a
slider drag yields one entry.

On a rollback-contract violation, the app shows a message that an unexpected internal error occurred
that should be reported to the developer (pointing to the editor log file to attach), and all editing,
Undo/Redo, Save, and Save As go disabled, leaving only Open/Import/Restore, Close, and Exit. The app
keeps running; a trusted state is restored by reopening the project, which rebuilds the live rig from
the saved tone document.

`RecordingEditorController` and UI tests record Undo/Redo requests.

## Shipping Decision

Do not expose partial-coverage undo. A live `Ctrl+Z` that silently skips the user's most recent
project/tone edit is worse than no undo.

Build incrementally, but keep the Edit menu and shortcuts disabled until every scoped category is
covered:

- insert;
- remove;
- move;
- placement;
- display type;
- plugin parameter edits (user plugins only), captured/restored as before/after full chunks, including
  gesture and non-gesture paths, with the self-animating-parameter guard;
- pending parameter flush before other commands;
- output gain;
- dirty tracking.

## Tests

`EditorUndoHistory`:

- push enables undo and disables redo;
- undo-to-redo and redo-to-undo happen only after commit;
- preflight, no-net-mutation, and repaired failures leave stacks unchanged;
- a rollback-contract violation does not commit the stack transition and reports a fault-required
  result/event for the controller to handle;
- push after undo clears redo;
- clean marker tracks push, undo, redo, and mark-clean;
- clean-marker eviction marks clean unreachable;
- reset clears stacks and clean state.

(No id-remap responsibility: recreate preserves instance ids, so stored entries never need
rewriting — see "Instance-Id Preservation".)

Controller:

- insert pushes one entry only on success and stores the returned inserted id;
- insert undo removes;
- insert redo re-inserts from the catalog candidate under the insert edit's stored id (preserved,
  not remapped) and restores its block placement, so a later parameter/display entry on the same
  plugin still resolves;
- remove captures audio and visual state before removing;
- remove capture failure removes nothing and pushes nothing;
- remove undo recreates under the original id (preserved, not remapped) and applies visual state;
- move undo/redo restores order and placement;
- placement/display-type undo/redo restore prior value and dirty state;
- a plugin gesture emits one before/after-chunk parameter entry and undo/redo restore the chunks via
  `setPluginState`;
- non-gesture parameter changes settle into one before/after-chunk entry using the state baseline as
  `before`;
- observation is attached only to user plugins: an output-gain change records exactly one
  output-gain entry and no parameter entry, and input-gain changes record nothing;
- self-animating / continuous non-gesture parameter motion does not create history entries;
- explicit flush drops continuous/uncertain pending parameter motion instead of recording it;
- pending parameter changes flush once at the action-dispatch chokepoint before undo, redo, save,
  close, load, insert, remove, move, placement, display-type, and output-gain commits;
- pending parameter observer notifications refresh Undo availability when pending starts, completes,
  or is dropped;
- output-gain drag pushes one entry;
- plugin-edit ingestion is suppressed during edit/undo/redo;
- undo availability is true while a net-changed plugin edit is pending;
- undo/redo availability appears in view state;
- undo/redo are disabled while busy and while calibration blocks signal-chain edits;
- a rollback-contract violation enters the faulted state (all editing, Undo/Redo, Save, and Save As
  blocked; Open/Import/Restore/Close/Exit allowed) without terminating; its developer-facing
  `RH_LOG_ERROR` record may be asserted with a focused Quill test sink only if proving emission is
  required;
- Save marks clean after flushing pending plugin edits;
- undoing back to saved state clears dirtiness;
- dirty tracking is history-based before Undo/Redo are enabled;
- close/open/import reset history.

Common audio:

- fake `IPluginHost` round-trips `PluginInstanceState`;
- fake candidate insert returns `inserted_instance_id`;
- fake restore can keep or change the id;
- fake attaches parameter observation only to user plugins and emits no edits for structural plugins;
- fake maintains a per-plugin state baseline and uses it as `before` for non-gesture edits;
- fake drops continuous/self-animating non-gesture motion;
- fake refreshes the state baseline when dropping continuous or uncertain pending motion;
- fake flushes pending plugin edits synchronously;
- fake emits pending-state notifications when pending plugin edits start, complete, or are
  dropped;
- fake `setPluginState` round-trips a chunk to an existing instance for undo and redo;
- fake emits a display-only `label_hint` naming the changed parameter(s) on a completed parameter edit;
- fakes cover every undo-wired mutating port's rollback proof: preflight failures, failures repaired
  to exact pre-state, and rollback-contract violations that include diagnostic context;
- fake verifies that expected recoverable failures leave the plugin chain, plugin state, and
  adapter-owned metadata unchanged;
- Tracktion-backed spike verifies callback patterns, observation scoped to user plugins, open-editor
  refresh on chunk restore, and undo quarantine behavior before final wiring.

UI:

- Edit menu shows Undo/Redo from controller-derived enabled state;
- `Ctrl+Z` and `Ctrl+Y` emit intents only when enabled;
- pending plugin edits make Undo enabled after the controller observes pending state;
- dropped pending plugin edits clear pending Undo availability without adding history;
- disabled items/shortcuts emit nothing;
- output-gain drag begin/commit emit the expected intents;
- a rollback-contract violation shows the report-to-developer message, blocks all editing, Save, and
  Save As while leaving Open/Import/Restore/Close/Exit, and does not terminate.

## Implementation Stages

Prerequisite: the durable file-backed editor logger already exists (the Quill-backed `Logger` facade
plus `JuceQuillBridge`), so logs persist. Action/undo-event logging and the rollback-contract
diagnostic are built in the stages below via `RH_LOG_*`, not up front, and need no new logging
interface.

0. Tracktion/JUCE behavior spike: internal undo stack mutation, safe quarantine mechanism, rollback
   feasibility for undo-wired Tracktion mutations, real plugin parameter callback patterns,
   observation scoped to user plugins, open-editor refresh on parameter restore, and safe parameter
   cache initialization. Plugin instance-id preservation on recreate confirmed (2026-06-14): keep a
   regression test that pins it (see "Instance-Id Preservation").
1. `EditorUndoHistory` and direct tests: two-phase commit, unchanged-on-error failures,
   rollback-contract non-commit results, and clean-marker eviction. The history stores
   `std::unique_ptr<IEdit>` and never inspects concrete types; cover `IEdit` undo/redo dispatch with
   stub edits and a fake `EditorEditContext`. Keep the type pure: no logger dependency and no
   controller dependency. Return compact transition results/events for push, undo, redo,
   clean-revision mark, clean-revision eviction, and non-commit failures so the controller can log
   them later and fault the session when required.
2. Update `IPluginHost` contracts: unchanged-on-error mutating methods, explicit insert result id,
   complete `PluginInstanceState`, `setPluginState` (in-place chunk restore), before/after-chunk
   plugin-edit observer/status, and fakes. Write the rollback proof and exact-pre-state tests for
   each mutating port before the controller records that port in user-visible history.
3. Undo/Redo action ids, intents, availability in all switches, view-state fields, Edit menu, and
   `Ctrl+Z` / `Ctrl+Y` routing. Keep user-facing commands disabled until coverage is complete. Log
   action lifecycle at the action gate for all actions (request, availability rejection, start,
   completion, recoverable failure, cancel, stale completion) via `RH_LOG_*` on the
   `editor.controller` category. Log the history transition results/events returned by
   `EditorUndoHistory` here.
4. Lightweight inverse tier with no audio change: block placement and display-type override. Log
   their undo-entry push and undo/redo apply.
5. Move as a compound inverse with placement restore. Log move entries.
6. Plugin edit boundary: per-plugin state baseline, observation scoped to user plugins, gesture-bounded
   before/after-chunk capture, non-gesture debounce settle with the self-animating guard, explicit
   flushing with drop/baseline-refresh semantics at the dispatch chokepoint, pending-state
   notifications, `setPluginState` replay, fake tests, and ingestion suppression. Log plugin edit
   started/completed/flushed/dropped and pending-state changes.
7. Output-gain gesture boundaries and output-gain undo entries. Log output-gain entries.
8. Remove memento boundary: capture/restore, the id-preserving recreate path (restored id equals the
   original), editor visual-state capture/apply, and insert/remove wiring. This renames/recontracts
   the already-implemented `insertPluginState` into `recreatePluginStatePreservingId` rather than
   adding a parallel method, and drops the `PluginInstanceRestoreResult` struct: the recreate path
   returns a bare `PluginChainSnapshot` (the preserved id is guaranteed by contract, so the
   `original_instance_id`/`restored_instance_id` echo is removed). There is no production caller today
   (only the fake/test overrides), so update those tests in the same change and either drop the
   general id-stripping insert path or re-contract it for a future duplication feature. Add rollback-contract handling here (the
   trigger lands in this stage): the `Error` diagnostic, entering the faulted state (block all editing
   + Save/Save As via the centralized availability check), and the report-to-developer +
   reopen/close message.
9. Migrate edit dirtiness onto the history clean marker, preserving `m_save_requires_destination`,
   with the before/after transition test matrix.
10. Enable the user-facing Edit menu/shortcuts once all scoped categories and dirty tracking are
    covered.

## Open Questions / Spikes

### Stage 0 Spike Status (closed for current undo gate)

The Stage 0 / Phase 2 spike ran in two halves. The mechanical half used temporary headless probes;
the interactive half used the full editor with a real VST3 and live input. It is closed for the
current undo gate: T1, T2, and T4 were confirmed; T3 is source-proven and deferred until automation
editing exists; no-gesture/self-animating plugin behavior is deferred until a suitable plugin is
available.

**Historical vehicle (mechanical half, removed):** `rock-hero-common/audio/tests/test_undo_spike.cpp`,
backed by temporary `Engine` probes that exposed internal Tracktion behavior as plain values:
`spikeObserveUndo`, `spikeClearUndoHistory`, `spikeStateRoundTrip`,
`spikeBeginUndoTransaction`, `spikeTracktionUndo`, `spikeTracktionRedo`,
`spikeUserPluginInstanceIds`, and `spikeRawLiveRigPluginRoles`. All of this was fenced with
uppercase `SPIKE` markers and has been removed. The authoritative cleanup ledger is in
`editor-engine-undo-master-plan-v3.md` under "Temporary Spike Code Ledger".

**Historical mechanical run procedure:**

- Build `rock_hero_common_audio_tests`.
- Set `ROCKHERO_SPIKE_PLUGIN` to a real `.vst3` file path (the reference plugin). Without it, the
  plugin-dependent case skips; the no-plugin case still runs.
- Run the `[spike]` cases with Catch2 output shown (e.g. `ctest --output-on-failure` or the test
  binary with `"[spike]" --success`). Observations are emitted via `WARN`, so they appear in the
  report. Paste the output back to record the runtime findings below.

The mechanical half ran on 2026-06-10 against `Archetype Nolly X` (VST3). The harness was adjusted
so that when JUCE's single-file scan returns no candidate it falls back to Tracktion's known-plugin
catalog and selects the candidate whose path matches `ROCKHERO_SPIKE_PLUGIN`; the run otherwise used
the probes as written. Both `[spike]` cases passed.

**Mechanical findings (Step 1, complete):**

- **Tracktion `canUndo()` is useless as a RockHero edit signal (Q1).** A freshly constructed engine
  already reports `canUndo=true` with `storedUnits=408` before any user edit, because Edit
  construction and structural-plugin setup populate the internal manager. RockHero must derive its
  own `canUndo()`/`canRedo()` from `EditorUndoHistory`, never from `Edit::getUndoManager()`. This
  hardens the existing quarantine rule from "preferred" to "required by observation".
- **Every undo-wired operation grows the internal manager (Q1).** In the pre-B cleanup run,
  `storedUnits` rose monotonically:
  insert +208 each, move +64, capture +144, remove +32, reinsert +32, and **output gain +776 per
  change** (same +776 delta with or without plugins loaded). The internal stack grows with each
  operation and is never empty after RockHero operations. (It is **not** truly unbounded: Tracktion
  caps stored units via `undoManager.setMaxNumberOfStoredUnits(1000 * numUndoLevelsToStore, …)` —
  `tracktion_Edit.cpp:643`. An earlier note here said "unbounded"; corrected.) Output gain is
  surprisingly heavy in Tracktion's manager — though note the +776 is largely the lazy structural-plugin
  creation bundled into the first gain command (see master plan v3 Phase B), not the gain value itself.
  The B3 cleaned-base rerun after eager anchors measured output gain at +72 units instead of +776,
  confirming that the lazy-anchor churn was removed. A follow-up fix synchronized
  `LiveRigGainPlugin`'s realtime target after Tracktion-restored `gainDb` ValueTree changes, so
  raw `Edit::undo()` / `Edit::redo()` now restore the project-facing output-gain value.
- **The spike's *unwrapped* operations produced empty `undoDesc`** — but Tracktion transactions
  **can** be labeled via `beginNewTransaction(name)` (the Option B spike's wrapped insert reported
  `undoDesc='rh-insert'`). An earlier note here said transactions are "unlabeled"; corrected. RockHero
  still supplies its own command labels regardless of mechanism.
- **Clearing after owned mutations is validated as safe for quarantine maintenance (Q2).**
  `clearUndoHistory()` reset the manager cleanly (`storedUnits=0`, `canUndo=false`) and the engine
  remained fully usable afterward: a post-clear output-gain edit succeeded and re-grew the stack (to
  72). No crash, no graph breakage, no failure. This makes clear-after-owned-mutation a viable
  adapter-local maintenance tool for the chosen memento/quarantine design, but it is optional:
  Tracktion already bounds the stack and RockHero never consumes it. It must not be used if the
  project later pivots to delegation, which relies on the Tracktion stack surviving. *Caveat:* the
  headless test has no open audio device and does not play, so live-playback-time side effects (graph
  rebuilds mid-transport, plugin-state flush, dirty/change timers) are not fully exercised; watch for
  them during implementation, but no blocker was found.
- **Restore changes the runtime id when the id is stripped; preserves it when kept (Q5).** With the
  id stripped (current `captureActiveRig()` behavior, `engine.cpp:3660`), capture/remove/reinsert
  yielded a new id (1011 -> 1017). Keeping `tracktion::IDs::id` in the state tree preserved the id
  (1017 -> 1017) — but only because the original was removed first, so the id was free. **Decision
  updated 2026-06-14:** undo relies on the id-preserving case through a dedicated recreate path,
  valid only after the original instance is gone. New-instance and future duplication paths must
  still strip ids instead of reusing an existing live id.
- **Full plugin chunks capture cleanly.** The Nolly state tree serialized to ~10 KB
  (`stateBytes=10118`) and round-tripped without error, confirming the remove memento can hold a
  complete external-plugin chunk.
- **Rollback feasibility (Q3) holds structurally.** As read from code, `insertPlugin` instantiates
  and load-checks before insertion and rolls back on later failure (`engine.cpp:3124-3161`);
  `movePlugin` reinserts at the prior index on failure (`engine.cpp:3235-3264`); `removePlugin`
  reinserts the removed plugin if graph rebuild fails (`engine.cpp:3294-3309`). The spike's
  round-trip exercised the capture/remove/reinsert path end to end without error. Per-port written
  rollback proofs remain Phase 4 work.
- **Headless real-plugin instantiation works.** `Archetype Nolly X` instantiated, inserted, moved,
  and round-tripped inside the Catch2 harness with no audio device, so this test vehicle is also
  viable for future adapter coverage.
- **Eager-anchor ordering holds with a real VST3.** The B1 follow-up spike inserted `Archetype
  Nolly X` into a fresh eager chain, appended a second instance, moved one instance, and removed one
  instance while asserting the raw Tracktion plugin-list roles remained
  `[input gain, input meter, user plugins..., output gain, output meter]`.
- **B3 cleaned-base transaction rerun is complete for structural list operations and output gain.**
  Wrapped insert (+208), move (+64), and remove (+32) each behaved as one labeled Tracktion
  transaction at the tree/id level: raw `Edit::undo()` and `Edit::redo()` restored the expected
  user-plugin chain and ids. A manual route repair after raw undo/redo succeeded in the headless
  default-route harness and did not add undo units. Output gain is a +72 labeled value transaction;
  after the `LiveRigGainPlugin` runtime-target synchronization fix, raw undo/redo restores the
  project-facing output-gain value.

**Option B confirming spike (the "why not lean on Tracktion's undo?" challenge), ran 2026-06-10:**

- **clean-pop:** an insert wrapped in one `beginNewTransaction("rh-insert")` (208 units) was undone
  by a single `Edit::undo()` — chain emptied — and `Edit::redo()` restored it **with the same id
  (1011)**. So Tracktion undo is atomic per transaction and id-preserving for an isolated command;
  its best case genuinely works. This contradicted the initial prediction of a partial-state undo.
- **wrong-target:** the original pre-B run used a gain change layered after an insert to show that
  `Edit::undo()` is a positional LIFO pop, not addressable to an arbitrary chosen command. The B3
  cleaned-base rerun corrected the gain-specific interpretation: after the
  `LiveRigGainPlugin` runtime-target synchronization fix, output-gain undo restores the
  project-facing gain value. The old result is evidence only for positional pop behavior, not for
  an output-gain undo failure.
- **Interpretation:** positional-pop alone is not disqualifying (any undo stack pops the most recent
  action). The earlier sweeping reading of this result — that "every ValueTree write makes a
  transaction" and that delegation "fails on unfilterable parameter motion" — was **wrong and has been
  retracted**: transaction boundaries are controllable, and parameter motion is atomic/flush-driven
  (see `undo-ownership-analysis.md`, authoritative for the mechanics). The genuine delegation
  considerations are the 1:1 command-to-transaction discipline (e.g. the capture flush at
  `engine.cpp:3658`) and the behavioral coupling across the `Engine::Impl` boundary. Phase M later
  chose RockHero mementos: the product stack owns tone undo entries, and Tracktion undo stays
  adapter-local.
- **Useful byproduct:** Tracktion preserves item ids across its own undo/redo of an isolated
  transaction. The chosen memento path relies on the same id-preservation primitive through a
  project-owned adapter path, not on Tracktion's undo stack. The finding remains useful evidence
  about Tracktion behavior.

**Step 2 — gesture-callback characterization (T1), ran 2026-06-12 in the full editor with a real VST3
(Archetype Nolly X), one plugin:**

- A single user knob drag emits exactly `parameterChangeGestureBegin` → repeated `parameterChanged`
  (each paired with a `currentValueChanged` carrying the same value) → `parameterChangeGestureEnd`.
  Both slow and fast drags follow this pattern.
- **A paused drag keeps the gesture open:** a ~1.45 s hold mid-drag produced no `gestureEnd` until the
  actual release. **Design consequence:** for gesture-capable plugins, use `gestureEnd` as the settle
  boundary, **not** a time-based debounce (a debounce would wrongly split a paused drag into multiple
  undo entries). Capture the chunk at `gestureBegin` (before) and `gestureEnd` (after) → one undo
  entry per drag, as the Stage 6 design assumed.
- **A trailing lone `parameterChanged` (same value, no `currentValueChanged` pair) can arrive right
  after `gestureEnd`** — a post-gesture value-confirmation, not a new edit. Harmless for the chunk
  design (we capture chunks only at `gestureBegin`/`gestureEnd`; the state at `gestureEnd` already
  equals the settled value). **Design rule:** suppress the non-gesture fallback path for a parameter
  while its gesture is open and for a brief window after `gestureEnd`, so this tail is not mis-read as
  a fresh non-gesture edit (the net-change-vs-baseline guard is a second safety net).
- **No self-animation:** the plugin emits no parameter callbacks when idle. The self-animating guard
  is still kept defensively for LFO/meter-param plugins, but this plugin does not exercise it.
- Caveat: one (well-behaved) plugin characterized; no LFO/no-gesture plugins available to test. The
  *no-gesture* and *self-animating* fallback paths remain empirically uncharacterized; the design
  handles both conservatively regardless. **T1 is otherwise conclusive: rely on `gestureEnd` as the
  settle boundary and capture before/after chunks at the gesture edges.**

**Step 2 — open-window refresh (T2), confirmed 2026-06-12 (Archetype Nolly X):** a full-chunk restore
through `setStateInformation` (the `setPluginState` path) **does refresh an open plugin editor window** —
the on-screen control visibly snapped back to the restored value. So reveal-on-undo's strong
open-window feedback happens for free on a chunk restore; no extra nudge is needed for the open-window
case.

**Step 2 — automation restore (T3), decision 2026-06-12: no headless probe built.** The claim that
matters (live audio follows an automation-curve restore) needs playback to exercise the audio-thread
`AutomationIterator`, and cannot be exercised in the editor either because automation editing does not
exist yet (no UI to author a curve). A headless probe could only confirm near-tautologies (a
`nullptr`-undo-manager curve edit does not record undo — true by definition; the curve *model* follows
a tree mutation — trivial), so it would add blind-API risk for no real assurance. Status: the
mechanism is **source-proven** (`undo-ownership-analysis.md`: parameter `valueTreeChild` listeners →
`triggerAsyncIteratorUpdate`), the design reuses Tracktion's own curve API + reactive rebuild, and
**empirical verification is deferred to when automation editing is built** (well after the first undo
scope). Automation is out of the MVP undo scope regardless.

**Step 2 — live-device route repair (T4), confirmed 2026-06-12 with a real device + guitar:**
live-input monitoring survived **insert, move, remove, and an audio-device switch** with no routing
failure — on par with REAPER. The only audible gap is the brief cutout while a heavy plugin
*instantiates* on insert (plugin load latency, not a route failure; expected, and present in every
host). This aligns with the design: the plugin-instantiating undo directions (insert-redo,
remove-undo) are already fenced behind `BusyOperation::LoadingPlugin`. This confirms the B2-lite
synchronous reroute — which memento undo restore re-uses through the same insert/move/remove paths —
holds up live.

**Step 2 is complete (T1, T2, T4 confirmed; T3 source-proven + deferred). The spike code is removed
per the cleanup ledger.**

- **Validated (Step 1):** clearing Tracktion undo history after owned mutations is mechanically safe
  in the tested cases, but active clearing is not required by the chosen memento mechanism. Tracktion
  already bounds the stack via `maxNumberOfStoredUnits`; RockHero never consumes it as product history.
- **Deferred targeted plugin-behavior follow-up:** when a suitable plugin is available, characterize
  no-gesture and self-animating/continuous parameter behavior (LFOs, envelopes, meters). The current
  implementation should keep the conservative rule: drop continuous/uncertain non-gesture motion
  rather than recording spurious history entries.
- **Deferred parameter-diversity follow-up:** as more target VST3 plugins become available, sample
  whether they emit gesture begin/end, raw changes only, or both. This is no longer a Stage 0 blocker;
  the design already has a gesture path and a conservative non-gesture fallback.
- **Deferred open-window diversity follow-up:** `setPluginState` refreshed Archetype Nolly X's open
  editor window. Recheck with other target plugins opportunistically, but the undo policy does not
  depend on a guaranteed visual refresh across every vendor editor.
- **Deferred debounce tuning:** tune the non-gesture debounce window when a real no-gesture plugin is
  available. Until then, keep the fallback conservative and prefer dropping uncertain motion.
- **Deferred automation follow-up:** empirically verify automation-curve restore audibility once
  automation editing exists in the application. Automation is outside the current undo scope, and the
  current mechanism is source-proven through Tracktion's curve/listener path.
- **Resolved (Step 1):** Tracktion preserves item/runtime ids only when the id is left in the state
  tree and the original is gone. The current general `insertPluginState` strips ids and gets a new
  id by design; undo uses a separate id-preserving recreate path without remapping ids.
- **Resolved (Step 1):** Remove memento captures a full external-plugin chunk fine (~10 KB for
  Nolly), round-tripped without error.
- Are command labels ("Undo Move Plugin") worth the first pass, or are booleans enough? (Tracktion
  supplies no labels of its own — confirmed in Step 1.)

## Non-Goals

- Do not expose Tracktion/JUCE plugin objects through editor-core.
- Do not expose Tracktion's `UndoManager` as RockHero undo/redo.
- Do not make input gain undoable; it is per-user input config, not project tone state.
- Do not make input calibration undoable in the project stack.
- Do not attach parameter observation to structural live-rig plugins (gain, meters, input gain).
- Do not implement chart/note undo before chart editing exists.
- Do not build a broad command framework that replaces `EditorController`.
- Do not treat history invalidation as acceptable routine UX.
- Do not change durable `docs/design/` documents from this plan unless the final implementation
  proves out and the user explicitly wants the design docs changed.
