# Tone Parameter Automation Plan

Status: **active, decisive** — 2026-07-07. A formal `juce-tracktion-expert` source review is complete;
its load-bearing findings are inlined below with `file:line` citations and are treated as settled.
This plan is written to be executed by Opus without further design discovery: every open question
from the old reference is resolved here.

This supersedes `docs/plans/todo/tone-automation-track-plan.md` (a read-only UX sketch full of "eventually"
and open questions). It is built directly on the shipped item-1 tone feature (tone-change model,
catalog, six operations, and UI gestures — all on `refactor`; see
[[project_tone_catalog_model]] in agent memory and `docs/plans/in-progress/tone-track-tempo-map-plan.md`).

## Goal

Let the user author **editable automation curves for individual plugin parameters within a tone's
signal chain**, presented as lanes beneath the tone strip and scoped to the selected tone region's
time window. A "+" picker in the signal-chain panel adds a lane for a chosen parameter; the panel
title reads `Signal Chain - <tone>`; point editing grid-snaps unless Ctrl is held; lanes are
vertically resizable for precise edits; everything is undoable. This is a real editing feature, not
the read-only first pass the old doc scoped.

Do this on a **separate branch off `refactor`** (per the original item-5 requirement).

## The audio reality this rests on (verified)

- Each tone is one **parallel rack branch**: the tone's user plugin chain
  (`std::vector<tracktion::Plugin::Ptr>`) → a `ToneBranchGainPlugin`, summed to the rack output
  (`rock-hero-common/audio/src/tracktion/multi_tone_rack.h:54-64`). One `RackInstance` sits on the
  instrument track.
- Rack-branch plugins are wrapped in a `PluginNode` that calls `applyToBufferWithAutomation`
  (`playback/graph/tracktion_RackNode.cpp:397`, `tracktion_PluginNode.cpp:230`), so **a branch
  plugin's parameters follow edit-timeline automation exactly like a track plugin's** — automation
  is evaluated once per block at block start (`plugins/tracktion_Plugin.cpp:656-696`).
- A parameter owns exactly **one** base `AutomationCurve` via `AutomationCurveSource`
  (`model/automation/tracktion_AutomatableParameter.cpp:269,992`), rooted on the plugin's own
  `ValueTree` state, in **seconds** (`TimeBase::time`, `:192`).

## Locked decisions (do not reopen without a new expert pass)

1. **Model = one base curve per (tone-branch plugin instance, `paramID`), edited within the selected
   region's `[startSeconds, endSeconds]` viewport.** RockHero `ToneRegion`s are sorted and never
   overlap (`rock-hero-common/core/.../tone/tone_track.h:97-99`) and the audio model is one branch
   **per tone, not per region**. So a tone reused by regions R1 and R3 has a single curve, but R1 and
   R3 occupy **disjoint edit-time windows**, so points authored in each window never collide — one
   global curve is already **per-region-distinct** for free. "Bounded to the region duration" is a
   UI/editing-viewport constraint, not an audio one.
   - **Rejected:** `AutomationCurveModifier` / `AutomationCurveList` (per-clip curves + looping).
     It is beats-based (collides with decision 4), clip-owned + graph-driven, its ability to target a
     rack-branch plugin param is unverified, and it persists in a separate tree that would miss our
     sidecar persistence. Name it only as the fallback if "author once on a tone, auto-apply to every
     region using it" or per-region looping ever becomes a hard requirement.
   - **UX consequence to surface:** editing a reused tone's lane is **per region-instance**, not
     auto-replicated across the tone's other regions. Copy/paste of a region's curve segment is the
     pragmatic bridge (later, not first pass).

2. **Persistence is FREE — no new format.** The curve is an `IDs::AUTOMATIONCURVE` child `ValueTree`
   parented under `plugin->state` (`tracktion_AutomationCurve.cpp:15,373-387`;
   `tracktion_AutomatableParameter.cpp:992`), re-bound to its parameter by `paramID` on load
   (`:322`). RockHero already serialises the **whole** live plugin tree into the per-tone
   `.tracktion-plugin` sidecar (`engine_live_rig.cpp:687-694`; `flushPluginStateToValueTree` only
   adds properties, never rewrites children, `tracktion_Plugin.cpp:924-936`), so the curve rides
   along and re-binds on reload. Add nothing to the RockHero package format for curves.

