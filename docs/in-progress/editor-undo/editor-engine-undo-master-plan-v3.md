# Editor Engine And Undo Master Plan v3

Status: in-progress master plan and the **entry point** for the editor undo initiative. Supersedes the
earlier v2 draft (removed). This file and its companions all live in `docs/in-progress/editor-undo/`
and form one plan: this file defines the order and decision; the others hold the detail.

## Document map (all under `docs/in-progress/editor-undo/`)

| Doc | Role | State |
|---|---|---|
| `editor-engine-undo-master-plan-v3.md` (this) | Coordinator: phases, decision record, implementation sequence | active |
| `editor-undo-plan.md` | Detailed undo implementation (Stages 0–10): contracts, fakes, tests | active — this is Phase 3+ |
| `undo-ownership-analysis.md` | Source-grounded Tracktion mechanics + the mementos-vs-delegation rationale | settled reference |
| `editor-undo-phase-m-decision-review.md` | Phase M challenge review + final decision text + the 8 review answers | settled reference |
| `editor-engine-undo-phase-b-findings.md` | Phase B evidence (B0 scoping + B3 measurement) | done reference |

Related plans outside this folder, on which the undo work does **not** depend:
`../remaining-god-object-decomposition-plan.md` (the `Engine::Impl` seam split — sequenced *after*
undo) and `../../todo/test-fixture-opportunities-plan.md` (deferred).

## Implementation sequence (the direct path)

Done: Phase 0, Phase 1, Phase B (B0–B3), B2-lite+ monitoring-mode centralization (2026-06-12),
Phase 2 (mechanical), Phase M decision. Remaining, in order — parallel tracks marked, per-stage detail
in `editor-undo-plan.md`:

1. **Now, in parallel:**
   - **Spike** (finish Phase 2 Step 2 / `editor-undo-plan.md` Stage 0): real-VST3 gesture callbacks,
     open-editor refresh on `setPluginState`, automation subtree-swap audibility, live-device route
     repair. Reuse `test_undo_spike.cpp`. Gates the audio-adapter stages only.
   - **Stage 1 — `EditorUndoHistory`**: pure, headless, fakeable; no Tracktion dependency.
2. **Headless, no spike dependency (build while the spike runs):** Stage 2 contracts + fakes +
   rollback-proof tests · Stage 3 action ids/intents/availability/menu/shortcuts (kept disabled) +
   reveal-on-undo intents · Stage 4 placement + display-type · Stage 5 move.
3. **After the spike confirms fidelity — audio-adapter stages:** Stage 2 adapter half
   (`setPluginState`, observer, capture/restore) · Stage 6 parameters · Stage 7 output gain ·
   Stage 8 remove memento + rollback-contract/faulted-session handling.
4. **Finalize:** Stage 9 dirty-state migration · Stage 10 enable the Edit menu/shortcuts (the ship gate).

Only after undo: `../remaining-god-object-decomposition-plan.md` (separate initiative; undo first so it
builds on the stable engine and adds adapter test coverage that de-risks the decomposition).

## Why v3 exists

The undo-ownership investigation (`undo-ownership-analysis.md`) showed that the strongest arguments
*against* leaning on Tracktion's undo manager were **artifacts of current engine implementation
choices, not fundamentals**:

