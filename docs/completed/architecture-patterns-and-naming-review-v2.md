# Architecture Patterns And Naming Review v2

Status: completed review. Active follow-up planning now lives in
`docs/in-progress/editor-controller-root-facade-plan-v2.md`,
`docs/in-progress/editor-core-framework-isolation-plan.md`, and
`docs/in-progress/readability-taxonomy-evaluation-plan.md`. Test-specific follow-up planning lives
in `docs/in-progress/test-file-decomposition-plan.md`.

## Scope

Full review of every production class, struct, and enum across `rock-hero-common`,
`rock-hero-editor`, and `rock-hero-game`. Three independent review passes (JUCE/UI integration,
Tracktion/audio architecture, and desktop GUI composition) were run in parallel and their findings
synthesized here.

After forming independent conclusions, the prior
`docs/completed/architecture-patterns-and-naming-review.md` was consulted. Where the two reviews
agree, this document confirms the finding. Where they diverge, this document states the independent
conclusion and notes the difference.

## Executive Summary

The project's naming and pattern vocabulary is overwhelmingly consistent. Across ~190 named types,
the conventions are uniformly applied in all but a handful of cases. The architecture — layered
product modules, ports and adapters, passive-view MVP with state projection, typed errors with
`std::expected` — is sound and well-documented.

The issues are not pattern conflicts or naming chaos. They are:

1. A few concrete areas where workflow policy lives in the wrong layer
   (`InputCalibrationWindow::Content` owns calibration workflow, not just presentation).
2. Editor audio capabilities have been made explicit and required through nested audio-port
   bundles.
3. Three overlapping loaded-plugin-chain value types that could be unified.
4. Minor naming inconsistencies (fewer than ten across the entire codebase).

None of these require a new architecture. They require applying the existing architecture more
consistently.

---

## Design Patterns In Use

### Patterns That Are Working Well

| Pattern | Where | Assessment |
|---|---|---|
| Layered product modules | `common`, `editor`, `game` × `core`, `audio`, `ui`, `app` | Clean dependency direction. Core never depends on UI. Products never depend on each other. Common never depends on products. |
| Ports and adapters | 11 `I`-prefixed ports in `common/audio`, all implemented by `Engine` | Tracktion and JUCE are fully isolated behind project-owned interfaces. Tests substitute hand-written fakes. |
| Passive-view MVP with state projection | `EditorController` → `EditorViewState` → `EditorView` | Controller derives state, pushes it atomically. View renders and emits intents. No view pulls. |
| Typed errors with `std::expected` | 9 error types, each with `*Error` struct + `*ErrorCode` enum | Most consistent pattern in the codebase. Every error type follows identical shape: `[[nodiscard]]` struct, `code` field, `message` field, two constructors. |
| Nested scoped listeners | `ITransport::Listener`, `TransportControls::Listener`, etc. | All listeners nested inside their broadcaster. All use `on*` callback methods. `ScopedListener<B,L>` RAII template automates deregistration. |
| Command/intent routing | `EditorAction` variants, `EditorActionId` enum | Clean separation between public action identity and private action payloads. |
| Pimpl | `EditorController::Impl`, `Engine::Impl` | Hides large framework implementations from public headers. |
| Composition root | `RockHeroEditorApplication` → `Engine` → `Editor` → controller + view | App folder is thin. No policy decisions in the composition root. |
| Value types with lifecycle suffixes | `*Request`, `*Result`, `*Progress`, `*Snapshot` | Applied uniformly across `LiveRig*` and `InputCalibration*` operations. |
| Transaction/session | `AudioDeviceSettings` + `AudioDeviceSettingsController` | Staged apply/cancel for hardware changes. The right pattern for this problem. |
| Factory | `IThumbnailFactory` → `IThumbnail` | Standard factory pattern, clearly named. |

### Patterns With Minor Issues

| Pattern | Issue | Severity |
|---|---|---|
| `dynamic_cast` capability discovery | `ILiveInput` and `IAudioMeterSource` were discovered from `ITransport` at composition time | Addressed by nested `Editor::AudioPorts` and `EditorController::AudioPorts` bundles. |
| God-adapter | `Engine` implements 8 interfaces | Documented and intentional (one Tracktion Edit = one adapter), but the interface list needs monitoring. |
| View-state default coupling | `EditorViewState::audio_device_status_text` previously defaulted from a formatting header | Fixed in this session. |

---

## Naming Consistency Matrix

