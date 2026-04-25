# Testable Modular Architecture Refactor Plan

## Summary

Preserve Rock Hero's current three primary domains:

- `rock-hero-core`: framework-free domain logic, timing, scoring, editor/game rules, use
  cases, and test fixtures.
- `rock-hero-audio`: Tracktion/JUCE audio integration, transport, playback, waveform backend,
  plugin hosting, realtime infrastructure, and audio-facing ports.
- `rock-hero-ui`: presentation domain, including JUCE widgets/views plus framework-free
  presenters and view models.

This deliberately follows the direction in `rockhero_modular_cmake_plan.md`: use folders as
conceptual submodules first, then promote submodules into separate CMake targets only when there
is a real enforcement, testing, dependency, or API-stability reason.

The main architectural change is not a rewrite. It is tightening dependency direction so UI no
longer talks directly to the concrete audio engine and app targets do not accumulate workflow
logic.

## Deliberate Design Decisions

Preserve these current design decisions:

- Keep `rock-hero-core`, `rock-hero-audio`, and `rock-hero-ui` as the primary domains.
- Keep `rock-hero-core` free of JUCE, Tracktion, SDL, bgfx, hardware, real clocks, and threading.
- Keep Tracktion isolated behind `rock-hero-audio`.
- Keep editor and game as separate executables.
- Keep app targets as composition roots.
- Keep Catch2/CTest as the main automated test pipeline.
- Keep the current fallback strategy where Tracktion can be replaced by a raw JUCE
  implementation behind the audio boundary.
- Keep timing/scoring architecture centered on audio-derived transport time, not render/UI
  clocks.

Deliberately change these current or emerging design issues:

- Stop treating `rock-hero-ui` as a concrete audio-engine consumer.
- Remove `WaveformDisplay`'s direct dependency on `audio::Engine`.
- Remove editor component code that directly calls `audio::Engine::play`, `pause`, `stop`,
  `seek`, and `loadFile`.
- Replace concrete engine coupling with narrow project-owned interfaces.
- Move editor workflow and presentation state into framework-free presenter/view-model code.
- Update architecture docs later to allow `rock-hero-audio` and `rock-hero-ui` to depend on
  `rock-hero-core`; the current design doc says neither library depends on the other, but newer
  architectural principles already imply core should be the shared pure foundation.

## Target Structure

Use this conceptual layout first:

```text
libs/
  rock-hero-core/
    domain/
    timing/
    gameplay/
    editor_commands/
    ports/
    test_fixtures/
    tests/

  rock-hero-audio/
    api/
    playback/
    thumbnail/
    tracktion_impl/
    threading/
    tests/

  rock-hero-ui/
    presenters/      # no JUCE
    view_models/     # no JUCE
    widgets_juce/    # JUCE Components
    views_juce/      # screen composition
    tests/

apps/
  rock-hero-editor/
  rock-hero/
```

Initial CMake remains:

```text
rock_hero_core
rock_hero_audio
rock_hero_ui
```

Future subtargets may be added only when useful:

```text
rock_hero_audio_api
rock_hero_audio_tracktion
rock_hero_ui_presenters
rock_hero_ui_widgets_juce
rock_hero_core_gameplay
```

The first likely split should be `rock_hero_ui_presenters`, because it should not link JUCE and
should be unit-testable without GUI initialization.

## Dependency Direction

Use this dependency direction as the target rule:

```text
core <- audio_api <- audio_tracktion
core <- ui_presenters <- ui_widgets_juce
audio_api <- ui_presenters
apps -> concrete implementations
```

In compact form:

- `core` depends on no project-specific module and no framework.
- `audio` depends on `core`.
- `ui` depends on `core` and audio interfaces.
- `ui` should not depend on concrete Tracktion implementation.
- Apps compose concrete implementations with presenters and views.

This means presenters can depend on `core` data and `audio` ports, but not on Tracktion, raw
JUCE audio implementation details, or the concrete `audio::Engine` facade.

## Core Responsibilities

Put behavior in `rock-hero-core` when it would still exist without any GUI:

- Song, chart, arrangement, note, and package-domain rules.
- Chart validation and chart transforms.
- Timing math, beat/second/sample conversion, timeline mapping.
- Latency calibration math.
- Note matching, scoring, combo/streak rules.
- Replayable gameplay simulation.
- Editor command logic that is independent of screen layout.
- Test fixtures/builders for domain objects.

Do not put screen presenters in `core` just because they are pure C++. Presenter code is
presentation-specific and belongs under the UI domain.

## Audio Responsibilities

Keep `rock-hero-audio` as an adapter/infrastructure domain:

- Tracktion-backed transport.
- Backing track playback.
- Tone/plugin host integration.
- Waveform/thumbnail backend generation.
- Audio-thread handoff, atomics, lock-free queues.
- Translation between project-owned interfaces and Tracktion/JUCE.

Introduce narrow audio-facing interfaces as needed, not all at once:

```text
ITransport
IPlaybackSession
IWaveformSource
IWaveformSourceFactory
IPlaybackStateListener
IToneEngine
IPitchDetector
```

