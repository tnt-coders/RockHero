# Undo Implementation Handoff - 2026-06-14

This is a short resume note for the active editor undo implementation. Treat the plan docs and
current code as authoritative when work resumes; this file is only a waypoint.

## Current Position

- Branch: `undo-implementation`.
- Phase M is settled: RockHero-owned mementos, Tracktion as backend, stay on B2-lite.
- The implementation is past the decision and spike gate.
- Completed implementation stages:
  - Stage 1: `EditorUndoHistory`.
  - Stage 2A: contracts, fakes, and rollback-proof fake behavior.
  - Stage 3: Undo/Redo actions added but user-disabled.
  - Stage 4: editor-only metadata undo.
  - Stage 5: plugin move undo, committed as `58dbb92b`.
  - Stage 2 adapter half: real plugin-state memento primitives, committed as `63b73f7c`.

Authoritative plan references:

- `docs/in-progress/editor-undo/editor-engine-undo-master-plan-v3.md`
- `docs/in-progress/editor-undo/editor-undo-plan.md`
- `docs/in-progress/editor-undo/undo-ownership-analysis.md`

## Latest Code Commit

`63b73f7c Add Tracktion plugin-state mementos`

Implemented in `rock-hero-common/audio/src/engine.cpp`:

- `Engine::capturePluginState` now captures external plugin state into opaque
  `PluginInstanceState` bytes by serializing Tracktion's plugin `ValueTree` XML.
- `Engine::insertPluginState` now parses a captured memento, strips the captured runtime id before
  insertion, recreates the plugin through Tracktion, reroutes via the normal chain mutation path,
  and returns the original/restored id mapping.
- Failed state insertion cleans up partially inserted Tracktion plugins before returning an
  ordinary `PluginHostError`.
- `Engine::setPluginState` now drives `ExternalPlugin::restorePluginStateFromValueTree`, then copies
  serialized state back to the target plugin while preserving the target runtime id and using a
  null undo manager for the copy.
- The parameter-edit observer endpoint is stored on the adapter, but actual Tracktion parameter
  listener ingestion remains for Stage 6.

Added concrete adapter coverage in `rock-hero-common/audio/tests/test_engine.cpp`:

- malformed plugin-state mementos are rejected;
- unknown capture/set targets report `PluginInstanceNotFound`;
- failed state insertion for a missing plugin leaves no plugin behind in captured live-rig state.

Verification run before the commit:

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
cmd.exe /d /c "call `"$vs`" -arch=x64 -host_arch=x64 && `
  ninja -C build/debug rock_hero_common_audio_tests"
& 'build/debug/rock-hero-common/audio/tests/rock_hero_common_audio_tests.exe'
```

Result: all common-audio tests passed, `588 assertions in 124 test cases`.

## Caveats To Remember

- A direct one-off `clang-tidy` run on `engine.cpp` is still noisy because the file has existing
  unrelated warnings and the compile command includes MSVC's `/MP` argument. Do not treat that
  one-off command as a clean Stage 2 verification signal.
- The new concrete adapter tests cover invalid and failed restore paths. They do not prove a
  successful real VST3 round-trip because the test suite does not own a portable VST3 fixture.
- `capturePluginState` uses Tracktion's `flushPluginStateToValueTree()`. That may write into
  Tracktion's internal undo manager. This is acceptable under the current quarantine design because
  RockHero never consumes Tracktion's undo stack as product history.
- `restoreChangedParametersFromState()` is protected in Tracktion, so the implementation does not
  call it directly after `setPluginState`. Stage 6 should verify that the public live-setter path
  is sufficient for parameter undo, open plugin windows, and any Tracktion parameter surfaces that
  RockHero observes.
- `setPluginParameterEditObserver` stores the observer and silently ignores wrong-thread calls
  because the current port method returns `void`. Stage 6 should keep message-thread enforcement
  explicit when wiring real parameter observation.
- Broad `git status` may warn about `tests/.pytest_cache` permission denial. Use
  `git status --short --untracked-files=no` when checking tracked worktree state.

## Next Step

Resume with Stage 6: plugin parameter undo.

Work should start by re-reading the current code plus the parameter sections in
`editor-undo-plan.md`, especially:

- "Plugin-parameter edits";
- "Recording Boundary";
- "Tracktion Undo Quarantine";
- adapter contract and rollback proof requirements;
- undo visibility and logging rules.

Expected Stage 6 shape:

- Wire Tracktion parameter observation in `common/audio` for user-inserted external plugins only.
- Do not attach observation to structural live-rig plugins.
- Maintain per-plugin baseline chunks.
- On gesture begin, capture the full before chunk.
- On gesture end, capture the full after chunk and emit one `PluginParameterEdit` when changed.
- Add the conservative non-gesture/debounce fallback described in the plan.
- Suppress observation while RockHero is applying edit, undo, redo, restore, or project load.
- Implement `flushPendingPluginParameterEdits` and pending-state notifications.
- Have editor-core subscribe to the observer, push parameter entries into `EditorUndoHistory`, and
  undo/redo them through `IPluginHost::setPluginState`.
- Keep the payload full-chunk mementos. Do not introduce granular parameter value replay.

After Stage 6, the remaining order is:

1. Stage 7: output gain undo.
2. Stage 8: remove memento and runtime-id remapping.
3. Stage 9: dirty tracking migration.
4. Stage 10: enable user-facing Undo/Redo.