3. **Undo = editor-core point-list mementos, applied via the `AutomationCurve` API with a null
   `UndoManager`.** DO NOT route curve undo through the plugin-chunk memento: the in-place restore
   path (`copyPluginStatePreservingInstanceId`, `engine_plugin_host.cpp:97-125`;
   `ExternalPlugin::restorePluginStateFromValueTree`, `tracktion_ExternalPlugin.cpp:1082-1141`)
   restores only the VST chunk + properties and **silently drops the `AUTOMATIONCURVE` child**, so a
   chunk-memento undo of a curve edit would no-op. Each curve edit is its own editor-core command
   whose memento is the parameter's point list `{musical_position, norm_value, curve_shape}`
   before/after; apply in the adapter via `curve.clear(nullptr)` then
   `curve.addPoint(TimePosition{seconds}, value, shape, nullptr)`
   (`tracktion_AutomationCurve.h:86-89`). Pass a **null `UndoManager`** everywhere (RockHero owns
   undo; matches the Phase-M decision). This matches the "params first-class, inverse commands,
   editor-core history" undo design already in place.

4. **Time base: musical is the source of truth; convert to edit seconds at the port boundary.**
   The base curve is seconds, and RockHero **never syncs its `TempoMap` into the Tracktion edit**
   (repo-wide: no `getTempoSequence`/`insertTempo`/`setBpm` in `rock-hero-common`), so
   `remapOnTempoChange` never fires — do not rely on it. Store authored points as musical
   `ToneGridPosition`-style positions (or fractional beats) in the RockHero model; **editor-core**
   converts musical↔seconds through the song `TempoMap`
   (`secondsAtGlobalBeatPosition` / `beatPositionAtSeconds`) and the port carries plain **seconds**
   (framework-free `double`). Grid-snap in musical space (Ctrl bypasses snap), then convert. On a
   tempo-map edit, re-derive seconds from the stored musical positions ourselves.

5. **Lane binding + value domain.** Key each lane by `paramID` + plugin instance id. Enumerate via
   `getFlattenedParameterTree()` (grouped, matches host UIs;
   `model/automation/tracktion_AutomatableEditItem.h:39`), filtering the two synthetic `dryGain`/
   `wetGain` params (`tracktion_ExternalPlugin.cpp:764-765`). **External-plugin params are normalised
   `{0.0, 1.0}`** (`tracktion_ExternalPlugin.cpp:804`), so lane Y is 0..1; render tick labels through
   `valueToString`, and treat discrete params via `isDiscrete()`/`getAllLabels()`. A lane whose
   `paramID` no longer resolves (VST2 index reordering; `:799-802`) must render **disabled**, never
   crash.

## Expert re-validation (2026-07-07 — corrections applied during execution; these win on conflict)

A second `juce-tracktion-expert` source pass (immediately before execution) confirmed the audio
reality and decisions 1–5 against the vendored source, with these load-bearing corrections.

- **P0 prerequisite — decouple the plugin-chunk undo domain from the `AUTOMATIONCURVE` child (not in
  the original plan; the single most important correctness item).** The existing plugin-parameter
  chunk-undo path `copyPluginStatePreservingInstanceId` (`engine_plugin_host.cpp:128-133`) does
  `removeAllChildren` + re-add-copies, and the runtime child-add rebind only fires for
  `IDs::name == paramID` (`tracktion_AutomatableParameter.cpp:1247`), never for an `AUTOMATIONCURVE`
  child (keyed by `IDs::paramID`). So any plugin-chunk undo/redo on a tone-branch plugin that also
  carries a curve orphans the live curve binding and clobbers the curve to the chunk snapshot. Fix
  (RockHero-side, and semantically correct — a curve is timeline data, not plugin-parameter state):
  strip `AUTOMATIONCURVE` children from the chunk-memento **capture** (`capturePluginState`,
  `engine_plugin_host.cpp:1239-1240`) and **skip** `AUTOMATIONCURVE` in both the remove and re-add
  loops of `copyPluginStatePreservingInstanceId`. Leave the per-tone **sidecar** capture
  (`engine_live_rig.cpp:687-688`) untouched — it must keep the curve for free persistence
  (decision 2). The two undo domains then provably never touch the same subtree.
