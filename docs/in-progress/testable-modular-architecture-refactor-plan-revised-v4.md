# Testable Modular Architecture Refactor Plan (Revised v4)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v3.md`. Review feedback
is in `testable-modular-architecture-refactor-plan-v3-review-notes.md`. All four points
are accepted; this revision applies them. v3's structure is preserved — patches are
targeted.

### Fixes Applied In This Revision

1. **Thumbnail lifetime in the composition sketch.** v3 passed a raw pointer from a
   thumbnail local into the view. Local destruction order would have destroyed the
   thumbnail before the view, leaving a dangling pointer. Fixed by transferring
   ownership of the thumbnail into the view (which already matches how
   `WaveformDisplay` holds the thumbnail today).
2. **"Not fakeable" was too absolute.** v3 said `audio::Thumbnail` has "no fake, no
   substitute, and no unit-testing story." Softened: it is not a *primary* unit-test
   seam and presenter tests must not depend on it, but a narrow fake is possible for
   focused JUCE component tests if useful.
3. **Thumbnail creation rule stated explicitly.** v3 implied but did not state that
   `Engine::createThumbnail(...)` may only be called from the app composition root.
   This rule is now explicit, so UI code cannot drift back toward including
   `<rock_hero/audio/engine.h>`.
4. **`Thumbnail::setSource` is another `audio -> core` dependency.** Called out
   alongside the one introduced by `ITransport` in step 1, so the dependency-line
   correction in `architecture.md` has the complete motivation.

### Intent Preserved From v3

- Port vs. translation-adapter distinction.
- `ITransport` is a genuine port with a single coarse state event.
- `audio::Thumbnail` is a translation adapter, routed directly from engine to the view
  without passing through the presenter.
- `EditorPresenter` takes only `audio::ITransport&`; click-to-time math uses
  `TransportState.length`.
- `core::AudioAsset`, `core::PlaybackPosition`, `core::PlaybackDuration` live in core.
- `audio::TransportState` lives in `rock-hero-audio/api/`.
- `architecture.md` dependency-line update lands in step 1.
- CMake splits are tripwire-driven.
- Deferred ports stay deferred, each with a tripwire.

---

## Terminology: Port vs. Translation Adapter

This distinction is load-bearing for the plan. Both hide framework types from the UI,
but they serve different roles.

**Port** — a narrow, project-owned interface at a boundary, designed so that:

- production code and tests can both implement it,
- multiple concrete implementations are plausible (e.g., Tracktion-backed vs. raw-JUCE),
- presenters and domain code can be unit-tested against hand-written fakes.

`audio::ITransport` is a port. Its contract (play/pause/stop/seek + state snapshot) is
small, independent of any specific framework, and naturally fakeable.

**Translation adapter** — an abstract base class whose purpose is to keep framework
types out of consuming headers, but which:

- has only one realistic production implementation (the framework-backed one),
- is bound at construction to framework-owned runtime state (an engine, a component),
- is not the primary unit-test seam.

`audio::Thumbnail` is a translation adapter. Its abstract header forward-declares
`juce::File`, `juce::Graphics`, `juce::Rectangle` so `waveform_display.cpp` does not
pull Tracktion in. The concrete `TracktionThumbnail` holds a `tracktion::Engine&` from
`audio::Engine`, runs Tracktion's async `SmartThumbnail` caching, and repaints a
specific JUCE component bound at construction.

`audio::Thumbnail` is not a framework-free presenter seam. Presenter tests must not
depend on it. A narrow fake is possible for focused JUCE component wiring or layout
tests (the interface is abstract, after all), but such tests are not how this
refactor's testing story is structured. The primary unit-test seam for the editor flow
is `ITransport`.

The plan treats ports and translation adapters asymmetrically. Ports carry the
testability contract. Translation adapters earn their existence by keeping framework
headers out of UI translation units.

---

## Guiding Principle

> **Pay for structure now where deferring it forces an invasive later refactor. Defer
> structure where adding it later is a local, non-disruptive change.**

---

## Target Architecture

Dependency arrows (arrows point from depender to dependency):

```text
apps            -> rock-hero-ui
apps            -> rock-hero-audio (concrete)
rock-hero-ui    -> rock-hero-core
rock-hero-ui    -> rock-hero-audio (ports + translation adapters, Tracktion-free)
rock-hero-audio -> rock-hero-core
rock-hero-core  -> (no project-specific dependency)
```

Explicit rules:

- `rock-hero-core` depends on no project-specific module and no framework runtime.
  Standard C++ only.
