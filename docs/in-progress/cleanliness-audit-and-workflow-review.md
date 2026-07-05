# Cleanliness Audit and Workflow Review

Full-project audit performed 2026-07-05 ahead of the tone-track (tone changes over time +
automation lanes) and tablature-view features. Scope: every class and interaction in
`rock-hero-common` and `rock-hero-editor`, each workflow's design pattern, and a re-review of the
overall structure including the self-registering-modules question. Grounded in a full header
census (~140 headers, all with stated roles), the four design documents, and outside research
(expression-problem framing for variant-vs-interface, C++ undo-framework comparisons,
self-registration pitfalls, Tracktion automation model, tablature rendering architecture).

## 1. Large-Object Inventory (verdicts)

| File | Lines | Verdict |
|---|---|---|
| `editor/core/controller/editor_controller.cpp` | 1,869 | **At equilibrium.** Dispatch (26 typed overloads), whole `deriveViewState` (recorded deviation), undo-transition orchestration, wiring. Only nit: `applyOutputGainChange`/`pushOutputGainUndoEntry` are signal-chain behavior that could live in `signal_chain_handlers.cpp`. |
| `common/core/package/rock_song_package.cpp` | 1,675 | **Split before chart work.** One ~1,370-line anonymous namespace of per-piece readers and writers. Cohesive today, but the tablature chart model will add note/technique serialization here. Split into `rock_song_package_read.cpp` / `rock_song_package_write.cpp` (public header unchanged). |
| `common/audio/engine/engine_plugin_host.cpp` | 1,627 | **Watch item.** One port, three sub-domains (catalog scan, chain mutation + state restore, edit-observer wiring). Sub-slicing a port along a stable axis is allowed by the multi-TU rule; do it if the file passes ~2,000 lines. |
| `editor/ui/signal_chain/signal_chain_view.cpp` | 1,570 | **Extract.** Three real components are nested inside the view TU: `PluginTileView` (~400), `InsertSlotView` (~200), `SignalPathContent`, plus `OutputGainSliderLookAndFeel`. Same disease `editor_view.cpp` had; same cure: promote to `signal_chain/` units. |
| `editor/core/project/project_handlers.cpp` | 1,361 | Fine. Deepest workflow (open/import/save/publish/close/restore + deferral + live-rig stage); cohesive. |
| `editor/ui/main_window/editor_view.cpp` | 1,301 | Fine. Already-decomposed composition hub. |
| `common/audio/device/audio_device_settings.cpp` | 1,154 | Fine-with-check: single staged-settings workflow class; verify no nested component-sized classes on next touch. |
| `editor/core/settings/editor_settings.cpp` | 937 | **Watch item.** Central store + per-feature codecs; grows ~150 lines per persisted feature. Split codecs per feature when it passes ~1,200 lines or six codecs. |
| `common/audio/engine/engine_live_rig.cpp` | 924 | Fine. Port TU + async load stages. |

Everything else is under 700 lines and feature-foldered.

## 2. Workflow Pattern Review

The census found six patterns in active use. The headline finding: **the apparent inconsistency
between the `std::variant` action system and the `IEdit` interface is not an inconsistency — the
two sit on opposite sides of the expression problem, each on its correct side.** This should be
codified so future code lands on the right side deliberately.

### 2.1 Actions (`EditorAction` variant) — keep

- Closed set of intents owned by one library, and **many operations** are applied over that set:
  `idOf`, availability gating, busy-supersede policy, prompt deferral, replay, dispatch.
  Many-operations-over-fixed-types is exactly where a sum type wins (each new operation is one
  `switch`/`visit`, no class hierarchy touched).
- The two-layer surface (public `on*Requested` intents + private action variant) is deliberate:
  intents read naturally at UI call sites; the variant is routing plumbing. A new action costs ~7
  touchpoints (intent, forwarder, impl decl, case struct, `idOf`, availability, `performActionImpl`)
  — the accepted price of typed-and-central; every touchpoint is a compile error if missed.
