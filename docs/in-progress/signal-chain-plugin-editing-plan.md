# Signal Chain Plugin Editing Plan

Status: in-progress plan. This is the next active editor target.

## Scope

Make the editor signal-chain panel support real plugin-chain editing:

- insert a scanned plugin before, between, or after existing plugins;
- move loaded plugins to a new position in the current chain;
- keep remove and open-plugin behavior working with the same stable instance IDs;
- keep the backend chain, controller state, and rendered panel in the same order.

This plan promotes the old `SignalChainWorkflow` placeholder from
`docs/todo/editor-structure-deferred-work.md` into an active feature plan. The plan name is feature
oriented on purpose: the user-visible goal is plugin editing, while `SignalChainWorkflow` is only a
possible implementation type.

## Current State

The editor already has the first minimal chain surface:

- `common::audio::IPluginHost` scans plugin candidates, appends a candidate with `addPlugin()`,
  removes an instance with `removePlugin()`, and opens a plugin window with `openPluginWindow()`.
- `EditorController` owns the current `std::vector<PluginViewState>` as `m_plugins`.
- Adding a plugin appends locally after the backend returns `PluginHandle`.
- Removing a plugin validates the UI instance ID, calls the backend, erases the local row, and
  reindexes locally.
- Loading and saving use `ILiveRig` snapshots containing `LiveRigPlugin` rows.
- `SignalChainPanel` renders one row per plugin, row-click opens the plugin editor, and each row has
  a remove button.
- The plugin browser has no insertion target. Selecting a browser item always means append.

The current shape is intentionally not enough for move/insert. Local reindexing in the controller is
acceptable for remove-only, but insert and reorder need an authoritative post-mutation snapshot from
the audio boundary so the UI cannot drift from the backend chain.

## Goals

- A user can add a plugin at any chain position, not just at the end.
- A user can move an existing plugin up/down or to another position.
- Every successful chain mutation returns the authoritative chain order from `common/audio`.
- Failed mutations leave the visible chain unchanged and report typed errors through the existing
  controller error path.
- Signal-chain edit availability follows the existing action gates: no busy operation, loaded
  arrangement, live-input audition available, and no active input-calibration prompt.
- Plugin mutation still marks the project/tone dirty so Save captures the edited rig.
- The first implementation uses explicit panel controls for insert and move. Drag-and-drop can be
  added later after the backend and controller semantics are settled.

## Naming

Use feature names for plans and user-facing API, and role names only for implementation types:

- Plan: `Signal Chain Plugin Editing`.
- Audio values: prefer `PluginChainEntry` and `PluginChainSnapshot` if a shared authoritative
  chain value is introduced.
- Editor workflow: `SignalChainWorkflow` is a reasonable private `editor/core` type if it deletes
  controller-owned chain state and validation logic.
- UI type: keep `SignalChainPanel`; it is already the right presentation name.

Avoid naming the plan or public behavior after `Workflow`. The user does not run a workflow; they
edit the signal chain.

## Stage 1: Audio Chain Mutation Contract

Add a project-owned chain snapshot surface before adding UI movement:

1. Introduce a common/audio value for one loaded chain row. Prefer `PluginChainEntry` with the same
   durable fields currently repeated across `PluginHandle`, `LiveRigPlugin`, and `PluginViewState`:
   instance ID, plugin ID, display name, manufacturer, format name, and chain index.
2. Introduce `PluginChainSnapshot` as the authoritative ordered vector returned after mutations.
3. Extend `IPluginHost` with position-aware mutation methods:
   - insert a `PluginCandidate` at an index in `[0, plugin_count]`, where `plugin_count` appends;
   - remove a plugin by instance ID and return the resulting snapshot;
   - move a plugin by instance ID to a destination index and return the resulting snapshot.
4. Either replace `addPlugin()` with insert-at-end semantics in one migration, or keep `addPlugin()`
   temporarily as a convenience wrapper around insertion at `plugin_count`.
5. Update `Engine` to mutate the Tracktion plugin list on the message thread, restore monitoring
   after the graph change, and report typed failures for missing instances, invalid indices,
   insertion failures, and monitoring-route failures.
6. Update `ILiveRig` load/capture results to use the same chain-entry value if that can be done
   without broad churn. If not, keep conversion local and note the remaining unification.

Exit criteria:

- audio tests cover insert at beginning, middle, and end;
- audio tests cover move up, move down, and no-op move-to-current-position;
- audio tests cover invalid insertion/move positions and missing instance IDs;
- every successful mutation returns a full ordered snapshot with fresh chain indices.

## Stage 2: Editor Core Policy

Move signal-chain state and edit decisions out of loose controller fields only after the audio
contract can return authoritative snapshots.

1. Add private `editor/core` chain-edit action payloads:
   - add/insert plugin with a selected plugin ID and insertion index;
   - move plugin with instance ID and destination index;
   - remove plugin by instance ID;
   - open plugin by instance ID.
