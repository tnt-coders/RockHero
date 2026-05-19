# Tracktion Built-In Controls Plan

## Status

This is a near-term follow-up to the stable package identity work and should stay at the top of
`docs/in-progress/` until Tracktion built-in tone controls are intentionally supported.

## Why This Exists

The current short-term engine behavior deliberately creates Rock Hero's instrument track without
Tracktion's default `VolumeAndPanPlugin` and `LevelMeterPlugin`, and live rig capture now fails on
non-`ExternalPlugin` entries instead of silently dropping them.

That explicitly addresses the review concern in
[`stable-package-identity-review-followups.md`](stable-package-identity-review-followups.md):
unsupported plugins must not disappear from saved tones without a visible failure.

This is only a temporary safety position. Volume, pan, and metering are important tone-design
controls and should become first-class Rock Hero controls soon rather than accidental Tracktion
defaults hidden inside the backend edit.

## Goal

Add intentional support for the Tracktion built-ins Rock Hero wants to expose first:

- `VolumeAndPanPlugin` for output level and pan.
- `LevelMeterPlugin` for runtime signal metering.

These controls should appear in the editor's signal-chain area as Rock Hero-owned control surfaces,
not as unknown generic plugins.

## Design Direction

- Keep Tracktion types private to `rock-hero-common/audio`.
- Expose project-owned control state through the audio boundary.
- Treat volume and pan as persistent tone state.
- Treat level meter readout as runtime state, not package-authored content.
- Keep capture failure for unknown non-`ExternalPlugin` entries until each built-in is explicitly
  modeled.
- Avoid re-enabling Tracktion default plugin insertion as an implicit behavior. Add the built-ins
  through Rock Hero-owned operations instead.

## Initial Scope

1. Add an audio-facing model for built-in chain controls.
2. Add an operation to ensure the instrument chain has a Rock Hero-owned output controls section.
3. Insert `VolumeAndPanPlugin` explicitly when that section is created.
4. Surface volume and pan values in the editor signal-chain panel.
5. Add UI controls for volume and pan.
6. Persist volume and pan with the tone document.
7. Restore volume and pan when loading a tone.
8. Add level meter readout after the volume/pan path is stable.

## Rack Direction

Tracktion racks can represent parallel tone branches. A future rack-backed tone graph can place a
`VolumeAndPanPlugin` at the end of each branch so two tones can have independent pan and level
settings before being summed to the rack output.

That should be implemented through a project-owned tone/rack model, not by exposing arbitrary
Tracktion rack internals directly to editor UI code.

## Testing Strategy

- Unit-test project-owned volume/pan state and persistence decisions without Tracktion.
- Adapter-test explicit insertion of the supported Tracktion built-ins.
- Adapter-test capture and restore of volume/pan tone state.
- Add a regression test that unsupported non-external plugins still fail capture loudly.
- Add UI wiring tests for emitted volume/pan edit intents once the controls exist.

## Non-Goals

- No arbitrary Tracktion built-in plugin browser in the first pass.
- No generic Tracktion rack editor in the first pass.
- No persistence of level meter readings.
- No silent fallback for unsupported non-external plugins.

## Open Questions

- Should volume/pan live as a fixed output strip outside the normal plugin list, or as a visible
  built-in card inside the list?
- Should level metering be per-chain, per-rack-branch, or output-only in the first UI?
- Should volume/pan automation use the same envelope system as external plugin parameters from
  day one, or land first as static tone state?
