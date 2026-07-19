# Tab Lane Pointer Drag Editing (Un-parked, Long-Term)

Status: **Todo** — un-parked by user decision 2026-07-18, deliberately long-term (not next in
plan 40's queue). Re-verify against the current interaction model and code before executing.

## History

Pointer drag-move of chart notes was parked behind a watch item on 2026-07-18 (morning) with the
rule "drag where time is continuous, keys where time is discrete": the chart lane was
grid-native, so a drag would have been a mouse-operated discrete stepper. Later the same day the
off-grid unification landed (interaction-model amendment record): note time became
continuous-capable — the Ctrl 1/960 fine tier applies to notes exactly as it does to automation
points, the grid is a default snap rather than a capability wall, and the caret steps a union
stop set that includes off-grid objects. That dissolved the parking rationale rather than merely
outweighing it, so the user put mouse drag semantics for the tab back on the table for full
pointer symmetry between the two surfaces.

## Scope (verify before building)

- Alt+drag moves the selected note(s): horizontal along the time axis with grid snap by default
  and the Ctrl 1/960 fine tier during the drag (matching the lanes' pointer grammar), vertical
  across strings. Delta-based from the press point, exactly like the lanes' value drags — the
  note never jumps to the raw pointer position.
- Live preview honoring §10 margin clamps and collision refusals (refused, never clamped, per
  the move-intent semantics in `moveChartSelection`).
- Edge auto-scroll once the drag leaves the viewport.
- Esc cancels the in-flight drag without committing (the gesture-cancel rung of the Esc ladder).
- One undo entry per completed drag.
- The armed caret rides the dragged note the same way it rides keyboard nudges.

## Coherence constraints

- The verb must match the lanes' pointer grammar (Alt = mutate via pointer, Ctrl = precision,
  Shift = dominant-axis constraint) so the two surfaces read as one system.
- Check the sustain tail-drag watch item ("Sustain tail-end drag") when building: a drag-move
  grab zone competes with a tail-resize grab zone on the same note; settle the hit-test split
  deliberately (the tail item's remedy names the shared layout manifest's tail rectangle).
