# Editor Engine Undo — Phase B Findings

Status: evaluation findings for `editor-engine-undo-master-plan-v3.md` Phase B (done). Consolidates the
B0 scoping analysis (2026-06-10, source-only) and the B3 cleaned-base measurement (2026-06-11, run via
the SPIKE harness). Line references are into `rock-hero-common/audio/src/engine.cpp` unless noted.

## Phase B0 — Baseline Engine Cleanup Scoping (source-only, 2026-06-10)

Purpose: confirm whether B1 (eager structural plugins) is sound and worth doing now, enumerate the code
it affects, decide the B1 clear/reset behavior, and choose between B2-lite and B2-full.

## 1. Eager structural plugins (B1): feasible, and lazy init is incidental

**Current behavior.** The four hidden structural plugins — input gain, input meter, output gain,
output meter — are created **lazily** by `ensureStructuralLiveRigPlugins()` (`:2304-2404`) on first
use. The constructor does **not** create them: `createEdit()` builds the instrument track with
`add_default_plugins = false` and a comment that structural plugins are "managed explicitly"
(`:1438-1471`), and the constructor's only post-edit call is
`rebuildInstrumentMonitoringGraphBestEffort(...)` for routing (`:2768`), which does *not* ensure
structural plugins. So a fresh engine has an empty plugin list until the first
`setInputGain`/`setOutputGain`/`loadLiveRig`.

**Feasibility of eager creation — yes.** The creators `createLiveRigGainPlugin` (`:2206-2235`) and
`createLevelMeterPlugin` (`:2237-2266`) call `m_edit->getPluginCache().createNewPlugin(...)` with
**internal** plugin types (`LiveRigGainPlugin::createState()` and `LevelMeterPlugin::xmlTypeName`).
Internal plugins need no scanning, no plugin binary, and no open audio device — only the `Edit`, which
exists after `createEdit()`. The Phase 2 spike already instantiated even a real VST3 with no device, so
creating internal plugins at construction is safe. **Conclusion: lazy creation is incidental, not
load-bearing; eager creation in/after `createEdit()` is feasible.**

**What eager creation makes dead or trivial.** Once the anchors are created once at construction and
kept (see §2):

- `ensureStructuralLiveRigPlugins()` (`:2304-2404`) — callers: `setInputGain` (`:3412`),
  `setOutputGain` (`:3543`), `loadLiveRig` (`:3828`). With anchors always present, the create-if-missing
  body is dead; the only remaining concern is slot ordering, which is established once at construction.
  Reduce to an assert/no-op or delete.
- `structuralLiveRigPluginsNeedUpdate()` (`:2269-2301`) — callers: `setInputGain` (`:3407`),
  `setOutputGain` (`:3538`). Becomes permanently `false`; the `if (needUpdate) stopTransportAndRelease`
  guards (`:3407-3410`, `:3538-3541`) become dead. Delete.
- The reorder churn inside `ensureStructuralLiveRigPlugins` (`:2377-2401`) that contributes the spike's
  `+776` units runs **once at construction**, before any user command, so it no longer lands in a
  command's transaction.

**Behavior-preserving for the user-visible chain.** `pluginChainSnapshot()` already filters structural
plugins (`:1930`), so `userVisiblePluginCount()` is 0 for an empty chain whether or not the anchors
exist. Capture also skips them (`isStructuralLiveRigPlugin`). So the public chain surface is unchanged.
The intentional public behavior correction is input-gain lifetime: it is user/device calibration,
not project tone state.

## 2. Chain-clearing/reset paths B1 must update (and a decision point)

Three paths today wipe the whole list and reset all four structural ids, relying on the lazy path to
recreate them later:

| Path | Location | Action |
|---|---|---|
| `clearLiveRig` | `:3372-3378` | `pluginList.clear()`; reset 4 ids; reroute |
| `loadLiveRig` setup | `:3779-3783` | `pluginList.clear()`; reset 4 ids (then restores user plugins) |
| `abortLiveRigLoad` | `:4002-4008` | `pluginList.clear()`; reset 4 ids; reroute best-effort |

Under B1 the required approach is **clear only the user plugins and keep the four anchor instances**
(so their `EditItemID`s stay stable; clear-then-recreate is a discouraged fallback that forfeits id
stability). But because destruction no longer happens, state that destruction used to clear must be
handled deliberately. Both gains today fall back to default after a wipe, because
`inputGain()`/`outputGain()` read `readGainFromPlugin(id)` which returns `Gain{}` when the id is null
(`:3387-3396`, `:2407-2415`). Meter plugins also retain `LevelMeasurer` instances, and RockHero keeps
`MeterReader` clients that attach to them. Keeping anchors means clear/load/abort must explicitly
detach readers and clear the retained input/output meter measurers instead of relying on plugin
destruction.

So today every wipe path resets **both** input gain and output gain to default. That exposes a
**decision point B1 must resolve deliberately, not silently:**

- **Output gain** is project tone state. Resetting it to default on clear/abort is correct and must be
  reproduced explicitly (`live_rig.setOutputGain(default)` equivalent on the kept anchor). On
  `loadLiveRig`, output gain is restored from the tone document, so the load path should reset-then-let
  the restore set it.
- **Input gain** is per-user, per-audio-device calibration config, explicitly **not** project tone
  state (see `editor-undo-plan.md` scope / `architecture.md`). Today it is reset to default by every
  clear and by every load (load does not restore it, since it is not in the tone document). **Decision
  (user-confirmed 2026-06-10): input gain is preserved across project clear/load/abort.** It should
  only return to default when the selected audio input device changes to a previously unknown device.
  When a known device is opened, RockHero should load that device's stored input calibration value.
  Keeping the structural anchor naturally supports the project-operation side of this policy, so B1
  must **not** reset input gain on clear/load/abort.

