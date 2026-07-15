# Undo Ownership Analysis: RockHero stack vs. Tracktion UndoManager

Status: **DECISION RECORDED (2026-06-11): RockHero mementos; Tracktion is a backend.** This document
captures the source-level investigation of how Tracktion's undo actually works that drove the choice.
The analysis below is retained as the rationale of record; the recommendation is now settled (see
"Decision" near the end and `editor-engine-undo-master-plan-v3.md` Phase M).

All line references are into `external/tracktion_engine/...` or `rock-hero-common/audio/src/engine.cpp`
as noted, valid at the submodule revision present on 2026-06-10.

## Why this exists

The ownership question was challenged: Tracktion's `UndoManager` already exists and is tested, so do
not reinvent it without cause. An earlier round of reasoning rejected Tracktion undo using claims
that turned out to be **wrong on the mechanics**. This analysis corrects them from source and lays
out the real decision space.

## Corrected facts (verified against source)

Earlier claims that were **false**, now corrected:

1. **"Every ValueTree write creates a transaction."** False. Undoable actions accumulate into the
   *current* transaction. A new transaction begins only when activity settles: `UndoTransactionTimer`
   starts a 350 ms timer on each change and, on fire, if `numUndoTransactionInhibitors == 0` and the
   mouse is up, calls `beginNewTransaction()` (`tracktion_Edit.cpp:41-50`). Boundaries are therefore
   controllable, and `Edit::UndoTransactionInhibitor` is a public RAII to suppress them
   (`tracktion_Edit.h:340-359`, impl `tracktion_Edit.cpp:1276-1278`).
2. **"Parameter motion floods undo (every LFO tick an undo step)."** False. The live value is
   `std::atomic<float> currentValue`, updated with no tree write (`tracktion_AutomatableParameter.cpp:401`,
   `1393-1399`). Only automation-*curve* edits pass `&edit.getUndoManager()` (line 1440). Plugin state
   reaches the tree only via explicit `flushPluginStateToValueTree()` (`tracktion_ExternalPlugin.cpp:1000`,
   passing the undo manager at 1004/1031), which is **not** on a recurring timer — the only
   `ExternalPlugin` timer is an async *deleter* (`tracktion_ExternalPlugin.cpp:178-201`).
3. **"Tracktion transactions are unlabeled."** False — artifact of passing no name. `beginNewTransaction(name)`
   stores a label; the spike's wrapped insert reported `undoDesc='rh-insert'`.
4. **"Restore can't preserve ids."** False — the spike's `Edit::redo()` restored item id `1011`.

Confirmed facts that matter:

5. `Edit::undo()/redo()` are first-class public ops and the actual user-undo in Waveform (Tracktion's
   own DAW): `Edit::undoOrRedo` stops recording, calls `undoManager.undo()/redo()`, and refreshes
   selection (`tracktion_Edit.cpp:1256-1274`).
6. The audio node graph **self-heals** from child **add/remove** and property changes: those tree
   events make listeners call `Edit::restartPlayback()`, which schedules an async (1 ms timer) rebuild
   of the playback context (`tracktion_Edit.cpp:1206-1212`). **Caveat: a pure child *reorder* is the
   exception** — Tracktion's edit watcher `valueTreeChildOrderChanged` is a no-op
   (`tracktion_Edit.cpp:378`), so a plugin move may revert order without rebuilding the graph; it must
   be handled/verified specially (see master plan v3 B3). So "self-heals" is not true for *all* tree
   changes, and Option A is not as uniformly safe as add/remove alone would suggest.
   Plugin object/instance lifecycle is driven by tree observation, so undo of an insert destroys the
   plugin and redo recreates it from its state chunk (spike-confirmed: redo restored a working plugin).
7. Undo depth is bounded: `undoManager.setMaxNumberOfStoredUnits(1000 * numUndoLevelsToStore, numUndoLevelsToStore)`
   (`tracktion_Edit.cpp:643`). The ~408-unit fresh baseline is Tracktion's own default track/plugin setup.
8. CachedValue plugin params take the edit undo manager (`tracktion_ParameterHelpers.cpp:24`), so our
   gain plugin's `gainDb` write is undoable.

### Correction (parameter undo is achievable via Tracktion)

