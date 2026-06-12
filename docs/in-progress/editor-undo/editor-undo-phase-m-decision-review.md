# Editor Undo Phase M Decision Review

Date: 2026-06-11

This note captures the current undo/redo architecture decision for further review. It is intended
to be handed to another reviewer and deliberately challenged. The goal is not to defend a favorite
implementation. The goal is to choose the cleanest long-term design for Rock Hero.

## Review Request

Please analyze this decision as if it could be wrong. Look for hidden coupling, false assumptions,
unnecessary custom infrastructure, future maintenance traps, and cleaner alternatives.

The current recommendation is:

- Use one RockHero-owned product undo stack.
- Represent tone-edit undo entries as RockHero-owned mementos restored through audio adapter
  primitives.
- Keep Tracktion undo out of the product-level undo history.
- Allow Tracktion undo only inside tightly bounded adapter-local rollback, preferably through a
  current-transaction-only rollback primitive rather than raw `Edit::undo()`.
- Stay on B2-lite for now.
- Do not implement B2-full unless the project intentionally pivots to a Tracktion-owned tone-edit
  architecture.

The specific question to challenge:

> Is RockHero-owned memento undo actually the clean design, or are we avoiding Tracktion's native
> undo because of design issues elsewhere that should be fixed instead?

## Project Context

Rock Hero is not intended to be a DAW. It is a guitar rhythm game and editor with real-time VST
tone design, future tablature/chart editing, arrangement authoring, and gameplay data. Tracktion
Engine is currently used for audio playback, plugin hosting, transport, and live processing.

The repository design documents emphasize:

- Tracktion should remain isolated behind `rock-hero-common/audio`.
- Product workflow and policy should live in headless, testable product-core code.
- Project-owned ports and fakes are preferred over coupling tests and logic to framework behavior.
- Framework integration should be thin and adapter-local where practical.

The current implementation already persists tone as a RockHero-owned document shape:

- RockHero owns the tone document and plugin ordering metadata.
- Tracktion owns opaque plugin state blobs used by that tone document.
- Editor presentation metadata such as block placement and display type is editor-owned and does
  not currently live in Tracktion's `Edit` tree.

That existing persistence shape matters because undo needs to decide whether Tracktion is merely
the tone backend, or whether Tracktion's `Edit` tree and undo manager should become the
authoritative tone-edit model.

## Options Being Compared

### Option B: RockHero Mementos

RockHero owns the visible undo stack. Each undo entry carries enough captured state to restore the
operation through project-owned audio/editor ports.

For tone edits, this likely means:

- Structural edits capture plugin state blobs, position, and editor metadata as needed.
- Parameter edits capture before/after plugin state or parameter values depending on the result of
  the parameter spike.
- Undo/redo calls adapter primitives such as insert plugin from captured state, remove plugin,
  restore plugin chunk, restore parameter values, rebuild live route, and remap runtime IDs where
  needed.
- Tracktion's internal undo manager is not consumed as product history.

This matches the existing "RockHero document references Tracktion blobs" persistence model.

### Option A: Tracktion Delegation

RockHero still needs a visible project-level undo stack, but tone entries would delegate their
inverse to Tracktion by calling `Edit::undo()` or `Edit::redo()` behind a narrow audio port.

For this to be clean, each tone command would need:

- A precise `beginNewTransaction()` boundary.
- Protection from Tracktion/JUCE auto-transaction behavior.
- A guarantee that no unrelated Tracktion undoable operation pollutes the transaction stream.
- A route-repair system after raw Tracktion undo/redo.
- A strategy for editor-owned metadata that is not part of the Tracktion tree.
- A save/capture path that does not silently add Tracktion undo actions.

B2-full would likely be required for this option so route/playback repair becomes reactive to
Tracktion tree changes rather than manually repaired after every delegated undo.

## Current Evidence

Phase B changed the assessment substantially:

- B1 eager structural anchors completed.
- B2-lite centralized synchronous mutation/routing.
- B3 re-measured Tracktion undo on the cleaned base.
- The old `+776` gain churn collapsed to about `+72`.
- Insert, move, and remove now look clean at the Tracktion tree/id level.
- Output-gain value undo/redo now works after the `LiveRigGainPlugin` value-tree listener fix.

So Tracktion delegation is no longer blocked by the earlier dirty-base behavior. It is a viable
technical option for tone-only structural edits.

The remaining concern is architectural rather than "Tracktion undo does not work."

## Strongest Case For Delegation

Delegation deserves serious consideration because:

- Tracktion owns the plugin list mutation machinery.
- Tracktion already records value-tree undo actions for many plugin operations.
- Tracktion preserves plugin IDs across its own undo/redo, avoiding `remapInstanceId` work.
- B3 showed structural tone edits can become clean labeled transactions.
- B2-full reactive route repair could be a real cleanup if Tracktion's tree is treated as the
  tone source of truth.
