# Timeline editing interaction model

Status: **settled direction, 2026-07-09; keyboard grammar amended 2026-07-16** (with the user,
during note-editing bring-up): plain keys never mutate — Alt is the authoring modifier for
keyboard input, arrows are pure navigation, and the original "plain arrows nudge the selection"
assignments (including the automation lanes' shipped 2026-07-09 behavior) moved under Alt. The
amendment record is inline below. Note authoring now implements this model; anchor editing
adopts it when built.

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
| **Ctrl** | Precision: bypass grid snap (placement quantizes to the 1/960-beat fine grid) |
| **Alt** | Author: the modifier that makes input mutate — a held "pencil" quasimode for the pointer (click inserts, wheel adjusts extent) and the mutation gate for the keyboard (Alt+arrows moves the selection) |
| **Shift** | Range/constrain: Shift+click selects a time range (Guitar Pro-style, replace semantics, anchored at the last non-Shift selection action — reassigned 2026-07-17, plan 52); axis-lock a 2D drag; composes with Alt for extent resize (Shift+Alt+Left/Right); plain Shift+arrows is reserved for future caret-anchored selection extension |

**The caret model** (2026-07-17 evening, the Guitar Pro posture — superseding both the
one-cursor/no-caret decision and the "plain keys never mutate" foundation): one position
concept per transport state. While paused, the **caret** (grid position × string) is THE
position — a white circle on empty slots, the selection highlight on notes, always present,
placed by clicking the highway band or the ruler (never the tone/automation surfaces), and
co-located with the note selection. While playing, the **playhead** is THE position; it
renders only during playback, Space plays from the caret, and pause snaps the caret to the
nearest grid line on the remembered string. Typing digits on an empty caret INSERTS a note
with the typed fret (multi-digit window; the widened entry stays one insert); digits on a
selection retype it. Plain arrows move the caret (Left/Right grid step, Up/Down across
strings, Ctrl+Left/Right measure jump) and re-derive the selection from what sits under it.
Deliberateness now comes from the caret itself — typing visibly lands where the caret is —
rather than from an Alt gate on all keyboard mutation; pointer drags keep their own friction
rationale (threshold, preview, Esc, single-undo commit). Alt remains the mutation gate for
move/duration/fret-shift verbs; the Alt insert quasimode and its ghost are retired.

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
| Hover | Affordance only: cursor shape, edge highlight (the chart's Alt ghost retired 2026-07-17 with the caret model; the tone strip keeps its Alt ghost boundary preview) | No |
| Click on object | Select the individual object; Ctrl+click toggles membership. Chart notes follow the containment hierarchy (2026-07-17): click = note, double-click = its chord, span-rail click = the span's full note set (rides the span slice) | No |
| Click on empty | Place the caret (highway band and ruler only; snapped, Ctrl fine) and deselect — with play-from-caret this IS the seek; tone/automation surfaces keep their own click meanings and never move the caret (2026-07-17) | No |
| Drag on object | Move (time, plus value/string when 2D); Shift axis-locks; commits once on release | Yes |
| Edge-drag on extent | Resize (region boundary, sustain tail, span edge) | Yes |
| Drag on empty | Marquee select, where multi-select exists | No |
| Alt+click | Insert at the snapped position — tone-surface objects only (splitting a tone region, placing automation points); chart notes insert via the caret + digits (2026-07-17) | Yes |
| Alt+drag from empty | Insert and place in one press-drag-release gesture (tone surfaces) | Yes |
| Alt+wheel | Adjust the selection's *displayed duration* by one grid step: sustain for bare notes, span extent for a whole chord/arpeggio group, member tails for a proper subset (2026-07-17 — see chart-span-and-selection-model.md); Ctrl+Alt+wheel steps the fine grid; successive ticks coalesce into one undo entry | Yes |
| Alt+Shift+wheel | Shift the selection's frets by one per tick, shape-preserving; refuses at fret zero and the fret cap (2026-07-17) | Yes |
| Double-click on object | Open its primary property editor (rename/pick tone, type BPM); chart notes diverge — double-click selects the chord (containment hierarchy), a span's future double-click opens its name/fingering editor, and there is no note-properties dialog (2026-07-17: derived-over-authored leaves nothing needing a form; bends get direct manipulation later) | Via editor |
| Delete / Backspace | Delete the selection | Yes |
| Right-click | Context menu, always; every gesture above has a menu equivalent; never destructive on its own. (Chart lane: deliberately deferred 2026-07-17 — every v1 candidate was a worse path to a direct gesture; lands when techniques give it non-redundant content) | Via menu |
| Esc | Cancel the in-flight gesture, restoring pre-gesture state | Reverts preview |
| Arrow keys | Move the caret: Left/Right by one grid step, Up/Down across strings, Ctrl+Left/Right by one measure (GP jump); selection re-derives from what sits under the caret (2026-07-17 caret model) | No |
| Shift+arrows | Caret-anchored time selection, text-editor style: extends from the caret's starting grid line while held; release keeps the range; a plain arrow clears it; Shift again resumes (settles plan 52's keyboard creation gesture; builds with the range object) | No |
| Digits on empty caret | Insert a note at the caret with the typed fret (multi-digit window widens the same insert) | Yes |
| Alt+arrows | Move the selection by one grid step (Ctrl+Alt by the fine step); the vertical axis is the surface's own (string for notes, value for automation points) | Yes |
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
- **Automation lanes**: Alt+click/Alt+drag inserts and places a point (clamped to the active
  region's window); plain click on empty lane area seeks and deselects instead of inserting —
  point selection clears on any transport move, the same rule tone-region selection follows.
  Drag moves a point (with a click-jiggle threshold); Shift axis-locks to the dominant axis;
  **Alt+arrows** nudge the selected point (Left/Right by grid step, Ctrl fine, Up/Down by value
  step — amended 2026-07-16 from plain arrows so the "plain keys never mutate" rule holds on
  every surface).
  Double-click opens typed exact-value entry in the parameter's native units (parsed through the
  plugin's own text-to-value handler); the point menu carries Delete / Set Value / Reset to
  Default. The lane's pinned name chip is the lane handle: clicking it opens the lane menu, since
  empty lane space now belongs to the seek overlay. Positional menu-insert is tone-strip-only;
  on lanes the insert gesture is Alt itself.
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
  span model); Alt+arrows move the selection (Left/Right by grid step, Up/Down across strings;
  refused, never clamped, at the neck edge or an occupied slot). Plain arrows move the armed
  caret (Ctrl+Left/Right jump measures, the GP jump). Pointer drag-move (horizontal with
  snap, vertical across strings) is the same plain move-drag verb points use and lands with
  the remaining plan 40 Phase 4 slice. **Sustain tail-drag is parked**
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
- **User-facing keybind documentation and rebinding** (user direction 2026-07-16): both are
  wanted eventually — the rebinding UI is docs/plans/roadmap/46-editor-keybinds.md's scope, and
  user-facing documentation of this grammar ships alongside it — but neither is to be settled or
  written while the grammar itself is still being tuned; this doc remains the design-side truth
  in the interim.

- **Latched pencil mode** (press B to stay in draw mode): charters will want it for long sessions;
  reintroduces persistent-mode errors. Revisit after note authoring exists; if added, it needs a
  loud cursor/toolbar indicator and Esc-to-exit.
- **Alt+click on an existing note as toggle-delete** (Ableton draw-mode style): fast for charting
  but makes Alt destructive. Decide with note authoring; everywhere else Alt+click only inserts.
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