- **Lane identity = `(instance_id, param_id)`, never `param_id` alone.** paramID is unique per plugin,
  not per branch; two VST2s in one chain can both expose paramID `"0"`. Add `instance_id` to
  `AutomatableParamInfo`, `AddToneAutomationLane`, and `RemoveToneAutomationLane`
  (`SetToneAutomationPoints` already carries it). instance_id = the owning plugin's Tracktion
  `itemID` string.
- **Picker is built from `getParameterTree()`, not `getFlattenedParameterTree()`** (the flattened
  call loses group names, `tracktion_AutomatableEditItem.cpp:117-127`). Recurse
  `getParameterTree().rootNode->subNodes` (`tracktion_AutomatableParameterTree.h:27-68`): `Group`
  node → submenu (`getGroupName()`), `Parameter` node → item. Filter the synthetic params by paramID
  string **"dry level"** / **"wet level"** (`tracktion_ExternalPlugin.cpp:671-672`), not the C++
  member names.
- **Curve write/read API (exact, non-deprecated).** Write: `auto& c = param->getCurve();
  c.clear(nullptr);` then per point `c.addPoint(EditPosition(TimePosition::fromSeconds(sec)),
  param->valueRange.convertFrom0to1(norm), shape, nullptr);` — the `EditPosition` overload
  (`tracktion_AutomationCurve.h:89`), NOT the deprecated `TimePosition` one (`:127`). Read:
  `for (int i=0; i<c.getNumPoints(); ++i)` → `c.getPointTime(i).inSeconds()` (`:121`),
  `param->valueRange.convertTo0to1(c.getPointValue(i))` (`:71`), `c.getPointCurve(i)` (`:72`). Curve
  values are parameter-native; always convert via `valueRange` (identity for external + branch-gain
  `{0,1}`, correct for any native-range plugin). curve_shape ∈ [-1,1], time ≥ 0. Resolve the param
  via `plugin->getAutomatableParameterByID(param_id)` (`tracktion_AutomatableEditItem.h:31`); null →
  render the lane disabled.
- **Port shape: a NEW framework-free `IToneAutomation` port**, not an `ILiveRig` extension (ILiveRig
  is a cohesive rig-lifecycle seam). One engine adapter implements both.
- **Threading:** editing curves while playing is safe (audio reads a decoupled snapshot; iterator
  rebuild deferred ~10 ms and swapped under `parameterStreamLock`). Keep `isReadingAutomation()`
  true. A test asserting on **processed audio** after a write must pump the message loop first;
  `readParameterCurve` reads the ValueTree directly so it round-trips synchronously.

## Implementation status (2026-07-07)

Built on branch `work-in-progress` (off `refactor`), each slice green + tested:

- **Audio adapter — DONE.** `IToneAutomation` port (`list/read/write`, normalised values, seconds)
  + engine impl; Tracktion-touching curve logic in `src/tracktion/tone_automation_curve.{h,cpp}`,
  headless-tested against an internal `VolumeAndPanPlugin` (round-trip, clear, unresolved id).
- **Port wiring — DONE.** Threaded through `Editor`/`EditorController`/`Impl`/`EditorEditContext`;
  `FakeToneAutomation` in the controller harness.
- **View state + projection — DONE.** `ToneAutomationViewState` on `EditorViewState`;
  `makeToneAutomationViewState` reads the selected tone's curves; unit-tested.
- **Editing + undo — DONE.** **Design simplification adopted:** a single
  `SetToneAutomationPoints` action + `ToneAutomationPointsEdit` point-list memento subsumes
  add/remove/edit lane (picking a param seeds a point → lane appears; clearing to empty → lane
  gone), so no separate add/remove-lane actions or open-set state are needed. `onToneAutomationPointsEditRequested` (named `onSetToneAutomationPoints` until the 2026-07-19 intent-vocabulary rename)
  intent wired through `IEditorController`; controller tests cover write→project and undo/redo.
