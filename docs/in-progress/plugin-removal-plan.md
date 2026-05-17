# Plugin Removal Plan

## Goal

Let the editor remove plugins from the current linear signal chain.

This is a small editing slice that should stay separate from project plugin persistence. Removal
should update the runtime Tracktion chain, update the controller-owned signal-chain view state, and
leave the project marked dirty only once plugin persistence exists.

## Current State

The current plugin flow is append-only:

- `IPluginHost::scanPluginFile()` discovers candidates.
- `IPluginHost::addPlugin()` appends a selected candidate to the instrument chain.
- `EditorController` stores runtime-only `PluginViewState` values in `m_plugins`.
- `SignalChainPanel` renders rows and exposes only the Add Plugin action.

No durable tone-chain model exists yet. `ProjectEditorState` persists editor cursor/selection state,
not plugins.

## Scope

Implement removal for the current linear chain only.

In scope:

- remove one plugin instance from the current runtime chain
- rebuild monitoring after removal
- update row indices in `m_plugins`
- expose a simple remove affordance in `SignalChainPanel`
- keep removal disabled while the controller is busy, matching Add Plugin routing
- report typed plugin-host failures through the existing view error path

Out of scope:

- plugin persistence in `.rhp`
- full plugin parameter/preset state
- reordering, bypass, automation, racks, or parallel chains
- confirmation prompts for removal

## Design Direction

Extend the audio boundary first:

```cpp
[[nodiscard]] virtual std::expected<void, PluginHostError> removePlugin(
    const std::string& instance_id) = 0;
```

Use `instance_id` rather than chain index at the boundary. The controller already stores the host
instance ID in `PluginViewState`, and instance IDs avoid deleting the wrong plugin if the UI state
and backend order ever diverge.

`Engine::removePlugin()` should remain a message-thread operation, stop/release backend context as
needed, remove the plugin from the instrument track plugin list, then rebuild the monitoring graph.
If the instance ID is missing, return `PluginHostErrorCode::PluginInstanceNotFound`. Keep this
separate from `PluginHostErrorCode::PluginNotFound`, which describes a missing scanned candidate
before insertion.

Use Tracktion `Plugin::deleteFromParent()` rather than `removeFromParent()` for deletion. The
plugin is leaving the edit entirely, so Tracktion should also hide related automation parameters
and plugin windows instead of merely detaching the plugin for reinsertion elsewhere.

## Controller Slice

Add `EditorAction::Id::RemovePlugin` carrying an instance ID.

Controller behavior:

- ignore removal when no plugin host or no loaded arrangement exists
- silently ignore a stale instance ID that is no longer present in `m_plugins`
- call `m_plugin_host->removePlugin(instance_id)`
- on success, erase the matching `PluginViewState`
- renumber remaining `chain_index` values from zero
- push derived state
- on failure, keep state unchanged and call `reportError("Could not remove plugin: ...")`

Do not add busy overlay for remove. It should be quick, and if it is not, we should measure before
adding more busy UI.

Add an explicit `SignalChainViewState::remove_plugins_enabled` field rather than treating
`add_plugin_enabled` as a proxy for all signal-chain mutation. The values will usually match in
the current linear chain, but keeping them separate prevents Add Plugin availability from becoming
hidden policy for remove, reorder, bypass, or future tone-slot operations.

## UI Slice

Update `SignalChainPanel` from paint-only rows to rows with a remove control.

Preferred first pass:

- keep Add Plugin in the header
- render one compact row component per plugin, or store button bounds and handle clicks
- use a small remove button on the right side of each row
- emit `onRemovePluginPressed(std::string instance_id)` through the panel listener
- disable remove controls from `SignalChainViewState::remove_plugins_enabled`

Using child row components is a little more code but easier to test and less fragile than manual
hit-testing if rows gain more controls later.

## Tests

Core controller tests:

- remove succeeds and erases the matching plugin
- remaining plugins are reindexed
- remove is blocked while busy
- remove failure reports an error and leaves state unchanged
- remove with an unknown/stale instance ID is a no-op or reports a controlled error, depending on
  the final controller policy

Audio tests:

- `IPluginHost` fake can remove a plugin
- `Engine::removePlugin()` rejects unknown instance IDs with a typed error

UI tests:

- rendered plugin rows expose remove controls
- remove button emits the matching instance ID
- remove controls disable when chain mutation is disabled

## Suggested Implementation Order

1. Add `removePlugin(instance_id)` to `IPluginHost`, `Engine`, and fakes.
2. Add `PluginHostErrorCode::PluginInstanceNotFound`.
3. Add explicit `remove_plugins_enabled` view state.
4. Add controller action, public controller callback, and controller tests.
5. Add signal-chain panel remove controls and UI tests.
6. Run focused core/audio/UI tests and focused clang-tidy.

## Settled First-Slice Decisions

- Silently ignore a stale instance ID if the controller no longer has that instance in
  `m_plugins`.
- Surface a typed error only when the backend rejects an instance that the controller still
  believes exists.
- Match Add Plugin's stop/rebuild path until measurement shows removal needs different playback
  behavior.
