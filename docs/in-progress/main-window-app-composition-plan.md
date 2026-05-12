# MainWindow App Composition Plan

Status: implemented

This plan addresses the first library-layout alert: `editor::ui::MainWindow` currently mixes
presentation with editor app composition. The goal is to improve the library boundary without
making automated testing worse.

## Problem

`rock-hero-editor/ui/src/main_window.cpp` currently owns several responsibilities:

- JUCE `DocumentWindow` presentation and close-button handling.
- Concrete construction of `common::audio::Engine`.
- Construction of the composed editor feature.
- Per-user restore-state persistence through `EditorSettings`.
- Previous-project restore behavior.
- Application shutdown through `juce::JUCEApplicationBase`.

Only the first item is clearly UI presentation. Concrete engine construction and application
shutdown are app-shell responsibilities. Restore-state policy is behavior worth testing, so it
should not be buried in `app/main.cpp`.

## Target Shape

- `rock-hero-editor/app` stays thin and owns only concrete process startup:
  - concrete audio engine construction
  - top-level JUCE application startup and quit callback
  - concrete settings construction through the editor-core settings type
- `rock-hero-editor/core` owns editor settings behavior and restore/close policy:
  - JUCE-backed last-open-project persistence through `juce::PropertiesFile`
  - startup restore workflow in `EditorController`
  - close-time persistence workflow in `EditorController`
  - focused tests with fakes or temporary files
- `rock-hero-editor/ui` owns reusable editor presentation:
  - `EditorView`
  - `ArrangementView`
  - `TransportControls`
  - the composed `Editor` wrapper, at least until a separate follow-up decides whether to move it
- Restore and close behavior remain testable through injected interfaces or a small headless
  workflow.

## Proposed Steps

1. Move last-open-project settings behavior into `editor/core`.
   - Keep the public surface narrow: `lastOpenProject()` and `setLastOpenProject(...)`.
   - Use `juce::PropertiesFile` directly instead of reimplementing app-settings persistence.
   - Store only the data the editor workflow owns, currently the last restorable project path.

2. Move the restore/close decision flow into `EditorController`.
   - Inputs: settings store and quit callback through `EditorController::Services`.
   - Use the controller's existing open/current-project/exit behavior instead of a separate
     callback bundle.
   - Preserve current behavior: clear stale restore paths, restore valid paths, clear paths when
     restore fails, and persist the current project before quit.

3. Keep the JUCE-backed `EditorSettings` implementation in `editor/core`.
   - Product core is required to stay headless and automated-testable, not JUCE-free.
   - `juce::PropertiesFile` is a narrow settings utility that keeps this behavior simpler than a
     custom settings format.
   - Keep explicit-path construction for focused tests and use the default per-user location in
     the app.

4. Move or replace `MainWindow` ownership in the app layer.
   - Prefer keeping `app/main.cpp` small; do not turn it into the home for workflow behavior.
   - Either define a tiny app-local JUCE window shell, or keep a thin UI window class that receives
     already-constructed dependencies and callbacks.
   - The app layer should construct `common::audio::Engine` and pass only project-owned audio
     ports into reusable editor UI code.

5. Add focused controller tests for the restore/close workflow.
   - Test stale saved path is cleared.
   - Test valid saved path opens on startup.
   - Test failed restore clears saved path.
   - Test allowed close stores the current project path and requests quit.
   - Test close with no restorable project clears the saved path.

6. Update CMake and includes.
   - Remove `editor/ui`'s dependency on settings implementation details.
   - Keep app targets free to link umbrella targets.
   - Keep library targets linking narrow submodule targets.

## Guardrails

- Do not move meaningful behavior into `app/main.cpp` if that would make it effectively untested.
- Keep `editor/core` headless and automated-testable; do not impose a blanket JUCE ban there.
- Do not introduce a broad abstraction over JUCE. Keep interfaces narrow and tied to project
  behavior.
- Do not change durable design docs unless the implementation reveals a lasting architecture
  decision that should be recorded there.
- Treat the `Editor` wrapper question as a separate alert unless this refactor naturally forces a
  minimal adjustment.

## Implementation Notes

- `editor/core` now owns JUCE-backed `EditorSettings`, and `EditorController` owns startup
  restore plus exit-time restore-state persistence.
- `editor/ui::MainWindow` receives audio ports and an exit callback instead of constructing the
  concrete audio engine or owning settings persistence.
- `editor/app/main.cpp` owns concrete startup wiring and delegates restore/close policy to
  the composed editor controller; default settings location is owned by `EditorSettings`.