| Role | Convention | Consistent? | Count |
|---|---|---|---|
| Interface/port | `I` prefix | Yes — all 15 across common + editor | 15 |
| Controller | `*Controller` suffix | Yes | 2 |
| View (core-interface-implementing) | `*View` suffix | Yes | 3 |
| View (standalone presentation) | Descriptive suffix (`*Panel`, `*Controls`, `*Overlay`, `*Meter`) | Intentional variation — see below | 4 |
| ViewState | `*ViewState` suffix | Yes | 9 |
| Window | `*Window` suffix | Yes | 4 |
| Error struct | `*Error` suffix + `[[nodiscard]]` | Yes | 9 |
| Error code enum | `*ErrorCode` suffix | Yes | 9 |
| Prompt struct | `*Prompt` suffix | Yes | 4 |
| Decision enum | `*Decision` suffix | Yes | 2 |
| Listener | Nested `::Listener` class | Yes | 6 |
| Domain value | Unsuffixed noun | Yes | ~28 |
| Adapter/concrete | Technology prefix or strategy prefix | Yes | 6 |
| Test fake | `Fake*` prefix | Yes — no `Stub*` or `Mock*` anywhere | ~20 |
| Composition wrapper | Plain feature name | Yes | 1 (`Editor`) |

### The View Suffix Divergence Is Intentional

Components implementing a core `I*View` interface consistently use `*View`:
`EditorView`, `ArrangementView`, `AudioDeviceSettingsView`.

Standalone presentation widgets use a suffix that describes their visual role:
`TransportControls` (command strip), `SignalChainPanel` (docked region),
`BusyOverlay` (layered overlay), `AudioLevelMeter` (gauge).

This is a two-tier convention, not an inconsistency. The descriptive suffixes communicate more about
the component's visual behavior than `*View` would. The existing review (v1) reached the same
conclusion: `SignalChainPanel` and `TransportControls` are correct as-is.

**Recommendation:** Document this as explicit policy in `coding-conventions.md`:
- `*View` for types implementing a core `I*View` port or for direct data-to-view rendering pairs.
- Descriptive suffixes (`*Panel`, `*Controls`, `*Overlay`, `*Meter`) for standalone presentation
  widgets whose visual role is more specific than "view."

---

## Issues Found

### Issue 1: `InputCalibrationWindow::Content` Owns Workflow Policy

`InputCalibrationWindow::Content` owns JUCE rendering, timer cadence, live-input sampling, the
`InputCalibrationCapture` state machine, phase tracking, and controller commits. The capture policy
belongs behind a core-owned calibration workflow boundary. The JUCE window should own window
lifetime and polling cadence, but not the product workflow.

This is the single clearest architecture violation in the codebase. The existing review (v1) flagged
the same issue and recommended refactoring toward a core-owned controller/session plus passive JUCE
content.

**Recommendation:** Address this as part of the `InputCalibrationWorkflow` extraction in the root
facade plan (Phase 5). The calibration capture state machine and decision policy move to
`editor/core`. The JUCE window keeps timer cadence, meter rendering, and intent emission.

### Issue 2: Completed Explicit Audio-Port Composition

Three sites used `dynamic_cast` to discover audio capabilities from `ITransport`:

1. `editor_controller.cpp`: `liveInputFrom()` discovers `ILiveInput` from `ITransport&`.
2. `editor_view.cpp`: discovers `ILiveInput` from `ITransport&` for meter display.
3. `editor.cpp`: `meterSourceFrom()` discovered `IAudioMeterSource` from `ITransport&`.

These bypassed explicit dependency injection. The capabilities are now passed as explicit required
fields at composition time.

**Recommendation:** Keep the completed nested audio-port bundles. `Editor::AudioPorts` owns the
composed editor feature contract, while `EditorController::AudioPorts` owns the headless controller
contract:

```cpp
struct AudioPorts
{
    common::audio::IAudioDeviceConfiguration& audio_devices;
    common::audio::IPluginHost& plugin_host;
    common::audio::ILiveRig& live_rig;
    common::audio::ILiveInput& live_input;
    const common::audio::IAudioMeterSource& meter_source;
};
```

The app composition root populates `Editor::AudioPorts` from the `Engine` (which implements all
these interfaces). `Editor` maps the controller-owned subset into `EditorController::AudioPorts`
and the view-owned subset into `EditorView::AudioPorts`. This eliminates the composition-time
`dynamic_cast` sites and makes the dependency graph explicit.

The six constructor overloads on `EditorController` and `Editor` that encoded the old
combinatorial port wiring have collapsed to the completed bundle-based constructors.

### Issue 3: Three Overlapping Loaded-Plugin Types

Three types represent a loaded plugin in the chain with overlapping fields:

