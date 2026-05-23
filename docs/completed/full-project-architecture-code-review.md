# Full Project Architecture Code Review

Core parts of this are completed so moving to completed for now. In the future another fresh review may be done.

Date: 2026-05-22

## Scope

This review covered the current first-party project structure:

- `rock-hero-common/core`, `rock-hero-common/audio`, and `rock-hero-common/ui`
- `rock-hero-editor/core`, `rock-hero-editor/audio`, `rock-hero-editor/ui`, and editor app startup
- `rock-hero-game/core`, `rock-hero-game/audio`, `rock-hero-game/ui`, and game app startup
- CMake target boundaries, module README files, and design/planning documents
- Public view, view state, controller, service, adapter, and test shapes

I read the repository guidance and durable design docs before reviewing code:

- `CLAUDE.md`
- `docs/design/architecture.md`
- `docs/design/architectural-principles.md`
- `docs/design/coding-conventions.md`
- `docs/design/documentation-conventions.md`

I also did repeated passes over includes, CMake linking, async callback lifetimes, listener
ownership, placeholder targets, UI/controller/view-state relationships, persistence code, audio
device settings, Tracktion/JUCE adapter code, and test structure.

## Essential Fixes Applied

### 1. Fixed async file chooser callbacks capturing raw `this`

Status: fixed in `rock-hero-editor/ui/src/editor_view.cpp`.

`EditorView::showOpenChooser`, `showImportChooser`, `showSaveAsChooser`, and
`showPublishChooser` launched async JUCE file choosers with lambdas that captured raw `this`.
If the editor view was destroyed while a chooser was still open, the callback could call into a
dead `EditorView` and its dead controller reference.

The callbacks now capture `juce::Component::SafePointer<EditorView>` and return immediately when
the component has been deleted.

### 2. Fixed audio settings apply dispatcher capturing the editor controller by reference

Status: fixed in `rock-hero-editor/ui/src/editor_view.cpp`.

`EditorView::showAudioDeviceSettingsWindow` passed an apply dispatcher that captured
`m_controller` by reference. The audio device settings window is self-managed and can temporarily
hide itself during apply, so it can outlive the launching view. If apply completed after the view
was gone, the dispatcher could call a dangling controller reference.

The dispatcher now captures `SafePointer<EditorView>`. When the view is still alive it routes
through `EditorController::onApplyAudioDeviceSettings`; when the view is gone it runs the apply
continuation directly so the settings transaction does not dereference a dead controller.

### 3. Fixed self-managed audio settings window outliving its owner

Status: fixed in `rock-hero-editor/ui/src/audio_device_settings_window.cpp`.

`AudioDeviceSettingsDialogWindow` owned `AudioDeviceSettings`, which holds references/listeners
against the audio backend. Because the dialog is self-managed, it could survive the editor
component that launched it. During app shutdown that creates a real dangling-backend risk: the
editor window can be destroyed before the audio engine.

The dialog now listens to its owning top-level component and deletes itself when that owner is
being destroyed. This keeps the settings service and listener registration scoped to the editor
window lifetime.

## Verification

Commands run:

- `git diff --check`
- `cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_ui_tests'`
- `& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'`

Result: targeted editor UI build passed, and `rock_hero_editor_ui_tests` passed
`240 assertions in 44 test cases`.

No commands were run with escalated permissions.

## Architecture Summary

The major product boundary is in better shape than the file sizes suggest:

- `common` does not include `editor` or `game`.
- `editor` does not include `game`.
- `game` does not include `editor`.
- Tracktion headers are isolated to `rock-hero-common/audio` implementation/private helper files.
- OpenPSARC is isolated to the editor import path.
- UI widgets are mostly passive, and core tests generally exercise controller or service APIs
  rather than concrete widgets.

The main pressure points are not broken layering violations. They are:

- `EditorController` accumulating too many workflows,
- a JUCE-shaped audio-device settings backend that is useful now but broad as a shared API,
- a few docs that describe target architecture without clearly separating current scaffolding.

## Findings

