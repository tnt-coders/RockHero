# Testable Modular Architecture Refactor Plan (Revised v2)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised.md`. Review feedback is
in `testable-modular-architecture-refactor-plan-review-notes.md`. All seven points in the
review are accepted; this revision applies them. The intent and structure of v1 are
preserved — this is a patch pass, not a rewrite.

### Fixes Applied From the Review

1. **`juce::File` in presenter/port contracts.** Replaced with a project-owned
   `rock_hero::core::AudioAssetRef` type. `juce::File` stays at the JUCE view/app edge
   and is converted once at the boundary.
2. **`TransportState` placement.** The full snapshot moves to `rock-hero-audio/api/`.
   Reusable time values (`core::PlaybackPosition`, `core::PlaybackDuration`) live in
   `rock-hero-core/timing/`. The snapshot references the core types.
3. **`IWaveformSource` was not compile-only.** Scope corrected. The current
   `audio::Thumbnail` is already a Tracktion-free abstract interface — it is effectively
   the port we need. This plan consumes `audio::Thumbnail` directly from the UI via the
   existing drawing contract, and does not promise a conversion to a pure data-source
   port. That conversion is a separate plan with its own tripwire.
4. **View/audio dependency rule conflict.** Resolved with an explicit rule: JUCE widgets
   may depend on narrow, Tracktion-free audio ports (like `audio::Thumbnail`). Widgets
   may not depend on `audio::Engine` or Tracktion implementation types.
5. **Worked test assumed optimistic view updates.** Rewritten. The transport-state event
   is the source of truth for view state; intents never speculatively mutate view state.
6. **`architecture.md` dependency-line update timing.** Moved to the first commit that
   creates `rock-hero-audio -> rock-hero-core` (step 1, which adds the core time-value
   types and makes `ITransport` reference them).
7. **Dependency diagram was ambiguous.** Replaced with explicit dependency arrows and
   stated rules.

### Intent Preserved From v1

- Three library domains stay.
- Dependency direction tightens.
- Presenter layer is introduced for the editor path.
- CMake stays at three targets; splits are driven by tripwires.
- Ports for subsystems that don't exist yet (`IPitchDetector`, `IToneEngine`, etc.) are
  deferred with documented tripwires.
- `audio::Engine::Listener` is removed; UI stops inheriting from engine-specific
  listener types.

---

## Guiding Principle

> **Pay for structure now where deferring it forces an invasive later refactor. Defer
> structure where adding it later is a local, non-disruptive change.**

Structural commitments made now: dependency direction, the presenter pattern, the
`ITransport` port, project-owned boundary types. These shape every later addition.

Deferred commitments: CMake target names, interfaces for subsystems that don't yet
exist, conversion of the thumbnail into a pure data-source port. Each has an explicit
tripwire.

---

## Target Architecture

Dependency arrows (arrows point from depender to dependency):

```text
apps                 -> rock-hero-ui
apps                 -> rock-hero-audio (concrete)
rock-hero-ui         -> rock-hero-core
rock-hero-ui         -> rock-hero-audio (ports only, Tracktion-free)
rock-hero-audio      -> rock-hero-core
rock-hero-core       -> (no project-specific dependency)
```

Explicit rules:

- `rock-hero-core` depends on no project-specific module and no framework runtime.
  Standard C++ only.
- `rock-hero-audio` may depend on `rock-hero-core`.
- `rock-hero-ui` may depend on `rock-hero-core` and on Tracktion-free audio ports. It
  may not depend on `audio::Engine` or Tracktion types.
- Apps depend on concrete implementations and wire them together.

Internal layout inside each library uses folders as conceptual submodules. CMake targets
stay at three (`rock_hero_core`, `rock_hero_audio`, `rock_hero_ui`) until a promotion
tripwire fires.

---

## The Editor Seam — What Gets Added, Renamed, and Deleted

### Added

**`rock_hero::core::AudioAssetRef`** — project-owned reference to a loadable audio
asset. Lives in `rock-hero-core/domain/` or similar.

```cpp
namespace rock_hero::core
{
struct AudioAssetRef
{
    std::filesystem::path path;
};
} // namespace rock_hero::core
```

Today this is just a path. The type exists so later additions (format hints, asset IDs,
bundled-resource resolution) are non-breaking at call sites. `juce::File` conversion
happens once at the JUCE view/app edge.