- `ActionConditions` sits at 15 booleans against the recorded ~25 re-open threshold; the tone
  track adds an estimated 2–4.

### 2.2 Undo (`IEdit` interface + two-phase pure history + full-state mementos) — keep

- Edits are the reverse axis: a **growing set of types** (tone track will add automation-point
  add/move/remove, curve edits, tone-change edits) under a **fixed set of operations**
  (`undo`, `redo`, `label`, `instantiatesPlugin`). Growing-types-under-fixed-operations is where
  the interface wins: each new edit is one new class in its feature folder, touching nothing
  central. A variant here would turn every new edit kind into edits of a central alternatives
  list plus every visitor.
- `EditorUndoHistory` as a pure two-phase state machine (begin → side effects → commit/abort,
  token-checked, rollback-contract faulting) matches the strongest published guidance: the undo
  spine is centralized and side-effect-free, and all mutation is funneled through one mechanism.
  Full-chunk mementos for plugin state remain settled and non-negotiable.

**Rule to codify (design docs):** model a closed set that many operations range over as a
`std::variant` sum type; model an open or growing set with a fixed operation surface as a small
interface. Name `EditorAction` and `IEdit` as the two exemplars.

### 2.3 Busy/async orchestration — keep the machinery, document the idioms

Layered stack, bottom-up: `BusyOperationState` (pure state machine) → `BusyOperationWorkflow`
(tokens, paint fences, progress) → controller helpers (`runWorkerThreadBusyOperation`,
`safeCallback`) → `IEditorTaskRunner` / `IMessageThreadScheduler` ports → engine-internal
`LiveRigLoadOperation` continuation chain and the shared `ProjectLoadLiveRigStage`.

There are exactly **three async execution idioms**, each forced by a real constraint:

1. **Worker offload + tokened completion** — project IO, catalog scans. CPU/IO work must leave
   the message thread; completions are guarded by (liveness, busy-token) uniformly.
2. **Paint-fenced message-thread work** — plugin instantiation. Tracktion requires the message
   thread, and `callAsync` starves `WM_PAINT` on Windows, so the busy overlay must paint before
   the thread blocks.
3. **Yielding message-thread continuation chain** — live-rig restore inside the engine, stepping
   per plugin with progress, so the pump stays alive during multi-second loads.

Plus one non-async modal-flow pattern: `DeferredProjectActionState` (prompt → defer → replay),
already a sum-type resolution.

Verdict: the gating is disciplined, not tangled — every completion path uses the same
(alive, token) guard, and progress flows through one busy view state. No new abstraction is
warranted (a unified "async operation" framework would be speculative complexity). The cleanup is
**documentation**: add an "Async Choreography" section to `architectural-principles.md` naming
the three idioms, when each applies, and the guard rules, so the tone track's automation work
picks the right one instead of inventing a fourth.

### 2.4 Two-tier controllers, workflows, view states — keep (already codified)

Modal-lifecycle features (audio device, input calibration) own controller triads; main-window
features are root-facade features with workflow/state + view-state slices; projection modules
hold pure geometry/text. Uniform across the census.

### 2.5 Ports and typed errors — keep (already codified)

Eight ports on the `Engine` facade; every boundary owns `<Subject>Error`/`<Subject>ErrorCode`.
One accepted deviation: `IEdit` returns a bare `EditorUndoFailureCode` enum — private boundary
with fixed reasons; fine.

### 2.6 Notification styles — one small rule to write down

Two styles coexist: JUCE-style nested `Listener` interfaces on ports (multi-event contracts,
RAII-managed via `ScopedListener`) and `std::function` observer structs on the engine
(`PluginEditObserver` et al. — single-consumer, install-by-value). Rule to codify: multi-event
multi-subscriber contract → nested `Listener` interface; single-consumer callback bundle →
observer struct of `std::function`s.

## 3. Structure Re-Review

### 3.1 Folder structure