- Delegation may require less custom code for insert/move/remove tone operations.
- For a DAW-like product, Tracktion-backed undo would be the natural design.

A coherent Tracktion-delegation architecture would probably look like this:

- Treat Tracktion's `Edit` tree as the authoritative tone-edit model.
- Put all tone-relevant metadata, possibly including editor placement/display metadata, into or
  alongside Tracktion tree state in a way that Tracktion undo captures atomically.
- Implement B2-full so route/playback repair reacts to Tracktion tree changes.
- Prevent or neutralize transaction pollution from save/capture flushes and other backend writes.
- Keep a RockHero product undo stack that contains Tracktion-backed tone tokens, chart tokens, and
  editor metadata tokens, while maintaining strict alignment with Tracktion's undo stack.

That design is coherent. The question is whether it is cleaner for Rock Hero.

## Main Reasons To Prefer Mementos

### 1. Product Undo Is Wider Than Tone Editing

RockHero undo eventually needs to order:

- Tone edits.
- Editor visual metadata edits.
- Future tablature/chart edits.
- Arrangement/session edits.
- Possibly project/package actions.

Tracktion's undo manager only knows about Tracktion `Edit` changes. It cannot naturally own chart
or tablature edits that do not touch the audio backend.

A single user-visible `Ctrl+Z` stack therefore belongs at the RockHero product level. Once that is
true, delegated tone undo creates two ordered histories that must stay aligned: RockHero's product
history and Tracktion's internal transaction history.

### 2. Tracktion Undo Is Positional, Not Addressable

Tracktion/JUCE undo is transaction-stack based. A call to undo pops the current transaction. It is
not an addressable "undo this RockHero command token" API.

That means delegated correctness depends on permanent global invariants:

- Every RockHero tone command creates exactly one Tracktion transaction.
- No unrelated Tracktion write appears between product undo entries.
- Redo clearing is mirrored correctly in both stacks.
- Save/capture flushes do not add unexpected transactions.
- Future automation, plugin editor actions, or backend maintenance do not pollute the stream.

Memento entries instead restore absolute captured state. Their correctness is local to the entry
and can be tested against project-owned fakes.

### 3. Editor Metadata Forces A Layering Choice

Current code treats placement/display-type metadata as editor-owned. It is not audio graph state.

Delegation forces one of two choices:

- Move editor presentation metadata into Tracktion's tree so Tracktion undo captures a whole tone
  edit atomically.
- Keep metadata in editor-core and make each delegated tone entry a compound operation:
  Tracktion undo for the plugin slice plus RockHero restore for the editor metadata slice.

The first choice couples editor presentation to the audio backend. The second means delegation is
not actually a unified undo model.

### 4. Parameter Undo Likely Needs Memento Mechanics Anyway

Fine-grained parameter undo likely requires before/after capture and replay, not just raw
Tracktion transaction delegation.

The remaining interactive parameter spike must answer:

- Do real VST3 editors emit usable gesture begin/end boundaries?
- Do some plugins self-animate or continuously publish parameter changes?
- Does restoring a full plugin chunk update audio state and an already-open plugin window?
- Does raw Tracktion undo of a plugin state property actually restore the live processor, or only
  mutate the value tree?

If parameter undo uses captured state either way, delegation may only avoid memento machinery for
structural operations.

### 5. B2-full Is Mainly A Delegation Tax

B2-full would likely be useful if raw Tracktion undo/redo can mutate plugin trees behind
RockHero's normal mutation path. In that world, route/playback state must react to tree changes.

Under mementos, RockHero undo/redo restores state through the normal adapter mutation paths. Those
paths can rebuild routing synchronously under B2-lite. B2-full may still be valuable someday, but
it is not required to make memento undo clean.

### 6. The Existing Persistence Shape Already Points Toward Tracktion-As-Backend

RockHero currently owns the tone document and references Tracktion plugin state blobs. That is
closer to:

> Tracktion is a backend primitive provider.

than:

> Tracktion's `Edit` tree is the authoritative product document.

Memento undo extends the same ownership model already used by save/load. Delegation would introduce
a different ownership model only for undo.

## Specialist Review Summary

Two read-only specialist agents were asked to challenge this decision.

### Tracktion Engine Specialist

Recommendation: choose RockHero-owned mementos for user-visible undo, keep B2-lite, and skip
B2-full for now.

Key points:

- Tracktion plugin-list writes go directly into `edit.getUndoManager()`.
- `Edit::undo()`/`redo()` delegate to JUCE's positional undo manager.
- Tracktion tree add/remove may restart playback, but child-order change does not provide all
  RockHero route repair.
