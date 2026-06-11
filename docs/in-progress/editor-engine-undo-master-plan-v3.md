# Editor Engine And Undo Master Plan v3

Status: in-progress master plan. **Supersedes v2** (`editor-engine-undo-master-plan-v2.md`). It
coordinates the active implementation order across `remaining-god-object-decomposition-plan.md`,
`editor-undo-plan.md`, `undo-ownership-analysis.md`, and `test-fixture-opportunities-plan.md`. It
does not replace those documents; it defines the preferred sequence.

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

Settled (does not depend on the mechanism choice):

- RockHero owns the user-visible undo *stack*: ordering, command labels, clean-revision dirty
  tracking, typed failure / faulted-session policy, `Ctrl+Z`/`Ctrl+Y`, and any future cross-domain
  (chart) ordering. Tracktion's undo manager is never exposed *as* RockHero history.
- Tracktion does the heavy primitive lifting regardless: plugin instantiation, plugin state
  capture/restore, plugin-list mutation, audio-graph rebuild.

Open (decided at the Phase M gate, after baseline + evaluation):

- The **inverse mechanism** for audio-domain edits (insert/move/remove/output gain/parameters):
  delegate to `Edit::undo()/redo()` (with gesture-end chunk flush for params), RockHero local
  mementos, or a defined hybrid.
- Whether editor metadata (block placement, display-type) is stored on the Tracktion tree node
  (undoable through Tracktion) or kept in editor-core.

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
Phase 2   Tracktion behavior spike                            [mechanical done; param step folds into M]
Phase M   UNDO MECHANISM DECISION GATE                        (evaluation -> decision + rationale)
Phase 3+  Undo implementation per the chosen mechanism        (revise editor-undo-plan.md to match)
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

The remaining god-object work (`remaining-god-object-decomposition-plan.md`, the `Engine::Impl`
seam split) is independent and may proceed in parallel; Phase B will touch the same file, so coordinate
ordering to avoid churn.

## Phase B - Baseline engine cleanup

Purpose: bring the live-rig adapter to its cleanest shape **before** undo, because (1) it is a net
improvement regardless, and (2) it determines whether Tracktion-delegated undo is viable. None of B
adds undo behavior.

### B0 - Evaluation (no code): scope the cleanup and record findings

Result: completed in `editor-engine-undo-b0-findings.md`. B1 is confirmed, B2-lite is the selected
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
    `restartPlayback`). Higher risk; only justified if undo delegation is chosen (Phase M), which needs
    mutations to be clean single transactions.
- **Decision:** confirm B1 is worth doing now; record the **expected mechanism lean** and therefore
  whether routing work is B2-lite (default if mementos are favored) or B2-full (only if delegation is
  favored).

Exit: written findings; B1 and the B2-lite/B2-full choice confirmed. **Done - see
`editor-engine-undo-b0-findings.md`:** B1 is feasible and behavior-preserving for the user-visible
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
`setOutputGain` remains outside the helper as a direct structural property write. B2-full remains
below as the deferred alternative to revisit only if Phase M chooses Tracktion undo delegation.
Verification: `rock_hero_common_audio_tests` built and passed via the Codex build workflow.

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

Result: completed in `editor-engine-undo-b3-findings.md`. Insert, move, and remove are clean
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

Purpose: choose the inverse mechanism on the cleaned base, with evidence. Produce a written decision
with rationale, then revise `editor-undo-plan.md` to match.

Evaluate:

1. **Structural ops:** is delegated `Edit::undo()/redo()` clean and id-preserving on the B-cleaned
   base (from B3)?
   Compare the B2-lite shape (explicit post-undo route repair inside the audio adapter) with the
   B2-full shape (reactive routing makes Tracktion tree undo closer to a complete runtime
   inverse). B2-full is not required for delegation, but it is the cleaner delegated tone design if
   Tracktion-backed tone undo is chosen.
2. **Parameters:** compare **(P1)** gesture-end chunk flush → Tracktion undo (coarse whole-chunk per
   settle, reuses Tracktion) vs **(P2)** RockHero capture/replay via `setPluginParameterValues`
   (fine-grained, owned). Assess granularity, undo-memory cost (~10 KB chunk/settle), gesture wiring
   (`parameterChangeGestureBegin/End`), and whether undo refreshes an open plugin window. This absorbs
   the old "Phase 2 Step 2" parameter spike.
3. **Editor metadata:** store block placement / display-type on the plugin tree node (undoable via
   Tracktion, travels with the plugin, accepts the layering coupling) vs editor-core memento.
4. **Cross-domain deferral:** confirm that whichever mechanism is chosen for tone-only undo now leaves
   a clean path to add chart-undo ordering later without a rewrite (charts do not exist yet).

Additional Phase M question: prefer one user-visible RockHero undo stack per open project unless a
future feature is truly a separate document. Tablature/chart edits, tone edits, and editor metadata
are all edits to the same project/timeline, so `Ctrl+Z` should normally undo the last project edit
regardless of which editor panel currently has focus.

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

Tracktion-backed tone undo does **not** require Tracktion to own undo for the whole program. The
intended delegated shape, if chosen, is a heterogeneous RockHero stack:

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

Decision criteria:

- Prefer the option that best delivers the stated requirement: a *guarantee* of consistent, correct,
  maintainable behavior. Given the cost comparison above, the clean split (mementos) buys containment
  for a modest, well-contained code delta; delegation's reuse savings are smaller than they appear and
  come with cross-boundary behavioral coupling and the capture-flush discipline.
- Treat B2-full as a decision-dependent cleanup, not an automatic next step. It is justified if the
  Phase M decision chooses Tracktion-backed tone undo; it is probably unnecessary if the decision
  chooses RockHero mementos for tone.
- The "cleanest" answer is not simply the one with less code. It is the one whose ownership
  boundaries remain obvious when tablature/chart editing, tone metadata, parameters, save/capture,
  and failure recovery are all present.
- A hybrid is acceptable only if it does not reintroduce stack desync (mixing delegated tree-edits with
  inverses that themselves write undoable tree actions).
  Local inverses for non-Tracktion domains such as tablature/chart state are safe in the same
  product stack. Local inverses that write the Tracktion tree must either be represented as
  Tracktion transactions in the same tone-undo scheme, or be isolated so they cannot advance
  Tracktion's pointer behind RockHero's back.

Exit: a recorded decision (delegation / memento / hybrid) with rationale; `editor-undo-plan.md`
Ownership Decision / Quarantine / Stage 0 sections rewritten to the corrected mechanics and the chosen
mechanism; the now-falsified claims removed.

## Phase 3+ - Undo implementation

Follow `editor-undo-plan.md` as revised by Phase M. The headless `EditorUndoHistory` ordering/policy
layer (clean revision, labels, two-phase commit, failure handling) is needed under **either**
mechanism; what changes is whether its entries' inverses call `Edit::undo()/redo()` or apply mementos.
Keep user-visible Undo/Redo disabled until the full selected scope is covered (the v2 shipping rule
still holds). Do not expose Tracktion's `Edit::undo()` directly as RockHero undo regardless of
mechanism — the product stack stays the single front door.

## Preferred next concrete step

Phase M: record the undo mechanism decision, including the B3 output-gain adapter finding and the
B2-lite post-undo routing cost, then revise `editor-undo-plan.md` to match the chosen mechanism.