2. Add public controller intents needed by the panel and browser. Keep the browser selection simple:
   the controller should remember the pending insertion index when it opens the browser, then apply
   that index when the user selects a plugin.
3. Add `SignalChainWorkflow` if it deletes real ownership from `EditorController`. It should own the
   current chain snapshot, pending browser insertion target, stale-ID validation, and view-state
   projection inputs. It should not call `IPluginHost`, `ILiveRig`, settings, or UI.
4. Let `EditorController` remain the root facade that resolves catalog candidates, executes
   `IPluginHost` mutations, applies returned snapshots to the workflow, marks the project dirty, and
   refreshes the view.
5. Extend action availability for insert/move while preserving existing gates for add/remove/open.
6. Ensure load, close, import, save capture, and failed live-rig load clear or replace the workflow
   snapshot in the same places that currently update `m_plugins`.

Exit criteria:

- stale insert/move/remove/open requests are ignored before calling the backend;
- failed backend mutations report errors and leave the workflow snapshot unchanged;
- successful insert/move/remove updates from the returned authoritative snapshot;
- all structural plugin mutations mark the project/tone dirty;
- direct workflow tests cover snapshot replacement, pending insertion target, stale IDs, and
  reordering decisions without constructing the full controller.

## Stage 3: Signal-Chain Panel Controls

Add explicit controls first. Avoid starting with drag-and-drop because drag introduces hit testing,
drop previews, and pointer-state complexity before the chain mutation semantics are proven.

1. Add insertion affordances for index `0` through `plugin_count`:
   - an empty-chain insert control;
   - gap insert controls before/between rows;
   - an append control after the last row.
2. Add move controls to each row:
   - move up disabled on the first row;
   - move down disabled on the last row;
   - all move controls disabled when chain editing is unavailable.
3. Keep row-click as open-plugin, and keep remove as a separate control so the row's click target
   does not conflict with editing buttons.
4. Consider compact icon-style buttons when the JUCE project has a suitable icon path. If not, use
   small text buttons with clear tooltips and stable component IDs for tests.
5. Keep row layout stable for long plugin names and small panel heights. Hidden overflow rows must
   not leave active edit controls floating in the wrong position.

Exit criteria:

- UI tests prove insert controls emit the intended insertion index;
- UI tests prove move controls emit the intended instance ID and destination index;
- disabled state prevents insert/move/remove requests;
- row-click still opens the plugin editor;
- layout tests cover first, middle, last, and empty-chain states.

## Stage 4: Browser Insertion Flow

Make the plugin browser aware of the selected insertion target through controller state rather than
teaching the browser about signal-chain layout.

1. Opening the browser from the main Add button should set the pending insertion target to append.
2. Opening the browser from a gap insert control should set the pending insertion target to that
   exact index.
3. Browser selection should call the existing add intent with the selected plugin ID; the controller
   combines it with the pending insertion target.
4. Closing the browser should clear the pending insertion target.
5. Add-state text can stay generic unless a clear existing label already exists for insertion mode.

Exit criteria:

- selecting a browser plugin after a gap insert inserts at that gap;
- selecting after the main Add button appends;
- closing/reopening the browser does not reuse a stale insertion target;
- failed add leaves the browser open and preserves the insertion target so the user can retry.

## Stage 5: Verification And Cleanup

Run focused verification after each implementation slice:

- `rock_hero_common_audio_tests` for the audio mutation contract;
- `rock_hero_editor_core_tests` for controller/workflow behavior;
- `rock_hero_editor_ui_tests` for panel and browser wiring.

After the feature is stable, reassess whether the repeated plugin chain row types should be fully
unified as durable design cleanup. Do not update `docs/design/` until the implemented shape has
proven itself and the user confirms it should become durable architecture.

## Non-Goals

- Do not implement tone slots, racks, parallel chains, or crossfades.
- Do not implement automation lanes or plugin parameter automation.
- Do not expose Tracktion plugin objects, JUCE plugin descriptions, or raw plugin-list APIs through
  editor code.
- Do not fake reorder by changing only `EditorController::m_plugins` or the panel row order.
- Do not make drag-and-drop the first implementation requirement.
- Do not split `Engine` or reorganize folders just to make this feature possible.
- Do not rename every plugin-related type as part of this plan unless it directly supports the
  authoritative chain snapshot.

## Definition Of Done

The plan is complete when:

- the audio boundary can insert, move, and remove plugins and return authoritative ordered
  snapshots;
- the editor can add a browser plugin at any visible chain position;
- the editor can move loaded plugins and the backend order changes with the UI order;
- remove/open behavior still validates stable instance IDs;
- save/capture persists the edited order;
- failures leave visible state unchanged and surface typed errors;
- focused common/audio, editor/core, and editor/ui tests pass.
