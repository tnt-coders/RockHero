# RockHero modular architecture and CMake plan

This document **refines and partially supersedes** the earlier `rockhero_architecture_analysis.md`.

## How it relates to the earlier file

The earlier file is still useful for the diagnosis:

- the repository already has good high-level separation
- `rock-hero-core` is the strongest existing foundation
- the main architectural risk is direct UI ↔ engine coupling

However, this document **replaces one implied direction** from the earlier analysis:

- **Old implication:** split aggressively into more separate libraries
- **Updated recommendation:** keep the **three primary module domains** and organize them internally as modules first, then promote submodules into separate CMake targets only when there is a real need

So:

- keep the earlier file for architectural context
- use **this file** as the more accurate implementation plan

---

## Goal

Preserve the current three-domain framework:

- `rock-hero-core`
- `rock-hero-audio`
- `rock-hero-ui`

while making the application:

- modular
- easier to evolve
- easier to test automatically
- less tightly coupled to JUCE and Tracktion
- ready for future internal target splitting without a disruptive rewrite

---

## Core architectural principle

Use **folders as conceptual modules first**.  
Use **CMake targets as enforcement boundaries when needed**.

That means the top-level module domains can stay the same, while internal submodules gradually become their own targets only when one of these becomes true:

- they need independent tests
- they need a stable API used by multiple other parts of the codebase
- they need different dependencies
- they may need alternative implementations
- they are becoming hard to reason about as part of a larger target

---

## Recommended dependency direction

The most important thing is dependency flow, not library count.

### High-level direction

```text
apps
  ↓
ui  →  application/presenters  →  audio api
  ↘             ↓                  ↓
    core/domain/use-cases      audio impl
```

### Rules

- `rock-hero-core` must not depend on JUCE or Tracktion
- `rock-hero-audio` may depend on `rock-hero-core`
- `rock-hero-ui` may depend on `rock-hero-core`
- `rock-hero-ui` should avoid depending directly on concrete Tracktion engine details
- applications should compose modules, not become the place where all business logic lives
- presenters/controllers should mediate between views and engine/services
- views should stay as passive as practical

---

## Recommended module structure

Keep the three primary module domains, but structure them internally like this.

```text
libs/
  rock-hero-core/
    include/
      rock_hero/core/
    src/
    domain/
    use_cases/
    ports/
    tests/

  rock-hero-audio/
    include/
      rock_hero/audio/
    src/
    api/
    playback/
    thumbnail/
    tracktion_impl/
    tests/

  rock-hero-ui/
    include/
      rock_hero/ui/
    src/
    views/
    widgets/
    presenters/
    tests/

apps/
  rock-hero/
  rock-hero-editor/
```

This keeps the current framework intact while making the next steps clearer.

---

## What each primary domain should own

## 1. `rock-hero-core`

This should be the most stable and most testable domain.

### Responsibilities

- domain models
- game/editor state that does not require JUCE
- chart/song/note/session data
- pure business logic
- use-cases
- small abstract ports/interfaces when needed

### Good candidates for subfolders

- `domain/`
- `use_cases/`
- `ports/`

### Example contents

- `Song`
- `Chart`
- `NoteEvent`
- `Arrangement`
- `PracticeSession`
- `LoadProjectUseCase`
- `SeekTransportUseCase`
- `StartPlaybackUseCase`

### Rules

- no JUCE UI types
- no Tracktion types
- avoid filesystem or platform APIs unless abstracted
- maximize unit tests here

---

## 2. `rock-hero-audio`

This should be the audio domain and adapter boundary.

### Responsibilities

- transport
- playback control
- waveform/thumbnail generation
- Tracktion integration
- future tone/effects routing
- audio/session orchestration that is not UI logic

### Internal submodules

- `api/`
- `playback/`
- `thumbnail/`
- `tracktion_impl/`

### Design goal

Split the current audio code conceptually into:

- **API / ports**
- **implementation**

### Example interfaces

- `ITransport`
- `IAudioLoader`
- `IWaveformSource`
- `IThumbnailFactory`
- `IToneEngine`

### Example concrete implementations

- `TracktionTransport`
- `TracktionAudioLoader`
- `TracktionThumbnailFactory`

### Rules

- UI should not need Tracktion headers
- keep Tracktion-heavy code isolated
- prefer small interfaces over exposing one giant engine object everywhere

---

## 3. `rock-hero-ui`

This should be the JUCE presentation module.

### Responsibilities

- JUCE components
- layout
- widget rendering
- user input collection
- display-only state
- forwarding intents to presenters/controllers

### Internal submodules

- `views/`
- `widgets/`
- `presenters/`

### View rule

Views should be as passive as practical.

They may:

- render state
- expose callbacks/signals
- forward user intent

They should not:

- directly own core business logic
- orchestrate engine behavior
- translate engine events into application policy
- become the place where editor workflows live

### Example split

- `widgets/TransportControls`
- `widgets/WaveformDisplay`
- `views/EditorView`
- `presenters/EditorPresenter`

---

## The most important immediate refactor

The first architectural improvement should be:

## Introduce a presenter/controller layer between UI and audio

Today, the editor path effectively does this:

```text
Component -> Engine directly
Engine callbacks -> Component directly
```

Recommended shape:

```text
Component -> Presenter -> Audio/Core services
Audio/Core events -> Presenter -> View state update
```

### Why this matters

This is the biggest unlock for automated testing.

Once that layer exists:

- you can unit test presenter behavior without booting JUCE
- you can fake transport/audio services
- your UI becomes easier to replace or redesign
- your audio module becomes less entangled with specific components

