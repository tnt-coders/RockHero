# 53 — Editor keyboard + pointer completion

Status: **Executing — Phases 0 and 1 complete 2026-07-20** (Phase 0: G53-FOLD-IN and G46-KEYMAP
closed, keymap matrix signed, 46-Q2 = parallel systems + extraction watch-item, 46-Q3 dissolved
by the non-rebindable-core-trio decision; Phase 1: the command registry spine landed as plan 46
Phases 1+5, plan 46 Phase 2's keymap persistence landed the same day, and plan 46 Phase 3's
shortcuts dialog (stock component, themed shell) is code-complete pending the user's in-action
review — execution records there). **Phase 3 landed ahead of sequence, and Phase 7's keyboard
half landed** (record below).
Next: Phase 2 here (discovery menus). This plan builds the complete editor
keybinding + mouse-operation model captured in `docs/plans/in-progress/keymap-matrix.md` and the
interaction-model fold-in.

**Standing sync rule (binding for every phase):** the change set that completes a phase also
updates this Status line, the plan's row in `docs/plans/roadmap/00-roadmap.md`'s status table,
and flips the affected `keymap-matrix.md` rows to Live. The matrix stays alive as the
build-tracking artifact until this plan completes, then dissolves into the authority docs
(amending Phase 0's original dissolve-now instruction).

## Landed ahead of sequence (2026-07-19/20, pre-registry)

Built directly in `EditorView::keyPressed` before Phase 1, while the registry decision was still
gated; Phase 1 migrates these bindings onto the registry like every other existing shortcut:

- **Phase 3 — fully landed.** Grid `+/-` including `Shift+=` and numpad (23d3ed7b, via
  `GridSpacingSelector::stepNoteValue`); `Ctrl+=/-` zoom sharing the `Ctrl`-wheel path
  (44f24ab6, `TrackViewport::zoomByStep`/`applyZoomAroundCursor`); `Home`/`End` +
  `Ctrl` aliases and `PageUp/Dn` section jumps via the `onChartCaretJumpRequested` intent with
  one `ChartCaretJump` sum type (ae0e7ad5); ladder-end + exact-rational-compare fixes
  (16a544b4). The planned `onSectionStepRequested`/`onChartBoundsRequested` intents shipped as
  the single jump intent instead — the phase text below reads historically.
- **Phase 7 — keyboard half landed.** The grid-locked `TimeSelection` as a mutually-exclusive
  `EditorSelection` kind with all four `Shift`+extend families (grid / measure / section /
  bounds) in 759b145f; transport-stranding + zero-width-collapse fixes in fd043657.
  Conservative defaults accepted 2026-07-20 as placeholders pending plan 52 (typing inert,
  Delete no-op, paused-only extend). Remaining in Phase 7: the ruler drag (with plan 47's loop
  wiring) and plan 52's range-verb consumption.

## Goal

Ship the entire agreed editor keyboard + pointer surface as one coherent, sequenced build:

- a central **command registry** (extends plan 46) that every keybind rides, with rebindable
  defaults and a **keybind-discovery context menu** on every surface;
- **navigation-reach** (measure/section/chart-bounds jumps, `Home`/`End`) and **GP-style
  zoom/grid** keys;
- the **vertical surface stack** — chart strings ↔ **tone-region row** ↔ automation lanes — with
  plain `↑/↓` crossing and `Ctrl+↑/↓` surface-jump;
- the **modal plugin-chain scope** (a separate focus, `Enter`-in/`Esc`-out, with the mandatory
  loud active-scope indicator);
- **automation-lane creation** (the "+ add automation" row) and **point multi-select**;
- a **grid-locked `TimeSelection`** as a mutually-exclusive kind of the one editor-wide selection,
  with `Shift`+arrow extension (amends plans 47/52);
- **chart pointer drag-move** (owned by `docs/plans/todo/tab-pointer-drag-editing.md`, scheduled).

The design is authoritative in `editing-interaction-model.md` (grammar) and
`chart-span-and-selection-model.md` (marker/selection); this plan is the *execution* wiring.

## Constraints

- Editor-only. `rock-hero-common` stays editor-free; the command registry lives in
  `rock-hero-editor/ui` (per roadmap tension #6 — the injected-binding-data seam if any registry
  concept ever needs common, but nothing here does).
- One `juce::ApplicationCommandManager` + `KeyPressMappingSet` is the single source of key→command
  mapping; the scattered predicates in `editor_view.cpp::keyPressed` are migrated onto it and
  deleted, not duplicated.
- The `EditorActionId` enum is the *controller-action* identity — do **not** conflate it with the
  registry's command id. Commands map to controller intents; they are different layers.
- Undo stays global and untouched: keyboard/pointer front-ends emit the **existing** controller
  intents and edits, so cascade/undo semantics (plugin remove, reorder, tone merge) are inherited,
  not re-implemented.
- Follows the settled interaction grammar exactly; any divergence is a doc bug, fix the doc first.

## Verified current-state inventory (re-stamp before executing)

From the two grounding passes (2026-07-20):

- **No command registry today.** `EditorView::keyPressed` (`editor_view.cpp` ~989–1203) hand-dispatches
  `Ctrl+Z/Y`, `Space`, arrows (+Ctrl/Shift/Alt combos), digits, `Delete`, `Insert`, `Ctrl+T`, `Esc`,
  `F3`, `F8`. No `rock-hero-editor/ui/src/keybinds/` exists.
- **Vertical row axis** is `stepCaretRow` (`editor_controller.cpp` ~2314–2353); plain `↑/↓` cross
  chart↔lanes only (tests `test_chart_editing.cpp:624–633,856–857` are within-surface and survive).
  `Ctrl+↑/↓` forwards a **dead** `measure` flag (safe to repurpose).
- **Signal chain** (`SignalChainView`/`SignalChainPanel`) is shipped but **keyboard-less** — every
  control `setWantsKeyboardFocus(false)`, no `keyPressed`, no slot-focus concept. Move/open/insert/
  remove **intents already exist** (`onMovePluginPressed`/`onOpenPluginPressed`/
  `onInsertPluginPressed`/`onRemovePluginPressed`); plugin-remove already cascades to automation
  (`signal_chain_handlers.cpp:582–617`); reorder routes through the audio boundary (instance-preserving).
- **Selection** = `EditorSelection` sum type, timeline-only (`editor_selection.h:44–52`);
  `clearCursorCoupledSelection` (`editor_controller.cpp:1666–1670`) clears tone-region/point
  selection on any cursor move (the rule the tone-region row must split). No `TimeSelection`
  alternative yet; plan 47's `LoopSelectionViewState` is unbuilt.
- **Automation lanes**: `AutomationPointSelection` is a single point (must become a set, mirroring
  `ChartSelection`); lane-add intent exists (`onToneAutomationLaneAddRequested`).
- `Enter`/`returnKey` is unbound in the editor keymap (free); `[ ]`, `Home/End`, `PageUp/Dn` unbound.

## Fold-in resolutions carried by this plan (A–H, settled 2026-07-20)

Full text in `keymap-matrix.md` → *Fold-in issue resolutions*. Summary of the load-bearing ones:

- **A2** — the plugin chain is a **separate modal focus scope**, NOT a variant of `EditorSelection`
  (no `PluginSlotSelection`). Verbs are scope-dependent; the timeline selection is *parked*, not
  cleared, while the chain has focus.
- **B** — the tone-region row's armed caret selects the **containing region** (a span,
  `ToneRegionSelection`, which exists); a deliberate caret step **re-derives** it (rides), passive
  transport still clears — the `clearCursorCoupledSelection` split. No new marker *kind*. Resize
  moves the region's **end** boundary (note-sustain parallel).