### Clarification: Empty placeholder modules are deliberate static-library scaffolding

Files:

- `rock-hero-common/ui/CMakeLists.txt`
- `rock-hero-editor/audio/CMakeLists.txt`
- `rock-hero-game/core/CMakeLists.txt`
- `rock-hero-game/audio/CMakeLists.txt`
- `rock-hero-game/ui/CMakeLists.txt`

The project guidelines say each product scope owns `core`, `audio`, and `ui` submodules only when
needed. Today several modules are static libraries with only `src/placeholder.cpp`. This is a
deliberate build-shape choice: keeping them as static libraries avoids converting targets from
`INTERFACE` to compiled libraries later when implementation files land.

Some placeholder targets already expose dependencies:

- `rock-hero-game/audio` publicly links `common::core`, `common::audio`, and `game::core`.
- `rock-hero-game/ui` publicly links `game::core`, `game::audio`, and `common::audio`.
- `rock-hero-editor/audio` publicly links `common::core` and `common::audio`.
- `rock-hero-common` umbrella links `common::ui` even though it is empty.

The remaining watch item is dependency shape, not placeholder existence. Avoid adding speculative
public dependencies to empty modules unless they are needed for the current executable scaffold.

### High: `EditorController` is doing too many jobs

Files:

- `rock-hero-editor/core/src/editor_controller.cpp`
- `rock-hero-editor/core/tests/test_editor_controller.cpp`

`EditorController` is disciplined internally: action routing, busy tokens, stale completion
guards, view-state derivation, and task-runner seams are all recognizable. The smell is not random
code. The smell is accumulation.

Current controller responsibilities include:

- open/import/save/save-as/publish/close/exit project workflow,
- startup restore policy,
- unsaved-changes prompts and deferred action replay,
- session loading and selected arrangement restoration,
- audio preparation and active arrangement switching,
- live rig capture/restore and progress presentation,
- plugin browser catalog scan/add/remove/open workflow,
- transport play/stop/seek workflow,
- audio device status projection and settings apply busy lifecycle,
- settings persistence for project path and audio-device state.

The matching test file is also large because it is testing a large public API. That is good
coverage, but it makes future changes harder to navigate.

Recommended extraction direction:

- Project workflow controller/service: open, import, save, save-as, publish, close, exit,
  prompts, and deferred replay.
- Plugin browser/signal-chain controller/service: catalog state, scan, add, remove, open plugin
  window, and dirty-tone behavior.
- Busy operation coordinator: token management, transition policy, stale completion dropping, and
  paint-fence scheduling.
- Audio-device workflow service: current status projection, serialized state restore/persist, and
  busy apply routing.

Do not split only because the file is large. Split around state ownership and public test seams.

### Medium: Not every view needs its own controller, but the rule needs to be explicit

Files:

- `rock-hero-editor/core/include/rock_hero/editor/core/editor_view_state.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/audio_device_settings_view_state.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/arrangement_view_state.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/plugin_browser_view_state.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/signal_chain_view_state.h`
- `rock-hero-editor/ui/include/rock_hero/editor/ui/transport_controls_view_state.h`

The current split is mostly coherent:

- `EditorView` has `EditorViewState` and `EditorController`.
- `AudioDeviceSettingsView` has `AudioDeviceSettingsViewState` and
  `AudioDeviceSettingsController`.
- `ArrangementView`, `SignalChainPanel`, `PluginBrowserWindow`, and `TransportControls` are
  passive/leaf UI pieces. They emit listener intents upward and do not currently need dedicated
  controllers.

The confusing part is navigation consistency. Most view states live in `editor/core`, but
`TransportControlsViewState` lives in `editor/ui`. That is defensible because it is a local
child-widget state derived by `EditorView`, not an editor workflow state. It still reads like an
exception unless the project documents the distinction.

Recommendation: do not adopt "every view must have a controller" as a blanket rule. Instead:

- Product/workflow views get a core controller and core view state.
- Passive leaf widgets can use listeners and local UI-owned state.
- If a leaf widget starts owning workflow policy, move that policy to `editor/core` with a
  controller/service and test it through the public API.