- **P0 chunk-memento decoupling — DONE.** `capturePluginState` strips `AUTOMATIONCURVE`; the
  in-place restore preserves live curves — the two undo domains are provably disjoint.
- **Storage — SUPERSEDED (2026-07-07):** the first pass stored points seconds-based end to end;
  the settled decision below (Option B / C2) moves the model and memento to musical positions
  persisted in `song.json`, with the Tracktion curve as a derived cache. The seconds-based
  `IToneAutomation` adapter is unchanged.
- **C2 migration — DONE.** Musical `ToneParameterAutomation` model on `Arrangement`, `song.json`
  round-trip (`"toneAutomation"`, chart grid-token grammar), durable plugin ids
  (`PluginRecord.stable_id` minted in editor-core, echoed through capture, merged from
  `LiveRigLoadResult.tone_chains`), musical mementos, and the load-completion derived-curve
  rebuild; state hygiene strips `AUTOMATIONCURVE` + `remapOnTempoChange` at capture and load.
- **UI (slices B–D) — DONE.** `ToneAutomationLanesView` renders lanes under the tone region
  ("+" picker in the first empty lane), with click-add/move/delete gestures, grid snap with Ctrl
  fine placement, resizable lane heights, and full-list musical commits on mouse-up.
- **Acceptance revisions (2026-07-08, expert-reviewed for minimality):** the "+" picker is
  hierarchical — one numbered submenu per chain plugin (chain order) with the plugin's parameter
  groups nested inside — and lane chips read "Plugin · Param", so multi-plugin chains stay
  unambiguous (`AutomatableParamInfo.plugin_name`). Picking a parameter now opens a
  **live-tracking lane** instead of authoring a seed point: session-scoped open lanes
  (`m_open_automation_lanes`, not persisted, not undoable) draw a flat full-width line at the
  parameter's live value (`IToneAutomation::readParameterNormValue`, polled per vblank with a
  moved-value gate) until the first point is authored; right-click closes an unauthored lane.
  Snapped placement stores the grid line's exact musical fraction
  (`nearestTempoGridPosition`; odd grids like 1/13 round-trip exactly), and only Ctrl-free
  placement quantizes to the 1/960-beat fine grid. Authored curves extend flat to both canvas
  edges, and the tempo grid shows through the lanes (canvas paints the automation band).
- **Gesture revision (2026-07-09):** the lane gestures were re-based onto the editor-wide
  interaction model (`docs/plans/in-progress/editing-interaction-model.md`): point insertion is now
  Alt+click/Alt+drag (plain empty-lane clicks seek + deselect), Shift axis-locks point drags,
  Esc cancels an in-flight gesture, double-click reset moved to the point context menu, and a
  ghost point + `CopyingCursor` preview shows under Alt.
- **Slice E — host-tempo mirror — DONE (2026-07-07).** `ISongAudio::mirrorTempoMap` +
  `Engine::mirrorTempoMap` (message-thread/edit guards) delegate to
  `src/tracktion/tempo_mirror.{h,cpp}` `mirrorTempoMapIntoSequence`: one flat step per anchor
  span at quarter-note positions, non-remapping prune (`removeTempo(i, false)`), clamp-free
  first-entry CachedValue writes + `insertTempo(BeatPosition, bpm, 1.0f)` for the rest, and
  signature mirroring the same way. `RockHeroEngineBehavior::lengthOfOneBeatDependsOnTimeSignature()
  → false` pins the edit beat unit to quarters; the backing clip is pinned `syncAbsolute`
  (defense-in-depth — the mirror's write paths never run Tracktion's remap snapshot, verified at
  `EditTimecodeRemapperSnapshot::remapEdit`). Editor-core mirrors at the shared rig-load
  completion beside the derived-curve rebuild; a future tempo-editing flow must re-mirror there
  too. Tested: focused sequence-content/no-clamp/prune/guard/stability cases
  (`test_tempo_mirror.cpp`), Engine port integration case, and an editor-core
  mirror-at-load-completion case.