An earlier version of this document claimed "parameter undo needs RockHero capture/replay no matter
what." That is **wrong**, traced to source on 2026-06-10:

- A VST3 knob change fires `audioProcessorParameterChanged` → async → `Edit::pluginChanged(p)`, which
  only marks the plugin dirty in `changedPluginsList` and starts a 500 ms timer that sets the dirty
  flag — it does **not** write the chunk (`tracktion_Edit.cpp:482-543, 2431-2437`).
- The chunk reaches the tree (with the undo manager) only via `flushState()`/`flushPluginStateIfNeeded()`,
  whose only callers are **save** (`tracktion_EditFileOperations.cpp:193`) and **copy**
  (`Clipboard.cpp:2058`).

So Tracktion does not *auto-record* third-party VST3 parameter edits during editing — but it **can**
undo them if we trigger a state flush at gesture-settle (e.g. on `parameterChangeGestureEnd`), wrapped
in a labeled transaction. That writes an undoable chunk action — exactly the "wait for the value to
settle, then record" behavior expected of a DAW. Tracktion's own internal plugins already store params
as undoable `CachedValue`s (`tracktion_ParameterHelpers.cpp:24`); only third-party VST3 params are
chunk-based and flush-driven.

Consequences for the decision space below:

- Parameter undo is **not** inherently a RockHero-only job. It is the *same* delegate-vs-own fork as the
  structural ops, with two viable shapes: **(P1)** flush the plugin chunk at gesture-end → Tracktion
  `Edit::undo()` (coarse, whole-chunk per settle, reuses Tracktion); or **(P2)** a RockHero-owned
  plugin-state memento. **Update (2026-06-11): the fine-grained before/after-value variant of P2 is
  rejected on fidelity grounds (see "Fidelity is non-negotiable" below). The RockHero parameter path is
  a full-chunk capture/restore memento — the same representation as remove-undo — not a parameter
  vector. Both P1 and the chunk memento are coherent full-state restores; granularity no longer
  distinguishes them.**
- The only things that remain genuinely outside the Tracktion tree are editor-only metadata (block
  placement, display-type) and future chart/session edits — and even block placement / display-type
  *could* be stored as undoable properties on the plugin's tree node if we accept that coupling.

This widens the viable Tracktion-delegated design considerably and is the reason the master plan now
front-loads baseline cleanup + an explicit mechanism-decision gate rather than pre-committing to
mementos.

### The +776 "gain churn", explained

A single `setOutputGain` produced ~776 undo units (consistent with and without user plugins). It is
**not** the gain value (one property). `Engine::setOutputGain` calls `ensureStructuralLiveRigPlugins()`
(`engine.cpp:3543`), which is **not** invoked on plugin insert (only on gain/monitoring/load —
`engine.cpp:3412, 3543, 3828`). So the first gain command **lazily creates the four hidden structural
plugins** — input gain, input meter, output gain, output meter — each a full plugin instantiation plus
state and reorder (`ensureStructuralLiveRigPlugins`, `engine.cpp:2304-2404`), all bundled into that
command's transaction.

**Implication:** a RockHero command's Tracktion transaction is **not** a clean 1:1 image of the
logical edit — it bundles incidental adapter work (lazy structural-plugin creation, reorders,
rebuilds). An `Edit::undo()` of that "gain change" would also delete the structural infrastructure.
This is controllable in principle (eager structural init, strict bracketing) but shows the discipline
delegation would require.

### B3 cleaned-base rerun

After B1 eager structural anchors and B2-lite route centralization, the Phase B3 rerun on
2026-06-11 measured the cleaned base directly (`editor-engine-undo-phase-b-findings.md`):

- insert: one labeled transaction, +208 units, raw undo removed the user plugin, raw redo restored
  the same id;
- move: one labeled transaction, +64 units, raw undo/redo restored user-plugin order and ids;
- remove: one labeled transaction, +32 units, raw undo/redo restored/removed the same id;
- output gain: one labeled transaction, +72 units, raw undo/redo restored the project-facing
  `outputGain()` value after a RockHero adapter fix.

Manual route repair after raw undo/redo of insert, move, and remove succeeded in the headless
default-route harness and did not add undo units. That reduces the routing concern from "undo
stack pollution" to "explicit post-undo runtime repair still required under B2-lite." It does not
prove live-device playback-graph correctness while monitoring is enabled.

