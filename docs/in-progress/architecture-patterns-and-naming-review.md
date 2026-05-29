# Architecture Patterns And Naming Review

Status: in-progress review.

This document records a project-wide review of architectural patterns, class naming, and folder
taxonomy. It is not an approved design change by itself. Any durable rule changes that belong in
`docs/design/` should be made only after the team confirms the target vocabulary.

The review covered production code under `rock-hero-common`, `rock-hero-editor`, and
`rock-hero-game`. Test fakes and temporary test harnesses are excluded from the class inventory
except where they demonstrate testability pressure. External/vendor code is excluded.

The review was checked through three passes with the JUCE, Tracktion Engine, and desktop GUI
architecture subagents. Their consensus is reflected here.

## Executive Findings

The project is not fundamentally using two incompatible architectures. The durable direction is
sound: pure domain types, project-owned audio ports, Tracktion/JUCE adapters at the boundary,
core-owned view state, and JUCE views that render state and emit user intents.

The problem is vocabulary drift and a few high-pressure areas that are starting to absorb too many
roles:

- `InputCalibrationWindow::Content` owns JUCE rendering, timer cadence, live-input sampling,
  `InputCalibrationCapture`, phase state, and controller commits. The capture policy belongs behind
  a core-owned calibration workflow/session boundary. The JUCE window should keep window lifetime
  and polling cadence, but not own the product workflow.
- Editor audio capabilities are now explicit and required at composition time through nested
  `Editor::AudioPorts`, `EditorController::AudioPorts`, and `EditorView::AudioPorts` bundles. Keep
  those bundles instead of reintroducing composition-time `dynamic_cast` discovery.
- `IAudioDeviceConfiguration::deviceManager()` exposes broad `juce::AudioDeviceManager` mutation.
  Raw access can remain internal to common/audio device settings, but editor core should not need
  raw JUCE XML persistence.
- `EditorController::Impl` and `EditorView` are both large. `EditorController::Impl` is the higher
  architecture risk because it owns unrelated workflow policy. `EditorView` can remain the root UI
  shell, but should not keep absorbing secondary workflow ownership.
- `PluginHandle`, `LiveRigPlugin`, and `PluginViewState` represent overlapping loaded-plugin chain
  concepts. `PluginCandidate` is not part of that problem; it is a catalog/discovery concept.
- `engine.cpp` is a private implementation monolith. Splitting it is useful, but only after the
  vocabulary is stable enough that ambiguity is not spread across many files.
- Message-thread behavior needs a clearer taxonomy. `JuceEditorTaskRunner`, `juce::MessageManager`
  checks, delayed callbacks, timers, and paint gates should not grow into ad hoc scheduling rules.

## Recommended Vocabulary

Use these terms consistently:

| Term | Meaning | Notes |
| --- | --- | --- |
| `Value` or unsuffixed noun | Domain value or aggregate | Examples: `Song`, `Arrangement`, `Gain`. Do not add suffixes to simple domain concepts. |
| `I*` | Project-owned boundary port | Examples: `ITransport`, `IPluginHost`, `ILiveRig`. Keep interfaces narrow and behavior-oriented. |
| `*Adapter` or technology-prefixed concrete type | Concrete boundary implementation | Prefer `Tracktion*` for new private Tracktion implementation files/types. Keep public `Engine` for now. |
| `*ViewState` | Core-owned, JUCE-free render snapshot | Passive data only. It should not own workflow policy. |
| `*View` | JUCE component that renders a conceptual surface | Use for a direct UI counterpart to a data/domain concept, such as `ArrangementView`. |
| `*Panel` | Persistent bounded editor region grouping controls/subviews | `SignalChainPanel` is correct. |
| `*Controls` | Compact command cluster | `TransportControls` is correct. |
| `*Window` | Top-level or modeless JUCE window/lifetime host | Suitable for `MainWindow` and simple modeless utilities. |
| `*Dialog` | Modal or transactional apply/cancel workflow host | Better target for audio device settings and input calibration. |
| `*Content` | Private hosted JUCE component inside a window/dialog | Do not promote a `Content` class without giving it a specific role name. |
| `*Controller` | Headless workflow/policy owner and intent receiver | Use when state transitions/backend calls/persistence decisions are involved. |
| `*Session` or `*Transaction` | Staged runtime workflow | Appropriate for device setup or calibration capture. |
| `*Snapshot` | Read-only observed runtime state | Examples: `AudioMeterSnapshot`, `LiveRigSnapshot`. |
| `*Request` / `*Result` / `*Progress` | Operation DTOs | Current live-rig names are good. |
| `*TaskRunner` | Async task execution abstraction | Keep `JuceEditorTaskRunner`, but document message-thread semantics. |
| `*Ports` or `*Capabilities` | Explicit bundle of optional services | Prefer this over composition-time `dynamic_cast`. |
| `*Settings` | Durable user/app settings or a clearly staged settings workflow | Avoid introducing new ambiguous `Settings` names. |
| `Listener` | Narrow UI-local callback surface | Scoped listener names are acceptable for simple components and chooser windows. |

