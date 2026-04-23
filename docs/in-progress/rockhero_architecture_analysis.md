# RockHero architecture analysis

Yes — **partly**. RockHero already has some of the right boundaries for a modular, testable JUCE app, but it is **not yet aligned cleanly with the MVVM-ish approach I suggested**.

The short version is: **the repo has good package-level separation, but still has too much direct UI ↔ engine coupling inside the editor path.**

## What it’s already doing well

At the top level, the project is split into:

- `libs/rock-hero-audio`
- `libs/rock-hero-core`
- `libs/rock-hero-ui`
- `apps/rock-hero`
- `apps/rock-hero-editor`

That is a strong starting point for modularity.

The **best part** architecturally is `rock-hero-core`:

- it is a standalone static library
- it depends only on standard C++
- it does not depend on JUCE or Tracktion
- it already has its own `tests` directory with Catch2-based tests discovered through CTest

That is exactly the sort of domain layer you want if automated testing matters.

The audio library also shows good instincts:

- it keeps Tracktion inside implementation files
- it exposes a pimpl-style `Engine`
- it provides a Tracktion-free `Thumbnail` abstraction so UI code does not need to include Tracktion headers directly

That is a real adapter boundary, not just a folder name.

## Where it does **not** fit the approach well yet

The main problem is that **the UI library still depends on the audio engine too directly**.

In CMake, `rock_hero_ui` links **publicly** against `rock_hero_audio`, and `WaveformDisplay` directly implements `audio::Engine::Listener` and is constructed with an `audio::Engine&`.

That means the UI layer is not really just “view”; it knows about engine events and lifetimes.

The editor app reinforces that coupling. Its `ContentComponent` wires button callbacks straight to:

- `play()`
- `pause()`
- `stop()`
- `seek()`
- `loadFile()`

The source comment literally says it is wiring controls directly to the audio engine while editor command services are absent.

The `MainWindow` also owns both the engine and the content component directly.

That is practical for early development, but it is closer to “smart view” than to MVVM or presenter separation.

The game app is even earlier: `apps/rock-hero/main.cpp` is currently just a temporary JUCE shell window, with a comment saying it exists until SDL/bgfx gameplay content owns the view.

So the current game target is not yet an example of a scalable modular UI architecture one way or the other.

## Verdict

The repository has **good bones** for the approach, but it is **not there yet**.

The existing split is already much better than a monolithic JUCE app because it separates:

- domain
- audio
- UI
- editor app
- game app

It also already proves you can unit test the pure core layer.

But the present editor/UI flow will become harder to test once more behavior accumulates, because the UI layer currently owns too much orchestration.

## What I would use instead

I would not throw this structure away.

I would **evolve it into a Clean Architecture / Hexagonal Architecture layout with passive JUCE views**.

That gives you almost the same practical benefits as MVVM, but it fits a realtime C++ / JUCE app better:

- **Core/domain**: pure song/chart/session data and rules
- **Application layer**: use-cases, presenters, session coordinators
- **Adapters**
  - JUCE UI adapter
  - Tracktion audio adapter
  - file persistence adapter
  - future SDL/bgfx renderer adapter
- **Apps**: thin composition roots for editor and game

This is very close to what the repo is already hinting at with `core`, `audio`, and `ui`; the change is mainly to stop letting the UI talk to the engine directly.

## Proposed structure

```text
libs/
  rock-hero-domain/         // Song, Chart, NoteEvent, session model
  rock-hero-application/    // presenters/use-cases/controllers
  rock-hero-audio-api/      // ITransport, IWaveformSource, IToneEngine
  rock-hero-audio-tracktion // Tracktion implementation of those interfaces
  rock-hero-ui-juce         // JUCE Components only
  rock-hero-renderer-api    // IGameplayView / IHighwayRenderer
  rock-hero-renderer-bgfx   // bgfx/SDL implementation

apps/
  rock-hero-editor
  rock-hero-game
```

## How that maps to the current repo

Keep `rock-hero-core` as the seed of the domain layer. It is already pure C++ and already tested.

Keep the good part of `rock-hero-audio`: the Tracktion isolation and thumbnail abstraction.

But split it into:

- an **audio API** module with small interfaces
- a **Tracktion adapter** module with the current implementation details

Right now the `Engine` abstraction is useful, but it still exposes JUCE `File` and engine-specific listener mechanics directly from the adapter boundary.

That is acceptable for now, but for stronger testability I would prefer smaller ports like:

- `ITransport`
- `IAudioFileLoader`
- `IWaveformSourceFactory`

Refactor `rock-hero-ui` so it becomes **truly passive**.

`TransportControls` is already close, because it is callback-based and explicitly says it has no knowledge of the audio engine. That is great.

`WaveformDisplay` is the one that breaks the pattern, because it directly listens to `audio::Engine`.

I would move that listener/coordinator behavior into a presenter or controller and let `WaveformDisplay` only:

- render state
- emit user intent such as `on_seek`

## The specific change I would make first

Introduce an **EditorPresenter** or **EditorController** between `ContentComponent` and the engine.

Today the component does this:

- click load → call engine
- click play/pause/stop → call engine
- waveform seek → call engine
- engine callback → mutate controls

That all works, but it makes the component hard to unit test without a live engine or a substantial fake.

Instead:

- `TransportControls` and `WaveformDisplay` stay dumb
- `EditorPresenter` owns an `ITransport` and `IWaveformSource`
- the view exposes methods like:
  - `setTransportState(...)`
  - `setWaveformProgress(...)`
  - `setErrorMessage(...)`
- the presenter handles:
  - play/pause/stop intent
  - file load intent
  - engine event subscription
  - state translation

That lets you test almost all editor behavior with plain fake objects.

## What “modular and automated testable” should look like here

I’d aim for three test layers:

### 1. Domain tests

Keep doing what `rock-hero-core` already does with Catch2.

That is your highest-value, lowest-cost test layer.

### 2. Application / presenter tests

Add tests for things like:

- pressing play when no file is loaded
- loading a file updates view state
- end-of-file transitions update play/pause/stop state
- seeking updates transport request correctly

These should run without JUCE windows and without Tracktion.

### 3. Adapter integration tests

A smaller set that exercises:

- Tracktion audio adapter loads files
- thumbnail generation works
- JUCE components repaint and route callbacks

Those are slower and fewer.

## Bottom line

**RockHero does lend itself to the direction you want, but only after one important shift:**

Move orchestration out of JUCE components and into a separate application layer.

So my recommendation is:

- **Keep** the current multi-library split
- **Keep** `rock-hero-core` pure
- **Keep** Tracktion hidden behind adapters
- **Change** `rock-hero-ui` into passive JUCE views
- **Add** presenters/use-cases between UI and audio
- **Compose** everything in `apps/rock-hero-editor` and `apps/rock-hero`

That will give you a codebase that is:

- modular
- easier to swap implementations in
- more testable without booting JUCE
- still very natural for a C++ audio/game app