The output-gain failure was not a Tracktion undo failure. Tracktion restored the `gainDb` ValueTree
property, but `LiveRigGainPlugin::gain()` reads a separate realtime atomic target. A
`LiveRigGainPlugin::valueTreePropertyChanged()` override now synchronizes direct ValueTree undo
back into that target. This changes the delegation picture: structural list edits are clean at the
tree/id level, output gain is clean at the value level, and B2-lite still requires an explicit
route/playback repair step outside the Tracktion undo tree for structural list edits.

## The decision space (three separable layers)

The original framing ("RockHero stack vs Tracktion stack") conflated three layers that should be
decided separately:

1. **Ordering / policy / labels / dirty / failure / unified Ctrl+Z.** Must be RockHero-owned. The
   product history spans plugin tree state **and** editor-only state that is not in the Edit tree at
   all — block placement and display-type overrides are written only to the tone document
   (`engine.cpp:3651-3657, 3682-3692`) — plus future chart/session edits, typed failure and
   faulted-session policy, and clean-revision dirty tracking. Not contestable.
2. **Inverse mechanism for the tree-affecting edits** (insert/move/remove/output gain). This is the
   real open question: delegate to `Edit::undo()/redo()`, or apply RockHero mementos.
3. **Primitives.** Tracktion does the heavy lifting regardless: plugin instantiation, plugin state
   capture/restore (`get/setStateInformation` — these *are* the memento bytes), list mutation, graph
   rebuild. Not reinvented under any option.

## Options for layer 2 (inverse mechanism)

Before choosing an inverse mechanism, keep the product shape separate from the tone mechanism:
RockHero should normally expose one user-visible undo history per open project. Tone edits,
tablature/chart edits, and editor metadata are all edits to the same saved project unless a future
feature becomes a truly separate document. A Tracktion-backed tone inverse can live underneath that
single RockHero stack without making Tracktion responsible for the whole program's undo policy.

### Option A — Full delegation to `Edit::undo()/redo()`

Each tree-edit command wraps its mutation in `beginNewTransaction(label)`; undo/redo call
`Edit::undo()/redo()`.

- **Pro:** reuses tested code; eliminates RockHero memento capture/restore and id remapping for
  structural ops (the spike showed atomic, id-preserving undo/redo for an isolated wrapped insert).
- **Con (architecture-specific, decisive):** our mutations are **tree edit + imperative runtime
  rebuild**. `applyInstrumentMonitoringRoute()` reassigns the live input via
  `input_instance->setTarget(..., nullptr, ...)` (undo manager = nullptr, so **not** undoable),
  sets monitor mode, and reallocates the playback context (`engine.cpp:2561-2645`). `Edit::undo()`
  reverts the tree and self-heals the node graph, but **never re-runs our routing**, and the input
  routing is not in the undo tree to begin with. After a raw `Edit::undo()` the live monitoring route
  and our cached structural plugin `EditItemID`s can be left inconsistent with the reverted tree.
- **Con:** correctness rests on a **global, implicit, silently-violable invariant** — strict 1:1
  between RockHero commands and Tracktion transactions, with our stack pointer kept aligned to
  Tracktion's internal pointer. The +776 finding shows transactions already bundle incidental churn;
  the 350 ms auto-transaction timer and any future subsystem touching the Edit can break the mapping
  with no compile-time signal.

Under B2-lite, Option A still needs explicit post-undo route/playback repair inside the audio
adapter. B2-full would deliberately move that cost behind reactive plugin-list/tree observation,
including child-order changes that Tracktion's own edit watcher ignores, and would make delegated
structural tone undo substantially cleaner. B2-full does not solve transaction discipline,
capture/save flushes, parameter granularity, or metadata ownership; it only makes the runtime
repair part of delegation cleaner.

### Option B — Local RockHero mementos (current `editor-undo-plan.md` design)

Each undo entry captures/restores exactly its own state via the `IPluginHost` memento surface.

- **Pro:** every entry is self-contained and verifiable in isolation; one uniform mechanism across all
  edit types (tree, parameter, metadata, future charts); no shared Tracktion pointer to keep aligned;
  testable headlessly with fakes; integrates typed failure / faulted-session / clean-revision directly.
  Best fit for a *guarantee* of correctness and maintainability.
