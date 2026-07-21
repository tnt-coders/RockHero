# 53 — Editor keyboard + pointer completion

Status: **Executing — Phases 0 and 1 complete 2026-07-20** (Phase 0: G53-FOLD-IN and G46-KEYMAP
closed, keymap matrix signed, 46-Q2 = parallel systems + extraction watch-item; Phase 1: the
command registry spine, keymap persistence, the custom themed actions dialog
(`ActionsWindow`/`KeymapEditorView`, branch `custom-keybind-menu`; renamed from "Keyboard
Shortcuts" with a `?` default chord 2026-07-20), and the generalized plugin-window shortcut
mirror all landed as plan 46 Phases 1–5 — execution records there. 46-Q3 dissolved twice over:
the non-rebindable-trio decision was reversed the same day, so every command is rebindable with
the trio mirrored live into plugin windows; the manual plugin verification passed 2026-07-20).
**Phase 1b (total rebindability) executed 2026-07-21** — the grammar decoder dissolved into
49 per-verb commands, everything rebindable, one dispatcher.
**Phase 3 landed ahead of sequence, and Phase 7's keyboard half landed** (record below).
Next: Phase 2 here (discovery menus). This plan builds the complete editor
keybinding + mouse-operation model captured in `docs/plans/in-progress/keymap-matrix.md` and the
interaction-model fold-in.

**Standing sync rule (binding for every phase):** the change set that completes a phase also
updates this Status line, the plan's row in `docs/plans/roadmap/00-roadmap.md`'s status table,
and flips the affected `keymap-matrix.md` rows to Live. The matrix stays alive as the
build-tracking artifact until this plan completes, then dissolves into the authority docs
(amending Phase 0's original dissolve-now instruction).

**Standing convention — every action is a registered command (adopted 2026-07-20).** Every new
user-triggerable verb this plan ships (and all future editor work) registers in the command
registry (`EditorCommandId` + registry row + `getCommandInfo`/`perform`) rather than as an
ad-hoc handler, even when it has no default chord. This is REAPER's "Actions" model in JUCE
form: the registry is one trigger-agnostic action list, and keyboard chords, menu items,
discovery menus, and future binding front-ends are interchangeable triggers over it
(`ApplicationCommandManager::invokeDirectly` fires any command from any source). Only
registered commands appear in the actions dialog, display live shortcut text in menus, and
become MIDI-bindable (`docs/plans/todo/midi-command-bindings.md`); a verb that bypasses the
registry is invisible to all of them, and retrofitting at a hundred commands is what this
convention avoids. The grammar decoder is the one deliberate carve-out — our equivalent of a
REAPER context *section*: its keys stay reserved and its context-ordered dispatch stays
sequential. Registering grammar verbs as commands that *delegate into* the decoder (keys stay
reserved; the verbs become listable and MIDI-bindable) is the recorded future path, owned by
the MIDI todo plan.

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
- **E2** — **keep `Ctrl+T`** (from anywhere) alongside the tone-row caret insert;
  fix the `Ctrl+Alt+T` bug by guarding `Ctrl+T` against `Alt`, not by removal. **Amended
  2026-07-21 (user-signed): the anchor is the marker rule** — the armed caret when one exists,
  else the transport position — extending play-from-the-marker's one-position-concept to the
  insert; the original "playhead" (raw transport) anchor predated that unification. Command
  named "Insert Tone Change at Cursor".
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
> refinement recorded in plan 46). Plan 46 Phases 2–5 subsequently landed the same day
> (persistence; the custom `KeymapEditorView` dialog on branch `custom-keybind-menu`; the
> generalized plugin-window mirror with the hardcoded predicates deleted — manual plugin
> verification passed 2026-07-20). The planned forwarding removal was revoked: the
> `MainWindow::keyPressed` forwarding is load-bearing for grammar keys at shell focus.

Stand up an `EditorCommandManager` over one `juce::ApplicationCommandManager` + `KeyPressMappingSet`
in a new `rock-hero-editor/ui/src/keybinds/`. Register every command; map to controller intents.
Migrate the scattered `keyPressed` handling out of `editor_view.cpp` and **delete** the duplicated
predicates (`isUndoShortcut`, etc.). Apply the settled matrix as the default map. Guard `Ctrl+T`
against `Alt` (E2). The grammar keys (digits, arrows, Esc, Delete, Insert) stay in the decoder
with their chords reserved; the core trio's short-lived non-rebindable registration (the first
46-Q3 resolution) was reversed the same day — every command is rebindable, `Ctrl+Shift+Z` joins
`Ctrl+Y` as a Redo alternative, and the trio's live chords mirror into plugin windows via plan
46's generalized Phase 4. **This phase gates everything below** — the discovery menu, rebindable
defaults, and per-surface front-ends all consume the registry.