The two-axis rule + hub folders + `shared/` + folder-only roots (machine-enforced) held up under
the full census: every one of ~140 headers has a rule-derived location, and both upcoming
features have unambiguous landing paths (below). No change proposed.

### 3.2 Self-registering modules — still no, now for one more reason

Re-checked every recorded break-even signal: `ActionConditions` 15/25 booleans;
`EditorEditContext` 4 domains of ~6 (5 after tone track); the browser↔chain merge removed the
only pairwise join and no second has appeared (automation lanes share timeline geometry through
the already-centralized projection modules — that is typed-and-central working, not a new join);
no optional or third-party in-process features (plugin extensibility already lives at the
`IPluginHost` boundary); the hub stopped growing after the TU split.

Research adds a concrete technical veto: self-registration relies on static registration objects,
which suffer the static-initialization-order fiasco and — decisive here — **linker
dead-stripping in static libraries**: unreferenced registrar objects in a static lib are simply
not linked, the classic silent failure mode. This repository is composed entirely of static
libraries, i.e. the worst-case linkage model for self-registration. Typed hub remains the right
call. Re-open trigger list stands, plus one hard trigger: a feature that must ship as a
separately linked artifact.

## 4. Dependency-Structure Analysis

Built from a ground-truth census: every `<rock_hero/...>` and third-party include in every file,
classified by where it appears (public header / src / tests), cross-referenced against declared
CMake links.

### 4.1 The library graph — clean

The actual include graph is a strict DAG with uniform direction and zero violations:

```
common/core ← common/audio ← editor/core ← editor/ui ← editor/app
     ↑              ↑             (game side: placeholders only)
common/ui      editor/audio (placeholder)
```

- No `common` → product includes anywhere. No `core` → `ui` includes anywhere. No cycles.
- Public-header dependencies match PUBLIC links: `editor/core`'s public headers use
  `common/core`, `common/audio`, and `juce_data_structures` (the `PropertiesFile` member) —
  exactly its three PUBLIC links. This is the model the other libraries should match.

### 4.2 Precision findings (all mechanical)

1. **`editor/ui` over-declares `common::audio` as PUBLIC.** Its public headers only
   forward-declare audio port types (`editor.h`); actual audio includes are src/tests only.
   Tighten to PRIVATE.
2. **`common/audio` under-declares direct JUCE modules.** `juce_core`, `juce_audio_devices`,
   `juce_audio_processors`, and `juce_data_structures` are included directly in src but ride
   transitively on the `rock_hero::tracktion_engine` wrapper. Declare them PRIVATE — "link what
   you include" protects against upstream module-graph changes the wrapper layer explicitly
   warns about.
3. **`editor/ui` under-declares `juce_graphics`** (direct include, rides on `juce_gui_basics`).
   Declare PRIVATE.
4. **`common/audio`'s PUBLIC `juce_gui_basics` is deliberate** — `i_thumbnail.h` forward-declares
   `juce::Graphics` and documents that consumers get definitions from `juce_gui_basics`; the
   PUBLIC link delivers those include paths as part of the port contract. Keep, documented.

## 5. CMake Analysis

- **Root `CMakeLists.txt`: dead duplicate.** `PROJECT_CONFIG_CLANG_TIDY_FILE_REGEX` is FORCE-set
  twice; the first (simple) block is dead code shadowed by the python-regex block. Delete the
  first.
- **Wrapper-module layer (`cmake/RockHeroExternalModules.cmake`): keep.** The static-wrapper
  conversion of JUCE/Tracktion modules is thoroughly documented, with trade-offs stated, and is
  what keeps third-party compilation single-instance across nine static libs. No change.
- **Umbrella targets: correct.** INTERFACE-only aggregates, linked only by app targets; libraries
  link narrow submodules — matches the documented rule everywhere.
- **Header listing in `target_sources` is inconsistent.** `common/audio` lists every private
  header; `editor/core` lists some; `editor/ui` lists none. Standardize on listing all headers
  (IDE navigation, and the file lists then double as the census).