Do not introduce `Presenter` now. The project already has core controllers producing view state and
JUCE views rendering it. `Presenter` would likely duplicate an existing boundary unless a distinct
third role appears.

## GUI Workflow Shapes

Two GUI workflow shapes should be explicitly allowed.

### Lightweight Command Window

Use this for simple chooser or command surfaces:

- Receives a `*ViewState`.
- Owns local presentation mechanics such as selection, filter text, row reuse, focus, and geometry.
- Emits a small scoped `Listener` interface.
- Does not own backend policy, persistence, rollback, hardware state, or transaction semantics.

`PluginBrowserWindow` currently fits this pattern.

### Stateful Or Transactional Dialog

Use this for workflows with staged side effects, rollback, async backend work, hardware interaction,
or persistent settings:

- Core owns a `*Controller`, `*Session` or `*Transaction`, and `*ViewState`.
- JUCE owns window lifetime, layout, focus, and message-loop/polling cadence.
- The view emits intents and renders state.
- Backend commits, rollback, persistence, and routing policy stay outside the JUCE component.

`AudioDeviceSettingsController` plus `AudioDeviceSettingsView` is the closest current example.
`InputCalibrationWindow` should move toward this shape.

## Design Patterns In Use

| Pattern | Current use | Value | Verdict |
| --- | --- | --- | --- |
| Layered product modules | `common`, `editor`, `game`; `core`, `audio`, `ui`, `app` | Keeps ownership and dependencies readable | Keep. |
| Ports and adapters | `ITransport`, `ISongAudio`, `IPluginHost`, `ILiveRig`, `Engine`, `TracktionThumbnail` | Hides Tracktion/JUCE implementation details | Keep, but reduce escape hatches. |
| Facade with pimpl | `Engine`, `EditorController` | Keeps public headers smaller and implementation replaceable | Keep, but split oversized private implementations. |
| View-state/controller/view | `EditorController` -> `EditorViewState` -> `EditorView` | Testable UI state and passive rendering | Keep. |
| Lightweight listener UI | `TransportControls`, `SignalChainPanel`, `PluginBrowserWindow` | Simple and idiomatic for local UI intents | Keep for simple surfaces only. |
| Transaction/session | `AudioDeviceSettings` staged apply/cancel | Makes hardware changes reversible/testable | Keep; use similar policy for calibration. |
| Typed errors and `std::expected` | `ProjectError`, `LiveRigError`, `PluginHostError` | Avoids exception-driven flow and gives testable failures | Keep. |
| Snapshot/request/result DTOs | `AudioMeterSnapshot`, `LiveRigLoadRequest`, `LiveRigLoadResult` | Clear operation boundaries | Keep. |
| Value objects | `Gain`, `TimePosition`, `TimeDuration`, `TimeRange` | Prevents primitive obsession | Keep. |
| Observer/listener | `ITransport::Listener`, scoped UI listeners | Keeps push notifications local | Keep, but do not use listeners for workflow policy. |
| State machine | `InputCalibrationCapture`, busy operation state | Makes stepwise behavior explicit | Keep, but keep ownership in the right layer. |
| Command/intent | `EditorAction`, `EditorActionId`, controller methods | Helps serialize user actions and async decisions | Keep. |
| Task runner/message dispatch | `IEditorTaskRunner`, `JuceEditorTaskRunner`, test-local runners | Makes async behavior testable | Keep, but define message-thread vocabulary. |
| Factory/importer | `IThumbnailFactory`, `ISongImporter`, concrete importers | Good boundary for creation and file formats | Keep. |
| Root shell composition | app `MainWindow`, editor `Editor`, `EditorView` | Keeps executable startup thin | Keep, but avoid shell classes owning feature policy. |

