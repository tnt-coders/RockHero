# Editor Controller Stage 05 Cleanup Plan

Status: in-progress planning note for the active editor runtime extraction work.

## Scope

This plan covers a focused cleanup pass inside Stage 05 of
`editor-runtime-extraction-plan.md`, after the runtime extractions changed the shape of
`EditorController`. It is a checkpoint cleanup, not a new extraction stage and not Stage 6
folder/readability work.

The goal is to remove accidental complexity that became visible after the current
`DeferredProjectActionState` and nested `EditorAction::ProjectAction` shape settled.

## Purpose

`EditorController` is still the correct root facade for editor UI intents, side-effect execution,
async liveness, and view-state publication. The smell is not simply that it has many functions. The
smell is that several distinct action layers now sit in one dense block:

- public UI intent wrappers;
- `EditorAction` gating and dispatch;
- deferred project prompt replay;
- project open/import/save/close execution;
- plugin catalog and current signal-chain execution;
- calibration effect execution;
- view-state projection.

The Stage 05 cleanup should make those responsibilities easier to scan and remove policy branches
that are broader than the behavior they currently support. It should not introduce another
coordinator type.

## Constraints

- Keep `EditorController` as the only public root controller for the editor view.
- Keep side effects in the root facade unless a workflow already owns the corresponding policy.
- Prefer deleting or narrowing helpers over adding new classes.
- Keep `EditorAction` private to `editor/core/src`.
- Do not add `ProjectWorkflow`, `EditorActionController`, `SignalChainWorkflow`, or any broad
  action router during this cleanup.
- Do not move files or public headers as part of this pass; folder grouping belongs to Stage 6.
- Preserve behavior. Any behavior change discovered during cleanup should be treated as a bug fix
  with focused tests, not hidden inside reordering.

## Cleanup Steps

Land one commit per step (1 doc, then 2, 3, 4, 5), each independently verifiable. This keeps the
large Step 4 reorder from burying the small logic changes in Steps 2 and 3. Step 5 depends on Step
2; Step 3 is independent of the others.

### 1. Align Planning Names

Update `editor-runtime-extraction-plan.md` so the Stage 2 references match the implemented type:
`DeferredProjectActionState`, not `DeferredEditorActionState`.

This is documentation cleanup only. It prevents the active plan from describing a type name that
no longer represents the chosen design.

### 2. Simplify Busy Action Policy

`ActionBusyPolicy` currently includes an `AllowedWhileBusy` branch, but no action returns it. That
makes the controller read as if cooperative busy-time actions exist when they do not. (Verified:
`AllowedWhileBusy` is defined at `editor_controller.cpp:380` with a live `canRunAction` arm at
`:2152`, yet `actionBusyPolicy()` only ever returns `SupersedesBusy` or `BlockedByBusy`.)

Preferred implementation:

- replace `ActionBusyPolicy` with a bool predicate `actionSupersedesBusy()` rather than keeping a
  two-value enum; once `AllowedWhileBusy` is gone the busy branch is binary, so `canRunAction()`'s
  switch collapses to `if (isBusy()) return actionSupersedesBusy(action);`. A two-state enum still
  reads like policy machinery for what is actually a yes/no question;
- simplify `canRunAction()` and `prepareAction()` accordingly;
- keep Close and Exit as the only actions that supersede a current busy operation. They currently
  return `true` unconditionally while busy (no idle-availability recheck); the predicate preserves
  that exactly.

Exit criteria:

- no dead busy-policy branch remains;
- no behavior changes for blocked actions, Close, or Exit;
- existing busy/action controller tests still pass.

### 3. Narrow Project-Write Metadata Helpers

`busyOperationForProjectWrite()` and `projectWriteErrorPrefix()` currently accept
`EditorAction::Id`, then assert for every non-write action. Their only valid callers have
`EditorAction::ProjectWriteAction`. (Verified: both at `editor_controller.cpp:226` and `:268` carry
a 15-arm `assert(false)` switch for non-write ids.)

Preferred implementation:

- change these helpers to accept `const EditorAction::ProjectWriteAction&`;
- map the three write alternatives with small type-specific free-function overloads, matching the
  typed-overload dispatch idiom already used by `applyProjectWriteSuccess`, rather than `std::visit`;
- delete the impossible non-write switch arms;
- update the call sites to pass `state->action` directly instead of `idOf(state->action)`.

Note: keep the `action_id` local in `completeProjectWriteAction`. It is still needed by the
post-save replay gate (`action_id == SaveProject || action_id == SaveProjectAs`); only the two
helper calls change.

Exit criteria:

- project-write helpers cannot be called with non-write actions;
- long assert-only switch arms disappear;
- save, Save As, and publish busy messages/errors stay unchanged.

### 4. Regroup `EditorController::Impl`

Reorder private declarations and nearby definitions by responsibility so the root facade is easier
to scan without creating new types.

This is the highest-risk, lowest-certainty step: a large pure reorder is the noisiest diff in the
plan and sits close to the Stage 6 readability work it is not supposed to be. Treat it as
provisional. If Steps 2, 3, and 5 already make the controller scan well enough, prefer folding this
into Stage 6 over doing it here. If it does land in this pass:

- do it last, in its own commit, isolated from the Step 2 and 3 logic changes;
- split it into a declarations-only reorder first (zero behavior risk), then any definition moves
  as a separate change, so each diff is reviewable on its own.

Suggested declaration groups:

- construction, view attachment, and public query adapters;
- UI intent entrypoints;
- central action gate and `EditorAction` dispatch;
- deferred project prompt and project-action replay;
- project open/import and live-rig load;
- project write/save/publish;
- close/restore/session helpers;
- plugin catalog and current signal-chain operations;
- input calibration and audio-device operations;
- view-state projection and view effects;
- busy/async/liveness helpers;
- simple state/fact helpers;
- member fields grouped by ports, services, view, workflows, project state, and async state.

Exit criteria:

- no behavior changes;
- declarations no longer mix unrelated action families in one long block;
- implementation order makes it clear which functions execute side effects and which only route
  actions or project state.

### 5. Reassess View-State Availability Projection

After the action-policy cleanup, inspect `deriveViewState()` and `actionAvailableWhenIdle()` again.
The first pass should not add a new projector type. Only consider a small helper if the same
availability fact is still recomputed or hard to reason about after the earlier cleanup.

Default expectation:

- keep availability in `EditorController`;
- do not introduce `EditorStateProjector` during this cleanup;
- leave broader projection work deferred unless duplication or unclear ownership remains obvious.

## Verification

Run the focused editor core target after implementation:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_core_tests'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
```

If files outside editor core are touched unexpectedly, stop and reassess the scope.

## Stage 05 Decision Record

After the cleanup is implemented, update `editor-runtime-extraction-plan.md` with the checkpoint
result:

- whether the four extractions deleted ownership rather than only adding forwarding;
- whether remaining `EditorController` responsibilities are intentionally root-facade work;
- whether any additional extraction should happen before Stage 6;
- whether message-thread scheduling remains deferred.

## Non-Goals

- Do not create a broad project lifecycle workflow.
- Do not create a generic action router or second controller.
- Do not extract signal-chain workflow until the audio boundary exposes a stronger chain model.
- Do not implement the deferred message-thread scheduling abstraction.
- Do not perform Stage 6 folder moves or public header moves.
- Do not update durable `docs/design/` documents from this cleanup plan.