- **C** — strict grid-lock + operation-`Ctrl`: the ruler drag is a *selection* using grid-locked
  semantics (not `placementModeFor`); `Ctrl+ruler-drag` = measure-snap; a time-selection anchored
  from an off-grid caret snaps to grid. **Amends plan 47** (drop its `Ctrl`-off-grid endpoints).
- **D** — object- and time-selection are **mutually exclusive** kinds; resolves plan 52 Q6/Q10/Q12;
  overrides its "complements, not competitors."
- **E2** — **keep `Ctrl+T`** (playhead insert, from anywhere) alongside the tone-row caret insert;
  fix the `Ctrl+Alt+T` bug by guarding `Ctrl+T` against `Alt`, not by removal.
- **F** — "Insert never mutates" gains **one** named exception (filled plugin slot = replace-with-
  confirm); the tone-row split is a *create*, inside the rule.
- **G** — the chain must render a **loud active-scope indicator** (slot focus ring + active panel +
  de-emphasized timeline); `Enter` escalates (tone region → chain → plugin window), `Esc` unwinds.
- **H** — cleanup: `+/-` is now grid (re-home the deferred GP-style `+/-` sustain-entry note);
  reconcile the `watch-items.md` sustain tail-drag entry (edge-drag dropped, `Alt`+wheel covers it).

