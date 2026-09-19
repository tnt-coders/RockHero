# Tab Lane Pointer Drag-Move

Status: **Todo, optional for the first release.** Re-verified against the code and the signed
keymap on 2026-09-19. `docs/plans/in-progress/first-releasable-editor.md` (D2, Phase 7) holds the
decision on whether it ships in the first release; this file is what gets built if it does.

## Why it exists

Every other 2D surface moves its objects with a plain drag — an automation point, a tone boundary.
The chart is the one surface where a note moves by keyboard only (`Alt`+arrows). The signed keymap
already carries the row: *Drag on object — move note*, scheduled for the chart and live on the
lanes and the tone strip (`docs/plans/in-progress/keymap-matrix.md`, the pointer table). This plan
closes that row and authors no new chart fact.

## The gesture

- **A plain drag that starts on a note head moves the selection.** No modifier: `Alt`+drag from
  empty is retired on the chart, and the `Ctrl`+drag off-grid tier was retired 2026-08-23 — a
  drag moves on the placement quantum, like every other position verb.
- **A press on a head that never travels past the drag threshold stays what it is today**, a
  select. A drag from EMPTY lane stays the marquee. Heads are the lane's only pointer targets, so
  no second grab zone competes: the sustain tail is not a target, and tail-drag resize is dropped
  (`Alt`+wheel is the mouse sustain command).
- **Pressing an unselected head selects it and drags it**; pressing a selected head drags the whole
  selection. `Shift` / `Ctrl` on the press keep their selection meaning and start no drag.
- **Horizontal travel is time, vertical travel is strings**, both DELTA-based from the press point
  — the note never jumps to the raw pointer position. Time quantizes to
  `placementQuantumNoteValue`: a grid step while snap is on, a tick (1/3840) while it is off. The
  move is RELATIVE, so a note sitting between lines keeps its offset.
- `Shift` held DURING the drag is the dominant-axis lock, as on the lanes.
- **Esc cancels** the drag in flight and restores the pre-press chart — the gesture-cancel rung of
  the Esc ladder.
- **One undo entry** per completed drag.
- **Refused, never clamped**: a drag position whose move the planner refuses (neck edge, occupied
  slot, a keyframe onto its neighbour) previews the last accepted position, exactly as a refused
  arrow press leaves the selection where it was.
- **Edge auto-scroll** once the pointer leaves the viewport horizontally.
- The armed caret rides a single dragged note the way it rides a keyboard nudge; a pointer drag
  does not move the transport cursor (the object is already under the mouse — the rule the tone
  boundary drag follows).

Two points above are PROPOSALS to rule when the build starts, not signed grammar: what a press on
an UNSELECTED head drags, and whether the drag is paused-only like the marker strip's pointer
gestures.

## What already exists

- The lane reports `ChartPointerPhase::Down` / `Drag` / `Up` to the controller, and a drag keeps
  reporting after the pointer leaves the lane (`rock-hero-editor/ui/src/tab/tab_view.cpp`).
- `EditorController::Impl::moveChartSelection` (`rock-hero-editor/core/src/chart/chart_handlers.cpp`)
  is already a GESTURE: `ChartMoveGesture` records the keys the run started on and every step
  replays the whole run from them, as one plan and one undo entry. Notes and keyframes are both
  operands of the time step; the string step reaches notes only.

So the drag is that same gesture fed a net step count from pointer travel instead of one step per
key press. **It must not become a second move implementation**: generalize the one gesture to take
the net displacement, and let the arrow keys be its one-step caller.

## What is new

1. The controller's pointer state machine gains the drag-move arm: threshold, net (time steps,
   string steps) from the delta, replay through the move gesture, commit on `Up`, restore on Esc.
2. Live preview: the replay runs per pointer move. Cheap for a handful of notes; MEASURE it on a
   large multi-select in a `relwithdebinfo` build before accepting it, and coalesce to one replay
   per frame if it shows.
3. Edge auto-scroll. The chart lane has none today. Plan 47's ruler-drag time selection needs the
   same thing, so build one seam for both rather than one each.
4. The drag threshold, shared with the same ruler drag.

## Verify

- Controller tests beside the existing move tests: a drag of N quanta equals N arrow presses (same
  chart, ONE undo entry); a refused position holds the last accepted one; Esc restores the
  pre-press chart and selection; a sub-threshold press is still a select; a press on an unselected
  head drags that head alone.
- `test_tab_view.cpp`: the phases still reach the controller unchanged.
- Sighting: the threshold, the snap feel at coarse and fine grids and with snap off, the string
  hop, auto-scroll speed.

## Docs to update in the same change

`keymap-matrix.md` (the *Drag on object* row goes Live; its gap list's item 1 closes),
`editing-interaction-model.md` (the chart's pointer paragraph), `docs/developer/the-editor-2d-views.md`
if it names the chart's pointer verbs, and the user-facing keybind docs if Phase 6 has written them.
