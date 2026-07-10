# Timeline editing interaction model

Status: **settled direction, 2026-07-09.** Being applied now to the surfaces that already exist
(tone track, tone-automation lanes). Note authoring and anchor editing adopt this model when they
are built; their sections here are binding design, not speculation.

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
| **Alt** | Create/author: a held "pencil" quasimode — click inserts, wheel adjusts extent |
| **Shift** | Extend/constrain: add to selection; axis-lock a 2D drag; keyboard extent resize |

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
| Hover | Affordance only: cursor shape, edge highlight; with Alt held, a ghost preview of exactly what a click would insert, at the snapped position | No |
| Click on object | Select | No |
| Click on empty | Seek + deselect | No |
| Drag on object | Move (time, plus value/string when 2D); Shift axis-locks; commits once on release | Yes |
| Edge-drag on extent | Resize (region boundary, sustain tail, span edge) | Yes |
| Drag on empty | Marquee select, where multi-select exists | No |
| Alt+click | Insert at the snapped position — works on occupied space too (splitting a tone region) | Yes |
| Alt+drag from empty | Insert and place in one press-drag-release gesture | Yes |
| Alt+wheel | Adjust the selected object's extent by one grid step (note sustain); Ctrl+Alt+wheel steps the fine grid | Yes |
| Double-click on object | Open its primary property editor (rename/pick tone, type BPM, note properties) | Via editor |
| Delete / Backspace | Delete the selection | Yes |
| Right-click | Context menu, always; every gesture above has a menu equivalent; never destructive on its own | Via menu |
| Esc | Cancel the in-flight gesture, restoring pre-gesture state | Reverts preview |
| Arrow keys | Nudge the selection by one grid step; Ctrl+arrows by the fine grid step | Yes |
| Shift+Left/Right | Grow/shrink the selected object's extent by one grid step | Yes |

A plain click **never mutates**. Every mutating gesture previews live (snap guide plus a
"position · value" readout chip), commits exactly once on release as a single undo entry, and can
be abandoned mid-flight with Esc.

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
  arrows nudge the selected point (Left/Right by grid step, Ctrl fine, Up/Down by value step).
  Double-click opens typed exact-value entry in the parameter's native units (parsed through the
  plugin's own text-to-value handler); the point menu carries Delete / Set Value / Reset to
  Default. The lane's pinned name chip is the lane handle: clicking it opens the lane menu, since
  empty lane space now belongs to the seek overlay. Positional menu-insert is tone-strip-only;
  on lanes the insert gesture is Alt itself.
- **Notes** (future): Alt+click on a string lane places a note carrying the last-used fret; typed
  digits set the fret of the selection; technique hotkeys set properties of the selection —
  Guitar Pro-style keyboard entry composes from the same selection verbs. Alt+wheel and
  Shift+Left/Right set sustain; dragging the sustain tail is the same resize verb regions use.
  Vertical drag changes string; marquee selects runs.
- **Ruler bands** (future): anchors per the section above; time signatures are point objects on
  the signature band with double-click = type the signature.

Keyboard accelerators form one family: Ctrl+T inserts a tone change at the playhead, and future
"insert at playhead" commands follow the same shape for anchors and notes.

## Deferred decisions

- **Latched pencil mode** (press B to stay in draw mode): charters will want it for long sessions;
  reintroduces persistent-mode errors. Revisit after note authoring exists; if added, it needs a
  loud cursor/toolbar indicator and Esc-to-exit.
- **Alt+click on an existing note as toggle-delete** (Ableton draw-mode style): fast for charting
  but makes Alt destructive. Decide with note authoring; everywhere else Alt+click only inserts.
- **Freehand lane painting** (Alt+drag across a lane writing multiple points at grid steps):
  powerful bulk-authoring upgrade, same grammar; not needed until it is.
- **Arrow-nudging a selected tone region** (moving both of its boundaries as one step): needs a
  compound two-boundary edit to stay one undo entry; deferred until such an edit exists.

## Revamp checklist for existing surfaces

Implemented 2026-07-09 (see the per-surface section above for the shipped behavior):

1. Automation lanes: Alt-gated insertion with ghost point + `CopyingCursor`; plain empty-area
   clicks pass through to seek; Shift axis-lock; drag threshold; Esc cancel; arrow-key nudges;
   typed value entry on double-click (new `parseParameterValue` on the automation port); point
   menu Delete / Set Value / Reset to Default; lane menu moved to the pinned name chip;
   selection clears on transport moves.
2. Tone strip: Alt+click / Alt+drag insert-tone-change intent (opens the tone picker, shared with
   Ctrl+T); ghost boundary line + `CopyingCursor` under Alt; Esc cancel; region menu gains
   "Insert Tone Change Here" and "Delete".
3. Both: gesture commit stays single-undo-entry; snap guides and readout chips unchanged.