**All slices complete.** The remaining follow-ups live outside this plan: the capture-scope save
holes (separate work item) and the slim `docs/plans/todo` note on a future follow/stay warp toggle.
The UI notes below are retained for reference.

**The UI (slices B–D).** Automation lanes render beneath the tone region on the timeline,
one lane per automated parameter, drawing `ToneAutomationViewState` curves. The **"+" picker lives in
the first empty automation lane** directly under the tone region (NOT in the signal-chain panel —
user correction 2026-07-07): clicking it opens the parameter menu (`listAutomatableParameters`) and
choosing a parameter seeds the lane (`onToneAutomationPointsEditRequested` (named `onSetToneAutomationPoints` until the 2026-07-19 intent-vocabulary rename) with a seed point), after which the
"+" moves down to the next empty lane. Then: interactive add/move/delete points with grid-snap (Ctrl
bypass), vertically resizable lanes, and disabled/discrete rendering. The interactive lane is complex
graphics/UI — consult the `juce-tracktion-expert` before building it, per the standing rule. It
consumes the editor-core intent + view state, which move to musical positions per the settled
storage decision below (that backend migration precedes or accompanies the UI).

## Storage decision: SETTLED (2026-07-07) — Option B with musical truth in `song.json` (C2)

**Settled after a final-gate re-verification (third `juce-tracktion-expert` pass, run on Fable,
instructed to re-derive every inherited claim from vendored source and to try to break the design;
verdict: GO).** Automation is RockHero-owned musical data that follows grid/tempo-map edits. The
Tracktion base `AutomationCurve` (seconds) is a **derived playback cache**, rebuilt via
`writeParameterCurve` (musical→seconds through the song `TempoMap`) after load and on every
tempo-map edit, and **stripped from saved plugin state**. The seconds `IToneAutomation` adapter
stays exactly as built.

**Storage home: `song.json`, arrangement-scoped (C2) — the final gate OVERTURNED the earlier
nest-in-PluginRecord lean (C1):**

- **Fact — the persistence trigger decides it.** tone.json is written only by `captureActiveRig`,
  which captures **only the audible branch** and runs only on save/arrangement-switch. Automation
  undo/redo applies by stored tone ref to any loaded branch after the selection has moved, so under
  C1/C3 an automation change on a non-audible tone is silently lost on save. Under C2 the hole
  disappears structurally: undo mutates the session model, `song.json` persists from the model
  regardless of capture, and the live curve is only cache. Rule: **persistence never relies on
  capture; an automation undo updates the model (truth) and best-effort rewrites the live curve.**
- **Fact — port surface:** C1 needed `LiveRigLoadResult`/`LiveRigCaptureRequest` widened with
  per-tone opaque musical payloads both directions; C2 needs only an opaque id pass-through.
- **Judgment (ours):** automation is timeline data and belongs with tone regions in `song.json`;
  tone.json stays a pure, timeline-free rig preset — which directly serves the planned
  signal-chain export/import (an exported preset must not drag song-specific automation along).
- **C3 (RockHero child in `plugin->state`) rejected:** RockHero's own chunk restore deletes unknown
  children (the P0 preserve-matrix would grow), every child mutation fires `Plugin::changed()` into
  the dirty tracker (spurious chunk-undo entries), and the capture-scope hole remains.

**Plugin identity (the piece that previously sank song.json keying):** editor-core maintains the
runtime `instance_id ↔ automation` association in memory — sound because every chain mutation
(add/move/remove/undo-reinsert/load) goes through editor-core-initiated port calls — and persists a
**minted durable per-plugin id** through `PluginRecord` as an opaque pass-through (the existing
`block_indices`/`display_type_overrides` precedent). No RockHero identity is written into Tracktion
trees. Mint ids before any plugin-chunk memento can be captured in a session, or older mementos
lose them on restore.