## Phased implementation (dependency-ordered — registry first)

### Phase 0 — Docs + plan amendments (no code) — COMPLETE 2026-07-20

Close the design. **G46-KEYMAP** (plan 46 Phase 0 gate) is answered by the settled matrix — record
it. Apply the interaction-model + chart-span fold-in (the fold-map in `keymap-matrix.md`), amend
plans 47 (strict grid-lock, drop `Ctrl`-off-grid range endpoints, two-kind framing), 52 (record D
resolving Q6/Q10/Q12, drop "complements"), and 46 (discovery-menu scope, `Ctrl+T` guard-not-remove,
adopt the matrix as the default-keymap appendix). Do the H cleanup. Dissolve `keymap-matrix.md`
into the authority docs once its content has landed. **Gate: user sign-off on the fold-in wording
(the source-of-truth rewrites — the `Ctrl` thesis, the one-selection law, the marker model).**

### Phase 1 — Command registry mechanism  *(extends plan 46, all its phases)* — REGISTRY SPINE COMPLETE 2026-07-20

> Landed as plan 46 Phases 1+5 (execution record there): the command manager, registry table,
> mapping-set attachment, menu migration, predicate deletion, and the settled default map.
> Grammar keys deliberately stay in the grammar decoder with their chords reserved (the
> refinement recorded in plan 46). Still open from plan 46: Phase 2 (persistence), Phase 3
> (shortcuts dialog + forwarding removal), rescoped Phase 4 (plugin-window redo alias +
> predicate dedupe).

Stand up an `EditorCommandManager` over one `juce::ApplicationCommandManager` + `KeyPressMappingSet`
in a new `rock-hero-editor/ui/src/keybinds/`. Register every command; map to controller intents.
Migrate the scattered `keyPressed` handling out of `editor_view.cpp` and **delete** the duplicated
predicates (`isUndoShortcut`, etc.). Apply the settled matrix as the default map. Guard `Ctrl+T`
against `Alt` (E2). The core trio (Undo/Redo/Play-Pause) and the grammar keys (digits, arrows,
Esc, Space, Delete, Insert) register as **non-rebindable** rows (the 2026-07-20 46-Q3 resolution);
`Ctrl+Shift+Z` joins `Ctrl+Y` as a Redo alternative, with the plugin-window mirror update riding
plan 46's rescoped Phase 4. **This phase gates everything below** — the discovery menu, rebindable
defaults, and per-surface front-ends all consume the registry.

### Phase 2 — Keybind-discovery context menus (every surface)  *(extends plan 46)*

Build the net-new chart-lane menu; route the existing tone/automation menus through registered
commands. Each item shows its **live** shortcut (JUCE `PopupMenu::addCommandItem` reads the current
mapping — stays correct after a rebind). Depends on Phase 1.

### Phase 3 — Navigation-reach + zoom/grid behaviors — LANDED AHEAD OF SEQUENCE (see record above)

New intents `onSectionStepRequested` / `onChartBoundsRequested` (sections read from `song.h`; note
`PageUp/Dn` may read oddly until plan 40 Phase 8 renders section markers — flag). Reuse
`onGridNoteValueChangeRequested` for `+/-` grid and `onTimelineZoomChanged` for `Ctrl+=/-`; add the
missing `Ctrl`-wheel branch in `track_viewport.cpp::mouseWheelMove`. `Home`/`End` (+ `Ctrl` aliases).
Depends on Phase 1.

### Phase 4 — Cross-surface vertical nav + tone-region row  *(the B marker model)*

Re-target `stepCaretRow` so plain `↑/↓` cross chart↔tone-region↔lanes and `Ctrl+↑/↓` surface-jump;
thread the **tone-region row** in between. Give the tone strip keyboard focus + `keyPressed`; wire
the armed caret to derive `ToneRegionSelection` (the containing region) and **ride** on deliberate
caret steps (split `clearCursorCoupledSelection`: deliberate-nav rides, transport clears). Keyboard
front-end over the existing tone intents (create/split, merge/delete, boundary-resize = move the
end boundary, `Shift+Alt` + `Ctrl` fine). Keep within-string tests; add crossing + ride tests.
Depends on Phase 1; interlocks Phase 5 (the `Enter` target).