- `rock-hero-audio` may depend on `rock-hero-core`.
- `rock-hero-ui` may depend on `rock-hero-core` and on `rock-hero-audio`'s
  Tracktion-free headers (ports and translation adapters). It may not depend on
  `audio::Engine` or on Tracktion types.
- Apps depend on concrete implementations and wire them together.

---

## The Editor Seam — What Gets Added, Renamed, and Deleted

### Added

**`rock_hero::core::AudioAsset`** — project-owned reference to a loadable audio asset.
Lives in `rock-hero-core/domain/`.

```cpp
namespace rock_hero::core
{
struct AudioAsset
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

**`rock_hero::audio::TransportState`** — audio-transport snapshot. Lives in
`rock-hero-audio/api/`.

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

**`rock_hero::audio::ITransport`** — the editor's audio-transport port.

```cpp
namespace rock_hero::audio
{
class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual bool loadAudioAsset(const core::AudioAsset& asset) = 0;
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

One coarse event carrying a full `TransportState` snapshot. If later profiling shows
the position-update rate is a problem, splitting position out into its own event is a
local change.

**`rock_hero::ui::EditorPresenter`** — framework-free (no JUCE headers in the
implementation file). Owns an `audio::ITransport&`. No thumbnail — click-to-time math
uses `TransportState.length`.

- Intents: `onLoadAudioAssetRequested(const core::AudioAsset& asset)`,
  `onPlayPausePressed()`, `onStopPressed()`, `onWaveformClicked(double normalized_x)`.
- View sink: `attachSink(IEditorViewSink&)`.
- Implements `audio::ITransport::Listener`; maps `TransportState` into
  `EditorViewState` and pushes it to the attached sink.

**`rock_hero::ui::EditorViewState`** — plain data struct describing what the view must
render (enabled flags, play/pause icon choice, cursor proportion, loaded-asset label).
Does **not** carry the thumbnail handle — the view owns its thumbnail independently.

**`rock_hero::ui::IEditorViewSink`** — single method, `setState(const EditorViewState&)`.

**Thumbnail ownership API on the view:**

```cpp
namespace rock_hero::ui
{
class EditorView
{
public:
    // Transfers ownership; the view keeps the thumbnail alive for its own lifetime.
    // Callers must ensure audio::Engine outlives this view.
    void attachThumbnail(std::unique_ptr<audio::Thumbnail> thumbnail);

    // Exposes the component that Tracktion's async proxy-ready callback must repaint.
    [[nodiscard]] juce::Component& waveformComponent();
    // ...
};
} // namespace rock_hero::ui
```

Internally, `EditorView::attachThumbnail` forwards ownership to its
`WaveformDisplay` member, which stores it as `std::unique_ptr<audio::Thumbnail>` —
matching how the widget holds it today (waveform_display.h:77).

**Tests:** `libs/rock-hero-ui/tests/editor_presenter_tests.cpp` with `FakeTransport`
and `FakeEditorViewSink` in `libs/rock-hero-ui/tests/fakes/`. No JUCE init. No thumbnail
fake — the presenter never touches a thumbnail.

### Renamed or Relocated

- **`audio::Engine`** grows to implement `audio::ITransport`. Tracktion wiring stays
  internal. Dispatches to `ITransport::Listener` via a private bridge.
- **`audio::Thumbnail::setFile(const juce::File&)`** is renamed to
  **`audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. The concrete
  Tracktion-backed implementation converts to `juce::File` internally. `Thumbnail` is
  still JUCE-aware because it draws into `juce::Graphics` — that remains fine under
  the dependency rule; it's the translation-adapter's job.
- **`Engine::createThumbnail(juce::Component& owner)`** keeps its shape. The `owner`
  parameter stays because Tracktion's async proxy-ready callback repaints it directly.
  **Rule:** this factory is called only from the app composition root. UI code must
  not call it.

### Deleted

- **`audio::Engine::Listener`** (the public nested interface used by
  `WaveformDisplay`). Replaced by `audio::ITransport::Listener`.
- **`WaveformDisplay`'s `audio::Engine&` constructor parameter** and its
  `audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member. The widget
  keeps its existing `std::unique_ptr<audio::Thumbnail>` member but receives the
  thumbnail via `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` instead of
  constructing it internally. `on_seek` is exposed as
  `std::function<void(double)>` emitting a normalized x; the presenter converts to
  `core::PlaybackPosition`.
- **`audio::ScopedListener`** — if `WaveformDisplay` was its only external user,
  delete it. If it remains useful as an audio-library-internal RAII helper, retain it
  as an implementation detail.

### Composition After the Refactor

The thumbnail is created by the engine and its ownership is transferred into the view
in a single expression, so no raw thumbnail pointer ever lives as a composition-root
local.

```cpp
// apps/rock-hero-editor/main_window.cpp
auto engine = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto view   = std::make_unique<rock_hero::ui::EditorView>();

// Thumbnail ownership moves straight into the view.
view->attachThumbnail(engine->createThumbnail(view->waveformComponent()));

auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(*engine);
presenter->attachSink(*view);
view->setPresenter(*presenter);
```

In `MainWindow`, the members must be declared in the order `engine`, `view`,
`presenter` so destruction runs presenter → view (which destroys the thumbnail) →
engine. The engine outlives every thumbnail it created.

The presenter sees an `audio::ITransport&` and nothing else from the audio library.
The view owns the thumbnail. Neither includes `<rock_hero/audio/engine.h>`; only the
app target does.

### Lifetime Rules

Stated explicitly so later changes can't silently violate them:

1. `audio::Thumbnail` instances must not outlive the `audio::Engine` that created
   them. The concrete `TracktionThumbnail` holds a `tracktion::Engine&` into the
   engine's runtime; destroying the engine invalidates every thumbnail.
2. `EditorView` (via its `WaveformDisplay` member) owns every thumbnail passed to it
   and destroys it during view teardown.
3. The app/window composition root must destroy the view before the engine. This is
   enforced by declaring the engine member before the view member in `MainWindow`.
4. `Engine::createThumbnail(...)` is called only from the app composition root. UI
   code must not call it and must not include `<rock_hero/audio/engine.h>`.

---

## Ports vs. Translation Adapters: Current Inventory

### Ports (genuine boundaries, fakeable, primary test seams)

- `rock_hero::audio::ITransport`

### Translation adapters (Tracktion-free headers, not primary test seams)

- `rock_hero::audio::Thumbnail` — hides Tracktion from the UI translation unit.
  Bound to an `audio::Engine` and a `juce::Component` at construction. A narrow fake
  could be written for focused JUCE component tests, but the testing story does not
  depend on one.

### Deferred ports — introduce when the subsystem lands

Each has a tripwire: the condition that justifies introducing the interface.

| Port | Tripwire |
|---|---|
| `IToneEngine` | First concrete effects/tone chain exists and a second consumer (editor preview + game) needs to drive it. |
| `IPitchDetector` | First pitch/onset detector implementation exists and core gameplay code needs to consume detections. |
| `IPluginHost` | Plugin scanning/loading exists and is invoked from more than one place. |
| `IClock` | Core code references wall-clock time. Likely never needed — architectural principles say inject transport position, not wall clock. |
| `ISongRepository` | Persistence policy needs to vary (filesystem, bundled resources, network) or needs fakes in tests. |
| `IScoringInputStream` | Gameplay subsystem exists and consumes a stream of player onsets separately from the raw detector. |
| `IWaveformDataSource` (pure data, no drawing) | UI needs to render the waveform in a non-JUCE context, or the UI renderer changes away from `juce::Graphics`. Would convert the drawing-oriented `Thumbnail` adapter into a data-source port. |

---

## Refactor Order

Each step is independently compilable; tests for step 4 do not wait on step 5.

1. **Add `core::PlaybackPosition`, `core::PlaybackDuration`, `core::AudioAsset`.
   Define `audio::TransportState`, `audio::ITransport`, `audio::ITransport::Listener`.
   Update `architecture.md`'s dependency-line wording in the same commit.** This is
   the step where `rock-hero-audio -> rock-hero-core` becomes true, driven by
   `ITransport` referencing `core::PlaybackPosition`, `core::PlaybackDuration`, and
   `core::AudioAsset`.

2. **Make `audio::Engine` implement `audio::ITransport`.** Keep Tracktion wiring
   internal. During transition the engine dispatches to both `Engine::Listener`
   (legacy) and `ITransport::Listener` (new).

3. **Rename `audio::Thumbnail::setFile(const juce::File&)` to
   `audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. Update
   `TracktionThumbnail` and all call sites. This adds a second reason `rock-hero-audio`
   depends on `rock-hero-core` (alongside `ITransport` from step 1); the
   dependency-line correction in `architecture.md` already covers it, so no further
   doc edit is required here.

4. **Add `ui::EditorPresenter`, `ui::EditorViewState`, `ui::IEditorViewSink` with
   unit tests.** `FakeTransport` and `FakeEditorViewSink` live in the test target.
   This lands the testing story before any JUCE widget changes.

5. **Refactor `WaveformDisplay`** to receive its `audio::Thumbnail` via
   `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` rather than constructing one
   from an `audio::Engine&`. Expose `on_seek` as `std::function<void(double)>`
   emitting a normalized x. Remove the engine listener and `audio::Engine::Listener`
   inheritance.

6. **Refactor `TransportControls`** to render from `EditorViewState`. Remove its
   internal state machine (`updateButtonStates`, cached `m_is_playing`,
   `m_file_loaded`, `m_transport_position`).

7. **Rewire `main_window`** to compose `Engine` → `ITransport&` →
   `EditorPresenter` → `EditorView`, with the thumbnail transferred into the view in
   one expression (see *Composition After the Refactor*). Verify `MainWindow` member
   order puts the engine before the view. Delete `audio::Engine::Listener`. Delete
   `audio::ScopedListener` if no remaining users.

---

## Testing: One Worked Example

`TransportState` is authoritative for view state; intents never speculatively mutate
view state.

```cpp
TEST_CASE("EditorPresenter: play intent calls transport when a file is loaded")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 1);
    // View state does not flip until the transport reports playing.
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
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 0);
}

