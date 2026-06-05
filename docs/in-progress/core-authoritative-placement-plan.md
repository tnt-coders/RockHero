# Core-Authoritative Signal-Chain Placement Plan

Status: in-progress refactor plan. This is the remaining work of "#1" from the plugin-chain design
cleanup: make `rock-hero-editor/core` the sole authority for the signal-chain block placement, with
the view reduced to a transient drag preview plus intent emission. Increment 1 (insert reconciliation
moved to core) is already done. This document covers the remaining two increments as Phase 1 and
Phase 2. Do them one commit at a time; build and run `ctest` between phases.

## Why

Block placement (which fixed visual block each plugin occupies, including authored gaps) is editor
document state. Today the view (`SignalChainBlockLayout`) keeps a second authoritative placement and
its own reconciliation engine that parallels the workflow's. That duplication is the root of the
"observation #1/#2" review findings. The end state: editor-core owns the committed placement and all
reconciliation; the view renders `PluginViewState.block_index` and computes only the in-flight drag
preview using the shared `core::SignalChainBlockPlacement` value type.

## Locked decision (already agreed)

The committed placement crosses the UI -> core boundary **keyed by `instance_id`**, not as a
positional vector. Core then applies it with a per-instance lookup that survives the backend
reorder, so the path-dependent remap (`placementAfterCommittedPreview`) is deleted rather than
relocated. Introduce one small editor-core value type for this:

```cpp
// rock-hero-editor/core: new public type (e.g. in plugin_view_state.h or a small dedicated header)
struct PluginBlockAssignment
{
    std::string instance_id;
    std::size_t block_index{};
};
```

## State after increment 1 (for orientation)

- `SignalChainWorkflow` (core) owns `m_plugins` (with `block_index`) and the committed placement via
  `placementForSnapshot` -> {insert-preserve, removal-preserve, adopt-valid, same-order-preserve,
  compact}. It also has `setBlockPlacement(positional vector)`, `blockIndices()`,
  `setPendingInsertBlock`, and `m_pending_insert_block`.
- `SignalChainBlockLayout` (ui) still owns a committed placement (`m_placement`) and reconciliation:
  `reconciledPlacement`, `placementAfterInsert`, `placementAfterCommittedPreview`, plus
  `m_pending_insert` and the `DragPreview::committed` flag. It also computes the transient hover
  preview (`dropIntent`, `previewMove`, `withPluginAtBlock`) — that part stays.
- `core::hasSamePluginOrder` lives in `plugin_view_state.h` and is used by both layers.
- Move flow today: `onMovePluginPressed(instance_id, dest)` -> `MovePlugin{instance_id, dest}` ->
  `movePlugin` -> `replaceSnapshot` (compacts the move) -> the view's `applyPlugins`
  committed-preview branch remaps the preview onto the new order and reports it back.

---

## Phase 1 — Move reconciliation into core (increment 2)

Goal: a reordering drop carries its committed placement (instance-keyed) with the move action; core
applies it after the engine move. Delete `placementAfterCommittedPreview`.

Steps:

1. Add the `PluginBlockAssignment` type (see above) in editor-core public include. Include `<string>`
   and `<vector>` where used.

2. `editor_action.h`: `MovePlugin` gains `std::vector<PluginBlockAssignment> placement;` (the
   committed arrangement). Keep `instance_id` and `destination_index` (the engine still needs the
   move-to-index). Update the constructor.

3. Intent chain (carry the placement):
   - `SignalChainView::Listener::onMovePluginPressed(std::string, std::size_t,
     std::vector<core::PluginBlockAssignment>)`.
   - `IEditorController::onMovePluginRequested(...)` and `EditorController` (public + Impl) same
     signature; `Impl::onMovePluginRequested` -> `runAction(MovePlugin{instance_id, dest,
     std::move(placement)})`.
   - `EditorView::onMovePluginPressed` forwards.
   - Update fakes: `RecordingEditorController::onMovePluginRequested`,
     `RecordingSignalChainViewListener::onMovePluginPressed`.

4. View emits the placement. In `SignalChainView` (the move-commit path, currently
   `completePluginDrop` -> `movePluginToBlockLocation`), build the assignment from the committed
   preview the layout already computed, keyed by the **pre-move** plugin order:
   `for i: { m_state.plugins[i].instance_id, m_block_layout.committedPlacement().blocks()[i] }`.
   Pass it to `onMovePluginPressed`.

5. Workflow applies it. Add
   `bool applyBlockPlacementByInstance(const std::vector<PluginBlockAssignment>&)`: for each plugin in
   `m_plugins`, look up its block by `instance_id`; build a placement; normalize through
   `SignalChainBlockPlacement::fromIndices` (compact fallback) like `setBlockPlacement` already does;
   return whether it changed. (Internally this can share a helper with `setBlockPlacement`.)