- RockHero live input routing is outside Tracktion undo because target assignment uses a null undo
  manager.
- Save/capture currently calls `flushPluginStateToValueTree()`, and Tracktion external-plugin
  flush writes chunk state through the edit undo manager.
- Block placement/display type are editor-owned opaque values, not audio semantics.

Important correction:

- For adapter-local rollback, prefer a current-transaction-only rollback primitive such as
  `UndoManager::undoCurrentTransactionOnly()` where available. Avoid raw `Edit::undo()` for scoped
  rollback because it can pop the previous transaction if the current scoped operation produced no
  undoable action or the bracketing is wrong.

### JUCE Specialist

Recommendation: adopt the proposed decision from the JUCE/UI/application side.

Key points:

- One visible project stack better matches user expectation for tone, future chart/tab edits,
  arrangement metadata, and editor metadata.
- Current app shape centralizes intents through `EditorController`, not JUCE commands or Tracktion
  UI.
- JUCE `UndoManager` is useful as a primitive but does not provide RockHero typed failure policy,
  faulted-session behavior, clean-revision tracking, or cross-domain project labels.
- Silent transaction pollution from plugin state flushes is a real delegation risk.
- B2-lite routing is explicit and outside Tracktion undo, which fits memento restore through normal
  adapter mutation paths.
- Keeping block placement/display type out of the Tracktion tree is consistent with current
  layering.

Important caveat:

- Do not treat plugin-editor parameter undo as solved until a real VST3 open-editor restore spike
  passes.

Additional hole to investigate:

- Tracktion's external-plugin state-property undo may not restore the live processor by itself.
  Ordinary state changes may only notify through `Plugin::changed()`, while actual chunk restore
  is handled by a separate `restorePluginStateFromValueTree()` path. This needs to be verified.

## What Would Change My Mind

Delegation may become cleaner if one of these is true:

- We decide Tracktion's `Edit` tree should be the authoritative tone-edit document, not just the
  backend representation.
- Editor placement/display metadata is legitimately tone-domain data and belongs in Tracktion tree
  state.
- We can prove all Tracktion undo transaction pollution risks are bounded behind a small adapter
  contract, including save/capture flushes, plugin editor changes, future automation, and backend
  maintenance writes.
- Future tone editing becomes heavily based on Tracktion-native editors or automation tools where
  reconstructing mementos would duplicate large parts of Tracktion behavior.
- Parameter undo turns out to work better and more reliably through Tracktion transactions than
  through explicit state capture/restore.

Without one of those, delegation appears to trade local memento code for a global synchronization
contract between two undo histories.

## Proposed Phase M Decision Text

Proposed wording:

> RockHero owns one project-level undo stack. Tone edits use RockHero-owned mementos restored
> through audio adapter primitives. Tracktion undo is not consumed as product history. Tracktion
> undo may only be used inside bounded adapter-local rollback, preferably through a
> current-transaction-only rollback primitive rather than raw `Edit::undo()`. B2-lite remains the
> active route repair model. B2-full is deferred unless the architecture later pivots to
> Tracktion-owned tone editing.

## Required Follow-Up Spike

Before implementing parameter undo, run a focused real-plugin spike:

- Open a real VST3 editor.
- Change a parameter through the plugin UI.
- Capture the intended memento state.
- Restore the captured state through the intended adapter path.
- Verify the live audio processor state changes.
- Verify the already-open plugin editor refreshes visibly.
- Verify whether raw Tracktion undo of the plugin state property restores the live processor or
  only changes the value tree.

This spike gates parameter undo fidelity regardless of whether the final architecture uses
mementos or delegation.

## Questions For Further Review

Please answer these directly:

1. Is the "two sources of truth" concern real, or is it overstated if Tracktion-backed tone undo is
   hidden behind a narrow audio port?
2. Is moving editor metadata into Tracktion's tree actually a layering violation, or would it be a
   reasonable way to make the tone document atomic?
3. Does B2-full have enough independent design value that we should implement it even if we choose
   mementos?
4. Are mementos likely to become a second serialization system that drifts from save/load, or can
   they safely reuse the existing plugin-state blob primitives?
5. Is `remapInstanceId` essential complexity, or is it a sign that Tracktion should own structural
   undo because it preserves IDs?
6. Are there future Tracktion automation or plugin-editor workflows that would make mementos feel
   like a leaky reimplementation of Tracktion behavior?
7. If we choose mementos, what adapter contract prevents Tracktion's internal undo manager from
   accumulating or interfering with backend state?
8. If we choose delegation, what exact invariant proves that RockHero's product undo stack and
   Tracktion's undo stack cannot desynchronize?