**`rock_hero::core::PlaybackPosition`** and **`rock_hero::core::PlaybackDuration`** —
reusable time value types. Live in `rock-hero-core/timing/`.

```cpp
namespace rock_hero::core
{
struct PlaybackPosition
{
    double seconds{0.0};
};

struct PlaybackDuration
{
    double seconds{0.0};
};
} // namespace rock_hero::core
```

Gameplay, calibration, and scoring code will reuse these.

**`rock_hero::audio::TransportState`** — audio-transport snapshot. Lives in
`rock-hero-audio/api/` because it describes audio-layer state, not a pure domain
concept.

```cpp
namespace rock_hero::audio
{
struct TransportState
{
    bool file_loaded{false};
    bool playing{false};
    core::PlaybackPosition position;
    core::PlaybackDuration length;
};
} // namespace rock_hero::audio
```

**`rock_hero::audio::ITransport`** — narrow audio-transport port.

```cpp
namespace rock_hero::audio
{
class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual bool loadAudioAsset(const core::AudioAssetRef& asset) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(core::PlaybackPosition position) = 0;

    [[nodiscard]] virtual TransportState state() const = 0;

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void onTransportStateChanged(const TransportState& state) = 0;
    };

    virtual void addListener(Listener& listener) = 0;
    virtual void removeListener(Listener& listener) = 0;
};
} // namespace rock_hero::audio
```

One coarse event carrying a full `TransportState` snapshot. If a future profiling result
shows the position-update rate is a problem, splitting position into its own fine-grained
event is a local change.

**`rock_hero::ui::EditorPresenter`** — framework-free (no JUCE headers in the
implementation). Owns `audio::ITransport&` and an optional `audio::Thumbnail*`. Exposes
intent methods and an observable view-state stream.

- Intents: `onLoadAudioAssetRequested(const core::AudioAssetRef&)`,
  `onPlayPausePressed()`, `onStopPressed()`, `onWaveformClicked(double normalized_x)`.
- View sink: `attachSink(IEditorViewSink&)`.
- Implements `audio::ITransport::Listener`; maps `TransportState` into
  `EditorViewState` and pushes it to the attached sink.

**`rock_hero::ui::EditorViewState`** — plain data struct describing what the view must
render (enabled flags, play/pause icon choice, cursor proportion, loaded-asset label,
thumbnail handle).

**`rock_hero::ui::IEditorViewSink`** — single method, `setState(const EditorViewState&)`.

**Tests:** `libs/rock-hero-ui/tests/editor_presenter_tests.cpp` (no JUCE initialization),
plus fakes in `libs/rock-hero-ui/tests/fakes/`: `FakeTransport`, `FakeEditorViewSink`.

### Renamed or Relocated

- **`audio::Engine`** grows to implement `audio::ITransport`. Tracktion wiring stays
  internal. Dispatches to `ITransport::Listener` via a private bridge.
- **`audio::Thumbnail::setFile(const juce::File&)`** is renamed to
  **`audio::Thumbnail::setSource(const core::AudioAssetRef&)`**. The concrete
  Tracktion-backed implementation converts internally. This removes the remaining
  `juce::File` from the port layer. `Thumbnail` is still JUCE-aware because it draws
  into `juce::Graphics` — that's fine under the dependency rule.
- **`Engine::createThumbnail(juce::Component& owner)`** keeps its shape. The `owner`
  parameter is a JUCE-aware concern used by the adapter internally; it stays because
  the factory is called from the JUCE view during composition.

### Deleted

- **`audio::Engine::Listener`** (the public nested interface used by `WaveformDisplay`).
  Replaced by `audio::ITransport::Listener`.
- **`WaveformDisplay`'s `audio::Engine&` constructor parameter and its
  `audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member.** The widget
  takes an `audio::Thumbnail*` directly (the existing Tracktion-free port) and exposes
  `on_seek` as `std::function<void(double)>` emitting a normalized x. The presenter
  converts normalized x to `core::PlaybackPosition`.
- **`audio::ScopedListener`** — if `WaveformDisplay` was its only external user,
  delete it. If it remains useful as an audio-library-internal RAII helper for the
  engine's Tracktion bridge, retain it as an implementation detail.

### Composition After the Refactor

```cpp
// apps/rock-hero-editor/main_window.cpp
auto engine = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto thumbnail = engine->createThumbnail(*editor_view_component);
auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(
    *engine, thumbnail.get());
