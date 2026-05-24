# Tracktion Built-In Controls Plan

## Status

Basic implementation complete.

## Why This Exists

The current short-term engine behavior deliberately creates Rock Hero's instrument track without
Tracktion's default `VolumeAndPanPlugin` and `LevelMeterPlugin`, and live rig capture now fails on
non-`ExternalPlugin` entries instead of silently dropping them.

That explicitly addresses the review concern in
[`stable-package-identity-review-followups.md`](stable-package-identity-review-followups.md):
unsupported plugins must not disappear from saved tones without a visible failure.

This plan intentionally deviates from the original volume, pan, and metering direction. Rock Hero
does not need a project mixer for this step because backing audio is already normalized during
import and playback. The first useful controls are just the live guitar path's input gain before
the signal chain and output gain after the signal chain.

## Goal

Add two simple Rock Hero-owned gain controls around the editor signal-chain panel:

- An input gain slider on the left side of the Signal Chain panel.
- An output gain slider on the right side of the Signal Chain panel.

Then add a final mix output meter in the global transport/status area, preferably right-aligned.
The meter shows whether the summed output is clipping. It is for visibility only; it is not a
master fader and it does not introduce mixer behavior.

## Design Direction

- Keep Tracktion types private to `rock-hero-common/audio`.
- Expose project-owned gain and meter state through narrow audio boundaries.
- Treat input and output gain as persistent tone state with `0.0 dB` defaults.
- Treat final mix meter readout as runtime state, not package-authored content.
- Keep the final mix meter outside the Signal Chain panel so it clearly includes backing audio
  plus the processed guitar path.
- Present the final mix meter as global playback/status UI, not as state owned by
  `TransportControls`.
- Keep gain controls outside the removable external-plugin list.
- Keep capture failure for unknown non-`ExternalPlugin` entries until each supported built-in is
  explicitly modeled.
- Avoid re-enabling Tracktion default plugin insertion as an implicit behavior. Add any Tracktion
  built-ins through Rock Hero-owned adapter operations instead.
- If the adapter uses Tracktion's `VolumeAndPanPlugin` internally, expose only its volume behavior;
  keep pan centered, uneditable, and unpersisted.

## Step 1: Input and Output Gain Sliders

Add the smallest testable gain surface around the current linear plugin chain.

1. Add project-owned gain state with `input_gain_db` and `output_gain_db`.
2. Use one shared validation policy for both gains, including defaulting and clamping.
3. Extend the live-rig audio boundary with operations to read and set the two gain values.
4. Persist the two gain values in the tone document as an additive section, defaulting missing
   fields to `0.0 dB` when loading older tones.
5. Restore the two gain values before or alongside plugin restoration so the live rig sounds the
   same after project reload.
6. In the Tracktion adapter, create explicit structural gain points:
   - input gain before the first external plugin;
   - output gain after the last external plugin and before the guitar path reaches the main mix.
7. Keep those structural gain points hidden from `LiveRigPlugin` and from the removable plugin rows.
8. Add `SignalChainViewState` fields for the two gain values and their enabledness.
9. Add left and right gain sliders to `SignalChainPanel`.
10. Add controller intents for input-gain and output-gain changes, then mark the tone dirty when
    either value changes.

## Step 2: Final Mix Output Meter

After Step 1 is stable, add clipping visibility for the final summed output.

1. Add runtime-only project-owned meter state, such as left/right peak dBFS values and a clipping
   flag for the latest readback window.
2. Read the meter after the normalized backing audio and processed guitar path are summed.
3. Keep the meter separate from tone persistence and from the input/output gain controls.
4. Surface a compact output meter in the global transport/status area, preferably right-aligned.
5. Show clipping clearly when the final mix reaches or exceeds 0 dBFS.
6. Do not add a resettable history, per-track meters, or a master fader in this step.

The meter state should come from the audio boundary and flow through editor view state into the
global status UI. It may share the same visual bar as play/stop, position, and audio-device status,
but it should not become part of the transport contract or the guitar signal-chain state.

The Tracktion adapter can use `LevelMeterPlugin` if it provides the smallest safe final-mix
readback. If that is awkward to read without leaking Tracktion details or coupling UI to runtime
objects, add a tiny adapter-owned metering point instead.

## Testing Strategy

- Unit-test gain defaulting, clamping, equality, and tone-document round trips without Tracktion.
- Controller-test that input/output gain intents call the audio boundary and mark the tone dirty.
- Adapter-test explicit insertion, ordering, capture, and restore of the two structural gain
  points.
- Add a regression test that unsupported non-external plugins still fail capture loudly.
- UI-test that the signal-chain panel renders enabled/disabled gain controls and emits gain-change
  intents.
- UI-test that the final mix meter is rendered in the global transport/status area rather than in
  the signal-chain panel.
- Unit-test meter state formatting and clipping threshold behavior without Tracktion.
- Adapter-test final-mix meter readback with a deterministic or fakeable audio source where
  practical; keep any hardware-dependent confidence checks as manual Windows ASIO acceptance steps.

## Rack Direction

Tracktion racks can represent parallel tone branches later. A future rack-backed tone graph can add
branch-level gain points for tone blending, but that should remain separate from this minimal
input/output gain work.

That future work should be implemented through a project-owned tone/rack model, not by exposing
arbitrary Tracktion rack internals directly to editor UI code.

## Non-Goals

- No pan control, pan persistence, or pan automation.
- No project mixer.
- No backing-track volume UI.
- No master output fader.
- No treating final mix metering as transport state.
- No arbitrary Tracktion built-in plugin browser in the first pass.
- No generic Tracktion rack editor in the first pass.
- No persistence of meter readings.
- No silent fallback for unsupported non-external plugins.