### Medium: `IAudioDeviceConfiguration` exposes a broad JUCE device manager

Files:

- `rock-hero-common/audio/include/rock_hero/common/audio/i_audio_device_configuration.h`
- `rock-hero-common/audio/src/audio_device_settings.cpp`

The shared audio-device settings backend lives in the right product scope: both editor and game
will need the same hardware route choices. The tradeoff is that the shared port exposes
`juce::AudioDeviceManager&`, which makes the common audio settings code heavily JUCE-shaped.

This is acceptable as a pragmatic Tracktion/JUCE adapter boundary today, especially because
`common/audio` already owns JUCE/Tracktion-facing audio infrastructure. The risk is API gravity:
future editor/game code can reach through the manager and bypass project-owned operations.

Recommendation: keep the current shape for now, but when game-side audio settings appear, review
whether `IAudioDeviceConfiguration` should expose narrower operations/state instead of the whole
manager. A narrower port would also make it easier to test hot-plug, route ranking, and default
selection without needing real JUCE manager behavior.

### Medium: Audio device scanning cache can go stale for same-backend hot-plug changes

File: `rock-hero-common/audio/src/audio_device_settings.cpp`

`scanCurrentDeviceTypeIfNeeded()` avoids rescanning when `m_last_scanned_device_type` equals the
staged type. That was added for a real performance reason: repeated WASAPI scans were making
apply visibly slow.

The cache key is only the audio system name. If a device is added or removed under the same backend
type, a listener-driven refresh can keep reusing the old scan result. The comment says external
backend changes refresh the list, but the implementation only rescans if the staged type changes.

Recommendation: keep the performance win, but add an explicit invalidation path for external
device-list changes. A small `m_device_scan_generation` or "force scan on external device change"
flag would preserve fast apply while avoiding stale menus.

Status: addressed in the follow-up by invalidating the settings scan and staged-device cache when
the configuration port broadcasts a backend change.

### Medium: `JuceEditorTaskRunner::submit()` can block the message thread

Files:

- `rock-hero-editor/core/include/rock_hero/editor/core/juce_editor_task_runner.h`
- `rock-hero-editor/core/src/juce_editor_task_runner.cpp`

The runner joins the previous worker before starting a new one. The controller's current action
gate mostly prevents concurrent submissions, and close/exit supersede busy operations by token.
That makes the current design workable.

The risk is future growth. Any future path that submits while a slow worker is still running will
block the caller, often the message thread, until the previous worker finishes. That is especially
visible for imports, plugin scans, or slow project IO.

Recommendation: keep this until the next async workflow needs more, then replace the join-before-
submit model with a small queue, cancellation token, or "drop stale completion" model that does
not block the message thread.

### Medium: `Engine` is a large adapter facade, but its boundaries are mostly intentional

Files:

- `rock-hero-common/audio/include/rock_hero/common/audio/engine.h`
- `rock-hero-common/audio/src/engine.cpp`

`Engine` is large because it is the concrete Tracktion/JUCE boundary for transport, audio
preparation, plugin hosting, live rig persistence, audio device configuration, and thumbnails.
The public API is still project-owned interfaces, and Tracktion includes are not leaking through
the public header.

The watch item is implementation concentration. Candidate future slices:

- plugin catalog and plugin window support,
- live rig tone document read/write and restore flow,
- instrument monitoring route binding,
- transport state bridge and future playback-clock support.

No immediate split is essential because these responsibilities all belong to the adapter layer.
Splitting too early could make Tracktion lifecycle ordering harder to reason about.

### Medium: Native package persistence is large but cohesive

File: `rock-hero-common/core/src/rock_song_package.cpp`

`rock_song_package.cpp` is over one thousand lines, but it is doing one coherent thing: safe
native package read/write around `song.json`, arrangement files, audio assets, and ZIP archive
boundaries. It has good safety checks for unsafe relative paths, duplicate archive entries, and
canonical arrangement/tone refs.

The main future split would be by private implementation concern:

