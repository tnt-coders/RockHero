\page guide_add_gesture Adding a Pointer Gesture or Editing Verb

*Applies to: Editor-only.*

Use this checklist when a timeline surface (tab lane, automation lanes, tone track) gains a new
pointer gesture or editing verb — a new click/drag meaning, a new modifier behavior, a new
insert/move/resize verb. This is the most actively evolved pipeline in the editor, and almost
every step is silent: the pattern pieces compile happily when half-wired and fail as subtle
interaction bugs instead.

Read the pipeline description in \ref guide_2d_views ("Automation lanes") and the pattern
entries in \ref guide_patterns (controller-owned pointer-intent seam, gesture frozen at press,
plan/apply split) first. The *semantics* — what the gesture should mean — are owned by
`docs/plans/in-progress/editing-interaction-model.md`; check the verb table there before
inventing a meaning, and record any new one in
`docs/plans/in-progress/keymap-matrix.md`.

# The shape

Every gesture walks the same stations: the view forwards a raw pointer event → the controller
resolves hit/snap/policy → a *plan* describes the edit → previews/ghosts publish back through
view state → release applies the plan and pushes exactly one undo entry.

# Part A — The compiler walks you through these {#add_gesture_part_a}

Very little. If the verb needs a new controller intent, the pure-virtual on `IEditorController`
forces the `EditorController` forwarder, the `Impl` member, and the `RecordingEditorController`
override. Everything else is Part B.

# Part B — Silent steps {#add_gesture_part_b}

1. **Modifier vocabulary.** If the gesture reads a new modifier combination, extend
   `ChartPointerModifiers` (`editor/core/include/.../chart/chart_pointer.h`) or
   `ToneAutomationPointerModifiers` (`.../tone/tone_automation_pointer.h`) — and check the
   meaning against the one-meaning-per-modifier law before binding it.
2. **Phase handlers.** Dispatch inside the surface's `on...PointerDown/Drag/Up/Move/Exit`
   handlers in `editor_controller.cpp`. Down re-resolves the hit and arms the gesture; Move
   drives affordances only. The Doxygen on `i_editor_controller.h`'s pointer methods is the
   policy contract — update it with the new behavior.
3. **Freeze at Down.** A drag verb needs a frozen-at-press struct (`ChartPointerGesture`,
   `ToneAutomationDrag` in `editor_controller_impl.h`): capture the model rows/points and the
   Down event's geometry, and never re-resolve during Drag/Up. Forgetting this compiles clean
   and surfaces as mid-drag warping when a state push relayouts the lane.
4. **Plan, don't mutate.** Express the edit as a pure planner returning
   `std::optional<Plan>` (empty = refused; see `chart_edits.h` and `planLanePointAtCaret`).
   Refuse whole operations at boundaries — never clamp an edit.
5. **Publish the affordance honestly.** Ghosts and previews are controller-published view state
   (`m_chart_insert_ghost`, drag previews) resolved through the *same* planner/occupancy the
   commit uses, so the ghost is absent wherever the click would no-op — no lying affordances.
   Hide it while playing and without its modifier.
6. **Esc rung.** A new cancellable gesture claims a rung: the view-side pre-cancel (for
   gestures the view still owns) or the core ladder in `onChartEscapePressed`. One rung per
   press — see \ref guide_keyboard.
7. **Commit once.** Release applies the plan and pushes exactly one undo entry
   (`applyChartEditPlan`, or the surface's one full-list intent). Preview phases push nothing.
   Wheel-tick verbs coalesce via the input-coalescing window instead (\ref guide_patterns).
8. **View forwarding.** The JUCE component forwards the raw event with painted geometry
   attached (`tab_view.cpp`, `tone_automation_lanes_view.cpp` mouse handlers) — it makes no
   policy decision, not even "was this a click or a drag".
9. **Selection and marker effects.** Decide explicitly what the gesture does to the one
   editor-wide selection and the marker (arm, dissolve-in-place, nothing), per the interaction
   model; wire it through `setSelection` / the marker helpers, never by assigning fields
   directly.
10. **Tests.** Editor-core gesture tests drive the pointer intents directly (see
    `test_chart_editing.cpp`, `test_editor_controller_tone_automation.cpp` for the idiom:
    synthesize Down/Drag/Up events, assert plan effects, ghost visibility, undo round-trip).
    UI wiring tests assert the component forwards events with correct geometry.