- **Tests: good shape, keep.** Per-library `*_testing` INTERFACE targets for shared fakes,
  `catch_discover_tests(... ADD_TAGS_AS_LABELS)`, fixtures copied into the build tree, and every
  test target links the library it tests.
- **Placeholders (`common/ui`, `editor/audio`, game libs) declare intent links** (e.g.
  `editor/audio` → `common::audio`) that nothing uses yet. Harmless statements of intended
  layering; keep.

**Overall verdict: the dependency structure and build graph came through the audit clean.** No
painful refactoring emerged from any direction of this audit — the candidates are all mechanical
(§7). The structure that exists after this week's cleanup is the shape I would design from
scratch for these requirements.

## 6. Landing Paths for the Upcoming Features

### 6.1 Tone track (tone changes over time + automation lanes)

- **Persistence**: automation curves live in Tracktion's model (`AutomatableParameter` +
  `AutomationCurve`), persisted through the Tracktion-managed tone state per the settled
  tone-persistence decision. RockHero's package stores no curve data.
- **Port**: a new project-owned automation boundary in `common/audio` (the architecture doc
  already reserves "an audio edit-command port if future audio-model mutation needs one" and
  forbids overloading existing ports). Public headers under a new feature group; implementation
  as a new engine port TU in `engine/` plus `tracktion/` adapter units as needed.
- **Editor**: new feature folders in `editor/core` and `editor/ui` sharing one feature name
  across libraries. **Open naming decision for the user**: `tone/` (user-facing feature word) vs
  `automation/` (mechanism word) — recommend `tone/`.
- **Undo**: new `IEdit` classes in the tone feature folder; parameter gestures per the recorded
  gesture-settle findings; `EditorEditContext` gains an automation domain (5 of ~6 threshold).
- **UI**: automation lanes share the timeline's zoom/scroll and tempo-grid geometry — already
  centralized in `editor/core/timeline` projection modules; lanes are new components in
  `editor/ui/timeline/` (or the tone feature folder if they are tone-specific surfaces).

### 6.2 Tablature view

- **Chart model**: notes/tunings/techniques land as a new `common/core` feature folder
  (suggest `chart/`), per the architecture doc's "add the chart model when note display needs
  it". Serialization joins the song package — do the read/write TU split first (§1).
- **UI**: tablature rendering is a timeline lane in `editor/ui/timeline/`; the engraving-style
  pipeline (data model → layout pass → draw) matches the projection-module rule: layout math as a
  pure `tablature_layout.h` projection module, drawing in the component.
- **Standing reminder honored**: the playback-follow decision deferred smooth scrolling until
  "tablature renders over the waveform" — that moment arrives with this feature; re-evaluate
  smooth scroll (plan preserved in `docs/todo/smooth-scroll-follow-evaluation.md`).

## 7. Execution Items

Ordered; each is one commit with the standard gate.

1. **Extract SignalChainView's nested components** — `plugin_tile_view.{h,cpp}`,
   `insert_slot_view.{h,cpp}`, `signal_path_content.{h,cpp}` (+ the slider look-and-feel with its
   consumer) into `editor/ui/src/signal_chain/`.
2. **Split `rock_song_package.cpp`** into read/write TUs (pre-chart-model preparation).
3. **Codify the three rules** in `docs/design/`: expression-problem rule (variant vs interface,
   §2.1/2.2), Async Choreography idioms (§2.3), notification-style rule (§2.6).
4. Optional nit: move output-gain undo helpers from the controller hub TU into
   `signal_chain_handlers.cpp`.

Completed during the audit: repaired two control-character-mangled `\file` commands
(`editor_controller_impl.h`, `editor_controller_logging.h`), added the missing `\file` block to
`plugin_move_index.h`.

Watch items (no action now): `engine_plugin_host.cpp` sub-split at ~2,000 lines;
`editor_settings.cpp` codec split at ~1,200 lines / six codecs; `EditorEditContext` at six
domains; `ActionConditions` at ~25 booleans.
