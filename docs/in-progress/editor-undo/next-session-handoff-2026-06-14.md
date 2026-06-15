# Undo Implementation Handoff - 2026-06-15

This is a short resume note for the active editor undo implementation. Treat the plan docs and
current code as authoritative when work resumes; this file is only a waypoint.

## Current Position

- Branch: `undo-implementation`.
- Phase M is settled: RockHero-owned mementos, Tracktion as backend, stay on B2-lite.
- The 2026-06-14 direction change is accepted: editor undo entries are polymorphic editor-core
  `IEdit` objects, not `EditorUndoEntry` plus a variant payload.
- Instance-id remapping is removed from `EditorUndoHistory`. Recreate now preserves plugin instance
  ids by contract.
- The implementation is aligned through Stage 8.

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
- Stage 8: id-preserving plugin recreate, insert/remove undo entries, editor visual-state restore,
  and rollback-contract fault handling.

Authoritative plan references:

- `docs/in-progress/editor-undo/editor-engine-undo-master-plan-v3.md`
- `docs/in-progress/editor-undo/editor-undo-plan.md`
- `docs/in-progress/editor-undo/undo-ownership-analysis.md`

## Current Code Shape

- `EditorUndoHistory` stores `std::unique_ptr<IEdit>` entries and never inspects concrete types.
- `EditorUndoPendingTransition` carries a non-owning `const IEdit*` for the two-phase transition.
- `EditorEditContext` injects apply-time dependencies: `SignalChainWorkflow`, `IPluginHost`,
  `ILiveRig`, and the controller-owned output-gain mirror.
- Concrete editor-core edit objects now own their apply logic:
  - `PluginInsertEdit`
  - `PluginRemoveEdit`
  - `PluginMoveEdit`
  - `PluginPlacementEdit`
  - `PluginDisplayTypeEdit`
  - `PluginParameterEdit`
  - `OutputGainEdit`
- `IPluginHost::insertPluginState` and `PluginInstanceRestoreResult` are gone.
- `IPluginHost::recreatePluginStatePreservingId(...)` returns a bare `PluginChainSnapshot` and
  guarantees the recreated runtime id equals the id encoded in the captured state.
- `Engine::recreatePluginStatePreservingId(...)` keeps the Tracktion `id` property intact, rejects
  missing or duplicate encoded ids, verifies the live `itemID` after insertion, and removes partial
  inserts before returning a recoverable restore error.
- `RecordingPluginHost` preserves decoded instance ids on recreate and rejects duplicate ids before
  mutation. Its old configurable `next_restored_instance_id` path is removed.
- `EditorController` records plugin insert/remove:
  - insert captures the inserted plugin state after successful candidate insert and stores a
    `PluginInsertEdit`;
  - remove captures plugin state and editor visual state before removal and stores a
    `PluginRemoveEdit`;
  - insert undo / remove redo remove the instance and restore the surviving block placement;
  - insert redo / remove undo call `recreatePluginStatePreservingId` and restore block placement plus
    display-type override.
- `PluginHostErrorCode::RollbackContractViolation` maps to
  `EditorUndoFailureCode::RollbackContractViolation`.
- Plugin-instantiating undo directions (insert redo and remove undo) run behind the
  `LoadingPlugin` busy-overlay paint fence. If close/exit supersedes the busy token before the
  recreate side effect starts, the pending undo transition is explicitly aborted so cancelling an
  unsaved-changes prompt cannot leave history stuck pending.
- The controller enters a faulted session after rollback-contract violations: editing, Undo/Redo,
  Save, Save As, Publish, and output-gain controls are blocked; Open/Restore/Import/Close/Exit
  remain available. The flag clears on successful open/import and close.
- User-facing Undo/Redo is still intentionally disabled in `deriveViewState()` until Stage 10, but
  controller tests continue to exercise `onUndoRequested()` / `onRedoRequested()` directly.

## Verification

The Stage 8 pass plus the busy-fence review fixes were verified with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_editor_core_tests -RunTouchedTests
```

Result:

- `rock_hero_common_audio_tests`: all tests passed, `597 assertions in 126 test cases`.
- `rock_hero_editor_core_tests`: all tests passed, `2429 assertions in 298 test cases`.
- `rock_hero_editor_ui_tests`: all tests passed, `624 assertions in 88 test cases`.

The first configure attempt needed network access for Conan/GitHub and succeeded after rerunning the
same helper with escalation. The final verification command above did not require reconfigure.

## Caveats To Remember

- Stage 9 has not landed yet. Dirty tracking still uses the existing hybrid state:
  `m_has_unsaved_changes`, `m_save_requires_destination`, and `EditorUndoHistory` clean markers.
- Stage 10 has not landed yet. `deriveViewState()` still reports `undo_enabled = false` and
  `redo_enabled = false` even when labels are present.
- The raw Tracktion ID-preservation regression exists in
  `rock-hero-common/audio/tests/test_tracktion_plugin_id_preservation.cpp`. A concrete
  Engine success-path recreate test still needs a real external plugin fixture; current adapter tests
  cover invalid/missing-state failure paths and the fake covers successful preserving-id behavior.
- Broad `git status` may warn about `tests/.pytest_cache` permission denial. Use
  `git status --short --untracked-files=no` when checking tracked worktree state.

## Next Step

Resume with Stage 9: dirty tracking migration.

Expected Stage 9 shape:

- Move edit dirtiness onto `EditorUndoHistory` clean-marker state where possible.
- Preserve `m_save_requires_destination` for imported/unsaved projects.
- Keep any truly non-history dirty cases explicit and narrow.
- Add the before/after transition test matrix for load, import, save, save-as, edit, undo, redo,
  redo truncation, clean-marker eviction, close prompts, and faulted sessions.

After Stage 9, the remaining order is:

1. Stage 10: enable user-facing Undo/Redo.