| Type | Source | Fields |
|---|---|---|
| `PluginHandle` | Returned by `IPluginHost::addPlugin()` | `instance_id`, `plugin_id`, `chain_index` |
| `LiveRigPlugin` | Returned by `ILiveRig::loadLiveRig()` | `instance_id`, `plugin_id`, `name`, `manufacturer`, `format_name`, `chain_index` |
| `PluginViewState` | Used in `SignalChainViewState` | `instance_id`, `plugin_id`, `name`, `manufacturer`, `format_name`, `chain_index` |

`LiveRigPlugin` and `PluginViewState` have identical fields. `PluginHandle` is a subset.
`PluginCandidate` is distinct (discovery/catalog, not chain).

**Recommendation:** Introduce a single `PluginChainEntry` in `common/audio` that carries the full
chain-entry fields. `IPluginHost::addPlugin()` and `ILiveRig::loadLiveRig()` both return it (or
vectors of it). `PluginViewState` in editor/core is then a trivial lift or direct use of
`PluginChainEntry`. This eliminates the field-by-field copying between `LiveRigPlugin`,
`PluginHandle`, and `PluginViewState`.

The existing review (v1) made the same recommendation.

### Issue 4: `LiveRigSnapshot` vs `LiveRigLoadResult` Naming Asymmetry

Both serve the same structural role — the success payload of a `LiveRig` operation — but use
different suffixes:

- Capture output: `LiveRigSnapshot`
- Restore output: `LiveRigLoadResult`

`LiveRigSnapshot` is defensible (it captures a point-in-time state), but the asymmetry with
`LiveRigLoadResult` is unnecessary.

**Recommendation:** Rename `LiveRigSnapshot` to `LiveRigCaptureResult` for consistency with the
`*Result` convention used by `LiveRigLoadResult` and `InputCalibrationResult`. Do this when
touching the live-rig capture path.

### Issue 5: `InputCalibrationError` Missing Default-Message Constructor

All other error types have both `explicit ErrorType(ErrorCode)` (with default message) and
`ErrorType(ErrorCode, std::string)` constructors. `InputCalibrationError` is a plain aggregate
populated inline at each call site.

**Recommendation:** Add the same two-constructor pattern for consistency. Do this when touching the
calibration code.

### Issue 6: Minor Doxygen Gap

`InputCalibrationStatus` enum values (`NoActiveInputDevice`, `MissingCalibration`, `Calibrated`,
`Unavailable`) lack `\brief` documentation, unlike every other enum in the codebase. Doxygen warns
on undocumented enumerators of a documented enum.

**Recommendation:** Add `\brief` to each value when touching the file.

### Issue 7: Selective `static_assert` Guardrails

`DifficultyRating`, `AudioMeterLevel`, and `AudioMeterSnapshot` have `static_assert` size and
triviality checks. Other equally small and trivially-copyable value types (`Gain`, `TransportState`,
`TimePosition`, `TimeDuration`) do not.

**Recommendation:** Add `static_assert` guards to all small pass-by-value types when touching their
headers. This is a low-priority consistency fix.

---

## Window vs Dialog Naming

The existing review (v1) proposed distinguishing `*Window` (simple top-level) from `*Dialog`
(transactional apply/cancel workflow). This is a sound distinction:

- `MainWindow`: top-level application shell. Correct as `*Window`.
- `PluginBrowserWindow`: lightweight chooser. Correct as `*Window`.
- `AudioDeviceSettingsWindow`: static factory that launches a transactional settings dialog.
  `*Dialog` would be more accurate.
- `InputCalibrationWindow`: hosts a transactional calibration workflow. `*Dialog` would be more
  accurate after the workflow extraction.

**Recommendation:** Rename `AudioDeviceSettingsWindow` to `AudioDeviceSettingsDialog` and
`InputCalibrationWindow` to `InputCalibrationDialog` when touching those files as part of the root
facade plan. Do not rename preemptively.

---

## Test Infrastructure

### Shared Testing Targets

The earlier review predated the shared-helper extraction. The project now uses module-owned
test-only helper targets:

- `rock_hero::common::audio_testing`
- `rock_hero::editor::core_testing`
- `rock_hero::editor::ui_testing`

This matches the fake-first testing policy without creating a global fake warehouse. Reusable
helpers live with the module that owns the production contract they implement, and production
targets do not link testing targets.

### Test Double Naming

Shared helpers now use behavior-specific names such as `Recording*`, `Configurable*`, `Null*`, and
`Immediate*`. Local one-off fakes may still use simple `Fake*` names while they remain private to a
single test file.

Reserve `Mock*` for expectation-driven Trompeloeil types. Trompeloeil remains deferred and is
tracked by `docs/todo/trompeloeil-adoption-plan.md`.