## Decision (settled 2026-06-11)

Mementos chosen. Not because Tracktion undo is broken, but because Tracktion undo is the wrong
ownership layer for RockHero's product-level undo. Tracktion remains the backend for plugin hosting,
state blobs, graph rebuilds, and bounded rollback (`UndoManager::undoCurrentTransactionOnly()`).
RockHero owns the visible undo history and restores product-visible state through project-owned
contracts. Recorded in `editor-engine-undo-master-plan-v3.md` (Phase M Decision) and
`undo-ownership-analysis.md` (Decision). Unified `Ctrl+Z` per project is confirmed as the product
choice; automation-curve undo under mementos is source-verified correct (below). Visibility is handled
by scope discipline + labels + reveal-on-undo (see `editor-undo-plan.md`, Undo Visibility).

## Review Answers (2026-06-11)

Verified finding that grounds the decision — **the tree-vs-runtime gap.**
`ExternalPlugin::valueTreePropertyChanged` (`tracktion_ExternalPlugin.cpp:1956-1974`) ignores
`IDs::state`; the processor is reloaded from the tree only at (re)init (`:1856` →
`restorePluginStateFromValueTree` → `setStateInformation` `:1090`). So raw `Edit::undo()` of an
in-place plugin-state edit reverts the *tree* but not the running processor. Delegation would need a
general reactive tree→runtime sync layer Tracktion does not provide; mementos restore through the
normal setter, so the runtime follows by construction. Automation is the opposite (verified reactive):
the parameter rebuilds its audio-thread `AutomationIterator` from any curve-tree change
(`tracktion_AutomatableParameter.cpp:1218-1262` → `curveChanged` → `triggerAsyncIteratorUpdate`
`:627-629`), so a memento subtree-swap restore (nullptr um) drives playback correctly and is recorded
nowhere in Tracktion's undo.

Answers to the eight questions:

1. **Two sources of truth — overstated behind a port?** No, real. A port hides types and the call
   site but cannot establish the correctness invariant, which is a closed-world property over an
   open-world framework (autonomous plugin editors, save/capture flush, future automation, Tracktion
   internals). Reducing surface ≠ removing the second stack.
2. **Editor metadata into Tracktion's tree — layering violation?** Yes. Placement/display-type have no
   audio semantics; no other `Edit` consumer reads them. Moving them in to gain undo atomicity is
   tail-wags-dog. Place data by domain; pick a mechanism that handles cross-domain entries.
3. **Does B2-full have independent value?** Partial. Genuine kernel (reactive derived routing kills
   ~20 imperative reroute sites + the forgot-to-reroute bug class) wrapped in a real tradeoff (async
   route-status surface, async timing). Not required for mementos. Do it, if at all, as a standalone
   routing refactor judged on its own merits — never as undo prep.
4. **Mementos = a second serializer that drifts?** No, if capture/restore is one shared adapter
   primitive used by both save/load and undo (`capturePluginState` == `captureActiveRig` flush+copy
   `engine.cpp:3710`; `insertPluginState` == `loadLiveRig` recreate). Forking it would be the smell;
   the design forbids it.
5. **`remapInstanceId` — essential or a sign to delegate?** Essential complexity. Identity genuinely
   changes on destroy+recreate, and `loadLiveRig` already yields new ids (capture strips the id,
   `engine.cpp:3712`), so editor state already survives id changes. Tracktion's id-preservation is a
   convenience of in-place undo and arguably less honest; relying on it is the fragile choice.
6. **Future Tracktion workflows that make mementos leaky?** No. The boundary case (automation curves)
   is verified to reuse Tracktion's own serialization (`state.createCopy()`), mutation API, and
   reactive iterator rebuild — zero reimplemented logic. Mementos never reimplement
   instantiation/graph/mutation; they capture/restore state via Tracktion's own primitives.
7. **Mementos: contract bounding Tracktion's internal undo manager?** Small and adapter-local.
   Tracktion self-caps (`maxNumberOfStoredUnits`, `tracktion_Edit.cpp:643`) and the graph does not
   depend on the manager (B0). Product undo never calls `Edit::undo()/redo()`; the `UndoManager` is
   private adapter state; adapter-local rollback uses `undoCurrentTransactionOnly()`. No active clearing
   needed.
8. **Delegation: invariant proving no desync?** There is no local invariant — only a global closed-world
   one (the multiset of undoable `Edit` writes equals exactly the product stack's tone commands, one
   bracketed transaction each, in order, redo-clearing mirrored, for all time, including autonomous
   plugin-editor actions, save flushes, future automation, and Tracktion-internal writes). It is not
   locally checkable or port-enforceable. That this question has no clean answer while the memento
   equivalent is trivial *is* the verdict.