---

## Concrete proposal for the editor

## New editor flow

```text
EditorView (JUCE)
  ↕
EditorPresenter
  ↕
ITransport / IWaveformSource / project use-cases
```

### `EditorView`

Owns or contains:

- `TransportControls`
- `WaveformDisplay`
- other editor-specific visual widgets

Exposes methods like:

- `setTransportState(...)`
- `setWaveformProgress(...)`
- `setLoadedFileName(...)`
- `setErrorMessage(...)`

Exposes callbacks like:

- `onPlay`
- `onPause`
- `onStop`
- `onSeek`
- `onLoadFile`

### `EditorPresenter`

Owns references to:

- `ITransport`
- `IWaveformSource`
- relevant core use-cases or state objects

Responsibilities:

- respond to view callbacks
- call audio/core services
- subscribe to transport state changes
- map backend state into view state
- contain orchestration that is currently in JUCE components

### Result

- view becomes passive
- presenter becomes highly unit-testable
- audio stays modular

---

## Suggested CMake evolution path

Do **not** explode the target graph too early.

Use a staged plan.

## Stage 1: keep the current three main targets

```text
rock_hero_core
rock_hero_audio
rock_hero_ui
```

Internal submodules are represented by folders only.

This minimizes churn and keeps the project easy to navigate.

## Stage 2: add internal enforcement targets only where valuable

Example future targets:

```text
rock_hero_core_domain
rock_hero_core_use_cases

rock_hero_audio_api
rock_hero_audio_tracktion
rock_hero_audio_thumbnail

rock_hero_ui_widgets
rock_hero_ui_presenters
```

These still live under the same three top-level module folders.

This means you are **not** abandoning the three-domain framework.  
You are just strengthening it over time.

## Stage 3: split tests alongside target boundaries

When a submodule becomes its own target, give it:

- independent tests
- clear public/private include boundaries
- explicit dependency declarations

---

## Recommended CMake dependency model

A good target evolution would look like this.

```text
rock_hero_core_domain
rock_hero_core_use_cases -> rock_hero_core_domain

rock_hero_audio_api -> rock_hero_core_domain
rock_hero_audio_tracktion -> rock_hero_audio_api rock_hero_core_domain

rock_hero_ui_widgets -> rock_hero_core_domain
rock_hero_ui_presenters -> rock_hero_ui_widgets rock_hero_audio_api rock_hero_core_use_cases

rock_hero_editor_app -> rock_hero_ui_presenters rock_hero_audio_tracktion
rock_hero_game_app   -> rock_hero_audio_tracktion plus renderer modules
```

Key idea:

- UI presenter layer talks to **audio API**, not Tracktion implementation
- app target composes the concrete implementation

---

## Suggested testing strategy

Use three layers of tests.

## 1. Domain tests

Location:

- mostly under `rock-hero-core/tests`

These should cover:

- chart rules
- note timing logic
- arrangement/session logic
- pure use-cases

These are fast and should be the largest test layer.

## 2. Presenter/controller tests

Location:

- `rock-hero-ui/tests` or a separate application-layer test target

These should cover:

- play/pause/stop behavior
- load file flow
- seek behavior
- state transitions
- error propagation to the view

These tests should use:

- fake `ITransport`
- fake `IWaveformSource`
- fake view sink / mock callbacks

These tests should run without opening a JUCE window.

## 3. Adapter/integration tests

Location:

- `rock-hero-audio/tests`

These should cover:

- Tracktion transport integration
- waveform generation
- file loading integration

Keep this layer smaller and more selective.

---

## What should happen to existing code patterns

## Keep and expand

- pure core logic with Catch2 tests
- Tracktion isolation
- callback-driven UI widgets
- top-level module separation

## Gradually reduce

- components directly calling engine methods
- components implementing engine listener logic
- wide public dependencies from UI to concrete audio implementation
- app windows/components acting as the main orchestration layer

---

## Suggested near-term roadmap

## Phase 1: preserve structure, improve flow

- keep `rock-hero-core`, `rock-hero-audio`, `rock-hero-ui`
- add `presenters/` under `rock-hero-ui`
- move editor orchestration out of components
- define small audio-facing interfaces

## Phase 2: isolate concrete audio better

- separate `api/` and `tracktion_impl/` inside `rock-hero-audio`
- update UI/presenters to depend on interfaces
- keep concrete wiring in the app target

## Phase 3: strengthen testability

- add presenter tests with fakes
- expand domain/use-case tests
- keep adapter tests targeted

## Phase 4: split targets where the codebase earns it

- introduce additional CMake targets only for mature submodules
- avoid target explosion before there is clear value

---

## Final recommendation

Yes, you can absolutely keep the current framework of:

- three primary module domains
- internal modular growth
- later sub-library extraction

That framework is compatible with a modular and automated-testable architecture.

The main architectural adjustment is not “create more libraries now.”

It is:

- keep the top-level domain structure
- make dependency direction stricter
- add a presenter/controller layer between JUCE views and audio/services
- split internal submodules conceptually first
- promote them to separate targets only when justified

That is the path I would recommend for RockHero.

---

## Practical summary

If you want one short version of the plan, it is this:

1. Keep the three top-level domains
2. Treat them as module boundaries now
3. Add internal submodules inside each domain
4. Move orchestration out of JUCE components into presenters/controllers
5. Depend on audio interfaces rather than concrete Tracktion-heavy types where possible
6. Test core first, presenters second, adapters third
7. Split into additional CMake targets only when the codebase has earned it

This gives you a clean growth path without a premature architecture explosion.