6. `performActionImpl(MovePlugin)`: after `applySignalChainMutationSnapshot(...)` and before
   `updateView()`, call `m_signal_chain.applyBlockPlacementByInstance(action.placement)`. The mutation
   already marks unsaved; no extra dirty handling needed.

7. Delete `placementAfterCommittedPreview` from `signal_chain_block_layout.cpp` and its branch in
   `applyPlugins`. After this, a move's `setState` runs `reconciledPlacement` which adopts the
   block indices core just set (correct), so the view stays consistent. The transient hover preview
   and `commitPreview`/`DragPreview::committed` (still used by `clearUncommittedPreview`) stay for now.

8. Tests:
   - New workflow test: `applyBlockPlacementByInstance` produces the gapped placement after a move
     snapshot regardless of the new chain order; invalid assignments compact.
   - Update/relocate the layout's committed-preview tests: the "maps committed previews to backend
     order" behavior is now core's; assert it at the workflow. Drag-preview tests
     (`dropIntent`/`previewMove`) stay in the layout tests.
   - Update fakes/tests for the new `onMovePluginPressed`/`onMovePluginRequested` signatures; the
     controller move tests assert order + that `applyBlockPlacementByInstance` ran.

Done when: a reorder-into-a-gap drag persists the exact arrangement with no dependence on a view-side
remap; `placementAfterCommittedPreview` no longer exists; build + `ctest` green.

---

## Phase 2 — Collapse the view to renderer + preview (increment 3)

Goal: the view stops owning committed placement and stops reconciling snapshots. It renders core's
`block_index` and computes only the transient drag preview.

Steps:

1. Switch the placement report contract to instance-keyed:
   - `SetSignalChainPlacement` action and `IEditorController::onSignalChainPlacementChanged` /
     `SignalChainView::Listener::onSignalChainPlacementChanged` carry
     `std::vector<core::PluginBlockAssignment>`.
   - `SignalChainWorkflow::setBlockPlacement` takes the instance-keyed form (or is replaced by
     `applyBlockPlacementByInstance` from Phase 1 — collapse the two into one).
   - The view's `reportSignalChainPlacement` builds the assignment from `committedPlacement()` +
     current plugin instance IDs (same builder as Phase 1 step 4 — factor it once).

2. `SignalChainBlockLayout::applyPlugins` becomes adopt-only: build `m_placement` from the incoming
   `PluginViewState.block_index` via `SignalChainBlockPlacement::fromIndices` (compact fallback). It
   returns whether the rendered placement changed (the view still reports on a genuine change so a
   no-reorder gap drag persists — see note).

3. Remove from the layout: `reconciledPlacement`, `placementAfterInsert`,
   `placementAfterCommittedPreview` (already gone in Phase 1), `m_pending_insert` /
   `PendingInsert`, and the `DragPreview::committed` flag and its `commitPreview` /
   `clearUncommittedPreview` bookkeeping if it is now unused. Keep `dropIntent`, `previewMove`,
   `completeDrop`, `withPluginAtBlock`, `beginInsertAtBlock` (now a pure query for the chain index).
   Drop the view's `using core::hasSamePluginOrder;` once the view no longer calls it.

4. The no-reorder gap drag (drop where `destination_index == source_index`): the view still emits the
   committed placement via `onSignalChainPlacementChanged` (now instance-keyed); core stores it. This
   path must keep marking the project dirty (it has no engine mutation) — preserve the
   `performActionImpl(SetSignalChainPlacement)` dirty handling.

5. Test migration: move the remaining reconciliation tests from
   `test_signal_chain_block_layout.cpp` (same-order-preserve, adopt, pending-insert mapping) to
   `test_signal_chain_workflow.cpp`, where that logic now lives. Keep the placement-algebra tests
   (`test_signal_chain_block_placement.cpp`) and the pure drag-preview tests in the layout test file.

Done when: `SignalChainBlockLayout` holds no authoritative committed placement and no snapshot
reconciliation — only the transient preview; core is the single placement authority; build +
`ctest` green.

---

## Risks and notes

- No local build here; both phases are blind edits validated by the project's build + `ctest`. Do
  one phase per commit and build between them.
- The intricate, well-tested drag-preview behavior (path-dependent reversal) lives in
  `dropIntent`/`previewMove`/`withPluginAtBlock` and must be preserved unchanged through both phases.
- Watch re-entrancy: placement reports fire inside `setState`; they call back into the controller
  which only stores + may `updateView`. Keep that non-recursive (the existing diff/no-op guards).
- After Phase 2, `core::hasSamePluginOrder` should have a single caller (the workflow); that is the
  intended end of the earlier de-duplication.
- This refactor is the foundation for undo/redo: once core solely owns the placement and every
  signal-chain edit is an `EditorAction` applied at one boundary, undo can snapshot/restore
  `(chain order + placement + output gain)` without reaching into the view.