TEST_CASE("EditorPresenter: waveform click at 0.25 seeks to 25% of length")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {12.0}});

    presenter.onWaveformClicked(0.25);

    REQUIRE(transport.last_seek.seconds == Approx(3.0));
}
```

No JUCE init. No Tracktion runtime. No thumbnail fake — the presenter never references
a thumbnail. Length comes from the transport-state event.

---

## CMake Evolution: Tripwires, Not Target Names

Keep `rock_hero_core`, `rock_hero_audio`, `rock_hero_ui`. Promote a submodule to its
own target when **any one** of these tripwires fires:

1. **Independent test surface needed** — enough tests in the submodule that mixing
   them into the parent library's test target hurts build times or triage.
2. **Different linkage requirements** — e.g., presenters must not link JUCE because a
   headless tool or server-side target consumes them.
3. **Alternative implementation swap** — e.g., Tracktion-backed vs. raw-JUCE audio at
   link time.
4. **API stability contract** — an external consumer needs a narrower versioned
   surface than the full library.
5. **Build-time compartmentalization** — editing the submodule forces rebuilding
   unrelated code in the same library, with material wall-clock cost.

No names are pre-committed; names follow the split.

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

The moves that make later growth painful:

- Concrete-engine references spreading across UI code. *Addressed: `ITransport`.*
- Workflow accreting in JUCE components. *Addressed: presenter owns workflow.*
- `core` acquiring framework dependencies. *Addressed: direction tightened.*
- App targets accumulating business logic. *Addressed: composition only.*
- Framework types (`juce::File`) leaking into project boundaries. *Addressed:
  `core::AudioAsset`, `core::PlaybackPosition`, `core::PlaybackDuration`.*
- Conflating ports with translation adapters so test fakes are expected where none
  are practical. *Addressed: inventory separates the two.*
- Raw-pointer lifetime coupling across ownership boundaries. *Addressed: thumbnail
  ownership transferred into the view as `std::unique_ptr`, with written lifetime
  rules.*

The moves that *don't* make growth painful, even if deferred:

- Choosing precise names for future CMake targets.
- Pre-declaring interfaces for subsystems that don't exist yet.
- Converting an already-Tracktion-free drawing adapter into a pure data source.
- Extracting one-line pure calculations into standalone types.

When the pitch detector arrives, its interface is added in the same PR as its first
implementation, against a presenter/gameplay layer already shaped to consume ports.

---

## Supersession

Once this plan is agreed, remove from `docs/in-progress/` (or archive):

- `testable-modular-architecture-refactor-plan.md` — original, superseded.
- `testable-modular-architecture-refactor-plan-revised.md` — v1, superseded.
- `testable-modular-architecture-refactor-plan-revised-v2.md` — v2, superseded.
- `testable-modular-architecture-refactor-plan-revised-v3.md` — v3, superseded by
  this v4.
- `testable-modular-architecture-refactor-plan-review-notes.md` — feedback applied.
- `testable-modular-architecture-refactor-plan-v3-review-notes.md` — feedback
  applied.
- `rockhero_modular_cmake_plan.md` — content folded into this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.