### Phase 1b — Total rebindability: every grammar verb becomes a command *(user direction 2026-07-20)* — EXECUTED 2026-07-21

> **Execution record (2026-07-21):** landed as planned, 49 new commands (10 Navigation, 17
> Selection, 8 Editing, 10 Value Entry, 4 Grid & Zoom; id blocks 0x1501/0x1601/0x1701/0x1801/
> 0x1901). Deltas from the plan text: the count came to 49, not ~41 (the Ctrl jump aliases and
> key-shape unions became alternative chords, but the fine tiers and per-digit commands add
> up); Esc/Delete/digits (and every silent-decline verb) register **always-active and
> self-gate in perform**, because a disabled command whose chord matches makes JUCE play the
> system alert sound (`playAlertSound` = `MessageBeep` on Windows) — the enablement-based
> fall-through stays reserved for real needs (the future chain-scope section), and idle
> Esc/Delete are now consumed silent no-ops instead of propagating (nothing downstream
> consumed them). Deletions all landed: the decoder, `grammar_reservations.{h,cpp}`, the
> dialog's fixed section and reservation refusals, the `MainWindow::keyPressed` forwarder.
> Tests: locked table pins all 64 commands; the digit test moved to mapping-set dispatch; a
> new test proves an arrow chord moves to another command through the one-owner dance and
> comes back on per-command reset. Full build + editor ui suite green.
>
> **In-action refinement (2026-07-21, user review):** (1) The `+`/`-` chord sets corrected —
> vendored-source verification showed JUCE's Windows path delivers numpad add/subtract as
> their plain character key codes (`doKeyDown` has no VK_ADD/SUBTRACT case; `doKeyChar`'s
> numpad remap covers digits only), so the registered `numberPadAdd/Subtract` chords never
> matched and were removed as lying entries — the bare `'+'`/`'-'` chords ARE the numpad
> bindings. GridFiner = Shift+`=` (main-row `+`), `'+'` (numpad arrival), `'='` (unshifted
> convenience alias, user-kept); the Shift+`'-'` (`_`) ride-along dropped. (2) The duplicate
> "+" chip fixed structurally: **display-equal chords group into one chip** (and one menu
> shortcut entry) whose change/remove operate on the whole group —
> `applyBindingChange`/`removeBindings` take index sets now, so no ghost binding survives a
> visible removal. (3) Arrows render as glyphs (←/→/↑/↓) in the shared formatter, replacing
> JUCE's "cursor left" wording — **revised same day to bare direction words** ("left",
> "ctrl + right"): the glyphs fell to font substitution in the running editor and the
> alternatives fail too (filled triangles rejected by the user; heavy arrows risk color-emoji
> presentation). The Shift+`'-'` (`_`) drop was also **reversed same day**: `_` and
> `Ctrl+_` stay as convenience aliases for Grid Coarser / Zoom Out until something better
> claims them, symmetric with the `=` alias on the plus side. (3b) **Display convention
> normalized to capitalized tight style with the middle-dot house separator** ("Ctrl·Shift·Z",
> "Space", "Esc", "Page Up", "Num 5") — JUCE's lowercase "ctrl + z" was its own idiosyncrasy,
> not a convention; the "+" joiner was tried first but collides with the `+`/`-` keys
> ("Ctrl++"), and after weighing "Ctrl+Plus" naming, segmented keycaps (rejected as odd here),
> and spaced/worded variants, the user chose the dot ("Ctrl·+") — U+00B7 MIDDLE DOT, Latin-1
> so immune to the font substitution that killed the arrow glyphs. Display-only, the stored
> keymap XML keeps JUCE's spellings, which its parser requires.
> (4) The "Editing" category renamed **"Authoring"** (collided
> with the Edit menu category) and Cancel/Clear recategorized under Selection (its
> user-visible rungs disarm the caret and clear the selection; id blocks are historical
> hints). (5) Show Waveform gained its `F5` default.