The existing `audio::Engine` can remain temporarily as the concrete Tracktion facade, but new
UI/presenter code should depend on smaller interfaces rather than the full engine.

## UI Responsibilities

Keep `rock-hero-ui` presentation-focused:

- JUCE widgets render state and emit intents.
- Framework-free presenters translate user intents into service/use-case calls.
- View models hold display-ready state such as enabled flags, labels, cursor position, and
  progress.
- JUCE views own layout and component composition only.

Recommended editor flow:

```text
Editor JUCE view
  -> emits intent
EditorPresenter
  -> calls core/audio ports
Audio/core state changes
  -> presenter maps to view model
Editor JUCE view
  -> renders updated state
```

Specific near-term changes:

- Extract `TransportViewModel` from `TransportControls`.
- Extract waveform click/position math from `WaveformDisplay`.
- Add `EditorPresenter`.
- Make `WaveformDisplay` accept a waveform/render state object and emit seek intent.
- Make `ContentComponent` wire view callbacks to the presenter instead of directly to
  `audio::Engine`.

## Testing Plan

Use the test pyramid defined in the design docs.

Primary tests:

- `rock-hero-core/tests`: pure domain, timing, scoring, chart, calibration, command, and
  simulation tests.
- `rock-hero-ui/tests`: non-JUCE presenter/view-model tests with fake audio/core ports.
- `rock-hero-audio/tests`: focused adapter tests around Tracktion-backed integration.

Selective tests:

- JUCE component tests only for synchronous callback wiring and layout.
- Snapshot/approval tests only when visuals stabilize.
- End-to-end JUCE app automation only later, once workflows are stable.

Avoid:

- Broad JUCE wrapper/mock layers.
- Mocking `juce::Component`, `juce::Graphics`, or Tracktion internals as the main test strategy.
- Tests that require real audio devices, plugin scanning, GPU windows, or message-loop timing
  unless explicitly categorized as integration/E2E.

For JUCE-aware tests, use a custom Catch2 main with `juce::ScopedJuceInitialiser_GUI`. Keep most
tests free of JUCE initialization.

## Refactoring Order

1. Extract pure calculations from current widgets.
   - Transport button state.
   - Play/pause/stop intent selection.
   - Waveform click-to-time mapping.
   - Cursor proportion calculation.

2. Add unit tests for those pure types.

3. Add audio-facing interfaces needed by the editor path only.
   - Start narrow: transport state, playback commands, load result, waveform duration/source.

4. Add `EditorPresenter`.
   - Handles load/play/pause/stop/seek intent.
   - Owns no JUCE widgets.
   - Talks to fakeable audio/core interfaces.
   - Emits view-state updates.

5. Refactor editor JUCE code to passive view wiring.
   - `ContentComponent` becomes layout and callback connection.
   - `WaveformDisplay` stops implementing `audio::Engine::Listener`.
   - `TransportControls` renders a view model instead of owning policy.

6. Keep concrete Tracktion wiring in the editor app target.
   - App constructs engine/adapters, presenter, and JUCE view.
   - App does not own workflow policy.

7. Expand core gameplay/timing/scoring once editor seam is clean.
   - Build replayable simulation before relying on app-level gameplay tests.

8. Promote submodules to CMake targets only when needed.
   - First candidate: `rock_hero_ui_presenters`.
   - Second candidate: `rock_hero_audio_api`.

## Reconsidered Alternatives

Aggressive library split now:

- Stronger enforcement immediately.
- But it creates CMake churn before the real boundaries are proven.
- Reject for now.

Separate fourth `application` top-level library:

- Clean in abstract architecture diagrams.
- But Rock Hero's current three domains already map well to the product.
- Presenter/application logic can live as non-JUCE UI and core use-case submodules.
- Reject as a top-level domain for now.

Keep current structure with no presenter layer:

- Lowest short-term cost.
- But it leaves the known coupling: UI directly owns workflow and concrete audio-engine calls.
- Reject because it will make automated testing worse as the editor grows.

Put presenters in `core`:

- Makes them easy to test.
- But it pollutes `core` with screen-specific state and UI concepts.
- Reject; presenters belong to UI domain, but should not depend on JUCE.

## Documentation Updates Needed Later

When implementation begins, update the canonical design docs to reflect these deliberate changes:

- `architecture.md`: clarify that the three primary domains remain, but internal submodules and
  future subtargets are expected.
- `architecture.md`: replace "neither library depends on the other" with a stricter dependency
  rule where `core` is the pure foundation and `audio`/`ui` may depend on it.
- `architecture.md`: clarify that `rock-hero-ui` contains both JUCE-facing widgets and
  framework-free presentation logic.
- `architectural-principles.md`: keep the current guidance, but add the three-domain submodule
  strategy and presenter placement rule.
- `documentation-conventions.md`: no architectural change needed; apply its Doxygen/comment rules to
  any new public headers.

## Assumptions

- The user prefers the three-domain model.
- The first practical refactor target is the editor path.
- The goal is automated testability, not maximum abstraction.
- CMake target splitting should follow demonstrated pressure, not precede it.
- Current design docs are authoritative unless this plan explicitly names a deliberate change.