### Remaining Test Pressure

The cross-target fake duplication has been addressed. The remaining test readability issue is large
test files, not a need for a global test-support target. That active follow-up is tracked in
`docs/in-progress/test-file-decomposition-plan.md`.

---

## Folder Organization

The current layer-based organization (`core/`, `audio/`, `ui/`, `app/`) is correct at this scale.
Feature-based folders within `src/` become valuable after the root facade extractions create
multiple private headers per feature area.

Do not reorganize public `include/` paths now. The public header count is manageable and the types
are findable by name. Public folder splits (`ports/`, `view_state/`, `project/`, `import/`) should
wait until the public API is genuinely hard to scan.

Private `src/` folders should be introduced only when a directory has enough files to make flat
listing unreadable. The root facade extractions will add private headers, and that is the right
time to group them:

```text
rock-hero-editor/core/src/
  (existing flat files until extraction creates enough to group)
```

```text
rock-hero-common/audio/src/
  (existing flat files; consider tracktion/ subfolder when engine.cpp is split)
```

The existing review (v1) proposed detailed folder structures. Those structures are directionally
correct but premature — introduce them when the files exist, not in advance.

---

## Comparison With Prior Review (v1)

The independent review agrees with the prior review on all major findings:

| Finding | v1 | This review | Agreement |
|---|---|---|---|
| Naming is overwhelmingly consistent | Yes | Yes | Full |
| `InputCalibrationWindow::Content` owns too much | Yes | Yes | Full |
| `dynamic_cast` discovery should become explicit composition | Yes | Yes | Completed |
| Plugin chain types overlap | Yes | Yes | Full |
| View/Panel/Controls suffix pattern is intentional | Yes | Yes | Full |
| Error types are highly consistent | Yes | Yes | Full |
| Folder restructuring is premature | Yes | Yes | Full |
| Do not rename `Engine` | Yes | Yes | Full |
| Do not introduce `Presenter` | Yes | Yes | Full |
| `AudioDeviceSettingsWindow` → `*Dialog` when touched | Yes | Yes | Full |

Minor differences:

- v1 suggested `InputCalibrationPrompt` → `InputCalibrationViewState`. This review disagrees: the
  `*Prompt` suffix is consistent with the other three prompt types in `EditorViewState`
  (`UnsavedChangesPrompt`, `SaveAsPrompt`, `RestoreInterruptedPrompt`). Renaming it to `*ViewState`
  would break the prompt naming pattern. If calibration gains a full ViewState in the future (after
  the workflow extraction), that would be a new type alongside the prompt, not a rename.

- v1 suggested renaming `InputCalibrationStatus::Unavailable` to `BackendUnavailable`. This review
  agrees the current name is vague but does not consider it high-priority.

- v1 proposed `LiveRigSnapshot` rename. This review agrees and recommends `LiveRigCaptureResult`.

---

## Recommended Action Order

1. **Document the view suffix convention** in `coding-conventions.md`: `*View` for core-interface
   implementations, descriptive suffixes for standalone widgets.
2. **Preserve the completed nested `AudioPorts` bundles** and keep composition-time
   `dynamic_cast` discovery out of editor code.
3. **Unify plugin chain types** into `PluginChainEntry`. Touch `IPluginHost`, `ILiveRig`, and
   `PluginViewState`.
4. **Rename `LiveRigSnapshot`** to `LiveRigCaptureResult` when touching the live-rig capture path.
5. **Fix minor consistency items** (InputCalibrationError constructors, InputCalibrationStatus
   Doxygen, static_assert guards) when touching those files.
6. **Rename `AudioDeviceSettingsWindow` → `AudioDeviceSettingsDialog`** and
   **`InputCalibrationWindow` → `InputCalibrationDialog`** when touching those files during the
   root facade plan.
7. **Refactor `InputCalibrationWindow::Content`** as part of the `InputCalibrationWorkflow`
   extraction (root facade plan Phase 5).
8. **Introduce `src/` subfolders** only after the root facade extractions create enough private
   files to justify grouping.

Items 1-2 can be done immediately and independently. Items 3-8 align with ongoing work and should
be done when the relevant code is being touched.

## Non-Goals

- Do not rename `Engine`.
- Do not introduce `Presenter`.
- Do not create a global test-fake library or add Trompeloeil by default.
- Do not restructure public `include/` paths.
- Do not force every window into a full controller/view/viewstate stack.
- Do not ban listener-only UI surfaces.
- Do not treat Tracktion-internal `dynamic_cast` in `engine.cpp` as the same problem as
  composition-time capability discovery.