auto view = std::make_unique<rock_hero::ui::EditorView>(*presenter, thumbnail.get());
presenter->attachSink(*view);
```

The presenter sees `audio::ITransport&` and `audio::Thumbnail*`. The view sees the
presenter plus the Tracktion-free thumbnail port. Neither sees `audio::Engine` or
Tracktion types. The one `juce::File` conversion to `core::AudioAssetRef` happens in
the view's file-chooser callback before handing off to the presenter.

---

## Ports: Committed Now vs. Deferred

### Introduced now (the editor path needs them)

- `rock_hero::audio::ITransport`
- `rock_hero::audio::Thumbnail` already exists and fills the waveform-port role;
  renaming it into an `I*`-prefixed form would be churn without benefit.

### Deferred — introduce when the subsystem lands

| Interface | Tripwire |
|---|---|
| `IToneEngine` | First concrete effects/tone chain exists and a second consumer (editor preview + game) needs to drive it. |
| `IPitchDetector` | First pitch/onset detector implementation exists and core gameplay code needs to consume detections. |
| `IPluginHost` | Plugin scanning/loading exists and is invoked from more than one place. |
| `IClock` | Core code references wall-clock time. Likely never needed — architectural principles say inject transport position, not wall clock. |
| `ISongRepository` | Persistence policy needs to vary (filesystem, bundled resources, network) or needs fakes in tests. |
| `IScoringInputStream` | Gameplay subsystem exists and consumes a stream of player onsets separately from the raw detector. |
| `IWaveformDataSource` (pure data, no drawing) | UI needs to render the waveform in a non-JUCE context, or the UI renderer changes away from `juce::Graphics`. |

The tripwires matter because an interface introduced before its implementation encodes a
guessed contract. Pressure to refactor comes from interfaces that don't match reality,
not from ones that don't exist yet.

---

## Refactor Order

Each step is independently compilable; tests for step 4 do not wait on step 5.

1. **Add `core::PlaybackPosition`, `core::PlaybackDuration`, `core::AudioAssetRef`.
   Define `audio::TransportState`, `audio::ITransport`, `audio::ITransport::Listener`.
   Update `architecture.md`'s dependency-line wording in the same commit.** This is the
   step where `rock-hero-audio -> rock-hero-core` becomes true. Nothing else changes yet.

2. **Make `audio::Engine` implement `audio::ITransport`.** Keep Tracktion wiring
   internal. During transition the engine dispatches to both `Engine::Listener` (old)
   and `ITransport::Listener` (new).

3. **Rename `audio::Thumbnail::setFile(const juce::File&)` to
   `audio::Thumbnail::setSource(const core::AudioAssetRef&)`** and update the concrete
   Tracktion-backed implementation plus all existing call sites.

4. **Add `ui::EditorPresenter`, `ui::EditorViewState`, `ui::IEditorViewSink` with
   unit tests.** `FakeTransport` and `FakeEditorViewSink` live in the test target. This
   lands the testing story before any JUCE widget changes.

5. **Refactor `WaveformDisplay`** to take `audio::Thumbnail*` and an `on_seek`
   callback emitting normalized x. Remove its engine listener. Remove
   `audio::Engine::Listener` inheritance.

6. **Refactor `TransportControls`** to render from `EditorViewState`. Remove its
   internal state machine (`updateButtonStates`, cached `m_is_playing`,
   `m_file_loaded`, `m_transport_position`).

7. **Rewire `main_window`** to compose `Engine` → `ITransport&` →
   `EditorPresenter` → `EditorView`. Delete `audio::Engine::Listener`. Delete
   `audio::ScopedListener` if no remaining users.

---

## Testing: One Worked Example

`TransportState` is authoritative for view state; intents never speculatively mutate
view state. The test shape reflects that.

```cpp
TEST_CASE("EditorPresenter: play intent calls transport when a file is loaded")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport, nullptr};
    presenter.attachSink(sink);

    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 1);
    // View state does not reflect playing until the transport reports it.
    REQUIRE(sink.last_state.play_pause_shows_pause_icon == false);

    transport.simulateState({.file_loaded = true,
                             .playing = true,
                             .position = {0.0},
                             .length = {10.0}});

    REQUIRE(sink.last_state.play_pause_shows_pause_icon == true);
}

