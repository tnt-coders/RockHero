# Timeline editing interaction model

Status: **settled direction, 2026-07-09; keyboard grammar amended 2026-07-16** (with the user,
during note-editing bring-up): plain keys never mutate — Alt is the authoring modifier for
keyboard input, arrows are pure navigation, and the original "plain arrows nudge the selection"
assignments (including the automation lanes' shipped 2026-07-09 behavior) moved under Alt. The
amendment record is inline below. Note authoring now implements this model; anchor editing
adopts it when built. **Amended again 2026-07-18** (unified selection, the marker's row axis,
keyboard-first automation, on-line point placement, the Insert verb — amendment record at the
bottom; implemented the same day).

## Goal

One interaction grammar for inserting, deleting, and editing every object on the timeline — tone
changes, automation points, and (future) notes, tempo anchors, and time-signature changes. The
model optimizes for consistency first and accident-resistance second, calibrated to the fact that
unified undo/redo already makes any single accidental edit cheap to recover from.

## The timeline-object abstraction

Every editable thing on the timeline is a **timeline object**: a musical position, plus optionally
a vertical coordinate (lane / string / value), an extent (sustain, span), and a payload (fret, BPM,
tone ref, techniques). The grammar below is defined once over this abstraction; each surface only
decides what its objects are.

- **Tone changes are the editable entity, not regions.** The format speaks
  `toneChanges: [{start, tone}]`; regions are the rendered spans between changes. Inserting a
  change splits a region, deleting one merges its region into the previous one, and dragging one
  moves the shared boundary. This makes the tone strip "a strip with point objects on it" —
  grammatically identical to an automation lane.
- **Notes** (future) are point objects in a 2D field (time × string) with an extent (sustain) and
  a payload (fret, techniques).
- **Tempo anchors** (future) are point objects on the ruler with *two* coordinates: a musical
  position (a beat — anchors define the grid) and an absolute audio time. See
  [Anchors](#anchors-free-time-by-nature) for how that changes their drag semantics.

## One meaning per modifier

Each modifier has exactly one meaning, everywhere:

| Modifier | Meaning |
|---|---|
| **Ctrl** | Precision: bypass grid snap (placement and moves quantize to the 1/960-beat fine grid) — **uniform on every surface** including chart notes (the off-grid unification, 2026-07-18 evening, superseding the same morning's grid-native-authoring exception: snap *default* follows the data's judge, the capability is universal). On the tab lane Ctrl additionally means the selection toggle (on clicks) and the measure jump (on plain arrows) |
| **Alt** | Author: the gate that makes pointing-device and arrow input mutate — a held "pencil" quasimode for the pointer (click inserts, wheel adjusts extent) and the mutation gate for arrows (Alt+arrows moves the selection, creating first at an empty armed lane slot). Typing has its own deliberate gate — the armed marker (or an existing selection) — not Alt: one deep rule, *plain input never mutates; every mutation passes a gate*, applied per input family (2026-07-18 framing). Which insert verb a surface gets follows from its payload: a note's discrete fret is keyboard-natural (digits at the caret), a point's continuous value is pointer-natural (Alt+click) — two consistent rules meeting different data, not an inconsistency |
| **Shift** | Range/constrain: Shift+click selects a time range (Guitar Pro-style, replace semantics, anchored at the last non-Shift selection action — reassigned 2026-07-17, plan 52); axis-lock a 2D drag; composes with Alt for extent resize (Shift+Alt+Left/Right); plain Shift+arrows is reserved for future caret-anchored selection extension |

**The two-state marker** (2026-07-17's caret model re-settled 2026-07-18; the authoritative
record is `docs/plans/in-progress/chart-span-and-selection-model.md` §9/§9a — superseding
both the one-cursor/no-caret decision and the "plain keys never mutate" foundation): one
position marker, **passive** (the transport rest; no lane furniture — the ruler's
always-shown mark is the play-from-here indicator) or **armed** (the caret, a rounded white
square at a grid slot × string, riding the selection highlight on notes). Plain clicks and
arrows arm it; multi-select gestures dissolve it in place; play dissolves and clears the
selection; pause/stop/seeks rest it passive at raw time; Esc steps gesture → disarm →
selection clear. Typing inserts at an armed empty-slot caret (multi-digit window; the widened
entry stays one insert), retypes the selection, and is inert while passive. Plain arrows move
the caret (grid-default steps over the union stop set, Ctrl+Left/Right measure jump) and
re-derive the selection from what sits under it; chart authoring snaps to the grid by default
with the uniform Ctrl 1/960 fine tier (off-grid unification, 2026-07-18 evening; tuplets still
via tuplet grids, §11). Deliberateness comes from the caret itself — typing visibly lands where the caret
is — rather than from an Alt gate on all keyboard mutation; pointer drags keep their own
friction rationale (threshold, preview, Esc, single-undo commit). Alt remains the mutation
gate for move/duration/fret-shift verbs; the Alt insert quasimode and its ghost are retired.

**The marker's row axis** (2026-07-18): the armed caret's vertical coordinate is a **row**, and
the row space does not end at the last string — it continues down through the visible automation
lanes (a lane row is identified by its instance + parameter, never its display index). Plain
Up/Down traverse the whole stack and cross the tab/lanes boundary in both directions;
Left/Right grid-step and Ctrl+Left/Right measure-jump identically on every row. Clicking an
automation lane seeks (as before) *and* arms the caret at the nearest grid line on that lane —
this supersedes the 2026-07-17 "tone/automation surfaces never move the caret" ruling, which
predates the caret having anywhere to be on those surfaces. Clicking an *object* arms the caret
onto it on both surfaces (chart notes always did; automation points joined 2026-07-18 evening),
so keyboard verbs always continue from what was just touched. The caret's stops are the **union
stop set** (2026-07-18 evening): the grid lattice *plus* the row's authored objects — a
fine-placed note or point is a first-class stop, so plain arrows land exactly on it (arming
onto it, selecting it) and continue past it to the next grid line, and nothing authored is ever
unreachable from the keyboard. Between stops the caret still never rests: fine *positioning*
remains an authoring verb — Ctrl composes with Alt (Ctrl+Alt+arrows) to fine-step the object,
and the caret rides the object it sits on through its own nudge. A lane that disappears from
view dissolves an armed caret on it back to passive.

**One selection, editor-wide** (2026-07-18): exactly one selection exists across all surfaces —
making a selection anywhere replaces the selection everywhere (chart notes, automation point,
tone region are alternatives of one editor-core sum type, so two live selections are
unrepresentable). The old Delete precedence ladder (automation point → chart selection → tone
region) existed only to disambiguate coexisting selections and is retired: Delete deletes *the*
selection. The other surface's highlights vanishing when you select elsewhere is itself the
feedback that verbs now aim there — no view disabling, no modes. A deliberate *click* on an
object arms the caret onto it (both surfaces, 2026-07-18 evening — this narrows the earlier
"cross-surface selection changes never touch the marker" phrasing: the marker follows deliberate
pointing, while selection *replacement* as a side effect of another gesture still moves nothing).

Quasimodes (active only while held) are the deliberate accident-prevention choice: unlike a
latched pencil *tool*, a held key cannot be forgotten, so "why did my click just create
something" mode errors cannot happen. Modifier-held pencils are standard practice: Cubase's
default turns the selection tool into Draw while Alt is held; Final Cut Pro, Motion, and Resolve
use Option/Alt+click to add keyframes on curves; the Adobe apps bind the same verb to Ctrl/Cmd.

Ctrl+drag-to-copy (the Windows convention) is deliberately **not** used — it would collide with
Ctrl = precision. Duplication is Ctrl+D on the selection when it arrives.

## The verb grammar

| Input | Meaning — identical on every surface | Mutates? |
|---|---|---|
| Hover | Affordance only: cursor shape, edge highlight, and the Alt insert ghost on every empty-slot surface — the tone strip's ghost boundary, the automation lanes' on-curve ring, and the tab lane's fret-0 note ring (the chart ghost retired 2026-07-17 with the caret model, then returned 2026-07-18 as the neutral-create preview when Alt+click became the mouse form of Insert) | No |
| Click on object | Select the individual object *and* arm the caret onto it — both surfaces (automation points joined chart notes 2026-07-18 evening); Ctrl+click toggles membership. Chart notes follow the containment hierarchy (2026-07-17): click = note, double-click = its chord, span-rail click = the span's full note set (rides the span slice) | No |
| Click on empty | Place the caret at the nearest grid slot on the clicked row and deselect — with play-from-caret this IS the seek. Applies on the tab lane, the ruler, *and* automation lanes (2026-07-18 row-axis amendment, superseding "tone/automation never move the caret"); the tone strip keeps its own click meanings | No |
| Drag on object | Move (time, plus value/string when 2D); Shift axis-locks; commits once on release | Yes |
| Edge-drag on extent | Resize (region boundary, sustain tail, span edge) | Yes |
| Drag on empty | Marquee select, where multi-select exists | No |
| Alt+click | Insert at the snapped position on every surface (2026-07-18, superseding the 2026-07-17 caret-only chart insert): splits a tone region, places an automation point, or plants a **fret-0 note** on the tab lane — the mouse form of the Insert verb. The neutral default lands sonically silent: an automation point lands **on the curve** (value = the curve's value at the snapped x; discrete lanes: the current state), a fret-0 note lands **selected with the caret armed on it** so the next digit retypes it. Over an occupied slot the press keeps its select meaning (Insert refuses occupied slots), so Alt+click is never destructive | Yes |
| Alt+drag from empty | Insert and place in one press-drag-release gesture, every surface. On automation lanes the drag is **delta-based** from the on-curve landing point — value follows the pointer's vertical delta, never jumping to the raw pointer y; time stays grid-snapped (Ctrl fine); Shift keeps the dominant-axis lock (2026-07-18). On the tab lane the note plants at the release slot with the ring following under Alt (marquee is suppressed while Alt is held) | Yes |
| Alt+wheel | Adjust the selection's *displayed duration* by one grid step: sustain for bare notes, span extent for a whole chord/arpeggio group, member tails for a proper subset (2026-07-17 — see chart-span-and-selection-model.md); Ctrl+Alt+wheel steps the fine grid; successive ticks coalesce into one undo entry | Yes |
| Alt+Shift+wheel | Shift the selection's frets by one per tick, shape-preserving; refuses at fret zero and the fret cap (2026-07-17) | Yes |
| Double-click on object | Open its primary property editor (rename/pick tone, type BPM); chart notes diverge — double-click selects the chord (containment hierarchy), a span's future double-click opens its name/fingering editor, and there is no note-properties dialog (2026-07-17: derived-over-authored leaves nothing needing a form; bends get direct manipulation later) | Via editor |
| Delete / Backspace | Delete the selection (THE selection — one exists editor-wide, so there is no precedence ladder; 2026-07-18) | Yes |
| Insert | Neutral create at an armed empty caret slot: a fret-0 note on the tab lane, an on-curve point on an automation lane; no-op on an occupied slot or while passive — Insert never mutates existing objects (2026-07-18) | Yes |
| Right-click | Context menu, always; every gesture above has a menu equivalent; never destructive on its own. (Chart lane: deliberately deferred 2026-07-17 — every v1 candidate was a worse path to a direct gesture; lands when techniques give it non-redundant content) | Via menu |
| Esc | Cancel the in-flight gesture, restoring pre-gesture state | Reverts preview |
| Arrow keys | Move the caret: Left/Right to the next stop on the row — the nearer of the adjacent grid line and the row's next authored object (the union stop set, 2026-07-18 evening; off-grid notes/points are first-class stops) — Up/Down across rows — strings first, then the visible automation lanes, crossing the boundary in both directions (2026-07-18 row axis) — Ctrl+Left/Right by one measure (GP jump, skipping intermediate stops of both kinds); selection re-derives from what sits under the caret (2026-07-17 caret model) | No |
| Shift+arrows | Caret-anchored time selection, text-editor style: extends from the caret's starting grid line while held; release keeps the range; a plain arrow clears it; Shift again resumes (settles plan 52's keyboard creation gesture; builds with the range object) | No |
| Digits on empty caret | Exact payload entry, per row kind: on a string row, insert a note with the typed fret (multi-digit window widens the same insert); on an automation lane row, open the typed-value editor seeded with the digit and create-or-retype the point at the caret in the parameter's native units (the double-click editor, reached from the keyboard; 2026-07-18) | Yes |
| Alt+arrows | Move the selection by one grid step (Ctrl+Alt by the fine step); the vertical axis is the surface's own (string for notes, value for automation points). At an armed *empty* lane slot, create first — the point lands on the curve, then the arrow's nudge applies — so "grab the curve here and pull" is one keystroke, mirroring digits-at-empty-caret as the typing gate (2026-07-18) | Yes |
| Shift+Alt+Left/Right | Grow/shrink the selected object's extent by one grid step (Ctrl composes the fine step) | Yes |
| Shift+Alt+Up/Down | Shift the selection's frets by one, shape-preserving — the Alt+Shift axis rule: horizontal = extent in time, vertical = fret, on arrows and wheel alike (2026-07-17) | Yes |

A plain click **never mutates**. Every mutating gesture previews live (snap guide plus a
"position · value" readout chip), commits exactly once on release as a single undo entry, and can
be abandoned mid-flight with Esc.

**Selection verbs follow the selection, not the pointer** (2026-07-17): with a selection active,
its modifier verbs (Alt+wheel, Alt+Shift+wheel, and the keyboard verbs, which never depended on
the pointer) work anywhere in the focused editor window — the pointer's position gates what a
*click* means, never what the selection's verbs do. Concretely, Alt marks a wheel as a selection
verb and never zooms; wheel-consuming surfaces route Alt-modified wheels to the editor shell's
selection dispatch, and everything else bubbles them there.

## Snapping

- **Snap is always on.** There is no snap toggle; off-grid placement is possible but deliberately
  not encouraged. Holding **Ctrl** during any placement or move bypasses the visible grid.
- Ctrl placement still quantizes to the **1/960-beat fine grid** so stored positions stay exact
  rationals (`timeline_cursor.cpp` — `g_fine_grid_denominator = 960`). 960 is the standard MIDI
  PPQ resolution, divides every practical straight/triplet/quintuplet subdivision, and lands well
  below audible granularity. On-grid placement does **not** pass through 960: it stores the grid
  line's own exact rational, so odd grids (1/13) round-trip perfectly. Both behaviors are already
  implemented and shared through `musicalGridPositionForX`; new surfaces must use the same helper.
- **Off-grid is a first-class state, not an error.** Imported RS-derived content is largely
  off-grid; render such objects normally, with the exact position visible in readouts rather than
  warning styling.
- **Multi-object moves are delta-based**: snap applies to the grabbed object; every other selected
  object preserves its relative offset, so moving an off-grid phrase never quantizes its members.

## Anchors: free-time by nature

A tempo anchor's *musical* position is a whole beat — the anchor defines the grid — so grid
snapping is meaningless for its time coordinate:

- **Insert** (Alt+click on the ruler's tempo band) chooses the anchor's musical beat, snapped to
  the beat grid like any insert.
- **Drag** moves the anchor's *audio time* and is **always free-time** — no Ctrl needed, no snap.
  Dragging an anchor is inherently the audio-sync gesture: you are pinning a beat to where it
  falls in the recording. (Even here, stored seconds keep the tempo map's millisecond grid.)

Because Ctrl-friction cannot protect a gesture that is free-time by design, anchors get a
dedicated interlock instead: a toolbar **grid lock**. While locked (the default), anchor insert,
move, and delete are disabled — cursor feedback shows the lock, and menu items are disabled with
the reason. Unlock deliberately, sync, relock. This is the model's single persistent toggle, and
it is acceptable because it is a safety interlock on one object class, not an editing mode: it
changes nothing about how any gesture behaves elsewhere. The lock ships together with anchor
editing; nothing implements it today.

## Cursor and feedback vocabulary

Verified against the vendored JUCE source — everything needed ships in
`juce_gui_basics/mouse/juce_MouseCursor.h`:

- **Alt held over insertable space** → `CopyingCursor` (arrow with a "+" badge; a JUCE-supplied
  cross-platform image, `juce_Windowing_windows.cpp:6123`), plus the ghost preview of the object
  that a click would insert. If we later want a literal pencil, custom runtime-drawn image cursors
  with HiDPI support exist (`MouseCursor(const ScaledImage&, Point<int>)`,
  `juce_MouseCursor.h:123`) — no asset pipeline required.
- **Object body** → `NormalCursor`; a grabbable boundary/edge → `LeftRightResizeCursor`
  (`UpDownResizeCursor` for vertical affordances such as lane resize bands).
- **During any drag** → snap guide line plus the "position · value" readout chip (both already
  exist for lanes and tone edges; they become universal).
- Modifier changes without mouse movement are already handled: JUCE sends a synthetic mouse-move
  whenever modifiers change (`juce_Component.cpp:3170`, `internalModifierKeysChanged` →
  `sendFakeMouseMove()`), so cursors and ghosts react to pressing/releasing Alt or Ctrl with no
  extra plumbing.

## Per-surface instantiation

- **Tone strip**: Alt+click (or Alt+press-drag-release, placing the ghost boundary first) inside
  a region inserts a tone change at the snapped x. The insert opens the **tone picker** rather
  than inheriting the split tone: a change to the same tone on both sides would be a no-op
  boundary, which the editor refuses — the picker is the payload chooser, exactly as the "+" lane
  picker chooses a parameter. Drag a boundary to move the change (both neighbors adjust; coverage
  stays gap-free). Delete removes the selected change/region and merges left; the region menu
  mirrors it. Ctrl+T stays as the "insert at playhead" keyboard accelerator. Double-click on a
  region body keeps its primary-edit meaning (rename prompt today).
- **Automation lanes** (keyboard-first amendment 2026-07-18 — lanes are full marker rows):
  Alt+click/Alt+drag inserts a point that lands **on the curve** at the snapped x (clamped to
  the active region's window); the drag phase is delta-based (value follows the pointer's
  vertical delta from the on-curve landing, time stays grid-snapped, Ctrl fine, Shift
  dominant-axis lock). Plain click on empty lane area seeks, deselects, *and arms the caret* at
  the nearest grid line on that lane; clicking a *point* selects it and arms the caret on it
  (2026-07-18 evening fix — the caret used to stay behind); point selection clears on any
  transport move, the same rule tone-region selection follows. Drag moves a point (with a
  click-jiggle threshold), the caret riding the moved point.
  Keyboard at an armed lane caret: **Alt+arrows** nudge the selected/under-caret point
  (Left/Right by grid step, Up/Down by value step; Ctrl+Alt fine), creating on the curve first
  when the slot is empty; **digits** open the typed-value editor seeded with the digit
  (create-or-retype at the caret); **Insert** plants an on-curve point without changing the
  sound; plain arrows navigate rows and grid slots without mutating. Double-click keeps typed
  exact-value entry in the parameter's native units (parsed through the plugin's own
  text-to-value handler); the point menu carries Delete / Set Value / Reset to Default. The
  lane's pinned name chip is the lane handle: clicking it opens the lane menu, since empty lane
  space belongs to the seek-and-caret overlay. Positional menu-insert is tone-strip-only; on
  lanes the insert gestures are Alt and the caret verbs.
- **Notes** (implemented 2026-07-16; granularity and span verbs settled 2026-07-17, the
  two-state marker 2026-07-18, both in
  docs/plans/in-progress/chart-span-and-selection-model.md §7/§9/§9a): the lane carries one
  position marker — the **passive cursor** (the paused playhead line at the transport
  position) or the **armed caret** (an exact grid slot × string). A plain click arms the
  caret (on a note it also selects it); an arrow while passive arms it at the cursor; typed
  digits insert a note at an armed empty-slot caret, retype the selection when one exists,
  and are inert while passive with no selection. Any multi-select gesture — Ctrl+click,
  double-click, marquee — dissolves the caret back into a cursor in its place, so the visible
  glyph always states the typing scope (square = insert here; highlights + line = verbs act
  on the selection). Esc steps down: armed → passive, then selection → cleared. Selection
  follows the containment hierarchy (note ⊂ chord ⊂ span, 2026-07-17): click selects the
  note, double-click its chord, span-rail click the whole span; Ctrl+click toggles individual
  notes; marquee stays geometrically precise by design; Shift+click selects a time range
  (plan 52). Scope is always the selection, never the verb (the uniform-scope law, §9a).
  Typed digits SET every selected note to the exact value — what you type is what appears
  (multi-digit window; Ctrl+digit and Alt+digit unbound).
  Alt+Shift+wheel SHIFTS the selection's frets by one per tick, shape-preserving (chords and
  runs keep their intervals), refusing — never clamping — at fret zero and the fret cap
  (settled 2026-07-17).
  Alt+wheel and Shift+Alt+Left/Right adjust displayed duration (sustain or span extent per the
  span model); Alt+arrows move the selection (Left/Right by grid step, or one 1/960-beat fine
  step under Ctrl+Alt — the uniform fine tier, off-grid unification 2026-07-18 evening — with
  Up/Down across strings; refused, never clamped, at the neck edge or an occupied slot; a caret
  sitting on the single moved note rides along). Plain arrows move the armed caret over the
  union stop set — grid lines plus this string's notes, so off-grid notes are reachable stops —
  with Ctrl+Left/Right jumping measures (the GP jump). Pointer drag-move (the same plain
  move-drag verb points use) is **un-parked and scheduled long-term**
  (docs/plans/todo/tab-pointer-drag-editing.md): the morning's "drag where time is continuous"
  parking rationale dissolved when note time became continuous-capable the same evening.
  **Sustain tail-drag stays parked**
  (2026-07-16): the wheel/keyboard verbs cover resizing precisely, and the tail's end zone is a
  small target that competes with drag-move grabs — a watch item in docs/tracking/watch-items.md
  holds the trigger for revisiting.
- **Ruler bands** (future): anchors per the section above; time signatures are point objects on
  the signature band with double-click = type the signature.

Keyboard accelerators form one family: Ctrl+T inserts a tone change at the playhead, and future
"insert at playhead" commands follow the same shape for anchors and notes.

## Deferred decisions

- **Timeline panning bindings.** Plain wheel is horizontal zoom (shipped, REAPER-style, kept by
  the 2026-07-16 amendment discussion). No wheel or button binding pans yet; the candidates that
  fit the grammar are Shift+wheel (horizontal pan as "constrain to axis") and middle-button drag
  (modifier-free). Undecided — pick when charting practice shows panning friction.
- **Caret-anchored keyboard selection extension** (Shift+arrows sweeping notes into the
  selection from the caret, text-editor style). Shift+arrows is deliberately kept unbound for
  this.
- **Keyboard sustain entry beyond Shift+Alt+arrows** (GP-style +/- keys) — only if practice
  wants it.
- **Friendlier grid-preset names** (user direction 2026-07-18, recorded when the triplet
  presets landed): the grid dropdown currently labels presets as raw fractions ("1/6",
  "1/12"); we should probably provide user-friendly names the way REAPER does ("1/4 triplet",
  "1/8 triplet") in a future pass. Display-only — free fraction entry and the stored value
  stay raw fractions.
- **User-facing keybind documentation and rebinding** (user direction 2026-07-16): both are
  wanted eventually — the rebinding UI is docs/plans/roadmap/46-editor-keybinds.md's scope, and
  user-facing documentation of this grammar ships alongside it — but neither is to be settled or
  written while the grammar itself is still being tuned; this doc remains the design-side truth
  in the interim.

- **Latched pencil mode** (press B to stay in draw mode): charters will want it for long sessions;
  reintroduces persistent-mode errors. Revisit after note authoring exists; if added, it needs a
  loud cursor/toolbar indicator and Esc-to-exit.
- **Alt+click on an existing note as toggle-delete** (Ableton draw-mode style): fast for charting
  but makes Alt destructive. Declined (2026-07-18, when Alt+click chart-note create landed): Alt+click
  on an occupied slot keeps its non-destructive select meaning, uniform with every other surface —
  Alt+click only ever inserts, never deletes.
- **Freehand lane painting** (Alt+drag across a lane writing multiple points at grid steps):
  powerful bulk-authoring upgrade, same grammar; not needed until it is.
- **Arrow-nudging a selected tone region** (moving both of its boundaries as one step): needs a
  compound two-boundary edit to stay one undo entry; deferred until such an edit exists.

## Amendment record — 2026-07-16 keyboard grammar

Decided with the user during note-editing bring-up and applied the same day to every shipped
surface:

1. Plain arrows never mutate: the tab lane's Left/Right step the timeline cursor along the grid
   — the editing-caret concept was removed entirely the next day (2026-07-17, user: the playhead
   is the position feedback and a second cursor icon is noise) (they
   briefly shipped as selection-nudges), and the automation lanes' plain-arrow point nudge —
   part of the original 2026-07-09 revamp below — now requires Alt. Where no navigation focus
   exists, plain arrows do nothing; a dead key beats a surprising one.
2. Alt is stated as the general *authoring* modifier (mutation gate), not merely "create":
   Alt+arrows moves the selection; Shift+Alt+Left/Right resizes extent (replacing the original
   table's plain Shift+Left/Right, which is now reserved for selection extension).
3. Sustain tail-drag on notes is parked behind a watch-item trigger rather than built;
   plain drag-move of whole objects stays a core verb on every surface.
4. Plain-wheel zoom (shipped) is affirmed over Charter-style modifier-gated zoom.

## Amendment record — 2026-07-18 two-state marker

Settled with the user after the multi-select deep analysis (full model in
docs/plans/in-progress/chart-span-and-selection-model.md §9a):

1. The paused cursor returns as the position marker's *passive* state; §9's caret is its
   *armed* state. Exactly one of the line, the square, or the selected-note highlight shows
   the position at any moment, and the visible glyph states the typing scope.
2. Multi-select gestures (Ctrl+click, double-click, marquee) dissolve the caret into a cursor
   in its place; plain clicks and arrows arm it. Esc steps armed → passive → selection
   cleared. Typing is inert while passive with no selection — the stray-digit insert after
   listening is gone.
3. Multi-select itself is confirmed permanent (pure-GP arity-one selection declined); scope
   is always the selection, never the verb.

## Amendment record — 2026-07-18 unified selection + keyboard-first automation

Settled with the user the same day as the two-state marker, extending it (implemented the same
day):

1. **One selection editor-wide**: chart notes / automation point / tone region become
   alternatives of one editor-core selection sum type; selecting anywhere replaces the
   selection everywhere; the Delete precedence ladder is retired. Cross-surface selection
   changes never touch the marker's armed/passive state.
2. **The marker's row axis**: the armed caret's rows extend past the strings into the visible
   automation lanes; plain Up/Down cross the tab/lanes boundary; clicking a lane arms the caret
   there (superseding "tone/automation never move the caret"). The caret stays grid-native on
   every row; Ctrl+Alt composition is the only fine tier, and it authors (moves points), never
   navigates.
3. **Keyboard-first automation authoring**: Alt+arrows create-if-absent-then-nudge (the point
   lands on the curve), digits open the typed-value editor seeded with the digit, and pointer
   placement adopts the same on-curve landing with delta-based drag. Placement is sonically
   silent until deliberately pulled.
4. **Insert = neutral create** on every surface (user addition, for symmetry): fret-0 note on a
   string row, on-curve point on a lane row; no-op on occupied slots and while passive.

## Amendment record — 2026-07-18 evening: the off-grid unification

Settled with the user the same evening, closing the last structural asymmetry between the two
surfaces (implemented immediately; commits `0f8e14f2` and `f6b4397e`):

1. **Snap *default* follows the data's judge; the capability is uniform.** The morning's
   "chart verbs are grid-native / grid binding follows the data's judge" rule is demoted from a
   capability wall to a default: notes still snap hard to the grid because rhythm is their
   judge, but the Ctrl 1/960-beat fine tier now applies to note moves exactly as to automation
   points. Motivations: one verb table with zero per-surface exceptions, and true sub-1ms note
   fidelity (imported RS-derived performances place notes at arbitrary times; a grid-closed
   editor would quantize them lossily on every touch). Zero data-model change — `GridPosition`
   always represented fine offsets; only the verbs were gated.
2. **One move intent.** Alt+arrows route through a single `onSelectionMoveRequested(direction,
   fine)` controller intent dispatching on the selection kind (mirroring the Delete dispatch);
   the automation nudge policy moved out of the lanes view into editor-core with it.
3. **The union stop set.** Plain Left/Right step the caret to the nearer of the adjacent grid
   line and the row's next authored object; off-grid notes and points are first-class stops
   (landing arms onto them), so nothing authored is keyboard-unreachable. The caret still never
   rests between stops, and it rides the object it sits on through that object's own nudge.
4. **Clicks arm everywhere.** Clicking an automation point arms the lane caret on it, matching
   chart note clicks; lane carets also gained the chart caret's measure-reveal viewport glide.
5. **Pointer drag-move of notes is un-parked** as a deliberate long-term item
   (docs/plans/todo/tab-pointer-drag-editing.md): the parking rationale — "drag where time is
   continuous, keys where time is discrete" — dissolved when note time became
   continuous-capable, rather than being outweighed.

## Amendment record — 2026-07-18: Alt+click chart-note create

Settled with the user the same day, closing the last create-gesture asymmetry the marker work
left standing — every empty-slot surface armed the caret on a plain click, but only the tone
surfaces *created* on Alt+click. The user's framing: since Alt+click already drops automation
points and tone splits, it should plant a note too, with the old white ring ghost back while Alt
is held.

1. **Alt+click plants a fret-0 note on the tab lane** — the mouse form of the Insert verb, the
   chart sibling of the automation lane's on-curve Alt+click. Fret 0 is the note's neutral
   default (its "on the curve"): the note lands **selected with the caret armed on it**, so the
   next digit retypes it — "place, then correct the value", one insert entry plus one retype
   entry. Alt+drag plants at the release slot (press-drag-release, marquee suppressed under Alt);
   Alt+click over an occupied slot keeps its select meaning (`planInsertNote` refuses the slot),
   so Alt+click is never destructive.
2. **The chart Alt ghost returns** — a hollow white ring at the prospective slot while Alt
   hovers an insertable empty slot, retired 2026-07-17 with the caret model and brought back now
   that Alt+click authors again. Round (distinct from the caret's square) and controller-resolved
   from the same snap + occupancy the click uses, so it shows only where an Alt+click would land
   (no lying affordance) — absent over occupied slots, without Alt, and while playing.
3. **Not the deleted quasimode.** This is the lightweight Insert-verb mouse form, *not* §9's
   removed Alt insert quasimode (the composable ghost, Alt+digit fret composition, session
   accumulation, sticky last-fret): fret entry stays the caret's typing rule and the ghost
   carries no editable fret. Supersedes §9's "Alt returns to being purely the mutation gate" for
   the empty slot — Alt is the mutation gate on objects and the neutral-create gate on empty
   slots, uniform across the tab lane, automation lanes, and tone strip.

Implemented 2026-07-09, amended 2026-07-16 per the record above (see the per-surface section
for current behavior):

1. Automation lanes: Alt-gated insertion with ghost point + `CopyingCursor`; plain empty-area
   clicks pass through to seek; Shift axis-lock; drag threshold; Esc cancel; arrow-key nudges;
   typed value entry on double-click (new `parseParameterValue` on the automation port); point
   menu Delete / Set Value / Reset to Default; lane menu moved to the pinned name chip;
   selection clears on transport moves.
2. Tone strip: Alt+click / Alt+drag insert-tone-change intent (opens the tone picker, shared with
   Ctrl+T); ghost boundary line + `CopyingCursor` under Alt; Esc cancel; region menu gains
   "Insert Tone Change Here" and "Delete".
3. Both: gesture commit stays single-undo-entry; snap guides and readout chips unchanged.
