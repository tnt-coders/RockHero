# Architecture Patterns And Naming Review v2

## Scope

Full review of every production class, struct, and enum across `rock-hero-common`,
`rock-hero-editor`, and `rock-hero-game`. Three independent review passes (JUCE/UI integration,
Tracktion/audio architecture, and desktop GUI composition) were run in parallel and their findings
synthesized here.

After forming independent conclusions, the prior
`architecture-patterns-and-naming-review.md` was consulted. Where the two reviews agree, this
document confirms the finding. Where they diverge, this document states the independent conclusion
and notes the difference.

## Executive Summary

The project's naming and pattern vocabulary is overwhelmingly consistent. Across ~190 named types,
the conventions are uniformly applied in all but a handful of cases. The architecture — layered
product modules, ports and adapters, passive-view MVP with state projection, typed errors with
`std::expected` — is sound and well-documented.

The issues are not pattern conflicts or naming chaos. They are:

1. A few concrete areas where workflow policy lives in the wrong layer
   (`InputCalibrationWindow::Content` owns calibration workflow, not just presentation).
2. Optional audio capabilities discovered via `dynamic_cast` instead of explicit composition.
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
| `dynamic_cast` capability discovery | `ILiveInput` and `IAudioMeterSource` discovered from `ITransport` at composition time | Moderate. Should be explicit constructor parameters. |
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

### Issue 2: `dynamic_cast` Capability Discovery

Three sites use `dynamic_cast` to discover optional audio capabilities from `ITransport`:

1. `editor_controller.cpp`: `liveInputFrom()` discovers `ILiveInput` from `ITransport&`.
2. `editor_view.cpp`: discovers `ILiveInput` from `ITransport&` for meter display.
3. `editor.cpp`: `meterSourceFrom()` discovers `IAudioMeterSource` from `ITransport&`.

These bypass explicit dependency injection. The capabilities should be passed as explicit optional
parameters at composition time.

**Recommendation:** Introduce an `EditorAudioPorts` aggregate that bundles all optional audio
capabilities:

```cpp
struct EditorAudioPorts
{
    common::audio::IAudioDeviceConfiguration* audio_devices{};
    common::audio::IPluginHost* plugin_host{};
    common::audio::ILiveRig* live_rig{};
    common::audio::ILiveInput* live_input{};
    common::audio::IAudioMeterSource* meter_source{};
};
```

The app composition root populates this from the `Engine` (which implements all these interfaces).
`Editor`, `EditorController`, and `EditorView` receive the bundle instead of discovering
capabilities at runtime. This eliminates the `dynamic_cast` sites and makes the dependency graph
explicit.

The existing review (v1) made the same recommendation. The six constructor overloads on
`EditorController` and `Editor` that encode the combinatorial optionality of audio ports would
collapse to two: one with `EditorAudioPorts` and one without.

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

### Fake Naming

All test fakes consistently use the `Fake*` prefix. No `Stub*` or `Mock*` prefixes appear anywhere.
This is clean.

### Fake Duplication

`FakeTransport`, `FakeEditorController`, `FakeThumbnail`, and `FakeThumbnailFactory` are each
defined in multiple test files with slightly different implementations. There is no shared test-fake
library.

At the current scale this is manageable. A shared fake library would reduce duplication but add a
maintenance target. The right time to introduce one is when fake implementations diverge enough to
cause bugs (a test passes because its local `FakeTransport` behaves differently from another test's
`FakeTransport`).

**Recommendation:** Do not create a shared fake library now. Watch for divergent fake behavior as
tests grow.

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
| `dynamic_cast` discovery should become explicit composition | Yes | Yes | Full |
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
2. **Introduce `EditorAudioPorts`** and remove `dynamic_cast` discovery. This is a standalone change
   with clear scope.
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
- Do not create a shared test-fake library yet.
- Do not restructure public `include/` paths.
- Do not force every window into a full controller/view/viewstate stack.
- Do not ban listener-only UI surfaces.
- Do not treat Tracktion-internal `dynamic_cast` in `engine.cpp` as the same problem as
  composition-time capability discovery.