## Class And Type Review

### Common Core

| Type | Role | Verdict |
| --- | --- | --- |
| `AudioNormalizationTarget` | Loudness normalization target value | Keep. |
| `AudioNormalization` | Loudness analysis/normalization result value | Keep. |
| `ArchiveErrorCode`, `ArchiveError` | Archive typed error | Keep. |
| `AudioAsset` | Audio asset metadata value | Keep. |
| `NoteEvent`, `Part`, `Arrangement` | Arrangement domain model | Keep. Naming is clear. |
| `Json`, `Json::ErrorCode`, `Json::Error`, `Json::Property` | Small JSON utility and typed error | Keep. Monitor only if it grows into a broad framework. |
| `Session` | Session aggregate | Keep. |
| `DifficultyTier`, `DifficultyRating` | Difficulty domain values | Keep. |
| `SongMetadata`, `Song` | Song domain aggregate | Keep. |
| `SongPackageErrorCode`, `SongPackageError` | Package typed error | Keep. |
| `TimePosition`, `TimeDuration`, `TimeRange` | Timeline value objects | Keep. |

### Common Audio Public API

| Type | Role | Verdict |
| --- | --- | --- |
| `TransportState` | Transport snapshot value | Keep. |
| `IAudioMeterSource` | Meter read port | Keep. |
| `IAudioDeviceConfiguration` | Device configuration port | Keep, but replace broad public `deviceManager()` usage with narrower project APIs. |
| `PluginHostErrorCode`, `PluginHostError` | Plugin host typed error | Keep. |
| `ISongAudio` | Song preparation and active arrangement playback port | Keep. |
| `LiveRigErrorCode`, `LiveRigError` | Live-rig typed error | Keep. |
| `InputDeviceIdentity` | Input device identity value | Keep. |
| `LiveInputErrorCode`, `LiveInputError` | Live input typed error | Keep. |
| `ITransport` | Transport port | Keep. |
| `InputCalibrationState` | Persisted/input calibration state value | Keep. |
| `IThumbnailFactory`, `IThumbnail` | Waveform thumbnail ports | Keep. Consider `IWaveformThumbnail` only if non-waveform thumbnails appear. |
| `Gain` | Gain value object | Keep. Use explicit field names for calibration gain vs rig output gain. |
| `PluginCandidate` | Catalog/discovery candidate | Keep. Do not merge with chain entries. |
| `PluginHandle` | Loaded plugin add result / chain-like entry | Replace or fold into `PluginChainEntry` when plugin DTOs are cleaned up. |
| `IPluginHost` | Plugin discovery/chain operation port | Keep. |
| `Engine` | Project-owned concrete audio facade backed by Tracktion | Keep public name for now. Use `Tracktion*` names for new private implementation types. |
| `AudioNormalizationErrorCode`, `AudioNormalizationError` | Audio normalization typed error | Keep. |
| `ILiveInput` | Live-input/meter/calibration port | Keep, but pass explicitly rather than discovering through `ITransport`. |
| `AudioDeviceStatus` | Device status snapshot | Keep. If route identity expands, consider `AudioDeviceRouteSnapshot`. |
| `IEdit` | Empty future edit-command placeholder | Either keep as a documented placeholder or remove until behavior exists. Do not rename now. |
| `LiveRigPlugin` | Live rig loaded plugin entry | Replace or fold into `PluginChainEntry`. |
| `LiveRigCaptureRequest` | Capture request DTO | Keep. |
| `LiveRigSnapshot` | Captured rig snapshot | Keep; update element type if `PluginChainEntry` is introduced. |
| `LiveRigLoadProgress` | Load progress DTO | Keep. |
| `LiveRigLoadRequest` | Load request DTO | Keep. |
| `LiveRigLoadResult` | Load result DTO | Keep; update element type if `PluginChainEntry` is introduced. |
| `ILiveRig` | Live rig persistence/load port | Keep. |
| `AudioMeterLevel`, `AudioMeterSnapshot` | Meter snapshot values | Keep. |
| `AudioDeviceSettingsErrorCode`, `AudioDeviceSettingsError` | Device settings typed error | Keep. |
| `StereoOutputPair` | Device output route value | Keep. |
| `AudioDeviceSettingsState` | Staged settings view/workflow state | Keep. |
| `IAudioDeviceSettings` | Staged device settings port | Keep. |
| `AudioDeviceSettings` | Staged device settings session/transaction | Keep for now; consider `AudioDeviceSettingsSession` only during a real touch. |

