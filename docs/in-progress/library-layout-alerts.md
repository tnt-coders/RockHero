# Library Layout Alerts

Status: in progress

This note records the library-placement concerns found during the current layout review. Treat it
as a working checklist for follow-up refactors, not as durable architecture documentation.

## Alerts

### 1. `MainWindow` Mixes App Composition Into `editor/ui`

Severity: medium

Status: addressed

`rock-hero-editor/ui/src/main_window.cpp` creates `EditorSettings`, constructs the concrete
`rock_hero::common::audio::Engine`, wires the composed editor feature, restores the previous
project, and calls JUCE application shutdown. Those responsibilities are closer to the
`rock-hero-editor/app` composition layer than to the presentation-focused `editor/ui` library.

Important caveat: moving this behavior wholesale into `app` would make automated testing harder.
The better direction is to keep policy and restore/close behavior testable behind narrow
interfaces, while moving only concrete composition and application-shell ownership out of UI.

Related plan: `docs/in-progress/main-window-app-composition-plan.md`.

### 2. `EditorSettings` Is App-Local Persistence, Not UI Presentation

Severity: medium

Status: addressed as part of alert 1

`rock-hero-editor/ui/include/rock_hero/editor/ui/editor_settings.h` owned a
`juce::PropertiesFile` and persisted per-user app state. It did not render UI or emit UI intents.
The current direction is to keep `EditorSettings` in `editor/core`: product core modules must stay
headless and automated-testable, but they are not required to be JUCE-free. This avoids replacing
`juce::PropertiesFile` with custom settings infrastructure just to satisfy a blanket restriction.

### 3. `Editor` Is A UI-Side Composition Wrapper

Severity: low/watch

`rock-hero-editor/ui/include/rock_hero/editor/ui/editor.h` owns both
`editor::core::EditorController` and `EditorView`. This is not a hard boundary violation because
it depends on project-owned ports, but it overlaps with the app-folder role of composing features.
If this wrapper grows more lifecycle or dependency-wiring behavior, consider moving the composition
boundary out of `editor/ui` or making app code construct the controller and view explicitly.

### 4. `common/audio` Thumbnail Ports Expose JUCE Drawing Types

Severity: low/watch

`rock-hero-common/audio/include/rock_hero/common/audio/i_thumbnail.h` exposes
`juce::Graphics` and `juce::Rectangle`, and `i_thumbnail_factory.h` accepts a `juce::Component`.
The current design docs allow pragmatic JUCE types in public interfaces, so this is not currently
wrong. A stricter future split would keep waveform source/proxy state in `common/audio` and move
JUCE drawing ownership into `editor/ui` or `common/ui`.

## Checks Already Passed

- No `common` code depends on `editor` or `game` code.
- No `editor` code depends on `game` code, and no `game` code depends on `editor` code.
- `rock-hero-common/core` does not include JUCE or Tracktion source headers.
- First-party library CMake files link project-owned JUCE/Tracktion wrapper aliases rather than
  raw `juce::` or `tracktion::` targets.
- Tracktion source includes are contained in `rock-hero-common/audio` implementation/private
  adapter files.
