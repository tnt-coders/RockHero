\page guide_add_action Adding an Editor Action

*Applies to: Editor-only.*

Use this checklist whenever the editor gains a new user-triggered operation. For a worked example
from history, `git show 34448045` (create/delete/rename tone handlers) shows the full pattern in
one commit — treat it as archaeology, not as current truth.

Read \ref guide_action_anatomy first if the pipeline is unfamiliar.

# Part A — The compiler walks you through these {#add_action_part_a}

Start with these two edits; the resulting compile errors enumerate most of the remaining work,
because the pipeline's switches are exhaustive with no `default:` and its interfaces are pure
virtual.

1. Add the id to `EditorActionId`
   (`rock-hero-editor/core/include/rock_hero/editor/core/controller/editor_action_id.h`).
2. Add the payload struct and its `std::variant` membership in
   `rock-hero-editor/core/src/controller/editor_action.h` — and, if the action participates in
   project lifecycle (deferrable behind the unsaved-changes prompt) or writes the project, add it
   to `ProjectAction` / `ProjectWriteAction` there too.

The compiler then demands, in `rock-hero-editor/core/src/controller/`:

- the id mapping arm in `idOfAlternative` (`editor_action.cpp`);
- a case in each exhaustive availability switch in `editor_action_availability.cpp`
  (`actionBlockedByInputCalibrationPrompt`, `actionAvailableWhenIdle`, `actionSupersedesBusy`) —
  these force you to *decide* the action's gating, which is the point;
- the `performActionImpl` overload declared in `editor_controller_impl.h`;
- the exhaustive `EditorActionId` switch in
  `rock-hero-editor/ui/src/main_window/editor_view.cpp` (unsaved-changes prompt wording);
- if you add a public `IEditorController` entry point: the override in `EditorController` and in
  the test double `RecordingEditorController`
  (`rock-hero-editor/core/tests/include/.../testing/recording_editor_controller.h`).

# Part B — Silent steps: nothing fails if you forget {#add_action_part_b}

These are the loose ends. Check each one deliberately.

1. **The handler body's location.** Define `performActionImpl` in the feature's handler file
   (`tone_handlers.cpp`, `project_handlers.cpp`, `signal_chain_handlers.cpp`,
   `tone_designer_handlers.cpp`, `input_calibration_handlers.cpp`, `audio_device_handlers.cpp`)
   — never in `editor_controller.cpp` for convenience.
2. **`actionUnavailableReason`** (`editor_controller.cpp`) — the rejection-logging text. A missing
   case degrades diagnostics without failing anything.
3. **Undo.** If the action mutates undoable state, write an `IEdit` in the feature's `*_edits.h`
   / `*_edits.cpp` pair, capture the before-state *before* mutating, and push exactly one entry
   per user gesture via `pushUndoEntry`. Nothing reminds you: an action without an edit simply
   isn't undoable, and a user will find that before a test does.
4. **New availability preconditions.** If gating needs a fact the policy cannot see, add a field
   to `ActionConditions` (`editor_action_availability.h`) and populate it in
   `currentActionConditions` — do not reach around the policy from the handler.
5. **View state.** If the action changes anything the UI shows (including a new prompt), extend
   the view-state structs, derive them in `deriveViewState()`, and render them in the component.
   A forgotten derivation shows stale UI, not an error.
6. **The trigger.** Wire the actual menu item, keystroke, or button in
   `rock-hero-editor/ui/src/main_window/editor_view.cpp` (or the owning component) — the action
   compiles and works perfectly while being unreachable.
7. **Tests.** Add a `test_*.cpp` under `rock-hero-editor/core/tests/` **and list it in that
   folder's `CMakeLists.txt`** (source lists are explicit; an unlisted test silently never runs).
   Drive the action through the harness
   (`editor_controller_test_harness.h`), covering: gating (available/unavailable states), the
   happy path's observable effects, and undo/redo round-tripping if step 3 applied.
8. **Doxygen** on everything you added to public headers (`editor_action_id.h`, the payload
   struct, any `IEditorController` method) — see \ref design_documentation_conventions.
