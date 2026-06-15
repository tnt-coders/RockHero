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
- The implementation is aligned through Stage 10.

Completed implementation stages:

- Stage 0: Tracktion/JUCE behavior spike and id-preservation decision.
- Stage 1: `EditorUndoHistory` two-phase policy and clean-marker behavior.
- Stage 2A: `IPluginHost` contracts, fakes, and rollback-proof fake behavior.
- Stage 2 adapter half: real plugin-state capture, state insertion, and `setPluginState`.
- Stage 3: Undo/Redo actions added behind the temporary disabled user-facing gate.
- Stage 4: signal-chain placement and display-type override undo.
- Stage 5: plugin move undo.
- Stage 6: plugin parameter undo via full-state before/after chunks.
- Stage 7: output-gain gesture boundaries and output-gain undo entries.
- Stage 8: id-preserving plugin recreate, insert/remove undo entries, editor visual-state restore,
  and rollback-contract fault handling.
- Stage 9: dirty tracking migrated to undo-history clean markers, with
  `m_save_requires_destination` for imported/unsaved projects and a narrow untracked dirty state
  for load normalization rewrites, undo-recording failures, and faulted sessions.
- Stage 10: user-facing Undo/Redo availability is enabled through the central action policy, with
  existing labels shown in the Edit menu and shortcut dispatch gated by the same view state.

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
  - if insert undo preparation fails after the plugin was inserted, the controller first tries to
    remove the inserted plugin and preserve existing undo history;
  - if that cleanup fails, the inserted plugin remains as an untracked dirty edit and undo history
    is cleared; rollback-contract violations fault the session;
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
- Dirty state is derived from `EditorUndoHistory::hasUnsavedEdits()`,
  `m_save_requires_destination`, and the narrow `m_has_untracked_unsaved_changes` fallback flag.
  Import dirtiness relies on `m_save_requires_destination`; normal tone edits rely on the undo
  clean marker.
- Live-rig persistence is the current editor-core path. The old hardcoded
  `hasLiveRigPersistence()` stub and no-persistence dirty fallbacks have been removed.
- User-facing Undo/Redo now flows through `deriveViewState()` using the same action-availability
  policy as direct controller requests. Empty history, busy work, calibration prompts, and faulted
  sessions still disable the commands.

## Dirty Transition Matrix

- Load saved project: reset undo history, mark the load baseline clean, and remain clean unless
  audio normalization rewrites project data on load.
- Import native song: reset undo history, mark the import baseline clean, and remain dirty through
  `m_save_requires_destination` until Save As chooses an editor project path.
- Save: clear untracked dirty state and mark the current undo position clean.
- Save As: clear untracked dirty state, clear `m_save_requires_destination`, update the project
  file, and mark the current undo position clean.
- Tracked edit: push an `IEdit`; dirtiness comes from the history position differing from the clean
  marker.
- Undo to clean marker: close/replacement prompts clear because the history position is clean.
- Redo away from clean marker: close/replacement prompts return.
- New edit after undo: redo truncation makes a clean marker in that discarded branch unreachable,
  so the project remains dirty even if all reachable edits are undone.
- Clean-marker eviction: bounded history eviction makes the marker unreachable and keeps close
  prompts active.
- Faulted session: rollback-contract violations set the untracked dirty flag and block Save,
  Save As, Publish, editing, and Undo/Redo while leaving recovery actions available.

## Verification

The Stage 10 pass was verified with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_editor_core_tests -RunTouchedTests
```

Result:

- `rock_hero_common_audio_tests`: all tests passed, `597 assertions in 126 test cases`.
- `rock_hero_editor_core_tests`: all tests passed, `2486 assertions in 304 test cases`.
- `rock_hero_editor_ui_tests`: all tests passed, `624 assertions in 88 test cases`.

The first configure attempt needed network access for Conan/GitHub and succeeded after rerunning the
same helper with escalation. The final verification command above did not require reconfigure.

## Caveats To Remember

- The raw Tracktion ID-preservation regression exists in
  `rock-hero-common/audio/tests/test_tracktion_plugin_id_preservation.cpp`. A concrete
  Engine success-path recreate test still needs a real external plugin fixture; current adapter tests
  cover invalid/missing-state failure paths and the fake covers successful preserving-id behavior.
- Broad `git status` may warn about `tests/.pytest_cache` permission denial. Use
  `git status --short --untracked-files=no` when checking tracked worktree state.

## Next Step

The ordered undo implementation sequence is complete. Before calling the feature user-ready,
manually exercise Undo/Redo from the UI for placement, display type, move, parameter, output gain,
insert, and remove. After that, the master plan points at the separate
`docs/in-progress/remaining-god-object-decomposition-plan.md` initiative.