- **Con:** more code than Option A for the structural ops, and we implement id remapping (the spike
  showed restore yields a new id when the captured state's id is stripped, which our capture does).

### Option C — Hybrid (delegate structural ops, mementos for params/metadata)

Rejected by the mechanics: any non-delegated inverse that writes the tree (a `setPluginParameterValues`
restore, a `setOutputGain`) pushes transactions Tracktion counts but RockHero's `Edit::undo()` calls do
not, desyncing the two stacks. To keep them aligned you must delegate *everything* (collapses to
Option A) — which then **cannot provide fine-grained parameter granularity without mementos**
(parameter undo via Tracktion is possible but only as a coarse whole-chunk flush per gesture-settle,
not per-parameter), and still cannot reverse the non-tree editor metadata.

This rejection is specific to mixing mechanisms that both mutate Tracktion's tree. It does not
reject a heterogeneous RockHero product stack where tone entries use Tracktion-backed inverses and
future tablature/chart entries use RockHero-owned editor-core inverses, because those chart entries
do not advance Tracktion's undo pointer.

### Option D — Mementos for the product stack, scoped Tracktion undo for per-operation rollback

Option B for user-visible history, **plus** a bounded `beginNewTransaction` + `Edit::undo()` *inside a
single adapter call* to roll an in-flight mutation back to its exact pre-state on failure. The scope is
one synchronous adapter operation where we control all tree writes, so the global-invariant problem
does not arise, and it directly satisfies `editor-undo-plan.md`'s per-port rollback-proof requirement
using tested Tracktion code.

## Coupling, the Impl boundary, and code cost (2026-06-10 refinement)

**The `Engine::Impl` boundary hides Tracktion *types*, not Tracktion *behavior*.** Delegation makes
`EditorUndoHistory`'s correctness depend on Tracktion's undo semantics (positional pop, transaction
contents, interposing flushes) — a behavioral contract that crosses the layer boundary even though no
`tracktion::` type does. Mementos keep the coupling fully contained: editor-core handles opaque
`PluginInstanceState` bytes with a trivial "capture then restore" contract; all Tracktion behavior
stays inside `get/setStateInformation`.

B2-full can contain, but not erase, that behavioral coupling. A cleaner delegated design would keep
Tracktion-backed tone undo behind an audio-domain port/token: editor-core records a tone entry and
asks common/audio to undo or redo that entry, receiving a typed result. Editor-core must not call
raw `Edit::undo()/redo()`, derive availability from `Edit::getUndoManager()`, or store Tracktion
cursor state. If those details remain inside common/audio, the coupling is adapter-local; if they
reach editor-core, it has leaked into the product undo policy.

The 1:1 command↔transaction invariant delegation needs is **real but narrower** than earlier framing:

- **Route repair is not a threat** — `setTarget`/`clearAllInputs` are called with a `nullptr` undo
  manager (`engine.cpp:2629-2630, 2473-2479`), so routing writes are not recorded.
- **Capture/save flush is the concrete threat** — `captureActiveRig()` flushes each plugin via
  `flushPluginStateToValueTree()` (`engine.cpp:3658`), which hardcodes the undo manager
  (`ExternalPlugin.cpp:1004,1031`); avoidable only by reading the chunk straight from the
  `AudioPluginInstance`.
- Copy flush is N/A (no Tracktion clipboard use); plugin-window/automation writes are
  unverified/standing-discipline risks.

**Code cost (estimate):** the memento delta over delegation is modest. Shared regardless:
`EditorUndoHistory` policy, `EditorUndoEntry` payloads, and the parameter gesture/debounce observer
(needed even for delegation's gesture-settle flush). Memento-specific: `capturePluginState`/
`insertPluginState`/`setPluginParameterValues` — largely **reuse** of existing `captureActiveRig`
flush+copy and `loadLiveRig` recreate-from-state — plus `remapInstanceId` (~50-100 LOC + tests, the one
genuinely-new piece, since delegation gets Tracktion id preservation free). Delegation-specific:
transaction bracketing, capture-flush mitigation, pointer-alignment, and the coupling maintenance risk.
Net: a few hundred lines, much reused and well-contained, in exchange for local coupling.

## Correctness / maintainability assessment (the stated priority)

The hard requirement is a *guarantee* of consistent, correct, maintainable behavior. That points away
from delegation:

- Option A's correctness depends on an invariant (1:1 command↔transaction, pointer alignment, and "no
  other code writes the Edit between a command and its undo") that the architecture cannot enforce and
  that fails silently. Our own engine already violates the spirit of it (imperative routing outside
  undo; transactions bundling structural-plugin creation). As automation editing, chart editing, and
  other Edit-touching features land, the burden only grows.
- Option B/D localize correctness: each entry is proven in isolation, the mechanism is uniform, and
  Tracktion is still used for the genuinely hard primitives (instantiation, state capture/restore,
  graph rebuild) and optionally for bounded rollback.

Post-B3, this assessment is closer than the original recommendation implied. Output gain is no
longer a delegation blocker, structural list edits are clean at the tree/id level, and B2-full
would address the remaining route/playback repair issue for tone structure. The remaining
delegation risks are transaction discipline, capture/save flushes, parameter granularity, metadata
storage, and keeping Tracktion's undo behavior behind the audio boundary.

## Verified: the tree-vs-runtime gap, and why mementos restore through setters (2026-06-11)

The deepest first-principles fact, source-verified: **Tracktion's `Edit` tree is the persistent model;
the live runtime is reconstructed from it only at specific lifecycle points, not reactively for all
in-place property undo.** This decides the mechanism more than any code-shape argument.

- **Plugin state has a runtime gap.** `ExternalPlugin::valueTreePropertyChanged`
  (`tracktion_ExternalPlugin.cpp:1956-1974`) reacts only to `IDs::layout`; it ignores `IDs::state`
  (the plugin chunk). The processor is loaded from the tree via `restorePluginStateFromValueTree` →
  `setStateInformation` only during plugin (re)initialisation (`:1856`, `:1090`). So a raw
  `Edit::undo()` that reverts the `IDs::state` property **does not reload the live processor** for an
  in-place edit — it changes the tree, not the audio. This is the same class of gap found for routing
  (`setTarget(..., nullptr, ...)`, `engine.cpp:2680`, not in the undo tree) and for the gain plugin's
  realtime target (B3 needed an explicit `valueTreePropertyChanged` sync).
- **Consequence for delegation:** to make delegated tree-undo affect the running system for in-place
  edits, you must build a general reactive tree→runtime sync layer (a generalized B2-full spanning
  plugin state, routing, gain, and any future in-place tone property) — i.e. reimplement runtime
  reconstruction Tracktion does not provide. That is *more* infrastructure than mementos, and the
  fragile framework-fighting kind.
- **Consequence for mementos (the clean side):** mementos restore through the **normal setter / mutation
  API** (`setStateInformation`; recreate via the load path), which is the same path that already drives
  the runtime, so the runtime follows by construction. Restores use a `nullptr` undo manager, so they
  never enter Tracktion's undo manager — preserving the single product-stack source of truth.

## Verified: automation-curve undo is correct under mementos (2026-06-11)

Because automation "must work without fail", this was traced end-to-end. Automation is the **opposite**
of the plugin-state case: it *is* reactively tree-following, so memento restore is correct by
construction.

- **The curve model is a read-through view over its ValueTree** — no cached point array.
  `getNumPoints()` = `state.getNumChildren()` (`tracktion_AutomationCurve.cpp:63`); `getPoint(i)` reads
  `state.getChild(i)`'s `t/v/c` (`:83-87`). Mutators are plain tree edits taking a `juce::UndoManager*`
  (`addPointAtIndex` → `state.addChild(...,um)` `:358`; `removePoint` → `state.removeChild(index,um)`
  `:369`).
- **The audio thread reads via an `AutomationIterator`** (`tracktion_AutomatableParameter.cpp:274`),
  but the parameter **rebuilds that iterator reactively on any curve-tree change**: it listens to the
  curve subtree — `valueTreePropertyChanged` (`:1218-1226`), `valueTreeChildAdded` (`:1241-1248`),
  `valueTreeChildRemoved` (`:1251-1256`), `valueTreeChildOrderChanged` (`:1259-1262`) — each routing to
  `curveHasChanged()` → `curveChanged()` → `triggerAsyncIteratorUpdate()` (`:627-629`). A whole-subtree
  swap re-binds the parameter via `getCurve().setState(newChild)` (`:1248`).
- **Load-bearing fact:** ValueTree listeners fire on any mutation **regardless of the `UndoManager*`
  argument** (the um governs only undo recording, not listener notification). So a memento restore that
  re-applies a captured `AUTOMATIONCURVE` subtree through Tracktion's own tree ops **with `nullptr` um**
  fires the identical listener → iterator-rebuild path as a normal user edit: the audio follows, and
  nothing is recorded in Tracktion's undo manager.
- **No leaky reimplementation:** capture is `state.createCopy()` (Tracktion's serialization); restore
  is Tracktion's own curve API / tree ops; propagation is Tracktion's own reactive iterator rebuild.
  Zero automation logic is reimplemented. Residual: a one-case confirmation spike (design is proven;
  the spike confirms async-timing audibility), not an open design risk.

## Verified: VST3 parameter undo is whole-chunk (2026-06-11)

Traced to source to settle whether Tracktion's parameter undo is "inefficient" (it is not — it is
the VST3 model):

- `ExternalPlugin::flushPluginStateToValueTree()` writes the **entire** opaque plugin state as one
  undoable property: `pi->getStateInformation (chunk)` then
  `state.setProperty (IDs::state, chunk.toBase64Encoding(), um)`
  (`tracktion_ExternalPlugin.cpp:1024, 1031`). That base64 blob (~10 KB for the reference plugin) is
  the undoable representation of a third-party plugin's parameters.
- The granular `saveChangedParametersToState()` path does **not** capture ordinary knob turns: it
  only writes params whose live value differs from their explicit/base value —
  `if (ap->getCurrentValue() != ap->getCurrentExplicitValue())`
  (`tracktion_AutomatableEditItem.cpp:311`) — i.e. the automation/macro layer, stored as one binary
  `IDs::parameters` blob (`:305-325`). A plain user knob turn has `currentValue == explicitValue`, so
  it lives in the chunk, not here.
- This is **not** Tracktion implementing undo poorly. Its own internal plugins store each parameter
  as a `CachedValue<float>` tree property *with the undo manager*
  (`tracktion_ParameterHelpers.cpp:24`) — fully granular, cheap, per-parameter undo. The coarseness
  is specific to third-party plugins whose full state is an **opaque binary chunk by the VST3/AU
  standard**; any host (including a hand-rolled RockHero memento using `getStateInformation`) hits the
  identical chunk.
- Tracktion's own idiom confirms the split: `restoreChangedParametersFromState()` uses granular
  `setParameter (value, juce::dontSendNotification)` (`:340`) **only** for the automation/explicit
  layer, and the chunk for base state. Chunk-for-base-state is the intended restoration unit.

## Fidelity is non-negotiable: granular parameter replay is rejected

The project requires a guarantee of exact reproduction on undo/redo. That makes granular
per-parameter value replay (capture before/after normalized values, replay via
`setPluginParameterValues`) **unacceptable for plugin parameters**, because it cannot guarantee the
plugin returns to its exact prior state:

- **Non-parameter internal state** — loaded IRs/cab files, preset/sample references, oversampling
  mode, hidden DSP state — is in `getState` but not in the exposed parameter vector, so a
  parameter-vector memento cannot restore it. (Guitar-tone plugins are full of this.)
- **Dependent / linked / stepped parameters** whose internal state is not a pure function of the
  exposed parameter vector can land in an intermediate or quantized state, or leave uncaptured
  dependent params unrestored. The chunk restores the plugin's self-consistent snapshot atomically.
- **Controller/processor sync and open-editor refresh** are handled coherently by the plugin's own
  `setState`; a bare parameter set can leave an open editor stale.

Setting individual parameters is *legal* (it is how automation works), so the issue is fidelity, not
legality. The robust, VST3-idiomatic restoration unit for a plugin edit is the **full chunk**
(`get/setStateInformation`).

**Granularity is orthogonal to stack ownership.** Two independent choices were previously conflated:
who owns the undo entry (RockHero vs Tracktion) and what representation it stores (granular vs full
chunk). A RockHero memento can store the **full chunk** and restore via set-state — VST3-idiomatic
*and* RockHero-owned/contained. Consequences:

- The earlier "mementos give fine-grained, lighter parameter undo (P2)" advantage is withdrawn: the
  fidelity-safe memento is a chunk memento, costing the same ~10 KB per settle as Tracktion's
  gesture-settle flush. Parameter efficiency no longer separates the two mechanisms.
- The memento case for parameters now rests purely on containment/uniformity/no-coupling, and it
  becomes very uniform — remove-undo and parameter-undo use the *same* full-chunk capture/restore
  primitive.
- Whichever mechanism is chosen, the Step 2 spike must verify chunk-restore fidelity against a
  **dependent-parameter plugin** and a plugin with **non-parameter editor state (IR/preset load)**,
  plus open-editor refresh after a coherent state restore.

## Runtime performance is not a deciding factor

Neither mechanism touches the audio thread; undo recording, undo/redo execution, and stack storage
are all infrequent message-thread work. Real-time rendering and gameplay timing are identical.
Differences are bounded message-thread memory/CPU: structural ops are a wash (both retain the removed
plugin's chunk somewhere); Tracktion's internal manager grows under delegation but is bounded
(`maxNumberOfStoredUnits`, `tracktion_Edit.cpp:643`) and is never consulted on the audio thread,
while mementos quarantine it; B2-full adds only a negligible structural-edit-time listener. With
granular parameter replay rejected on fidelity grounds, the one place performance previously favored
mementos is moot. Performance must not break the ownership tie.

## Decision (settled 2026-06-11)

**RockHero mementos; Tracktion is a backend.** One RockHero-owned project undo stack; tone entries
restore absolute captured state through audio-adapter primitives; Tracktion's undo manager is never
consumed as product history; adapter-local rollback uses `UndoManager::undoCurrentTransactionOnly()`,
not raw `Edit::undo()`. Decided on first principles (Tracktion's tree-vs-runtime architecture +
Rock Hero's unavoidable cross-domain unified `Ctrl+Z`), not on current codebase shape — a full rebuild
would land here too, the existing persistence shape merely confirms it. The mechanism choice is the
single-source-of-truth (memento) vs two-synchronized-stacks (delegation) distinction; everything else
(performance, fidelity, code cost) is secondary or neutral. The text below is retained as the analysis
of record.

## Prior alternatives considered (historical context)

Own the product stack either way: user-visible ordering, labels, clean state, failure policy, and
future tablature/chart ordering remain RockHero responsibilities. The Phase M choice was between two
candidate architectures:

- **Tracktion-backed tone undo plus B2-full:** idiomatic for Tracktion tone state if all
  Tracktion undo behavior stays behind a common/audio tone-undo port, tone metadata lives in the
  chosen authoritative store, and parameter/save-flush discipline is resolved.
- **RockHero mementos plus optional scoped Tracktion rollback:** more explicit at the product
  boundary and uniform across tone/chart/metadata, at the cost of more adapter code and id
  remapping.

The non-negotiable fidelity requirement and the verified VST3 whole-chunk mechanism did not decide
this axis by themselves: both candidates restore plugin state coherently via the chunk. They did,
however, withdraw the granular parameter-efficiency point that previously favored mementos and
confirmed that runtime performance is not a tiebreaker. The choice reduced to the ownership question:
"Tracktion owns the tone subsystem" (delegation, done fully with B2-full, metadata on the tree) vs.
"Tracktion is a backend" (mementos with full-chunk plugin-state capture/restore).

Phase M chose the second model. Tracktion-backed tone undo should be revisited only if the project
intentionally pivots to a Tracktion-owned tone document, including reactive runtime repair,
metadata-on-tree coupling, and a provable single transaction stream for the entire tone subsystem.

## Open items a further spike could close (if desired)

- With a real audio device open and monitoring enabled, measure whether forcing
  `applyInstrumentMonitoringRoute()` after `Edit::undo()/redo()` fully restores live runtime state.
- Finish the interactive parameter spike: real VST3 gesture callbacks, self-animating parameter
  behavior, and open-editor refresh after parameter restore.