**Decision (reverses the 2026-07-20 "fixed + listed" grammar policy, user-directed):** every
keybind is editable — the grammar keys included. "If a user makes bad keybinds that's their
problem; they have the defaults to fall back on." The composed modifier algebra survives as the
*shape of the default map*, not as an enforced restriction.

**The verified mechanism (juce_KeyPressMappingSet.cpp:322-357):** the mapping set's dispatch
loop visits **every** command mapped to a chord, **skips disabled commands and keeps looking**
(:340-346), and returns **false** when nothing enabled fired — the key falls through to other
handlers. So per-verb commands with state-derived enablement reproduce the decoder's two
load-bearing behaviors natively: context-ordered dispatch (enablement picks the live verb) and
decline-and-fall-through (Esc with no rung, Delete with no selection, digits with no surface).
This also means one chord *can* legally serve several context-exclusive commands — the future
mechanism for the Phase 5 chain scope's REAPER-style section, though today every chord keeps
exactly one owner and the dialog's one-owner law stands unchanged.

- **Split per chord-verb, not per key**: each (chord → verb) pair in the decoder becomes its
  own command, including the Ctrl precision/reach tiers as separate commands (caret step vs.
  measure jump; selection move vs. fine move; sustain adjust vs. fine; extend by grid vs.
  measure). The full inventory from `EditorView::keyPressed` (~41 commands): caret step ×4,
  measure jump ×2, extend grid ×2 / measure ×2 / section ×2 / bounds ×2, selection move ×4 +
  fine ×4, sustain adjust ×2 + fine ×2, fret shift ×2, type-digit ×10 (numpad chords as
  first-class aliases), grid finer/coarser ×2 (the '='/'+'/numpad union becomes alternative
  default chords), zoom in/out ×2, jump start/end ×2 + prev/next section ×2 (Ctrl aliases as
  alternative chords), delete-selection, neutral-insert, escape. New append-only id blocks
  (0x1501 navigation, 0x1601 selection/authoring, 0x1701 payload/grid) — record in the enum's
  growth conventions.
- **Perform = the decoder branch moved verbatim**: each command's perform emits the same
  controller intent (Esc keeps its view-gesture cancels first; digits keep the
  lane-then-chart try order inside one perform). Undo/gesture semantics are untouched — only
  the trigger moves. Enablement mirrors today's guards: Esc active only when a rung is live,
  Delete only with a selection, digits when a typing surface exists; verbs that forward
  unconditionally (core self-gates) register always-active.
- **Deletions**: the `EditorView::keyPressed` grammar decoder; `grammar_reservations.{h,cpp}`
  (the reservation predicate, the capture/apply refusal, the dialog's fixed section — grammar
  rows become ordinary rebindable rows under Navigation/Selection/Editing/Grid categories);
  the `MainWindow::keyPressed` manual forwarding (its sole purpose was reaching the decoder
  from shell focus; with everything commands, the window's mapping-set listener covers it —
  the original plan 46 Phase 3 removal, finally earned).
- **Dialog**: ~56 rows; verify scrolling/size still work; reference-section tests retire in
  favor of ordinary-row coverage.
- **Docs**: amend `editing-interaction-model.md`'s fixed-binding rule (grammar semantics and
  default shape unchanged — this conversation is the user confirmation), keyboard-input.md's
  two-dispatcher model collapses to one, keymap-matrix rows, plan 46 grammar-policy record
  gets the reversal note.
- **Tests**: locked table grows the full inventory; grammar dispatch tests move from
  `view.keyPressed` to mapping-set dispatch; fall-through coverage (disabled Esc/Delete
  propagate); rebind-a-grammar-verb coverage.

Depends on Phase 1 (done). Gate: none — user-directed 2026-07-20; execute next.

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