TEST_CASE("EditorPresenter: play intent is ignored when no file is loaded")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport, nullptr};
    presenter.attachSink(sink);

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 0);
}

TEST_CASE("EditorPresenter: waveform click at 0.25 seeks to 25% of length")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport, nullptr};
    presenter.attachSink(sink);

    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {12.0}});

    presenter.onWaveformClicked(0.25);

    REQUIRE(transport.last_seek.seconds == Approx(3.0));
}
```

No JUCE init. No Tracktion runtime. No audio device. These tests cover rules currently
encoded across `TransportControls::updateButtonStates()`, `WaveformDisplay::mouseDown()`,
and glue in `ContentComponent`/`main_window`. After the refactor the rules are observable
from outside, and the game code can consume the same presenter pattern when it needs
similar transport semantics.

---

## CMake Evolution: Tripwires, Not Target Names

Keep `rock_hero_core`, `rock_hero_audio`, `rock_hero_ui`. Promote a submodule to its
own target when **any one** of these tripwires fires:

1. **Independent test surface needed** — the submodule has enough tests that mixing
   them into the parent library's test target hurts build times or triage.
2. **Different linkage requirements** — e.g., the presenter submodule must not link
   JUCE because a headless tool or server-side target consumes it.
3. **Alternative implementation swap** — e.g., Tracktion-backed audio vs. raw-JUCE
   audio chosen at link time.
4. **API stability contract** — another repository or a long-lived external consumer
   needs a narrower versioned surface than the full library.
5. **Build-time compartmentalization** — editing the submodule forces rebuilding
   unrelated code in the same library, and the wall-clock cost is material.

This plan does not pre-commit target names; names follow the split.

Likely first split, based on pressures this plan creates: whichever target houses the
presenters, because they should build without JUCE (tripwire #2). No date committed.

---

## Explicit Anti-Scope

This plan does **not**:

- Introduce `IPitchDetector`, `IToneEngine`, `IPluginHost`, `IClock`,
  `ISongRepository`, `IScoringInputStream`, or `IWaveformDataSource`.
- Convert `audio::Thumbnail` into a pure-data waveform source.
- Add a fourth top-level library or an `application` domain.
- Move gameplay, scoring, or calibration logic into dedicated modules.
- Split CMake targets.
- Rewrite `architecture.md` beyond the dependency-line correction in step 1.
- Add snapshot, approval, or end-to-end tests.
- Introduce a replayable simulation harness (belongs to the gameplay-layer plan).

None of these are blocked by this plan. They become *easier* because the dependency
direction and presenter pattern are in place.

---

## Why This Is the Scalable Choice

The moves that make growth painful:

- Concrete-engine references spreading across UI code. *Addressed: `ITransport`.*
- Workflow accreting in JUCE components. *Addressed: presenter owns workflow.*
- `core` acquiring framework dependencies. *Addressed: direction tightened.*
- App targets accumulating business logic. *Addressed: composition only.*
- Framework types (`juce::File`) leaking into project boundaries. *Addressed:
  `core::AudioAssetRef`, `core::PlaybackPosition`, `core::PlaybackDuration`.*
- Time and threading reaching into domain code. *Reinforced by principle; specific
  enforcement comes as gameplay lands and the replayable-simulation plan follows.*

The moves that *don't* make growth painful, even if deferred:

- Choosing precise names for future CMake targets.
- Pre-declaring interfaces for subsystems that don't exist yet.
- Converting an already-Tracktion-free drawing adapter into a pure data source.
- Extracting one-line pure calculations into standalone types.

This plan commits to the first list and defers the second. When the pitch detector
arrives, its interface is added in the same PR as its first implementation, against a
presenter/gameplay layer already shaped to consume ports. That is a codebase that
scales without traumatic refactors — one where the *skeleton* is right and the *organs*
grow into it.

---

## Supersession

Once this plan is agreed, remove from `docs/in-progress/` (or archive):

- `testable-modular-architecture-refactor-plan.md` — original, superseded.
- `testable-modular-architecture-refactor-plan-revised.md` — v1 revision, superseded by
  this v2.
- `testable-modular-architecture-refactor-plan-review-notes.md` — review feedback has
  been applied; artifact can be archived.
- `rockhero_modular_cmake_plan.md` — content folded into this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing for
the refactor described here.