**Load / tempo-edit rebuild:** editor-core-orchestrated, as a final cooperative load stage through
the existing seconds port, readiness-gated per plugin (`isInitialisingAsync()` false and
`getAutomatableParameterByID != nullptr`). On the only reachable path today (Windows; VST3/VST2/LV2
all instantiate synchronously in this vendored JUCE — async is macOS/AUv3-only) the gate is
trivially satisfied. Straggler policy for a future async format: lane renders `resolved=false`;
re-run the rebuild on the plugin's Selectable change after instance completion. **Never replace an
`AUTOMATIONCURVE` node to rebuild a curve** — the final gate proved there is NO live rebind path at
all (the `IDs::name` rebind branch is dead code for plugin params); always write through the bound
curve, which is exactly the committed adapter's path.

**Strip-on-save (expanded):** capture strips `AUTOMATIONCURVE` children (redundant derived data;
absence is clean, and an emptied curve otherwise leaves an empty parented husk) **and strips the
persisted `remapOnTempoChange="1"`** that `createNewPlugin` writes into every RockHero-created
plugin's state (`EngineBehaviour::arePluginsRemappedWhenTempoChanges()` defaults true and is not
overridden — a prior-pass error corrected by the final gate). Tempo-remap inertness rests on a
single invariant — **RockHero never writes the edit's tempo sequence** — which stays an enforced,
grep-able rule (no `getTempoSequence`/`insertTempo`/`setBpm` writes in `rock-hero-common`).

**Migration of already-built code:** editor-core action/memento/projection move from seconds to
musical positions (exact song-format grammar; sub-beat capable via the fractional substrate);
automation becomes part of the arrangement session model + `song.json` format; the UI snaps in
musical space and emits musical positions; `readParameterCurve` drops off the production path
(kept as a test affordance).