### Common Audio Private Implementation

| Type | Role | Verdict |
| --- | --- | --- |
| `TracktionThumbnail` | Tracktion-backed thumbnail adapter | Keep. |
| `InstrumentChannelRole`, `InstrumentChannelDescription`, `InstrumentRouteMask`, `InstrumentWaveDescription`, `InstrumentWaveDeviceDescriptions` | Instrument wave routing model | Keep. |
| `LiveRigGainPlugin` | Private Tracktion gain plugin | Keep private. |
| `PluginScanTimeout` | Private scan timeout guard | Keep. |
| `PluginIdentity`, `PluginRecord` | Private plugin catalog records | Keep; move under a plugin catalog file when splitting. |
| `ToneDocument` | Private live-rig/tone document representation | Keep; move under a live-rig document file when splitting. |
| `LiveRigLoadOperation` | Private load operation state | Keep; move with live-rig loading implementation. |
| `RockHeroEngineBehaviour`, `RockHeroUIBehaviour` | Tracktion behavior customizations | Keep. |
| `PluginWindow` | Private hosted plugin window | Rename to `TracktionPluginWindow` if extracted. |
| `MeterReader` | Private meter adapter/helper | Rename to `TracktionMeterReader` if extracted. |
| `Engine::Impl` | Private Tracktion-backed implementation | Split after vocabulary cleanup; consider `TracktionAudioEngineImpl` only when moved to a private header. |
| `LoudnessMeasurement`, `ValidationHashInputStream`, `Ebur128StateDeleter` | Audio normalization helpers | Keep private. |

### Editor Core Public API

| Type | Role | Verdict |
| --- | --- | --- |
| `IEditorTaskRunner` | Async editor task port | Keep. Clarify whether concrete runners marshal completion to the JUCE message thread. |
| `IAudioDeviceSettingsView` | Passive device settings view port | Keep. |
| `EditorSettings` | Durable editor settings store | Keep. |
| `AudioDeviceSettingsViewState`, `AudioDeviceSettingsViewState::Choice` | Device settings render state | Keep. |
| `AudioDeviceSettingsController` | Device settings workflow controller | Keep. This is the preferred pattern for transactional dialogs. |
| `ArrangementViewState` | Arrangement render state | Keep. |
| `IEditorView` | Root editor view port | Keep. |
| `IEditorController` | Root editor intent surface | Keep public shape for now; decompose private implementation first. |
| `EditorActionId` | Command/action identifier | Keep. |
| `IAudioDeviceSettingsController` | Device settings intent port | Keep. |
| `PluginCandidateViewState` | Plugin catalog presentation DTO | Keep. |
| `JuceEditorTaskRunner` | JUCE-backed task runner | Keep. Document message-thread behavior before adding more dispatchers. |
| `PluginBrowserViewState` | Plugin browser render state | Keep. |
| `ISongImporter` | Song importer port | Keep. |
| `UnsavedChangesDecision`, `RestoreInterruptedDecision` | Prompt decision enums | Keep. |
| `UnsavedChangesPrompt`, `SaveAsPrompt`, `RestoreInterruptedPrompt` | One-shot prompt state | Keep. |
| `InputCalibrationPrompt` | Current calibration prompt/workflow state | Replace with `InputCalibrationViewState` or calibration session state during calibration refactor. |
| `EditorViewState` | Root render snapshot | Keep, but avoid unlimited growth. |
| `BusyOperation`, `BusyPresentation`, `BusyViewState` | Busy overlay state | Keep. |
| `EditorController` | Root editor facade/controller | Keep public facade, but split private workflow ownership. |
| `EditorController::Services` | Service bundle | Keep distinct from the completed `EditorController::AudioPorts` bundle. |
| `ProjectEditorState`, `Project` | Project state/model | Keep. |
| `ProjectErrorCode`, `ProjectError` | Project typed error | Keep. |
| `PluginViewState` | Signal-chain presentation DTO | Keep, but source it from a unified loaded-plugin chain DTO. |
| `TransportViewState` | Transport render state | Keep. |
| `InputCalibrationStatus` | Signal-chain calibration status enum | Keep, but rename ambiguous `Unavailable` value to `BackendUnavailable` if touched. |
| `SignalChainViewState` | Signal-chain render state | Keep. |
| `SongImportErrorCode`, `SongImportError` | Song import typed error | Keep. |