- safe archive extraction/write helpers,
- song document parse/build,
- workspace asset import/copy,
- tone/arrangement reference validation.

No immediate split is required unless this file starts owning more package formats or more song
schema versions.

### Medium: Public API test shape is mostly good, but huge fakes are growing

Files:

- `rock-hero-editor/core/tests/test_editor_controller.cpp`
- `rock-hero-editor/ui/tests/test_editor_view.cpp`
- `rock-hero-common/audio/tests/test_audio_device_settings.cpp`
- `docs/todo/centralized-test-fakes-and-trompeloeil-plan.md`

The current tests mostly follow the public API preference:

- controller tests drive `IEditorController`,
- audio device settings tests drive `IAudioDeviceSettings`,
- audio settings controller tests drive the controller/view public seam,
- UI tests use component IDs for focused widget wiring and layout.

The duplication in fakes is not a design bug by itself. The risk is that `test_editor_controller`
has become a second representation of the controller's size. Extracting shared fakes too early
would add helper API surface without fixing the real problem. Extracting controller responsibilities
first would naturally reduce fake size and make any later test-support library easier to justify.

Recommendation: continue testing through public APIs. Delay centralized fakes until duplicated
setup blocks become stable and clearly shared across multiple modules.

### Low: Durable docs have small drift from current scaffolding

Files:

- `docs/design/architecture.md`
- `rock-hero-editor/ui/README.md`

The design docs still describe the target game view as SDL3 + bgfx, while the current game app is
a temporary JUCE shell. That is probably target architecture rather than an implementation bug, but
the doc should distinguish "current scaffold" from "target design" to avoid confusion.

Specific drift:

- `docs/design/architecture.md` says the game view "Lives in `rock-hero`"; the actual scope is
  `rock-hero-game`.
- `docs/design/architecture.md` has duplicate "Signal chain panel" bullets.
- `rock-hero-editor/ui/README.md` says editor UI includes "JUCE-backed editor settings helpers",
  but `EditorSettings` currently lives in `rock-hero-editor/core`.

Recommendation: update durable design docs after confirming whether the SDL/bgfx direction is
still the intended target. Do not routinely synchronize `docs/todo/` plans; repository guidance
explicitly allows those to be stale until implemented.

Status: the specific drift called out above has been corrected. Broader target-versus-scaffold
documentation should wait for a durable rendering-direction decision.

### Low: A small Doxygen style exception remains

File: `rock-hero-editor/ui/include/rock_hero/editor/ui/transport_controls.h`

Two wrapped one-line Doxygen comments use a leading `*` continuation:

- copy assignment comment,
- move assignment comment.

This conflicts with `docs/design/documentation-conventions.md`, which avoids leading `*` prefixes
inside Doxygen blocks. This is not functionally important and can be cleaned up opportunistically.

Status: fixed in the follow-up Doxygen cleanup.

## Extraneous Class Assessment

I did not find random helper classes in the recent audio-device settings split that should be
collapsed immediately.

Current audio settings shape:

- `common/audio/AudioDeviceSettings`: shared route enumeration/staging/apply backend.
- `editor/core/AudioDeviceSettingsController`: editor workflow/controller mapping.
- `editor/core/AudioDeviceSettingsViewState`: editor-specific render state.
- `editor/ui/AudioDeviceSettingsView`: passive JUCE controls.
- `editor/ui/AudioDeviceSettingsWindow`: modal window/lifetime host.

That split maps to real responsibilities. I did not find audio-device settings helper files that
should be collapsed immediately.

## Recommended Order

1. Review and keep the essential lifetime fixes made during this pass.
2. Extract `EditorController` by workflow responsibility, starting with project workflow or plugin
   browser/signal-chain workflow.
3. Revisit `IAudioDeviceConfiguration` when the game gets its own audio settings UI.
4. Keep an eye on audio-device refresh performance now that backend broadcasts force a rescan.

## Remaining Essential Findings

After the fixes above, I do not see another issue I would call essential to fix immediately before
normal review. The highest remaining items are design cleanup and future-proofing rather than
crash/data-loss bugs.
