# Editor Undo/Redo Plan

Status: in-progress planning note. Consolidated from the v9 review draft and supersedes the
earlier numbered drafts that existed during plan review. This version keeps the faulted-session
failure policy, fixes diagnostics ownership so `EditorUndoHistory` stays pure and directly
testable, clarifies that undo-specific diagnostics are introduced with the undo stages that need
them, and tightens faulted-session persistence so the app does not save from an untrusted live
backend.

> **Provisional relative to master plan v3 (2026-06-10).** This document predates
> `editor-engine-undo-master-plan-v3.md` and `undo-ownership-analysis.md`. Per v3, the undo **inverse
> mechanism** (delegate audio inverses to Tracktion `Edit::undo()` vs. RockHero local mementos vs. a
> hybrid) is **open until the Phase M decision gate**, taken after the Phase B baseline cleanup. The
> sections below describing memento capture/replay and Tracktion-undo *quarantine* therefore describe
> **one candidate mechanism**, not a locked choice. For the corrected Tracktion mechanics (transaction
> grouping, parameter flush, what actually records to the undo manager), `undo-ownership-analysis.md`
> is authoritative; where this document conflicts with it, that document wins until this one is
> rewritten to match the Phase M decision. What *is* settled regardless of mechanism: RockHero owns the
> stack/ordering/labels/dirty/failure policy.

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
  batch started/completed/flushed/dropped; rollback attempt started/completed; runtime id remapped;
  clean revision marked or made unreachable by eviction.

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

### Inverse mechanism: open until Phase M (see undo-ownership-analysis.md)

An earlier version of this section argued against Tracktion delegation using mechanics that were later
**disproved** — "every ValueTree write creates a transaction" and "Tracktion records all
`AutomatableParameter` motion indistinguishably" are both **false** (transaction boundaries are
controllable via the idle timer + `UndoTransactionInhibitor`; live parameter values are atomics and
reach the undoable tree only via an explicit chunk flush). That reasoning has been removed.

The accurate, source-grounded treatment now lives in `undo-ownership-analysis.md`, and the decision is
deferred to the **Phase M gate** in `editor-engine-undo-master-plan-v3.md`, taken after the Phase B
baseline cleanup. In brief, what is established:

- For a single isolated command wrapped in one `beginNewTransaction`, `Edit::undo()` is clean and
  id-preserving (spike: insert undone to empty chain, redone with the same id 1011).
- The real cost of delegation is not "pollution everywhere": it is the standing 1:1 command↔transaction
  discipline (e.g. the capture/save flush at `engine.cpp:3658` writes undoable chunk actions) and that
  it couples editor-core undo correctness to Tracktion's undo *behavior* across the `Engine::Impl`
  boundary (which hides types, not behavior).
- Parameter undo is achievable either way — Tracktion-side via a gesture-settle chunk flush, or
  RockHero-side via capture/replay; this is part of the Phase M comparison.

Settled regardless of the choice: RockHero owns the user-visible stack (ordering, labels, clean
revision, failure policy, cross-domain ordering). The memento capture/replay and quarantine details in
the following sections are the **memento-mechanism candidate**, retained for when that option is chosen
at Phase M.

## Tracktion Undo Quarantine

> *Provisional — applies if the **memento** mechanism is chosen at Phase M. If audio inverses are
> delegated to `Edit::undo()` instead, this section is replaced by a delegation design.*

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
- Suppress RockHero parameter-edit ingestion while RockHero is applying an edit, undo, or redo.

Preferred implementation shape:

- Keep quarantine adapter-local if possible. The editor should not need a Tracktion-shaped
  `clearUndoManager` port.
- If the Tracktion adapter can safely call `Edit::getUndoManager().clearUndoHistory()` after
  RockHero-owned mutations, do that inside the adapter after the mutation has completed and state
  has been refreshed.
- If clearing has side effects, do not clear periodically as a fallback. Periodic clearing has the
  same safety burden as immediate clearing, but with worse observability. Instead, prefer one of
  these proven-safe options:
  - inhibit Tracktion transaction creation for RockHero-owned adapter mutations if Tracktion exposes
    a scoped mechanism for that;
  - bound Tracktion `UndoManager` storage to the smallest practical internal history depth if that
    cap has no user-visible side effects;
  - leave the internal stack inaccessible and document why its memory growth is bounded by the
    adapter's operation shape.

The key invariant is that RockHero never consumes Tracktion's internal stack as user history.