### Editor Core Private Implementation

| Type | Role | Verdict |
| --- | --- | --- |
| `EditorAction` and nested action structs | Serialized editor command/intent model | Keep. |
| `AnalysisPaintGate` | Paint/message-loop sequencing guard | Keep for now; fold into clearer message-thread vocabulary if this pattern grows. |
| `EditorController::Impl` | Root controller implementation | Split privately by workflow. |
| `OpenTaskState`, `ImportTaskState`, `AddPluginTaskState`, `PluginCatalogTaskState`, `ProjectWriteTaskState`, `ProjectLoadLiveRigStage` | Async task state holders | Keep, but group by feature when splitting `EditorController::Impl`. |
| `InputCalibrationRouteState`, `ActiveInputCalibration` | Calibration routing/workflow state | Extract into a calibration workflow/session. |
| `LiveRigProgress` | Live rig progress state | Keep near live-rig load workflow. |
| `RockSongImporter`, `PsarcSongImporter` | Concrete song importers | Keep. |

### Editor UI

| Type | Role | Verdict |
| --- | --- | --- |
| `Editor` | Editor UI composition wrapper | Keep for now. Consider `EditorUi` or `EditorFeature` only if composition wrappers multiply. |
| `MainWindow` | Editor main top-level shell | Keep. Namespace makes it clear enough today. |
| `EditorView` | Root editor JUCE shell/view | Keep, but split subwindow/timeline responsibilities before more features land. |
| `ArrangementView` | Arrangement domain view | Keep; name follows conventions. |
| `TransportControls` | Compact transport command cluster | Keep. |
| `SignalChainPanel` | Persistent signal-chain region | Keep. |
| `AudioLevelMeter`, `AudioLevelMeterOrientation` | Reusable meter component | Keep. |
| `BusyOverlay` | Transient busy overlay | Keep. |
| `MenuBarButton` | Custom menu button | Keep. |
| `PluginBrowserWindow` | Modeless plugin chooser | Keep listener-only pattern while it remains a chooser. |
| `InputCalibrationWindow` | Current calibration window host/workflow owner | Refactor. Consider `InputCalibrationDialog` plus passive content/view. |
| `AudioDeviceSettingsWindow` | Static launcher for actual dialog | Rename to `AudioDeviceSettingsDialog` or `AudioDeviceSettingsDialogLauncher` when touched. |
| `AudioDeviceSettingsView` | Passive device settings JUCE view | Keep. |
| `CursorOverlay` | Timeline cursor overlay | Keep, but extract from `EditorView` when timeline grows. |
| `TrackViewport` and nested `Content` | Timeline viewport/scroll surface | Keep, but extract from `EditorView`. |
| `PluginRowView` | Private plugin row inside signal chain | Keep. |
| `OutputGainSliderLookAndFeel`, `MenuLookAndFeel` | Private look-and-feel helpers | Keep private. |
| `AudioDeviceSettingsDialogWindow` | Actual private dialog window | Keep or align with public launcher rename when touched. |
| `AudioDeviceSettingsWindowContent` | Private hosted content | Rename to `AudioDeviceSettingsDialogContent` when touched. |
| `PluginBrowserWindow::Content` | Private hosted chooser content | Keep private. |
| `InputCalibrationWindow::Content` | Current hosted calibration workflow owner | Refactor into passive content plus core session/controller. |
| `SaveAsChooserPurpose` | Private file chooser purpose enum | Keep. |