**Adjacent pre-existing capture-scope debt (found by the final gate; separate work item, NOT this
plan):** (1) plugin-chain edits on a non-audible tone are lost the same way automation would have
been (capture writes only the audible branch — and writes it to `arrangement->tone_document_ref`
rather than the region's tone ref, possibly unreconciled with the tone-catalog model); (2)
`reloadLiveRigForToneSet` (create/reset tone) reloads all branches from documents without capturing
first, discarding unsaved audible-tone chain edits.

## Architecture

### Audio adapter — the only Tracktion touchpoint (`rock-hero-common/audio`)

Add a small framework-free port (a new `IToneAutomation`, or additions to `ILiveRig` if it stays
cohesive) with **seconds-based, normalised-value** signatures:

- `listAutomatableParameters(tone_document_ref) -> std::vector<AutomatableParamInfo>` where
  `AutomatableParamInfo { std::string param_id; std::string name; std::string group; bool is_discrete;
  std::vector<std::string> labels; float default_norm_value; }`. Resolve the tone's branch, walk its
  user plugins, and for each build the flattened tree (minus dry/wet).
- `readParameterCurve(tone_document_ref, instance_id, param_id) -> std::vector<CurvePoint>` where
  `CurvePoint { double seconds; float norm_value; float curve_shape; }`.
- `writeParameterCurve(tone_document_ref, instance_id, param_id, std::span<const CurvePoint>)` —
  `curve.clear(nullptr)` then `addPoint(...)` per point; null `UndoManager`.

Implementation notes: resolve the branch by `tone_document_ref` (the branch already tracks its chain,
`multi_tone_rack.h:47-51`); resolve the `AutomatableParameter` by `param_id`
(`getAutomatableParameterByID`, `tracktion_AutomatableEditItem.h:31`). All message-thread. Curve
edits trigger an async iterator rebuild under `parameterStreamLock`
(`tracktion_AutomatableParameter.cpp:207-226,264,299`), so editing/undo while the transport runs is
safe. Keep automation **reading** enabled (defaults true; `AutomationCurveSource::setPosition`
early-returns if reading is off, `:246-249`).

### Editor-core — headless state, actions, mementos (`rock-hero-editor/core`)

- Model: `ToneAutomationLane { std::string param_id; std::string param_name; bool is_discrete;
  std::vector<ToneAutomationPoint> points; }`, `ToneAutomationPoint { <musical position>; float
  norm_value; float curve_shape; }`. Persisted? No — the curve lives in the tone's plugin state
  (decision 2); the model here is a transient editing projection, rebuilt from `readParameterCurve`.
- View state: `ToneAutomationViewState { std::vector<ToneAutomationLane> lanes; }` derived for the
  **selected region** and its time window; lane point seconds ↔ musical via the song `TempoMap`.
- Actions (each with a controller method + `performActionImpl` + memento, following the item-1
  pattern in `tone_handlers.cpp` / `tone_region_edits.{h,cpp}`):
  - `AddToneAutomationLane{ tone_document_ref, param_id }` / `RemoveToneAutomationLane{...}` — lane
    presence is editor state (which params are shown); adding a lane with no points writes nothing to
    the curve until the user authors a point.
  - `SetToneAutomationPoints{ tone_document_ref, instance_id, param_id, points }` — the edit command;
    memento captures before/after point lists (musical + norm). `undo`/`redo` convert to seconds and
    call `writeParameterCurve`.
- The controller converts musical↔seconds (it holds the `TempoMap`); the port stays seconds-only.

### UI (`rock-hero-editor/ui`)

- **Signal-chain panel:** title becomes `Signal Chain - <tone>` (the selected/audible tone — in
  item 1 selection == audibility). Add a **"+" button** that opens a `juce::PopupMenu` built from
  `listAutomatableParameters` (grouped via submenus); choosing a parameter emits
  `AddToneAutomationLane`.
- **Automation lanes:** rendered beneath the tone strip as full-width timeline rows aligned to the
  shared song timeline, drawn/edited **only within the selected region's window**. Each lane:
  reads its `ToneAutomationLane` from view state; draws the curve (Y = 0..1); supports add / move /
  delete point with **grid-snap in musical space unless Ctrl is held**; is **vertically resizable**
  by dragging its bottom edge (persist the height in UI state). Commit gestures emit
  `SetToneAutomationPoints`.
- The tone strip stays the collapsed state; selecting a region expands its lanes (disclosure), matching
  the old doc's visual model.

## Slices (sequenced; each builds green + tested before the next)

- **Slice A — read the curve.** Port `listAutomatableParameters` + `readParameterCurve`; editor-core
  projection + `ToneAutomationViewState` (musical↔seconds); read-only lane rendering under the strip.
  No editing. Engine + editor-core + a UI layout test.
- **Slice B — pick + add/remove lanes.** "+" picker in the signal-chain panel; `AddToneAutomationLane`
  / `RemoveToneAutomationLane`; `Signal Chain - <tone>` title.
- **Slice C — edit points.** `SetToneAutomationPoints` + point-list mementos + `writeParameterCurve`;
  add/move/delete with grid-snap (Ctrl bypass); undo/redo. This is the core of the feature.
- **Slice D — resizable lanes + polish.** Vertical resize; discrete-param rendering; disabled state
  for unresolved `paramID`; value-label ticks via `valueToString`.
- **Slice E — host-tempo mirror (scheduled last; closes the 120 BPM gap).** One-way, write-only
  mirror of the song `TempoMap` into `edit.tempoSequence` so tempo-synced plugins see real host
  tempo. Details in the dedicated end-of-plan section.

## Testing strategy

- **Adapter:** on a real `Engine` + `TemporarySongDirectory`, exercise `listAutomatableParameters`
  and read/write curve. Use the built-in `ToneBranchGainPlugin`'s parameter (or a simple internal
  plugin) as the target so no real VST is required; assert points round-trip and that a written
  curve persists through the tone sidecar (mirror the existing `mintEmptyTone` engine test style).
- **Editor-core:** headless tests for the projection (musical↔seconds via a pinned `TempoMap`) and
  for `SetToneAutomationPoints` undo/redo through a fake `IToneAutomation`. No JUCE.
- **UI:** component tests for lane layout/visibility, "+" picker wiring, and vertical resize.
- Never require real plugins or audio devices in normal tone-automation tests.

## Risks (from the review — bake these in)

1. **Memento-restore child-node gap.** Curve undo MUST use point-list mementos (decision 3); a
   chunk-memento restore silently drops the `AUTOMATIONCURVE` child. This is the single most important
   correctness constraint.