### Phase 5 — Plugin-chain keyboard model  *(A2 + F + G; reserve its own split number)*

Introduce a **separate signal-chain focus scope** (NOT part of `EditorSelection`): a selected-slot
state on `SignalChainView` (view-state + keyboard focus + paint), drill-in (`Enter` from a selected
tone region) / drill-out (`Esc`), which **parks** the timeline selection. Keyboard front-end over
the existing move/remove/insert/open intents: `←/→` slots, `Alt+←/→` reorder (same instance-
preserving `MovePlugin` contract as the drag path), `Enter` open window (empty→picker), `Insert`
picker (empty=create / **filled=replace-with-confirm**, F), `Delete` remove-with-confirm (the
confirm justified by slow undo-reload). **Loud active-scope indicator is mandatory** (G): slot focus
ring + active chain panel + de-emphasized timeline. Depends on Phases 1 + 4.

### Phase 6 — Automation "+ add" row + point multi-select

Always-present focusable **"+ add automation" row** (`Enter`/`Insert` → plugin→parameter picker via
`onToneAutomationLaneAddRequested`; descent never skips an empty automation surface; the no-plugins
edge points to the chain). Promote `AutomationPointSelection` from a single point to a sorted-unique
**set** (mirror `ChartSelection`); add `Ctrl`+click toggle + marquee on lanes. Depends on Phase 1;
interlocks Phase 4.

### Phase 7 — Time-selection grid-locked span + `Shift`+arrows  *(amends plans 47/52; D + C)* — KEYBOARD HALF LANDED (see record above)

Build plan 47's `LoopSelectionViewState` as a **strictly grid-locked span**; add a `TimeSelection`
alternative to `EditorSelection` and wire **mutually-exclusive** two-kind dispatch (making a time
selection clears the object selection and vice-versa; the loop region stays a separate transport
state). `Shift+←/→` extend by grid, `Shift+Ctrl+←/→` by measure, `Shift+PageUp/Dn` by section,
`Shift+Home/End` to bounds; `Shift+↑/↓` unbound. Ruler drag creates it (grid-locked; `Ctrl` =
measure-snap). Copy/paste consumes it (plan 52). Depends on Phases 1 + 3.

### (Out of scope here) — Chart pointer drag-move

`docs/plans/todo/tab-pointer-drag-editing.md` owns it (drag a note to reposition; plain = grid,
`Ctrl` = off-grid; sustain edge-drag **dropped** — `Alt`+wheel covers it). Promote that plan when
scheduling; it reuses the lane-pointer state machine.

## Deferred enhancements (revisit after the surface settles)

Plugin **bypass/enable** toggle (a new state-toggle verb-class + `PluginBypassEdit`), `Ctrl+D`
duplicate plugin, `Ctrl+C/V` copy-paste plugin/chain (needs an editor clipboard), the targeted
`Ctrl+↑` drill from a plugin to its lanes, `Ctrl+Alt+←/→` move-to-end, chain-level A/B snapshots,
GP-style digit tone-pick on the tone row.

## Dependencies / sequencing

`Phase 0 (sign-off) → Phase 1 (registry, gates all) → { Phase 2, Phase 3, Phase 6 } in parallel →
Phase 4 → Phase 5 (needs 4) ; Phase 7 needs 1 + 3`. Phases 4, 5, 7 carry the design-sensitive
subsystems and each want their own review. Phase 5 is the largest self-contained chunk — reserve a
split-out number if it grows.

## Final acceptance bundle

Per-phase: the sanctioned RockHero verification (full build, touched tests, clang-tidy, pre-commit)
as four separate invocations; new Catch2 tests beside the code they validate (registry mapping;
tone-row ride + seams; chain drill/park/confirm; time-selection mutual exclusivity + grid-lock;
point multi-select). End-to-end: drive each surface's keyboard + pointer path in the real editor
and witness the feel (the loud chain indicator especially). No phase is "done" until its matrix
rows behave as written and the interaction-model doc matches.

## Open decision (Phase 0 gate)

**G53-FOLD-IN** — user signs off the source-of-truth *wording* of the fold-in (the `Ctrl`-thesis
rewrite from "one meaning" to the operation partition, the one-selection→timeline-selection
rewording, the third marker generalization). Content is settled (A–H); only the authoritative
phrasing awaits confirmation before it is baked into `editing-interaction-model.md`.