### Editor And Game App

| Type | Role | Verdict |
| --- | --- | --- |
| `RockHeroEditorApplication` | Editor executable application object | Keep. |
| `rock_hero::game::MainWindow` | Temporary game app shell | Keep while game app is placeholder-level. Consider `GameMainWindow` if it becomes public or more complex. |
| `RockHeroApplication` | Game executable application object | Keep. |

## Folder Taxonomy

Do not churn public include paths yet. Public headers are still small enough that folder moves would
create include churn before improving architecture.

Start with private implementation folders when real ownership extraction happens:

```text
rock-hero-editor/ui/src/
  shell/
  timeline/
  transport/
  signal_chain/
  plugin_browser/
  audio_devices/
  input_calibration/
  shared/
```

```text
rock-hero-editor/core/src/
  editor_controller/
  audio_devices/
  project/
  import/
  tasks/
```

```text
rock-hero-common/audio/src/tracktion/
  tracktion_audio_engine_impl.*
  tracktion_engine_behaviour.*
  tracktion_plugin_window.*
  tracktion_plugin_catalog.*
  tracktion_live_input_routing.*
  tracktion_live_rig_document.*
  tracktion_live_rig_gain_plugin.*
  tracktion_meter_reader.*
  tracktion_thumbnail.*
  tracktion_instrument_wave_device_mapping.*
```

Only introduce public header folders later if the public API becomes hard to scan after actual
extractions. A plausible future split would be `ports/`, `view_state/`, `project/`, and `import/`,
but that should be a deliberate public API cleanup, not part of the first refactor.

## Recommended Cleanup Order

1. Confirm this vocabulary before changing `docs/design/`. Do not add a second set of architecture
   names while trying to clean up the first.
2. Keep explicit editor audio capability composition through `Editor::AudioPorts` and
   `EditorController::AudioPorts`. Tracktion-internal casts in `engine.cpp` are a different issue
   and are acceptable inside the adapter.
3. Refactor input calibration into a core-owned session/controller/view-state plus passive JUCE
   dialog/content. Keep JUCE timer cadence and window lifetime in UI.
4. Contain `juce::AudioDeviceManager` access behind narrower project APIs for persistence and route
   snapshots. Keep raw manager use inside common/audio device settings where unavoidable.
5. Normalize loaded-plugin DTOs by introducing a single chain-entry concept such as
   `PluginChainEntry`; keep `PluginCandidate` distinct.
6. Split `EditorController::Impl` privately by feature. Start with calibration, plugin/signal-chain,
   audio devices, and project lifecycle. Keep `IEditorController` stable until the private seams are
   proven.
7. Split `EditorView` as a shell. Extract timeline viewport/cursor and secondary-window
   presentation helpers before adding more editor surfaces.
8. Split private Tracktion implementation files along stable seams: routing, plugin catalog,
   live-rig document/load, plugin windows, meter reader, thumbnail, and instrument wave mapping.
9. Rename misleading types only when touching their area. High-value candidates are
   `AudioDeviceSettingsWindow`, `InputCalibrationPrompt`, `PluginHandle`, `LiveRigPlugin`, and
   possibly `AudioDeviceSettingsWindowContent`.

## Non-Goals

- Do not force every window into `ViewState + Controller + View + Dialog`.
- Do not ban listener-only UI surfaces.
- Do not rename public `Engine` during this cleanup.
- Do not introduce `Presenter` without a real third role.
- Do not split public include folders before ownership boundaries are actually extracted.
- Do not treat every use of `dynamic_cast` as architecture drift. The problematic casts are
  composition-time capability discovery in app/editor/UI code, not Tracktion-internal
  adapter casts.
