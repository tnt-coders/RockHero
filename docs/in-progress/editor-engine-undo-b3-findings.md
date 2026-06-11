# Phase B3 Findings - Cleaned-Base Tracktion Undo Measurement

Status: evaluation findings for `editor-engine-undo-master-plan-v3.md` Phase B3. The
measurement ran on 2026-06-11 after B1 eager structural anchors and B2-lite routing
centralization.

## Test vehicle

The B3 probe extends the temporary SPIKE harness in
`rock-hero-common/audio/tests/test_undo_spike.cpp`.

Run used:

```powershell
$env:ROCKHERO_SPIKE_PLUGIN='C:\Program Files\Common Files\VST3\Archetype Nolly X.vst3'
& 'build/debug/rock-hero-common/audio/tests/rock_hero_common_audio_tests.exe' `
  'Spike: cleaned base Tracktion undo transaction cleanliness' --success
```

The full `rock_hero_common_audio_tests` binary also passed with the same reference plugin:
113 test cases, 577 assertions.

## Measurements

Each operation was isolated by clearing Tracktion undo history, opening one labeled transaction,
running the RockHero adapter operation, then calling raw `Edit::undo()` / `Edit::redo()`.

| Operation | Stored-unit delta | Raw undo result | Raw redo result |
|---|---:|---|---|
| insert | +208 | Removed the user plugin; anchors remained | Restored the same plugin id |
| move | +64 | Restored the original user-plugin order | Restored the moved order |
| remove | +32 | Restored the removed plugin with the same id | Removed it again |
| output gain | +72 | Restored the previous gain value | Restored the changed gain value |

Manual `spikeRebuildInstrumentMonitoringGraph()` after raw undo/redo of insert, move, and
remove returned success and did not add undo units. This was a headless no-device/default-route
run, so it proves the repair call is not recorded in the undo manager and is not failing in the
default harness. It does not prove audible live-route or playback-graph correctness while an
audio device is open and monitoring is enabled.

## Findings

The B1/B2-lite cleanup removed the old lazy-structural-plugin churn from output gain. The old
`+776` style output-gain observation is now `+72` after the anchors already exist.

The first B3 output-gain run exposed a separate RockHero adapter bug: Tracktion undo restored the
plain `gainDb` ValueTree property, but `LiveRigGainPlugin::gain()` reads a realtime atomic target
that was not refreshed by direct ValueTree undo. `LiveRigGainPlugin::valueTreePropertyChanged()`
now synchronizes the cached property into that realtime target, and the rerun verifies raw
`Edit::undo()` restores 0 dB while raw `Edit::redo()` restores the changed value.

Tracktion delegation is clean at the tree/id level for insert, move, and remove on the cleaned
base. The move case specifically proves that Tracktion undo/redo restores the ValueTree child
order and preserves plugin ids for the raw chain snapshot. Because Tracktion's edit watcher still
does not rebuild playback for pure child reorder by itself, a delegated move would still need an
explicit post-undo route/playback repair strategy under B2-lite.

Output gain is a clean value transaction on the current cleaned base after the
`LiveRigGainPlugin` runtime-state synchronization fix. The fixed output-gain plugin remains a
structural plugin in the chain; the user-visible edit is only the gain value write.

## Phase M input

For structural user-plugin list operations, delegation is more viable than it was before Phase B:
the accidental lazy-anchor churn is gone and insert/move/remove can be wrapped as clean
transactions at the tree/id level.

For the first undo scope as a whole, B2-lite still leaves runtime route repair as an explicit
responsibility outside the Tracktion undo tree for insert, move, and remove. Output gain no longer
adds a separate delegation blocker after the adapter fix, so Phase M should weigh the remaining
post-undo routing cost, Tracktion transaction-discipline cost, parameter behavior, and editor
metadata ownership.
