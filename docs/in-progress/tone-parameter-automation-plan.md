# Tone Parameter Automation Plan

Status: **active, decisive** — 2026-07-07. A formal `juce-tracktion-expert` source review is complete;
its load-bearing findings are inlined below with `file:line` citations and are treated as settled.
This plan is written to be executed by Opus without further design discovery: every open question
from the old reference is resolved here.

This supersedes `docs/todo/tone-automation-track-plan.md` (a read-only UX sketch full of "eventually"
and open questions). It is built directly on the shipped item-1 tone feature (tone-change model,
catalog, six operations, and UI gestures — all on `refactor`; see
[[project_tone_catalog_model]] in agent memory and `docs/in-progress/tone-track-tempo-map-plan.md`).

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
- No tempo-sync of RockHero's `TempoMap` into the Tracktion edit.
- No exposure of Tracktion `Track`/`Clip`/`RackType`/`Plugin`/`AutomatableParameter` to editor UI code
  — the port is framework-free (seconds + normalised values + string ids only).
- No crossfade/tone-gain shaping on the region rectangles (separate future feature).
