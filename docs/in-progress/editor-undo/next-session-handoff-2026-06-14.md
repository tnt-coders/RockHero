# Undo Implementation Handoff - 2026-06-15

This is a short resume note for the active editor undo implementation. Treat the plan docs and
current code as authoritative when work resumes; this file is only a waypoint.

## Current Position

- Branch: `undo-implementation`.
- Phase M is settled: RockHero-owned mementos, Tracktion as backend, stay on B2-lite.
- The 2026-06-14 direction change is accepted: editor undo entries are polymorphic
  editor-core `IEdit` objects, not `EditorUndoEntry` plus a variant payload.
- Instance-id remapping is removed from `EditorUndoHistory`. Stage 8 must preserve plugin
  instance ids during recreate rather than rewriting stored history.
- The implementation is aligned through Stage 7.

Completed implementation stages:

- Stage 0: Tracktion/JUCE behavior spike and id-preservation decision.
- Stage 1: `EditorUndoHistory` two-phase policy and clean-marker behavior.
- Stage 2A: `IPluginHost` contracts, fakes, and rollback-proof fake behavior.
- Stage 2 adapter half: real plugin-state capture, state insertion, and `setPluginState`.
- Stage 3: Undo/Redo actions added but user-facing commands still disabled.
- Stage 4: signal-chain placement and display-type override undo.
- Stage 5: plugin move undo.
- Stage 6: plugin parameter undo via full-state before/after chunks.
- Stage 7: output-gain gesture boundaries and output-gain undo entries.

Authoritative plan references:

- `docs/in-progress/editor-undo/editor-engine-undo-master-plan-v3.md`
- `docs/in-progress/editor-undo/editor-undo-plan.md`
- `docs/in-progress/editor-undo/undo-ownership-analysis.md`

## Current Code Shape

The staged code change that accompanies this handoff does the `IEdit` alignment pass:

- `EditorUndoHistory` stores `std::unique_ptr<IEdit>` entries and never inspects concrete types.
- `EditorUndoPendingTransition` carries a non-owning `const IEdit*` for the two-phase transition.
- `EditorEditContext` injects apply-time dependencies: `SignalChainWorkflow`, `IPluginHost`,
  `ILiveRig`, and the controller-owned output-gain mirror.
- Concrete editor-core edit objects now own their apply logic:
  - `PluginMoveEdit`
  - `PluginPlacementEdit`
  - `PluginDisplayTypeEdit`
  - `PluginParameterEdit`
  - `OutputGainEdit`
- `EditorController` pushes `std::make_unique<...Edit>()` entries and dispatches undo/redo through
  `pending.edit->undo(context)` / `pending.edit->redo(context)`.
- Parameter edit labels use the observer's display-only `label_hint` when present, for example
  `Edit Gain`. The restore payload remains the full plugin chunk.
- Removed stale editor-core symbols and tests for `EditorUndoEntry`, payload variants,
  `EditorUndoHistory::remapInstanceId`, `InstanceIdRemapped`, and pending-entry copies.

User-facing Undo/Redo is still intentionally disabled in `deriveViewState()` until Stage 10, but
controller tests continue to exercise `onUndoRequested()` / `onRedoRequested()` directly.

## Verification

The `IEdit` alignment pass was verified with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_editor_core_tests,rock_hero_editor_ui_tests -RunTouchedTests
```

Result:

- `rock_hero_editor_core_tests`: all tests passed, `593 assertions in 125 test cases`.
- `rock_hero_editor_ui_tests`: all tests passed, `2318 assertions in 292 test cases`.
- Touched common/editor test run: all tests passed, `624 assertions in 88 test cases`.

The handoff rewrite itself is documentation-only and was added after that verification.

## Caveats To Remember

- Stage 8 has not landed yet. Insert/remove undo entries do not exist in editor-core.
- Common-audio still exposes the Stage 2 adapter-half shape:
  - `insertPluginState(...)`
  - `PluginInstanceRestoreResult`
  - `original_instance_id` / `restored_instance_id`
- Stage 8 should recontract that already implemented path into
  `recreatePluginStatePreservingId(...)` returning a bare `PluginChainSnapshot`, with the
  preserved id guaranteed by contract.
- Do not reintroduce history-wide id remapping. If recreate does not preserve the encoded id, that
  is a restore failure after rollback, not a signal to rewrite stored `IEdit` entries.
- `insertPluginState` currently strips the captured `id` for the general new-instance path. Undo
  recreate must keep the id intact after the original instance is gone.
- Broad `git status` may warn about `tests/.pytest_cache` permission denial. Use
  `git status --short --untracked-files=no` when checking tracked worktree state.

## Next Step

Resume with Stage 8: remove memento boundary and id-preserving recreate.

Start by rereading the current code plus these plan sections in `editor-undo-plan.md`:

- "Representation decided"
- "Audio Boundary"
- "Editor Visual State On Recreate"
- "Instance-Id Preservation"
- "Recording Boundary"
- staged implementation item 8

Expected Stage 8 shape:

- Rename/recontract `IPluginHost::insertPluginState` into `recreatePluginStatePreservingId`.
- Drop `PluginInstanceRestoreResult` and the original/restored-id mapping from the undo path.
- Update `Engine`, `RecordingPluginHost`, common-audio tests, and editor-core fakes/tests for the
  id-preserving contract.
- Add adapter-level coverage that captured state can be removed and recreated with the same id.
- Add editor-core `PluginRemoveEdit` with captured full plugin state plus editor visual state.
- Wire remove undo as id-preserving recreate plus visual-state restore.
- Keep insert redo on the same id-preserving recreate path if insert undo/redo is included in this
  stage.
- Add rollback-contract handling/faulted-session behavior if the Stage 8 implementation reaches
  the first real rollback-contract trigger.

After Stage 8, the remaining order is:

1. Stage 9: dirty tracking migration.
2. Stage 10: enable user-facing Undo/Redo.