- Lazy structural-plugin creation makes a RockHero command's Tracktion transaction bundle incidental
  infrastructure churn (the spike's `+776` was lazy creation of the four hidden structural plugins).
- Imperative monitoring-route rebuilds (`applyInstrumentMonitoringRoute()` called at ~15 mutation
  sites) mean `Edit::undo()` reverts the tree but never replays our routing.
- The claim that VST3 parameter undo is impossible in Tracktion was wrong: Tracktion can undo a VST3
  parameter if we flush the plugin chunk at gesture-settle (`undo-ownership-analysis.md`, Correction).

Designing undo on top of that shape would bake the accidents in. v3 therefore front-loads **baseline
engine cleanup that is valuable regardless of the undo design**, adds **explicit evaluation gates**,
and makes the **undo mechanism an evidence-based decision** taken on the cleaned base — instead of
pre-committing to RockHero mementos (as v2 + `editor-undo-plan.md` implicitly did).

## What is settled vs. open

Settled before Phase M (did not depend on the mechanism choice):

- RockHero owns the user-visible undo *stack*: ordering, command labels, clean-revision dirty
  tracking, typed failure / faulted-session policy, `Ctrl+Z`/`Ctrl+Y`, and any future cross-domain
  (chart) ordering. Tracktion's undo manager is never exposed *as* RockHero history.
- Tracktion does the heavy primitive lifting regardless: plugin instantiation, plugin state
  capture/restore, plugin-list mutation, audio-graph rebuild.
- **Fidelity of plugin-state reproduction is a non-negotiable hard requirement.** Undo/redo of a
  plugin edit must restore the plugin's exact prior state. Plugin-parameter undo therefore uses
  **coherent full-state restore** (the plugin's own chunk via `get/setStateInformation`), *not*
  granular per-parameter value replay. Granular capture/replay of normalized parameter values is
  **rejected** because it cannot guarantee exact reproduction: it misses non-parameter internal
  state (loaded IRs, preset/file references, oversampling, hidden DSP state), can diverge for
  dependent/linked/stepped parameters whose internal state is not a pure function of the exposed
  parameter vector, and risks controller/processor desync. See `undo-ownership-analysis.md`
  ("Verified: VST3 parameter undo is whole-chunk" and "Fidelity is non-negotiable").
- **Representation granularity is orthogonal to stack ownership.** "Memento" does not mean
  "granular": a RockHero memento can hold the full plugin chunk and restore it coherently, which is
  both VST3-idiomatic and RockHero-owned. This withdraws the earlier (granular) parameter-efficiency
  argument for mementos — a chunk memento and a Tracktion gesture-settle chunk flush cost the same
  ~10 KB per settle — without weakening the containment argument.
- **Tone plugin state/automation is persisted via Tracktion-managed files**, so RockHero does not
  hand-serialize every plugin setting into its own project format. This is a *persistence* decision
  and is independent of the undo *restore mechanism*: mementos are transient in-memory undo state
  and add no persistence cost under either mechanism.
- **Runtime performance does not decide the mechanism.** Neither mechanism touches the audio thread;
  real-time rendering and gameplay timing are identical. The only differences are bounded
  message-thread memory/CPU at edit/undo time, imperceptible on target hardware.

Decided at the Phase M gate (2026-06-11) — see Phase M for the full rationale:

- **Inverse mechanism: RockHero mementos (Tracktion is a backend).** Tone-edit undo entries are
  RockHero-owned and restore through audio-adapter primitives (full-chunk plugin state capture/
  restore; automation captured/restored as `AUTOMATIONCURVE` subtrees through Tracktion's own curve
  API). Tracktion's undo manager is **not** consumed as product history; raw `Edit::undo()/redo()`
  is never the product inverse. Adapter-local rollback may use a current-transaction-only primitive
  (`UndoManager::undoCurrentTransactionOnly()`), never raw `Edit::undo()`.
- **Editor metadata stays in editor-core.** Block placement / display-type are editor presentation
  with no audio semantics and are not moved onto the Tracktion tree.
- **Stay on B2-lite.** B2-full is not required for memento undo. Reactive routing may still be done
  later as a standalone routing-decoupling refactor justified on its own merits, never as undo prep.

## Ordering principles

- Baseline cleanups (Phase B) must preserve project/tone behavior unless the user explicitly accepts
  a boundary correction, and they must be justified **independently of undo**. If a cleanup is not a
  net improvement on its own merits, it does not belong in Phase B.
- Decide the undo mechanism on the cleaned base with measured evidence, not on current-code accidents.
- Each evaluation gate produces a short **written finding** before the next implementation step.
- Keep every step independently buildable and reviewable. Tests are gates, not cleanup.
- Build/verification ownership is agent-dependent: steps state the passing condition, not who runs the
  build (Claude does not invoke cmake/ctest in this repo; the user or the Codex build workflow runs it).
- Update `docs/design/` only after implementation proves durable and the user confirms a durable rule.

## Phase map

```text
Phase 0   Planning / logging reconciliation                  [done]
Phase 1   Focused baseline tests                              [done]
Phase B   BASELINE ENGINE CLEANUP (pre-undo, value on its own merits)
   B0  Evaluation: scope eager structural init + routing centralization [done]
   B1  Eager structural plugins at construction               [done]
   B2  Routing centralization: B2-lite implemented            [done]
   B3  Re-measure transaction cleanliness on the cleaned base  [done]
Phase 2   Tracktion behavior spike                            [mechanical done; interactive parameter spike pending]
Phase M   UNDO MECHANISM DECISION GATE                        [DECIDED 2026-06-11: RockHero mementos / Tracktion-as-backend]
Phase 3+  Undo implementation (RockHero mementos)             [follow revised editor-undo-plan.md]
```

## Temporary Spike Code Ledger

Temporary spike code must be obvious to find and remove. The cleanup rule is:

- Every temporary probe, test, and build entry uses uppercase `SPIKE` in a nearby file header,
  block comment, CMake comment, or declaration comment.
- Future spike-only code should land in its own commit and be added to this ledger.
- Production undo implementation must not depend on any `Engine::spike...` method.
- When Phase 2 / Phase M spike work closes and the findings have been recorded, remove all source
  hits from `rg -n "SPIKE|test_undo_spike|spike[A-Z]" rock-hero-common/audio`.

Current temporary spike-code commits:

- `253fcd1a Add temporary undo spike probes`
  - Added temporary `Engine` probes in `engine.h` / `engine.cpp`.
  - Added `rock-hero-common/audio/tests/test_undo_spike.cpp`.
  - Added the `test_undo_spike.cpp` CMake entry.
- `3cd499a6 Add spike anchor order check`
  - Added `spikeRawLiveRigPluginRoles()`.
  - Added the real-VST3 eager-anchor ordering spike case.
- B3 cleaned-base spike extension (current branch change)
  - Added temporary `spikeRebuildInstrumentMonitoringGraph()`.
  - Added the cleaned-base transaction cleanliness spike case for insert, move, remove, and output
    gain.

Do not revert permanent baseline commits as part of spike cleanup:

- `4706a27c Create live rig structural anchors eagerly`
- `717b18cb Centralize plugin mutation rerouting`

Expected source cleanup at spike close:

- Delete `rock-hero-common/audio/tests/test_undo_spike.cpp`.
- Remove its CMake entry from `rock-hero-common/audio/tests/CMakeLists.txt`.
- Remove the temporary `Engine` SPIKE declaration block from
  `rock-hero-common/audio/include/rock_hero/common/audio/engine.h`.
- Remove the matching temporary `Engine` SPIKE implementation block from
  `rock-hero-common/audio/src/engine.cpp`.
- Do not trust this ledger as exhaustive. After the targeted removals, scan the source with
  `rg -n "SPIKE|test_undo_spike|spike[A-Z]" rock-hero-common/audio`; source hits should be gone
  unless a newer explicitly-ledgered spike still exists.

The remaining god-object work (`../remaining-god-object-decomposition-plan.md`, the `Engine::Impl`
seam split) is a separate initiative, **sequenced after undo** (see Implementation sequence): undo
builds against the stable engine and adds `IPluginHost`/adapter test coverage that then de-risks the
decomposition. It is not a prerequisite for undo.

## Phase B - Baseline engine cleanup

Purpose: bring the live-rig adapter to its cleanest shape **before** undo, because (1) it is a net
improvement regardless, and (2) it determines whether Tracktion-delegated undo is viable. None of B
adds undo behavior.

### B0 - Evaluation (no code): scope the cleanup and record findings

Result: completed in `editor-engine-undo-phase-b-findings.md`. B1 is confirmed, B2-lite is the selected
baseline route cleanup, B2-lite is now implemented, and the expected Phase M mechanism lean remains
local mementos.

Produce a short findings note (append to `undo-ownership-analysis.md` or a sibling) answering:

- **Eager structural plugins:** confirm lazy creation is incidental, not load-bearing. `createEdit()`
  already builds the instrument track with `add_default_plugins = false` and a comment that structural
  plugins are "managed explicitly" (`engine.cpp:1438-1471`). Verify the four structural plugins
  (input gain, input meter, output gain, output meter) can be created at construction without an open
  audio device/context (the spike already instantiates VST3s with no device, so this is expected to
  hold). Identify every site that calls `ensureStructuralLiveRigPlugins()` /
  `structuralLiveRigPluginsNeedUpdate()` (`engine.cpp:3412, 3543, 3828`) and what becomes dead once
  creation is eager. **Also enumerate the chain-clearing paths that reset the structural ids**
  (`clearLiveRig` `:3372-3377`, `loadLiveRig` setup `:3779-3783`, load abort `~:4002`) **and the
  observable resets they currently produce by destruction** (notably `outputGain()` → default). B1
  keeps the anchors (clear only user plugins, stable ids), preserves input gain as device-scoped user
  calibration, and must reproduce project-owned resets explicitly.
- **Routing centralization (B2-lite vs B2-full):** enumerate the ~15
  `rebuildInstrumentMonitoringGraph()` call sites (`engine.cpp:3155, 3258, 3302, 3378, 3468, 3512,
  3557, 3709, 3843, 4047, 2768`, plus the best-effort rollback calls) and the inputs routing depends on —
  instrument-vs-backing target by monitoring mode (`engine.cpp:2574-2575`), current audio device, and
  the monitoring-enabled flags. Note `setOutputGain` (`:3557`) drops out of this set after B1 (it is a
  property write, not a chain-structure mutation; see B2-lite). Decide between:
  - **B2-lite (synchronous centralization):** route every chain mutation through one
    "mutate-then-reroute" helper, collapsing the duplicated route calls and rollback branches into one
    place while keeping the existing **synchronous** `LiveInputError` contract. Low risk; no timing or
    API change.
  - **B2-full (reactive routing):** make chain mutations pure tree edits and re-apply routing from a
    single reactive unit (listeners on plugin-list / device / mode change). This requires a new
    **route-status surface** because routing failure can no longer be returned synchronously from a
    chain mutation, and it shifts routing timing from synchronous to async (sequenced after Tracktion's
    `restartPlayback`). Higher risk; it would only have been justified under a Phase M delegation
    decision, which would have needed mutations to be clean single transactions.
- **Decision recorded:** B1 and B2-lite were chosen and completed; B2-full remains the rejected
  delegation-dependent branch.

Exit: written findings; B1 and the B2-lite/B2-full choice confirmed. **Done - see
`editor-engine-undo-phase-b-findings.md`:** B1 is feasible and behavior-preserving for the user-visible
chain (lazy structural init is incidental); B2-lite is the selected baseline route cleanup. B1 keeps
structural anchors stable across clear/load/abort and applies the resolved reset semantics: reset
output gain to default as project tone state, preserve input gain as device-scoped user calibration,
and explicitly clear retained meter state.

### B1 - Eager structural plugins at construction

Create the four structural plugins in `createEdit()` (or a dedicated init step) so the live-rig chain
always holds the invariant `[input gain, input meter, <user plugins>, output gain, output meter]`.
Remove the lazy `ensureStructuralLiveRigPlugins()` / `structuralLiveRigPluginsNeedUpdate()` machinery
and the defensive calls at the mutation sites.

**The invariant is not free — the chain-clearing paths must be updated to maintain it.** Today three
paths wipe the whole plugin list and reset all four structural ids to `{}`:
`clearLiveRig` (`engine.cpp:3372-3377`), the `loadLiveRig` setup (`engine.cpp:3779-3783`), and the
load-abort path (`~engine.cpp:4002`). After B1 the required approach is to **clear only the user
plugins and keep the four structural plugin instances in place**, so their ids stay stable.
(Clear-then-recreate is a fallback only: it produces *new* ids, forfeits the stability invariant, and
would force invalidation of every cached structural id — avoid it.)

But keeping the instances means their **state no longer resets implicitly.** Today clearing the list
destroys the gain plugin, so `outputGain()` falls back to default afterward (`engine.cpp:3395` →
null plugin → `Gain{}`). B1 must therefore **explicitly reset project-owned structural state** that
the old destroy-and-recreate produced: output gain returns to default on clear and load-abort, and
retained input/output meter measurers are cleared. Input gain is excluded from project reset because
it is device-scoped user calibration: clear/load/abort preserve it; known audio input devices reload
their stored calibration when opened; previously unknown input devices start at default. B1 is
incomplete until these three paths preserve the anchors and apply those semantics.

Rules / expected payoff:

- Structural plugin `EditItemID`s are stable for the engine's lifetime, **including across clear and
  load** (because clear/load keep the instances rather than recreating them).
- Clear/load/abort reset project structural state (output gain and retained meter state)
  **explicitly**, while preserving device-scoped input gain across project operations.
- Insert/move/remove operate against fixed anchors; index math simplifies.
- A RockHero command no longer bundles structural creation into its first transaction.
- Public plugin-chain/capture behavior is unchanged; input-gain lifetime is intentionally corrected
  to follow device-scoped calibration instead of project clear/load lifetime.

Verification: `rock_hero_common_audio_tests` builds and passes; structural plugins are excluded from
capture/snapshot exactly as before; clear and load keep the structural anchors present; clear/empty
load leave `outputGain()` at default; project clear/load preserve `inputGain()`; retained live-rig
meter state is cleared.

### B2 - Routing centralization

Two mutually exclusive shapes, chosen at B0 by the expected mechanism lean. Do exactly one.

#### B2-lite - Synchronous centralization (default; do this if mementos are favored)

Route the **chain-structure mutations (insert/move/remove)** through a single "mutate-then-reroute"
helper so the duplicated `rebuildInstrumentMonitoringGraph()` calls and their route-failure/rollback
branches live in **one** place. Routing stays synchronous and keeps returning `LiveInputError`, so the
caller-facing contract and the existing tests are unchanged.

**Output gain is deliberately excluded.** It is not a chain-structure mutation — after B1 it is just a
property write on the always-present structural gain plugin, with no context teardown and no reordering.
After B1, `setOutputGain` no longer re-routes at all. Forcing a stop/release/re-route on every gain
change would add avoidable graph churn and audible/editor timing side effects. Keep `setOutputGain`
as a plain structural property write outside the helper.

What this buys: most of the maintainability win (deduplicates the insert/move/remove route calls and
their rollback branches) at low risk. What it does **not** do: it does not make mutations "pure tree
edits", so it does not help an undo-delegation design — which is fine, because mementos do not need that
property.

Rules:

- One helper owns "stop/release context → mutate → re-route → handle/rollback on route failure" for
  insert/move/remove only.
- `setOutputGain` stays a direct structural property write with no re-route.
- Public port behavior and typed errors are unchanged.
- No new asynchronous timing and no new route-status API.

Verification: `rock_hero_common_audio_tests` builds and passes unchanged; insert/move/remove/gain,
live-input/calibration monitoring, device-change rebinding, and route-failure reporting all behave
exactly as before.

Result: completed with `Engine::Impl::mutateAndReroutePluginChain()` centralizing the synchronous
stop/release -> mutate -> re-route path for `insertPlugin`, `movePlugin`, and `removePlugin`.
Mutation failures now trigger a best-effort monitoring re-route from the helper, and route failures
still roll back before the best-effort re-route. This intentionally fixes the pre-existing
`removePlugin` mutation-failure path that could return after context teardown without a re-route.
`setOutputGain` remains outside the helper as a direct structural property write.
Verification: `rock_hero_common_audio_tests` built and passed via the Codex build workflow.

**B2-lite+ (2026-06-12, done before undo implementation — a cleaner base to build on):** extended the
same synchronous centralization from chain mutations to the **monitoring-mode toggles**. The two
public setters `setLiveInputMonitoringEnabled` / `setCalibrationInputMonitoringEnabled` were ~90%
duplicated (identical preflight / mutual-exclusion / reroute / rollback-to-off, differing only in
channel and rollback-context string); they now delegate to one `Engine::Impl::setMonitoringChannelEnabled(channel, enabled, input_device_available, context)`
helper and keep only the message-thread check. Line-by-line behavior-preserving; synchronous
`LiveInputError` contract unchanged; guarded by the existing monitoring enable/disable + rollback
tests. The *pure route-decision extraction* floated under "B2-lite+" was assessed and **skipped** as
not worth it: the route decision is two trivial lines and the meaningful pure piece (channel
descriptions) is already its own testable free function. No reactive listener, no async — see the
B2-full note below for why the reactive/async version is moot under the memento decision.

**B2-full is not being implemented.** Its reactive plugin-list listener existed to catch out-of-band
`Edit::undo()` tree mutations under *delegation*; the Phase M memento decision removes any out-of-band
mutation (undo/redo restore through RockHero's rerouting methods), so the reactive mechanism — and its
async route-status surface and timing risk — has no remaining job. Routing already reacts
asynchronously only where warranted (external device changes, via the JUCE device-manager callback)
and stays synchronous for RockHero-driven inputs (chain via the B2-lite helper, mode via the new
B2-lite+ helper). That split is the intended end state.

#### B2-full - Reactive routing (only if undo delegation is favored)

Make chain mutations **pure tree edits** and re-apply routing from a single reactive unit triggered by
(instrument plugin-list change OR audio-device change OR monitoring-mode change). This needs a **new
explicit plugin-list/tree listener** — the existing `Engine::Impl` listeners do not cover it:
`changeListenerCallback` (`:1491`) handles only device-manager/transport broadcasts, and
`valueTreePropertyChanged` (`:1546`) watches only transport position/end detection. Neither observes
the instrument track's plugin list, so B2-full must add that observation deliberately (and scope its
liveness/threading).

This is the higher-risk option and is only worth it because delegation needs each command to be a clean
single Tracktion transaction. It requires resolving two things B0 must sign off:

- **Error contract:** a chain mutation can no longer return a route `LiveInputError` synchronously, so
  introduce a **route-status surface** (listener/notification) carrying the typed error. Operations
  whose *purpose* is routing (`setLiveInputMonitoringEnabled`, calibration toggle, device restore)
  keep an explicit synchronous route call.
- **Timing:** routing must re-apply after the playback graph is ready; sequence the reactive re-route
  after context allocation / Tracktion's async `restartPlayback` rather than relying on the current
  synchronous `ensureContextAllocated(true)` inside the route.

Verification: `rock_hero_common_audio_tests` builds and passes (route assertions move to the
status surface or gain a settle/pump step); monitoring, device rebinding, and route-failure reporting
behave as before through the new surface.

### B3 - Re-measure transaction cleanliness (evaluation/spike)

Result: completed in `editor-engine-undo-phase-b-findings.md`. Insert, move, and remove are clean
single transactions at the Tracktion tree/id level on the B1/B2-lite base. Manual route repair
after raw undo/redo succeeded in the headless default-route harness and did not add undo units,
but live-device route/playback behavior remains outside the Tracktion undo tree under B2-lite.
Output gain improved from the old lazy-anchor `+776` churn to `+72`. The first rerun exposed that
`LiveRigGainPlugin` failed to synchronize a Tracktion-restored `gainDb` property into its
realtime target; after that adapter fix, raw `Edit::undo()` and `Edit::redo()` restore the
project-facing output-gain value correctly.

On the B1 (+ chosen B2) base, re-run the Option A/Option B spike probes: wrap insert/move/remove/output
gain in labeled transactions and confirm each is now a **clean single transaction** that `Edit::undo()`
reverts exactly. Record the unit deltas (expect the `+776`-style churn to be gone for gain after B1).
Route/id consistency after a raw `Edit::undo()` is only fully clean under **B2-full**; under B2-lite,
measure what a manual post-undo re-route restores (this gap is itself evidence in the Phase M decision —
it is the residual cost delegation would carry).

**Prove move/reorder undo as its own case.** Tracktion's edit watcher has an empty
`valueTreeChildOrderChanged` (`tracktion_Edit.cpp:378`), so a pure plugin reorder does not trigger its
`restartPlayback` the way insert/remove (child add/remove) do. A delegated `Edit::undo()` of a move may
therefore revert the tree order without rebuilding the processing graph. Measure move-undo/redo
explicitly — chain order, graph state, and audio — rather than assuming it behaves like insert/remove.

Exit: measured evidence of whether delegated `Edit::undo()` is clean enough for audio-domain structural
edits, feeding the Phase M decision.

## Phase M - Undo mechanism decision gate

### DECISION (settled 2026-06-11): RockHero mementos; Tracktion is a backend

RockHero owns one project-level undo stack. Tone-edit entries are RockHero-owned mementos restored
through audio-adapter primitives. Tracktion's undo manager is not consumed as product history;
adapter-local rollback may use `UndoManager::undoCurrentTransactionOnly()`, never raw `Edit::undo()`
as the product inverse. B2-lite remains the route model; B2-full is not required.

The decision was taken on first principles (not on current codebase shape), resting on two
framework/product facts that would hold for any rebuild of the project:

1. **What Tracktion is.** Its `Edit` tree has no model for tablature/chart/gameplay edits, and its
   live runtime does not reactively follow in-place tree undo for all properties (verified: reverting
   the `IDs::state` plugin chunk property does **not** call `setStateInformation` —
   `tracktion_ExternalPlugin.cpp:1956-1974` ignores `IDs::state`; the processor is reloaded only at
   (re)init, `:1856`). So delegated `Edit::undo()` restores the *tree*, not the running system, for
   in-place edits, and would require building a general reactive tree→runtime sync layer Tracktion
   does not provide.
2. **What Rock Hero is.** `Ctrl+Z` must linearly order tone + chart/gameplay + metadata edits in one
   user-facing stack (confirmed product choice: unified undo per project). Charts can never live in
   Tracktion's `Edit`, so the unified stack is a product-level concern owned by editor-core.

Given those, delegation would keep **two synchronized sources of truth** (editor-core's order and
Tracktion's transaction stack) for the tone subset, with correctness resting on a closed-world
invariant over an open-world framework (no Tracktion-undo write may ever occur outside the product
chokepoint — autonomous plugin-editor windows, save/capture flush at `engine.cpp:3710`, future
automation, internal maintenance). Mementos keep **one source of truth**: each entry restores absolute
captured state, provable in isolation against fakes, and reuses the capture/recreate primitives the
persistence path already uses (`captureActiveRig` flush+copy `engine.cpp:3710`; `loadLiveRig`
recreate). Tracktion remains the backend for instantiation, blob capture/restore, graph rebuild, and
bounded rollback — its proper role.

Delegation's residual frictions were each assessed: routing staleness is smoothable (B2-full, an
independently-justifiable cleanup); the save/capture flush is smoothable (capture chunk from
`AudioPluginInstance`); transaction bracketing is smoothable (discipline). The **irreducible** smell
is the two-sources-of-truth coupling plus the metadata layering choice (move presentation data into
the audio tree, or accept a non-unified compound entry). Mementos have neither. Performance is a
non-factor (neither touches the audio thread). Fidelity is satisfied by chunk capture/restore either
way. See `undo-ownership-analysis.md` for the full source-grounded analysis and the automation-curve
correctness proof, and `editor-undo-phase-m-decision-review.md` for the challenge review and the
eight review answers.

Implementation follows `editor-undo-plan.md` (rewritten to the memento mechanism). A scoped runtime
spike (finishing Phase 2 Step 2) de-risks the fidelity-critical adapter operations before the audio
stages: real-VST3 gesture callbacks, open-editor refresh on chunk restore, automation-curve restore
audibility, and live-device route repair after a memento restore. The pure `EditorUndoHistory` layer
is headless and may proceed in parallel.

### Original evaluation framing (retained for context)

Purpose was to choose the inverse mechanism on the cleaned base, with evidence; this has been
completed. The framing below is retained as context for why the decision was made.

Evaluate:

1. **Structural ops:** is delegated `Edit::undo()/redo()` clean and id-preserving on the B-cleaned
   base (from B3)?
   Compare the B2-lite shape (explicit post-undo route repair inside the audio adapter) with the
   B2-full shape (reactive routing makes Tracktion tree undo closer to a complete runtime
   inverse). B2-full is not required for delegation, but it is the cleaner delegated tone design if
   Tracktion-backed tone undo is chosen.
2. **Parameters (fidelity-constrained):** both viable paths must use **coherent full-state restore** —
   the granular per-parameter replay path is rejected (see Settled, above, and
   `undo-ownership-analysis.md`). The remaining choice is only *who owns* the chunk-based restore:
   **(P1)** gesture-settle chunk flush → Tracktion `Edit::undo()` (reuses Tracktion's undo), or
   **(P-mem)** RockHero captures the plugin chunk before/after a settled gesture and restores via
   set-state (the same representation as the remove memento, RockHero-owned). Both cost ~10 KB per
   settle, so granularity/efficiency no longer separates them; the gesture/debounce/self-animating
   observation needed to *detect* a settled user edit is shared by both. The remaining Step 2 spike
   questions are real-VST3 gesture-callback characterization and whether a coherent state restore
   refreshes an open plugin window — and it must verify chunk-restore fidelity against a
   dependent-parameter plugin and a plugin with non-parameter editor state (IR/preset load). This
   absorbs the old "Phase 2 Step 2" parameter spike.
3. **Editor metadata:** store block placement / display-type on the plugin tree node (undoable via
   Tracktion, travels with the plugin, accepts the layering coupling) vs editor-core memento.
4. **Cross-domain deferral:** confirm that the selected tone-undo mechanism leaves a clean path to
   add chart-undo ordering later without a rewrite (charts do not exist yet).

Additional Phase M question: prefer one user-visible RockHero undo stack per open project unless a
future feature is truly a separate document. Tablature/chart edits, tone edits, and editor metadata
are all edits to the same project/timeline, so `Ctrl+Z` should normally undo the last project edit
regardless of which editor panel currently has focus.

### Ownership question resolved by the decision

The mechanism choice was made around one question, not around undo mechanics or performance:
**is the Tracktion `Edit` the authoritative tone model, or an implementation backend behind
RockHero's tone model?** The mixed ownership of a single conceptual tone edit is the real source of
discomfort with either mechanism — plugin chain/order/state live in Tracktion; output gain is a
Tracktion plugin property; block placement and display-type live only in the tone document; future
chart state will be RockHero-owned; runtime routing is imperative adapter state; and save/capture can
flush plugin state into Tracktion's undo manager.

Two facts external to undo constrain the answer:

- **The project file format.** Making the `Edit` authoritative would couple RockHero's durable,
  versioned project file to Tracktion's `ValueTree` schema, or require a projection layer (at which
  point the `Edit` is not really the model). Today tone state is captured *out of* the Edit into the
  project document on save and rebuilt *into* a fresh Edit on load, so RockHero is already
  authoritative at the document level and the Edit is a live projection.
- **Two products + future charts.** The game also consumes project/tone state and does not edit a
  Tracktion `Edit`; charts/tablature cannot live in the Edit naturally. Tone is therefore a
  *component* of the RockHero project, not the project, and `Ctrl+Z` must order tone and chart edits
  in one stack — which rules out the Edit as the authoritative *document*.

Counterweight: tone plugin state/automation **is** persisted via Tracktion-managed files (settled),
so Tracktion already owns tone *storage* as well as runtime and mutation. That strengthens the
*coherence* case for Tracktion-backed tone undo and makes its clean variant viable (store
placement/display-type on the Tracktion tree too, so the whole tone domain is one undoable model).

The two coherent end-states:

- **A — Tracktion owns the tone subsystem (delegation).** Plugin state + automation + placement +
  display-type all live in the Edit; tone undo delegates to `Edit::undo()/redo()` behind an
  audio-domain tone-undo port (editor-core never calls it raw, never reads `getUndoManager()`, never
  stores Tracktion cursor state); RockHero history orders tone entries and future chart entries.
  **Requires** B2-full (reactive routing so `Edit::undo()` is a complete runtime inverse, not just a
  tree revert), the capture/save-flush mitigation (read the chunk straight from `AudioPluginInstance`
  instead of `flushPluginStateToValueTree`, `engine.cpp:3658`), and standing 1:1 command↔transaction
  discipline. Only coherent if done **fully**; the half-delegated middle (delegate plugin ops, leave
  metadata in RockHero, skip B2-full) is the worst path and is explicitly forbidden.
- **B — Tracktion is a backend (mementos).** Tracktion persists + provides primitives; each RockHero
  undo entry restores its own slice — plugin state via full-chunk capture/restore, output gain /
  placement / display-type / charts via their natural inverses — with no shared Tracktion pointer to
  keep aligned, no B2-full, framework-isolated per `architectural-principles.md`. Cost:
  `remapInstanceId` plus a few hundred well-contained, headless-testable lines.

The decisive trade was correctness coupling: delegation's correctness rests on a global,
silently-violable invariant (RockHero's history pointer staying aligned with Tracktion's undo
pointer across cross-domain interleaving, the 350 ms auto-transaction timer, the capture/save flush,
and any future code that writes the Edit between a command and its undo). Mementos have no such
invariant — each entry restores an absolute captured state. With fidelity non-negotiable and the
parameter-efficiency argument neutralized, the gate reduced to whether the project commits to
"Tracktion owns the tone subsystem" (and pays B2-full + the permanent alignment discipline +
metadata-on-tree coupling) or keeps undo as a thin RockHero-owned layer over Tracktion primitives.

### The core tradeoff (must be weighed explicitly)

**Delegation couples editor-core undo correctness to Tracktion's undo *behavior*, which the
`Engine::Impl` boundary does not insulate.** The Pimpl hides Tracktion *types*, but delegated
`Edit::undo()/redo()` makes the correctness of `EditorUndoHistory` depend on Tracktion's undo-pointer
semantics — positional pop, what each transaction contains, and interposing flushes. No `tracktion::`
type leaks, yet a behavioral contract crosses the layer boundary. Mementos, by contrast, keep the
coupling fully contained: editor-core holds opaque `PluginInstanceState` bytes and replays them, with
one trivial local contract ("capture then restore yields the same state"); all Tracktion behavior
stays sealed inside `get/setStateInformation`.

B2-full changes the severity, but not the existence, of that coupling. With B2-full, Tracktion tree
changes can drive runtime routing and graph repair reactively, so delegated tone undo can be hidden
behind a project-owned audio port/token. In that shape, editor-core does not call raw
`Edit::undo()/redo()`, does not derive availability from `Edit::getUndoManager()`, and does not store
Tracktion cursor state. If any of those details reach editor-core, the behavioral coupling has
leaked. If they stay in `rock-hero-common/audio`, the coupling is adapter-local and much more
defensible.

The strict 1:1 command↔transaction invariant delegation requires is **real but narrower than first
stated** (verified 2026-06-10):

- **Route repair is NOT a threat.** `InputDeviceInstance::setTarget(..., juce::UndoManager*, ...)` and
  `clearAllInputs(AudioTrack&, juce::UndoManager*)` are called with `nullptr`
  (`engine.cpp:2629-2630, 2473-2479`; signatures `tracktion_InputDevice.h:128`,
  `tracktion_EditInputDevices.h:26`), so routing writes are not recorded.
- **Capture/save flush IS a concrete threat.** `captureActiveRig()` calls
  `external_plugin->flushPluginStateToValueTree()` per plugin (`engine.cpp:3658`), which hardcodes the
  undo manager (`ExternalPlugin.cpp:1004,1031`), so saving writes undoable chunk actions for changed
  plugins. Avoidable only by capturing the chunk straight from the `AudioPluginInstance` instead of via
  Tracktion's flush path.
- **Copy flush is N/A** (we do not use Tracktion's clipboard). Plugin-window-close writes and future
  automation-curve edits are unverified/standing-discipline risks.

Tracktion-backed tone undo did **not** require Tracktion to own undo for the whole program. The
evaluated delegated shape was a heterogeneous RockHero stack:

- tone entries call a narrow audio-domain inverse backed by Tracktion transactions;
- tablature/chart entries use RockHero-owned editor-core inverses or diffs;
- metadata entries use whichever mechanism owns their authoritative storage;
- the unified RockHero history still owns ordering, labels, clean revision, failure policy,
  shortcuts, and future cross-domain ordering.

**Code-cost comparison (estimate, not false precision).** Most of the perceived "extra memento code"
is shared with delegation or is reuse:

- *Shared regardless of mechanism:* `EditorUndoHistory` ordering/policy/clean-revision/two-phase
  (~200-300 LOC + tests); `EditorUndoEntry` + payloads (~100-150 LOC); parameter gesture/debounce/
  self-animating observation in the adapter (~150-250 LOC) — needed even for delegation's P1, since the
  flush must be triggered at gesture-settle; controller action gate, dirty migration, UI wiring.
- *Memento-specific delta vs delegation:* `capturePluginState`/`insertPluginState`/
  `setPluginParameterValues` ports — but largely **reuse** of existing capture (`captureActiveRig` flush
  + `state.createCopy`) and recreate-from-state (`loadLiveRig` `pluginList.insertPlugin(stateTree, -1)`);
  plus `remapInstanceId` across payloads + tests (~50-100 LOC, the clearest genuinely-new piece, since
  delegation gets Tracktion's id preservation free); plus fake-host round-trip/rollback tests.
- *Delegation-specific delta vs mementos:* transaction bracketing + labels per command; capture-flush
  mitigation (fighting `ExternalPlugin`'s hardcoded flush); pointer-alignment maintenance or a
  black-box tone-undo port; and the behavioral-coupling maintenance risk above.

Net: the memento delta is **modest — on the order of a few hundred lines, much of it reused and the
genuinely-new part (id remapping) being well-contained, headless-testable logic** — in exchange for
keeping the coupling local. Delegation's savings are partly offset by transaction-discipline code and
buy a behavioral dependency the Pimpl cannot hide.

Decision criteria applied:

- **Fidelity of plugin-state reproduction is a hard gate, not a criterion to trade off.** The chosen
  mechanism must restore exact prior plugin state via coherent full-state restore. This eliminates
  granular per-parameter value replay outright and is not subject to balancing against code cost,
  performance, or convenience.
- Prefer the option that best delivers the stated requirement: a *guarantee* of consistent, correct,
  maintainable behavior. Given the cost comparison above, the clean split (mementos) buys containment
  for a modest, well-contained code delta; delegation's reuse savings are smaller than they appear and
  come with cross-boundary behavioral coupling and the capture-flush discipline.
- Treat B2-full as a decision-dependent cleanup, not an automatic next step. It would have been
  justified if Phase M chose Tracktion-backed tone undo; it is unnecessary under the selected
  RockHero memento mechanism.
- The "cleanest" answer is not simply the one with less code. It is the one whose ownership
  boundaries remain obvious when tablature/chart editing, tone metadata, parameters, save/capture,
  and failure recovery are all present.
- A hybrid is acceptable only if it does not reintroduce stack desync (mixing delegated tree-edits with
  inverses that themselves write undoable tree actions).
  Local inverses for non-Tracktion domains such as tablature/chart state are safe in the same
  product stack. Local inverses that write the Tracktion tree must either be represented as
  Tracktion transactions in the same tone-undo scheme, or be isolated so they cannot advance
  Tracktion's pointer behind RockHero's back.

Exit: complete. The recorded decision is RockHero mementos with Tracktion as a backend, and
`editor-undo-plan.md` Ownership Decision / Quarantine / Stage 0 sections now reflect the corrected
mechanics and selected mechanism.

## Phase 3+ - Undo implementation

Follow `editor-undo-plan.md` as revised by Phase M. The headless `EditorUndoHistory` ordering/policy
layer (clean revision, labels, two-phase commit, failure handling) is the first implementation layer;
tone entries then apply RockHero-owned mementos through the audio adapter. Keep user-visible
Undo/Redo disabled until the full selected scope is covered (the v2 shipping rule still holds). Do
not expose Tracktion's `Edit::undo()` directly as RockHero undo; the product stack stays the single
front door.

## Preferred next concrete step

The full ordered path is in **Implementation sequence** at the top of this document. In short: start
the **spike** (Phase 2 Step 2) and the headless **Stage 1 `EditorUndoHistory`** in parallel now, keep
building the headless stages (2 contracts/fakes, 3 action plumbing disabled, 4 placement/display, 5
move) while the spike runs, then do the spike-gated audio-adapter stages (2 adapter half, 6 parameters,
7 output gain, 8 remove memento), and finish with Stage 9 dirty migration and Stage 10 enable.