2. **Index-keyed VST2 `paramID`s.** Lane re-binding can mis-bind if a VST2 reorders params between
   save and load. Key by `paramID`, render unresolved lanes disabled. VST3 is safe.
3. **Author-once-per-tone expectation.** Editing a reused tone's lane is per-region-instance, not
   replicated. Document in-UI; copy/paste is the later bridge.

## Non-goals (first pass)

- No `AutomationCurveModifier` (per-clip curves / looping / auto-replication across a tone's regions).
- No reading of `edit.tempoSequence` back into RockHero state, ever — the tempo authority never
  moves. (The former blanket "no tempo-sync into the edit" non-goal is narrowed: the one-way
  write-only mirror is now IN scope as Slice E; what stays out of scope is reads and any authority
  transfer.)
- No exposure of Tracktion `Track`/`Clip`/`RackType`/`Plugin`/`AutomatableParameter` to editor UI code
  — the port is framework-free (seconds + normalised values + string ids only).
- No crossfade/tone-gain shaping on the region rectangles (separate future feature).

## Slice E — host-tempo mirror (end of plan; closes the 120 BPM gap)

**Problem (source-verified by the final-gate pass):** hosted plugins receive host tempo through
Tracktion's `PluginPlayHead`, which reads `edit.tempoSequence` — and RockHero never writes it, so a
tempo-synced plugin in a tone chain sees the default constant 120 BPM, never the song's tempo map.
For a guitar rig (host-synced delays and modulation), that is a real user-visible defect whenever a
song's tempo differs from 120.

**Fix: a one-way, write-only mirror.** After project load and after every tempo-map edit, rewrite
`edit.tempoSequence` from the song `TempoMap` — the same derived-output pattern as the automation
curve cache. Nothing ever reads the edit's tempo back into RockHero state; the timing authority
does not move.

Preconditions (all verified in the final-gate pass; each is load-bearing):

- **Non-remapping, non-clamping writes only.** `TempoSequence::insertTempo` neither remaps nor
  clamps; `TempoSetting::setBpm` / `setCurve` remap by default AND clamp BPM to [20, 300] (real
  imported charts exceed 300 BPM), so the official mutators must not be used.
- **Beat-unit translation.** Tracktion's default engine beat is time-signature-denominator-dependent
  (`LengthOfOneBeat::dependsOnTimeSignature`, an `EngineBehaviour` default), while RockHero's map is
  quarter-note-referenced: the mirror must translate quarter↔engine-beat, or set the
  `EngineBehaviour` quarter-note override (which is then fixed for the app's lifetime).
- **Pin the backing wave clip to absolute sync first.** Its default `syncType` is `syncBarsBeats`,
  so an unpinned clip would be beat-preservingly MOVED by tempo writes.
- **Scrub `remapOnTempoChange="1"` from plugin state** (strip-on-save already does this per the
  settled storage decision), so Tracktion never beat-remaps the derived automation curves under the
  mirror.
- **Invariant update:** "no edit-tempo writes in rock-hero-common" narrows to "edit-tempo writes
  happen only inside the mirror unit, and nothing anywhere reads `edit.tempoSequence` into RockHero
  state" — keep it grep-able.

Fidelity (resolved by the tempo-authority verification pass): RockHero's step tempo
(changes-only-at-anchors) maps **exactly in shape** to `TempoSetting` `curve = ±1.0` (one flat
section per span — Tracktion's own time-based `insertTempo` default). Interior segment BPMs pass
through float casts in the bezier-ends path (~1e-7 relative; microsecond-order drift over a song) —
irrelevant at this boundary, because nothing RockHero-side ever reads the mirror.

Tests: mirror a nontrivial map and assert the sequence contents (and, where feasible, the
play-head-reported BPM through a hosted stand-in); assert the backing clip's position and a derived
automation curve's seconds are unchanged across a mirror rebuild (no remap fired).

Bonus once landed: Tracktion's tempo-driven click track becomes usable for a future charting
metronome, partially refunding the "metronome won't come free" cost.
