# Signal Chain Plugin Editing Plan

Status: completed. The editor can insert, move, remove, open, save, restore, and render the
linear signal-chain plugin list through project-owned snapshots.

## Scope

This plan implemented the first real signal-chain editing surface for authored guitar tones:

- insert a scanned plugin before, between, or after existing plugins;
- move loaded plugins to another position in the current chain;
- keep remove and open-plugin behavior addressed by stable instance IDs;
- keep the backend chain, controller state, and rendered signal-chain view in the same order;
- enforce the current product cap of eight user plugins everywhere a chain enters or changes.

Tone slots, racks, parallel chains, crossfades, automation lanes, and plugin parameter automation
remain out of scope.

## Implemented Shape

The audio boundary now exposes the authoritative loaded-chain value used by editor workflow code:
`common::audio::PluginChainEntry` and `common::audio::PluginChainSnapshot`. `IPluginHost` inserts a
candidate at a requested user-visible chain index, moves an existing plugin instance to a final
chain index, removes by instance ID, and returns the full post-mutation snapshot for successful
structural changes.

The plugin cap is a product rule in `common::audio::max_signal_chain_plugins`. Plugin insertion,
live-rig load, live-rig capture, editor workflow capacity checks, action availability, and view
presentation all use that shared value so increasing the cap later remains localized.

`editor/core` owns signal-chain editing policy in `SignalChainWorkflow`. The workflow stores the
latest authoritative snapshot projected as `PluginViewState` rows, validates stale instance IDs and
insertion slots, remembers the pending browser insertion target, and reports whether the chain has
capacity. It does not call `IPluginHost`, `ILiveRig`, settings, or UI components. `EditorController`
remains the root facade that executes audio-port mutations, applies returned snapshots to the
workflow, marks tone changes dirty, and refreshes the editor view state.

`editor/ui` keeps the panel/view split used elsewhere in the editor. `SignalChainPanel` is the
bottom-panel wrapper, while `SignalChainView` renders `SignalChainViewState` and emits
signal-chain intents. The view presents a horizontal fixed-block chain, gap insert affordances,
plugin tiles, remove/open controls, meters, output gain, and drag/drop move previews. Drop handling
stays presentation-local; the controller receives only the final move intent.

## Design Notes

The branch intentionally kept Tracktion and JUCE plugin descriptions behind `common/audio`.
Editor-core code consumes project-owned snapshots and remains headless and directly testable.

The UI initially planned explicit move buttons, but the implemented fixed-block drag/drop surface
proved usable once the audio and controller contracts were authoritative. Drag/drop does not own
chain mutation policy: it calculates a final destination index and lets the controller/audio
boundary perform the real move.

The first signal-chain editor is still linear. Future racks, containers, or parallel blended chains
should evolve the state model and audio addressing behind the same boundaries rather than placing
plugin controls into arrangement track rows or exposing backend plugin-list objects to editor UI.

## Verification

The completed implementation has focused coverage in:

- `rock_hero_common_audio_tests` for insertion, movement, removal, load/capture, snapshots, and
  cap enforcement;
- `rock_hero_editor_core_tests` for workflow state, stale-ID handling, action availability,
  pending insertion targets, and controller mutation paths;
- `rock_hero_editor_ui_tests` for signal-chain view rendering, insert/drop wiring, disabled states,
  focus behavior, and editor-view containment.

## Remaining Follow-Ups

No deferred `SignalChainWorkflow` extraction remains. Reasonable future work is feature-driven:
tone-slot/rack addressing, automation, plugin-window persistence, and broader tone graph editing.
Those should be planned when they become active product work rather than by continuously updating
this completed implementation record.