Required spike before broad implementation:

1. Log whether `edit.getUndoManager().canUndo()` changes after insert, move, remove, output gain,
   plugin parameter changes, plugin-state capture, and plugin-state restore.
2. Verify whether clearing Tracktion undo history after those operations affects playback graph
   rebuilds, plugin state flush, dirty/change timers, or subsequent plugin operations.
3. Verify one real VST3 plugin's knob-change callback pattern: gesture begin/end, raw
   `parameterChanged`, or both.

Spike outcome (see Stage 0 Spike Status): items 1-2 ran on 2026-06-10 and **validated that clearing
Tracktion undo history after owned mutations is safe** (it reset the manager cleanly and left the
engine usable). This is a candidate quarantine mechanism for the **memento path only** — it is **not
chosen** until the Phase M gate, and would be actively wrong under delegation (clearing would destroy
the very Tracktion stack delegation relies on). Item 3 (real knob-change callback pattern) is the
pending Step 2.

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
5. Mutate the pending entry payload if capture or id remapping changed it.
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

The audio adapter emits completed parameter edit batches to editor-core. Editor-core stores them
opaquely and replays them through `IPluginHost::setPluginParameterValues`.

The adapter maintains a last-known normalized value cache for every observed plugin parameter:

- initialize the cache when a user plugin enters the chain, after project load/restore, after
  `insertPluginState`, after candidate insert, and after any full plugin-state refresh;
- update the cache after every observed user parameter change;
- update the cache after dropped or uncertain non-gesture changes using the accepted current value
  or readback before clearing their pending state;
- update the cache after host-driven `setPluginParameterValues` while recording is suppressed;
- discard cached values when a plugin leaves the chain.

Gesture path (the reliable source):

- On `parameterChangeGestureBegin`, capture `before` from the last-known cache, falling back to a
  direct current-value read if the cache is missing.
- On `parameterChanged`, update the in-progress `after` value.
- On `parameterChangeGestureEnd`, emit one batch if the value net-changed, then update the cache.

Non-gesture fallback (best effort, must stay conservative):

- If `parameterChanged` arrives with no open gesture and recording is not suppressed, read `before`
  from the last-known cache, not from the already-updated Tracktion parameter.
- Start or update a short debounce window for that plugin instance, capturing each touched
  parameter's first cached value as `before` and latest callback value as `after`.
- Emit a batch only when motion settles: the debounce fires after no further change for that plugin
  arrives within the window.
- Explicit flush is a classification point, not a bypass around the self-animating guard. It emits
  only pending parameters that the adapter can classify as settled/discrete user edits. It drops
  continuous, high-rate, meter-like, or uncertain parameters, advances their last-known cache to the
  accepted current value or readback, and clears their pending state without history.
- **Guard against self-animating parameters.** Plugins can move their own visible parameters (LFOs,
  envelopes, MIDI-learn sweeps, meter-like values). That motion must not become history:
  - a parameter that changes continuously never settles, so it is never recorded;
  - additionally drop parameters that change at a high sustained rate, or that the adapter can
    identify as non-user-writable / meter-like;
  - prefer dropping an uncertain non-gesture change over recording a spurious one. The gesture path
    remains the trustworthy source; the non-gesture path exists only so discrete randomize/preset
    actions are not silently lost.

Replay note: undo/redo through `setPluginParameterValues` must drive the value through Tracktion so
an open plugin editor window reflects it. Verify against real VST3 plugins in the stage-0 spike.

Suppression:

- Ignore parameter callbacks while RockHero is applying an edit, undo, redo, plugin restore, project
  load, project close, or any other host-driven state application.
- Ignore automation-originated callbacks if Tracktion exposes enough context to distinguish them.
- If automation origin cannot be distinguished reliably, prefer suppressing only during known
  RockHero applies and record user-origin plugin editor changes.

Parameter identity:

- Store stable plugin-provided parameter ids when available.
- If Tracktion/JUCE falls back to a numeric/index id, treat it as session-local. It is valid for
  undo entries inside the current open project, but it should not become a durable project-file
  contract.

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
| Plugin parameter batch | set values to before | set values to after | instance id + parameter values | sync |
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
- Candidate insert returns the inserted runtime id explicitly. Insert-redo remaps from the insert
  entry's stored id to the returned `inserted_instance_id`.
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

struct PluginInstanceRestoreResult
{
    PluginChainSnapshot snapshot;
    std::string original_instance_id;
    std::string restored_instance_id;
};