B1 is incomplete until all three paths keep the anchors **and** apply the resolved reset semantics:
**reset output gain to default** (project state) on clear/abort, **preserve input gain** (user config),
clear retained input/output meter state, and let `loadLiveRig` restore output gain from the tone
document.

## 3. Routing call sites and dependencies (B2 scope)

`applyInstrumentMonitoringRoute()` (`:2561-2645`), wrapped by `rebuildInstrumentMonitoringGraph()`
(`:2657-2662`) and `...BestEffort()` (`:2666-2674`). Routing depends on:

- the monitoring-mode flags `m_live_input_monitoring_enabled` / `m_calibration_input_monitoring_enabled`;
- the **target track** (backing in calibration mode, instrument otherwise — `:2574-2575`);
- the current audio device (name + channel layout);
- the resolved wave input instance.

It assigns the input with `setTarget(..., nullptr, ...)` — a `nullptr` undo manager, so routing is
**not** recorded in undo (confirmed `tracktion_InputDevice.h:128`, `:2629-2630`).

Result-returning reroute call sites (`rebuildInstrumentMonitoringGraph()`): construction-time best
effort (`:2768`); arrangement/device paths (`:2963`, `:2992`, `:4047`); `insertPlugin` (`:3155`);
`movePlugin` (`:3258`); `removePlugin` (`:3302`); `clearLiveRig` (`:3378`); `setInputGain` (`:3426`,
only when structural creation happened); `setLiveInputMonitoringEnabled` (`:3468`);
`setCalibrationInputMonitoringEnabled` (`:3512`); `setOutputGain` (`:3557`, only when structural
creation happened); `captureActiveRig` (`:3709`); and live-rig load completion (`:3843`). Plus ~10
`...BestEffort` calls inside rollback paths.

Note: `setLiveInputMonitoringEnabled` (`:3448`) and `setCalibrationInputMonitoringEnabled` (`:3491`) do
**not** call `ensureStructuralLiveRigPlugins` — they only flip flags and reroute, implicitly assuming
the anchors already exist. After B1 that assumption is always true (a latent fragility B1 removes).

## 4. B2-lite vs B2-full — recommendation: B2-lite

Given the mechanism lean toward RockHero mementos (where B2-full's "pure tree edit / clean single
transaction" property buys nothing), **recommend B2-lite**:

- Route `insert`/`move`/`remove` through one synchronous "stop/release → mutate → reroute →
  rollback-on-route-failure" helper, collapsing those three sites and their rollback branches.
- Keep the synchronous `LiveInputError` contract and existing tests — no route-status API, no async
  timing change.
- **Exclude output gain** (and input gain): after B1 they are structural property writes with no
  context teardown. `setOutputGain` only rerouted in the `if (*ensured)` branch (`:3557`), which never
  fires post-B1; `setInputGain` likewise (`:3426`). Leave both as plain property writes.

Defer **B2-full** unless Phase M chooses delegation; only then is the new plugin-list/tree listener +
route-status surface + async sequencing justified.

## 5. Decisions and recommendations

- **B1: do it now.** Eager structural creation at construction is feasible and behavior-preserving for
  the user-visible chain; it stabilizes the structural ids, deletes the lazy
  `ensure`/`needUpdate` machinery, and removes the `+776` churn from command transactions. **Resolved
  (user-confirmed):** input gain is device-scoped user calibration and is preserved across project
  clear/load/abort; it resets to default only for previously unknown audio input devices, while known
  devices reload their stored calibration. Output gain resets to default on clear/abort and comes from
  the tone document on load.
- **B2: B2-lite** (synchronous centralization of insert/move/remove), output/input gain excluded.
  B2-full deferred to a delegation outcome at Phase M.
- After B1+B2-lite, run **B3** to re-measure transaction cleanliness on the cleaned base.

## 6. Verification notes for B1/B2-lite implementation

- Existing `rock_hero_common_audio_tests` must still pass unchanged for the user-visible chain,
  insert/move/remove, monitoring enable/disable, device rebinding, and route-failure reporting.
- Add/adjust tests for the section 2 reset semantics: after `clearLiveRig`, `outputGain()` is default
  and `inputGain()` is **unchanged** (preserved); after `loadLiveRig`, output gain comes from the tone
  document and input gain is preserved. Lock the decision with these tests.
- Add or retain device-calibration tests where the audio-device settings code owns them: known devices
  restore their stored input gain when opened, while previously unknown devices start at default.
- Verify clear/load/abort detach RockHero meter readers and clear retained input/output meter
  measurers so the next `audioMeterSnapshot()` starts from silence.
- Assert the structural anchors are present immediately after construction (new invariant) and that
  `userVisiblePluginCount()` is still 0 then.

## Phase B3 — Cleaned-Base Tracktion Undo Measurement (2026-06-11)

Ran after B1 eager structural anchors and B2-lite routing centralization.

### Test vehicle

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

### Measurements

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

### Findings

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

### Phase M input (now resolved — mementos chosen)

For structural user-plugin list operations, delegation was measured to be more viable than before
Phase B: the accidental lazy-anchor churn is gone and insert/move/remove can be wrapped as clean
transactions at the tree/id level. B2-lite still leaves runtime route repair as an explicit
responsibility outside the Tracktion undo tree. These measurements fed the Phase M decision, which
chose RockHero mementos (Tracktion as backend) — see `editor-engine-undo-master-plan-v3.md` Phase M
and `undo-ownership-analysis.md`.
