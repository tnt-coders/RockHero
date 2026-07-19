# Automation Lane Pointer Pipeline Migration (Phases 2–5)

Status: **In progress** — Phase 1 shipped 2026-07-19; Phases 2–5 in execution 2026-07-19 (plan
finalized with user fixes, ship-in-full go-ahead). Baseline stamp: Phase 1 landed at commit
`bbfe915c` (chart Alt+click sibling work at `024b086d`).

## Goal

Make the automation lane's pointer/edit pipeline **controller-centric**, matching the tab lane's
`ChartPointerEvent` architecture, so `IEditorController` owns every hit-test / snap / placement /
gesture decision and the policy is testable without JUCE (the `chart_pointer.h` contract). This
continues the off-grid unification's direction (commit `0f8e14f2` moved the automation *nudge*
policy out of the lanes view into editor-core); this plan moves the rest.

## Why this is lower-urgency than it first looked

The snap **math** already lives in editor-core (`tempo_grid_geometry.h`:
`nearestTempoGridPosition`, `fineGridPositionForBeat`; the lane view merely calls
`musicalGridPositionForX`, itself a composition of those). So there is **no functional drift**
between the chart and lane today — both bottom out in the same core snap. This migration is a
**structural / testability** improvement (controller-owned, JUCE-free lane pipeline), not a bug
fix. It is behavior-preserving **except for three small deliberate fixes** called out below: the
≤1px ghost/placement snap discrepancy (Phase 2), the occupied-slot refusal for mouse placement,
and the restored insert-ghost occupancy gate (both Phase 3). Weigh that before spending the
effort.

## Phase 1 — DONE (`bbfe915c`)

The lane insert ghost is now controller-computed and published through
`ToneAutomationViewState::insert_ghost`, replacing the view-local `GhostPoint`. The view forwards
a hover intent (`onToneAutomationLaneHovered` / `onToneAutomationLaneHoverEnded`) through its
`Listener` and `EditorView`; the controller snaps and publishes `{lane_index, seconds}`,
dirty-checked. The view paints the ring, deriving the on-curve `y` locally. Behavior-preserving.

## Phases 2–5 (verify scope before building)

Each phase is independently build/test/committable. Mirror the chart's committed pipeline
(`ChartPointerEvent` + `onChartPointer{Down,Drag,Up,Move,Exit}` + `chartPlacementAt`) as the
template.

- **Phase 2 — Pointer-event seam + hit-testing.** Introduce a `ToneAutomationPointerEvent` (raw
  lane-local pixels plus the lane geometry — the per-lane extents/heights, which today live in the
  view as `m_lane_heights`) and `onToneAutomationPointer{Down,Drag,Up,Move,Exit}`. Move the
  *editing* hit resolution (point vs. empty-lane-area) into the controller. Keep the pure-view hits
  in the view: resize bands, name chips, the "+" picker, and all popup menus are layout/JUCE
  concerns, not editing policy. Folding the hover into the event seam replaces Phase 1's targeted
  `onToneAutomationLaneHovered` intents. **This phase erases the known ≤1px slot-boundary snap
  discrepancy** between the ghost (`secondsForX`) and the readout/placement (`musicalPositionForX`)
  by giving both one geometry path, as on the chart.

- **Phase 3 — Move/insert drag state machine.** Move `MovePointDrag` into editor-core: snap
  position, neighbor-clamp to keep points strictly ascending, delta-based value from the press
  point (never jump to the raw pointer `y`), Shift dominant-axis lock, editable-window clamp, and
  the snap-guide publish. When this lands, **give mouse placement the occupied-slot refusal** the
  keyboard Insert already has (`planLanePointAtCaret`), and then **re-add the insert-ghost
  occupancy gate** removed in Phase 1 (the ghost and the gesture become honest together, matching
  the chart's `§7` no-lying-affordance rule).

- **Phase 4 — Caret, typed entry, keyboard nudges on one snap.** Route lane caret arming, the
  typed-value point creation, and the Alt+arrow create-then-nudge through the controller's single
  snap authority (the nudge already goes through `onSelectionMoveRequested`).

- **Phase 5 — Cleanup + parity + docs.** Drop the lane view's `m_tempo_map` / `m_grid_note_value`
  where no longer needed; verify no behavioral regression against pre-migration behavior; record
  the lane's controller-centric pointer pipeline in `editing-interaction-model.md` and the
  developer guide (`the-editor-2d-views.md`).

### Deferred sub-item: fully controller-owned ghost value

Phase 1 publishes the ghost's **position** only; the view still computes the on-curve `y`
(`curveValueAt`, which for unauthored/tracking lanes needs the live `IToneAutomation` value the
view polls per vblank). A fully chart-parity ghost would publish the value too. Do this only if
the controller gains clean access to the tracking value — otherwise the position-only split is the
right seam.

## Execution notes (for the implementing agent)

- Move this file to `docs/plans/in-progress/` when execution starts.
- **Each phase that moves policy into editor-core must land Catch2 tests for that policy in the
  same commit** — that testability is the plan's payoff, not an optional extra. Use the chart's
  coverage as the template: `test_chart_editing.cpp` for controller policy,
  `recording_editor_controller.h` for view-seam intent checks, `test_tab_view.cpp` /
  `test_tone_automation_lanes_view.cpp` for the view side. Phase 3's drag state machine
  (neighbor clamp, delta-based value, Shift axis lock, window clamp, occupancy refusal) is the
  highest-value test target.
- Build and test per phase through `.agents/rockhero-build.ps1`; commit each phase separately
  with the three deliberate behavior fixes named in the message of the phase that ships them.
- Do not run clang-tidy; leave that to the user.

## Coherence constraints

- Mirror the chart's pipeline shape so the two surfaces read as one system; do not invent a
  divergent lane-only pattern.
- Keep genuinely presentational concerns in the view (painting, lane resize/heights, value
  readouts, menus, typed-value callouts, tracking-line vblank). Move only editing *policy*
  (hit-test, snap, placement, gesture state).
- Preserve the deferred-state-during-gesture guard (`m_pending_state`) semantics: a state push
  arriving mid-gesture must not reset the edit in progress.