struct PluginParameterValueChange
{
    std::string param_id;
    float before{};
    float after{};
};

struct PluginParameterTargetValue
{
    std::string param_id;
    float value{};
};

struct PluginParameterEdit
{
    std::string instance_id;
    std::vector<PluginParameterValueChange> values;
};

struct PluginParameterEditObserver
{
    std::function<void(bool)> pending_changed;
    std::function<void(PluginParameterEdit)> edit_completed;
};

[[nodiscard]] virtual std::expected<PluginInsertResult, PluginHostError> insertPlugin(
    const PluginCandidate& plugin_candidate, std::size_t chain_index) = 0;

[[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> movePlugin(
    const std::string& instance_id, std::size_t destination_index) = 0;

[[nodiscard]] virtual std::expected<PluginChainSnapshot, PluginHostError> removePlugin(
    const std::string& instance_id) = 0;

[[nodiscard]] virtual std::expected<PluginInstanceState, PluginHostError>
capturePluginState(const std::string& instance_id) = 0;

[[nodiscard]] virtual std::expected<PluginInstanceRestoreResult, PluginHostError>
insertPluginState(const PluginInstanceState& state, std::size_t chain_index) = 0;

[[nodiscard]] virtual std::expected<void, PluginHostError> setPluginParameterValues(
    const std::string& instance_id,
    std::span<const PluginParameterTargetValue> values) = 0;

virtual void flushPendingPluginParameterEdits() = 0;
[[nodiscard]] virtual bool hasPendingPluginParameterEdits() const = 0;
virtual void setPluginParameterEditObserver(PluginParameterEditObserver observer) = 0;
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
- `insertPluginState` may preserve the original runtime id, but callers must treat the returned
  mapping as authoritative.
- The parameter-edit observer is attached only to user-inserted plugins' automatable parameters.
  Structural live-rig plugins are not attached (the gain plugin has no automatable parameter, and
  meters/input gain are not user tone edits).
- Capture, restore, parameter apply, and pending-parameter flush are message-thread operations.
- The parameter-edit observer is the single notification surface for this stream. It reports
  pending-state changes and completed edit batches so editor-core can keep command availability in
  sync with gesture/debounce state.
- `flushPendingPluginParameterEdits` synchronously emits settled/discrete pending edits, drops
  continuous or uncertain pending edits, updates last-known caches, clears pending state, and sends
  a pending-state notification when the aggregate pending state changes.
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
- `insertPluginState`: preflight memento validity, destination index, and plugin cap. Capture the
  pre-chain state before restore begins. If state restore, insertion, metadata refresh, or graph
  rebuild fails, remove any partial plugin and restore the pre-chain snapshot before returning an
  ordinary error.
- `setPluginParameterValues`: preflight the target instance and every parameter id before setting
  anything. Capture every before-value. Apply values as one logical batch. If any set or required
  readback fails, restore all captured before-values and verify them before returning an ordinary
  error.
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
- After successful recreate, remap `instance_id` to `restored_instance_id`.
- Apply the restored chain snapshot first, because backend order/metadata are authoritative.
- Apply block placement and display override after the snapshot refresh using the restored id.

## Instance-Id Remapping

A recreate may yield a new runtime id, and editor state keys on runtime `instance_id`.
`EditorUndoHistory` therefore needs a tested `remapInstanceId(old_id, new_id)` that updates every
payload storing an instance id across both stacks and the active pending entry.

Both instantiating directions trigger a remap:

- remove-undo uses `insertPluginState`'s `original_instance_id` -> `restored_instance_id` mapping;
- insert-redo uses `insertPlugin`'s `inserted_instance_id`.

Payloads include:

- insert/remove/move ids;
- the insert entry's stored block placement and remove-on-undo target id;
- plugin visual state;
- display-type override ids;
- plugin parameter batch ids;
- ids nested inside placement vectors.

`old_id == new_id` is a no-op. Tests must cover changed-id restore for both remove-undo and
insert-redo with a fake `IPluginHost`.

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
cannot forget to flush. The flush applies the same classification rules as the debounce path:
settled/discrete pending values may become a history batch; continuous or uncertain values are
dropped, their last-known cache is advanced, and their pending state is cleared without history. If
a flush emits a batch, that batch becomes the newest history entry, and the command (including an
undo/redo request) then proceeds against it.

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
  `canRedo()`, labels, pending two-phase transitions, failure/invalidation handling, and
  `remapInstanceId`.
- `EditorUndoEntry`: a variant of insert, remove, move, placement, display-type,
  plugin-parameter batch, and output-gain payloads.
- Payload structs for insert state, remove recreate state, plugin visual state, move state,
  placement state, display-type state, parameter batch state, and output-gain state.

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
  `IPluginHost::hasPendingPluginParameterEdits()` is true;
- redo requires a non-faulted session and `canRedo()`;
- close and exit remain the only normal actions that supersede busy work.

Execution:

- the controller subscribes to the plugin-parameter edit observer and refreshes view state whenever
  pending state changes or a completed batch is emitted;
- pending parameter edits are flushed once at the action-dispatch chokepoint before any command;
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
- plugin parameter batches (user plugins only), including gesture and non-gesture paths, with the
  self-animating-parameter guard;
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
- reset clears stacks and clean state;
- `remapInstanceId` updates every payload, including nested placement ids and parameter batches.

Controller:

- insert pushes one entry only on success and stores the returned inserted id;
- insert undo removes;
- insert redo re-inserts from the catalog candidate, restores its block placement, and remaps ids
  from the explicit returned id (including a later parameter/display entry on the same plugin
  retargeting to the new id);
- remove captures audio and visual state before removing;
- remove capture failure removes nothing and pushes nothing;
- remove undo recreates, applies visual state, and remaps ids;
- move undo/redo restores order and placement;
- placement/display-type undo/redo restore prior value and dirty state;
- plugin gesture emits one parameter batch and undo/redo set values;
- non-gesture parameter changes use cached prior values and coalesce into a batch;
- observation is attached only to user plugins: an output-gain change records exactly one
  output-gain entry and no parameter batch, and input-gain changes record nothing;
- self-animating / continuous non-gesture parameter motion does not create history entries;
- explicit flush drops continuous/uncertain pending parameter motion instead of recording it;
- pending parameter changes flush once at the action-dispatch chokepoint before undo, redo, save,
  close, load, insert, remove, move, placement, display-type, and output-gain commits;
- pending parameter observer notifications refresh Undo availability when pending starts, completes,
  or is dropped;
- output-gain drag pushes one entry;
- parameter-edit ingestion is suppressed during edit/undo/redo;
- undo availability is true while a net-changed parameter batch is pending;
- undo/redo availability appears in view state;
- undo/redo are disabled while busy and while calibration blocks signal-chain edits;
- a rollback-contract violation enters the faulted state (all editing, Undo/Redo, Save, and Save As
  blocked; Open/Import/Restore/Close/Exit allowed) without terminating; its developer-facing
  `RH_LOG_ERROR` record may be asserted with a focused Quill test sink only if proving emission is
  required;
- Save marks clean after flushing pending parameters;
- undoing back to saved state clears dirtiness;
- dirty tracking is history-based before Undo/Redo are enabled;
- close/open/import reset history.

Common audio:

- fake `IPluginHost` round-trips `PluginInstanceState`;
- fake candidate insert returns `inserted_instance_id`;
- fake restore can keep or change the id;
- fake attaches parameter observation only to user plugins and emits no batches for structural
  plugins;
- fake maintains last-known parameter values for non-gesture edits;
- fake drops continuous/self-animating non-gesture motion;
- fake advances last-known cache when dropping continuous or uncertain pending values;
- fake flushes pending plugin parameter edits synchronously;
- fake emits pending-state notifications when pending parameter edits start, complete, or are
  dropped;
- fake applies parameter values for undo and redo;
- fakes cover every undo-wired mutating port's rollback proof: preflight failures, failures repaired
  to exact pre-state, and rollback-contract violations that include diagnostic context;
- fake verifies that expected recoverable failures leave the plugin chain, parameter values, and
  adapter-owned metadata unchanged;
- Tracktion-backed spike verifies callback patterns, observation scoped to user plugins, open-editor
  refresh on parameter restore, and undo quarantine behavior before final wiring.

UI:

- Edit menu shows Undo/Redo from controller-derived enabled state;
- `Ctrl+Z` and `Ctrl+Y` emit intents only when enabled;
- pending parameter edits make Undo enabled after the controller observes pending state;
- dropped pending parameter edits clear pending Undo availability without adding history;
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
   cache initialization.
1. `EditorUndoHistory` and direct tests: two-phase commit, unchanged-on-error failures,
   rollback-contract non-commit results, clean-marker eviction, and `remapInstanceId`. Keep the type
   pure: no logger dependency and no controller dependency. Return compact
   transition results/events for push, undo, redo, clean-revision mark, clean-revision eviction, id
   remap, and non-commit failures so the controller can log them later and fault the session when
   required.
2. Update `IPluginHost` contracts: unchanged-on-error mutating methods, explicit insert result id,
   complete `PluginInstanceState`, parameter target values, pending-parameter observer/status, and
   fakes. Write the rollback proof and exact-pre-state tests for each mutating port before the
   controller records that port in user-visible history.
3. Undo/Redo action ids, intents, availability in all switches, view-state fields, Edit menu, and
   `Ctrl+Z` / `Ctrl+Y` routing. Keep user-facing commands disabled until coverage is complete. Log
   action lifecycle at the action gate for all actions (request, availability rejection, start,
   completion, recoverable failure, cancel, stale completion) via `RH_LOG_*` on the
   `editor.controller` category. Log the history transition results/events returned by
   `EditorUndoHistory` here.
4. Lightweight inverse tier with no audio change: block placement and display-type override. Log
   their undo-entry push and undo/redo apply.
5. Move as a compound inverse with placement restore. Log move entries.
6. Parameter boundary: last-known cache, observation scoped to user plugins, gesture grouping,
   non-gesture debounce grouping with the self-animating guard, explicit flushing with drop/cache
   semantics at the dispatch chokepoint, pending-state notifications, fake tests, and ingestion
   suppression. Log parameter batch started/completed/flushed/dropped and pending-state changes.
7. Output-gain gesture boundaries and output-gain undo entries. Log output-gain entries.
8. Remove memento boundary: capture/restore, restore result/id mapping, editor visual-state
   capture/apply, insert/remove wiring, and id remapping. Add rollback-contract handling here (the
   trigger lands in this stage): the `Error` diagnostic, entering the faulted state (block all editing
   + Save/Save As via the centralized availability check), and the report-to-developer +
   reopen/close message.
9. Migrate edit dirtiness onto the history clean marker, preserving `m_save_requires_destination`,
   with the before/after transition test matrix.
10. Enable the user-facing Edit menu/shortcuts once all scoped categories and dirty tracking are
    covered.

## Open Questions / Spikes

### Stage 0 Spike Status (in progress)

The Stage 0 / Phase 2 spike runs in two halves. The mechanical half is built; the interactive
parameter half is not yet.

**Vehicle (mechanical half):** `rock-hero-common/audio/tests/test_undo_spike.cpp`, backed by
temporary `Engine` probes that expose internal Tracktion behavior as plain values:
`spikeObserveUndo`, `spikeClearUndoHistory`, `spikeStateRoundTrip`,
`spikeBeginUndoTransaction`, `spikeTracktionUndo`, `spikeTracktionRedo`,
`spikeUserPluginInstanceIds`, and `spikeRawLiveRigPluginRoles`. All of this is fenced with
uppercase `SPIKE` markers and must be removed when the spike closes. The authoritative cleanup
ledger is in `editor-engine-undo-master-plan-v3.md` under "Temporary Spike Code Ledger".

**How to run:**

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
- **Every undo-wired operation grows the internal manager (Q1).** `storedUnits` rose monotonically:
  insert +208 each, move +64, capture +144, remove +32, reinsert +32, and **output gain +776 per
  change** (same +776 delta with or without plugins loaded). The internal stack grows with each
  operation and is never empty after RockHero operations. (It is **not** truly unbounded: Tracktion
  caps stored units via `undoManager.setMaxNumberOfStoredUnits(1000 * numUndoLevelsToStore, …)` —
  `tracktion_Edit.cpp:643`. An earlier note here said "unbounded"; corrected.) Output gain is
  surprisingly heavy in Tracktion's manager — though note the +776 is largely the lazy structural-plugin
  creation bundled into the first gain command (see master plan v3 Phase B), not the gain value itself.
- **The spike's *unwrapped* operations produced empty `undoDesc`** — but Tracktion transactions
  **can** be labeled via `beginNewTransaction(name)` (the Option B spike's wrapped insert reported
  `undoDesc='rh-insert'`). An earlier note here said transactions are "unlabeled"; corrected. RockHero
  still supplies its own command labels regardless of mechanism.
- **Clearing after owned mutations is validated as safe (memento/quarantine path only; Q2).**
  `clearUndoHistory()` reset the manager cleanly (`storedUnits=0`, `canUndo=false`) and the engine
  remained fully usable afterward: a post-clear output-gain edit succeeded and re-grew the stack (to
  72). No crash, no graph breakage, no failure. This makes clear-after-owned-mutation a viable
  quarantine mechanism **if mementos are chosen at Phase M** — it must not be used under delegation,
  which relies on the Tracktion stack surviving. *Caveat:* the headless test has no open audio device
  and does not play, so live-playback-time side effects (graph rebuilds mid-transport, plugin-state
  flush, dirty/change timers) are not fully exercised; watch for them during implementation, but no
  blocker was found.
- **Restore changes the runtime id when the id is stripped; preserves it when kept (Q5).** With the
  id stripped (current `captureActiveRig()` behavior, `engine.cpp:3660`), capture/remove/reinsert
  yielded a new id (1011 -> 1017). Keeping `tracktion::IDs::id` in the state tree preserved the id
  (1017 -> 1017) — but only because the original was removed first, so the id was free. **Decision
  stands: support changed-id restore unconditionally** (it is the only collision-safe option while
  an original may still exist); id preservation is possible but not relied upon.
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

**Option B confirming spike (the "why not lean on Tracktion's undo?" challenge), ran 2026-06-10:**

- **clean-pop:** an insert wrapped in one `beginNewTransaction("rh-insert")` (208 units) was undone
  by a single `Edit::undo()` — chain emptied — and `Edit::redo()` restored it **with the same id
  (1011)**. So Tracktion undo is atomic per transaction and id-preserving for an isolated command;
  its best case genuinely works. This contradicted the initial prediction of a partial-state undo.
- **wrong-target:** with a gain change layered after the insert (separate transaction), one
  `Edit::undo()` reverted the gain (0 -> -5 -> 0) and **left the plugin in place** — confirming
  `Edit::undo()` is a positional LIFO pop, not addressable to a chosen command.
- **Interpretation:** positional-pop alone is not disqualifying (any undo stack pops the most recent
  action). The earlier sweeping reading of this result — that "every ValueTree write makes a
  transaction" and that delegation "fails on unfilterable parameter motion" — was **wrong and has been
  retracted**: transaction boundaries are controllable, and parameter motion is atomic/flush-driven
  (see `undo-ownership-analysis.md`, authoritative for the mechanics). The genuine delegation
  considerations are the 1:1 command↔transaction discipline (e.g. the capture flush at
  `engine.cpp:3658`) and the behavioral coupling across the `Engine::Impl` boundary. **What is settled
  is only that RockHero owns the stack; the inverse mechanism is *not* decided here — it is deferred
  to the Phase M gate** in `editor-engine-undo-master-plan-v3.md`.
- **Useful byproduct:** Tracktion preserves item ids across its own undo/redo of an isolated
  transaction. Whether we rely on it depends on the Phase M mechanism choice; either way it confirms
  id stability is a Tracktion capability, not an accident.

**Still open — interactive half (Step 2), not yet built:** real VST3 parameter-callback
characterization (gesture begin/end vs. raw `parameterChanged`, self-animating/continuous params)
and whether undo through `setPluginParameterValues` refreshes an open plugin window (Q4). This needs
temporary `AutomatableParameter`-listener instrumentation driven by hand in the running editor and
is the true long pole; it gates Phase 8 parameter work. Phase 2 is not complete until Step 2 runs.

- **Validated (Step 1), not chosen:** clearing Tracktion undo history after owned mutations is
  mechanically safe and is the candidate quarantine for the **memento path** — deferred to Phase M, and
  wrong under delegation. (Tracktion already bounds the stack via `maxNumberOfStoredUnits`; clearing
  just resets it.)
- How reliably can the adapter distinguish user-driven parameter edits from a plugin animating its
  own parameters (LFOs, envelopes, meters) on the non-gesture path? The conservative default is to
  drop continuous/uncertain motion; Step 2 should characterize real VST3 behavior.
- Do real VST3 plugins commonly emit gesture begin/end, raw changes only, or both? (Step 2)
- Does undo of a parameter through `setPluginParameterValues` update an open plugin editor window
  across the VST3 plugins we target? (Step 2)
- What debounce window gives acceptable grouping for non-gesture plugin changes without delaying
  undo availability noticeably? (Step 2)
- **Resolved (Step 1):** `insertPluginState` can preserve Tracktion item/runtime ids only when the
  id is left in the state tree and the original is gone; current capture strips it, so restore gets
  a new id. Support changed-id restore unconditionally.
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
