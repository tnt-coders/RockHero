# Editor Action Policy Cleanup Plan

Status: implemented planning note. Focused Stage 5/6 follow-up to the editor runtime extraction
plan. The companion `editor-controller-stage-05-cleanup-plan.md` is complete and now lives in
`docs/plans/completed/`; its busy-policy and project-write metadata steps are implemented (recorded
below), and the availability follow-up is now implemented.

## Scope

Clean up the controller-adjacent action policy that became visible after the Stage 05 review. This
is not a new runtime extraction stage and not a broad replacement controller.

The immediate target is the pattern where small anonymous helpers in `editor_controller.cpp` encode
pure action metadata or policy, while nearby controller methods also perform side effects and async
orchestration. The current code makes those responsibilities look equivalent even though they
should have different homes.

## Finding

`EditorController` currently contains several distinct kinds of action knowledge:

- action taxonomy: `EditorAction`, ids, and subset variants;
- action metadata: project-write busy operation and failure prefix;
- action availability policy: idle/busy action gates;
- action dispatch: side-effect execution for accepted actions;
- workflow-result execution: prompt resolutions and calibration effects;
- view-state projection: mapping availability into `EditorViewState`.

The smell is not that helper functions exist. The smell is that pure action metadata, availability
policy, side-effect dispatch, and view projection are all represented as local controller helpers
with little structural separation.

## Design Direction

Use this split:

- `EditorAction` and adjacent private action-policy code own pure action taxonomy and metadata.
- `EditorController` owns side effects, async liveness, project IO coordination, settings writes,
  port calls, view publication, and transient error reporting.
- Stateful policy stays in focused workflow/state types such as `BusyOperationState`,
  `DeferredProjectActionState`, `PluginCatalogWorkflow`, and `InputCalibrationWorkflow`.
- Availability policy may move to a small pure facts-and-query helper if it deletes controller
  branching without becoming a second controller.

Do not introduce `ProjectWorkflow`, `EditorActionController`, `SignalChainWorkflow`, or a generic
action router as part of this cleanup.

## Immediate Cleanup (implemented)

Done in `editor_controller.cpp`. Both the project-write metadata helpers and the busy-action policy
have landed:

- `busyOperationForProjectWrite` and `projectWriteErrorPrefix` now take typed write alternatives
  with a thin `std::visit` over `EditorAction::ProjectWriteAction`; the `EditorAction::Id` versions
  and their assert-only non-write arms are gone, and the call sites pass `state->action` directly;
- the dead `ActionBusyPolicy::AllowedWhileBusy` branch is replaced by a pure `actionSupersedesBusy()`
  predicate, collapsing `canRunAction()`'s busy branch to `if (isBusy()) return
  actionSupersedesBusy(action);`.

Two notes for the record:

- This landed as the *typed-overload* shape from the stage-05 plan, not the "one metadata query
  returning a bundled value" shape this note originally proposed. That is the better outcome: the
  busy operation is read when a write starts and the failure prefix only on failure, so keeping
  them as two pure lookups avoids carrying an unused prefix through the task state.
- Non-write actions remain unrepresentable at the metadata call sites, and a future fourth write
  alternative fails to compile until its overload is added.

## Availability Follow-Up (implemented)

With the metadata and busy-policy cleanup done, this was the final live step in this note.

`actionAvailableWhenIdle()` and `deriveViewState()` still mix three ideas:

- collecting current controller facts;
- applying pure action availability policy;
- projecting enabled flags into view state.

The duplication is real, not hypothetical: `deriveViewState()` calls `canRunAction()` about a dozen
times (one per enabled flag), and each call re-runs `actionAvailableWhenIdle()`, which recomputes
the same facts every time -- `hasLoadedArrangement()`, the input-calibration snapshot, plugin-catalog
candidates, and so on. So treat the facts-snapshot extraction as expected, not as a gated maybe:

- collect the availability facts once into a private `ActionFacts` value;
- pass it to a pure `actionAvailableWhenIdle(EditorAction::Id, const ActionFacts&)` so the action
  gate and the view projection share one computation and one source of truth;
- keep view-state construction in `EditorController`; do not add an `EditorStateProjector`.

Placement rule for anything this extracts: split by whether a fact is intrinsic to the action or
depends on runtime state. Action-intrinsic, pure facts (`actionSupersedesBusy`, the project-write
metadata) belong adjacent to the taxonomy in `editor_action`-side code; state-dependent availability
stays computed in `EditorController` and is fed to the pure query as `ActionFacts`.

Keep the bar honest: the facts value plus shared query should delete the repeated recomputation and
make availability directly testable. If it would only move lines without removing that duplication,
stop.

Implemented as private editor-core action policy:

- `ActionConditions` captures the current controller conditions used for action availability;
- `editor_action_availability.h/.cpp` owns pure busy and idle availability policy;
- `EditorController` collects `ActionConditions` once during view-state projection and reuses it
  for enabled flags;
- direct `test_editor_action_availability.cpp` coverage verifies project, prompt, transport,
  plugin, calibration-prompt, and busy availability gates.

## Exit Criteria

- Project-write metadata is keyed on `EditorAction::ProjectWriteAction` (not `EditorAction::Id`)
  with no assert-only non-write arms. [done]
- Non-write actions remain unrepresentable at project-write metadata call sites. [done]
- Side-effect dispatch remains in `EditorController`.
- No broad action router or second controller is introduced.
- Availability policy moved behind a small conditions-based pure helper with a clear name. [done]
- Focused editor-core tests still pass after implementation. [done]

## Verification

Verified with the focused editor core target:

```powershell
& 'C:\Program Files\JetBrains\CLion 2025.3.2\bin\cmake\win\x64\bin\cmake.exe' --preset debug
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_core_tests'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
git diff --check
```

## Non-Goals

- Do not split `EditorController` by line count.
- Do not move public headers or folders as part of the metadata cleanup.
- Do not create a workflow around project lifecycle as a whole.
- Do not extract signal-chain policy before the audio boundary has an authoritative chain model.
- Do not update durable `docs/design/` documents from this note.
