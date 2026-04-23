# Testable Modular Architecture Refactor Plan (Revised v11)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v10.md`. v11 fixes one
implementation-contract ambiguity in the presenter's cached-state rule. No architectural
moves change.

### Fix Introduced in v11

1. **Presenter cache seeding is explicit.** v10 said `attachSink` pushes the cached
   `EditorViewState`, while also testing that a presenter constructed after
   `transport.set_state(...)` immediately reflects that state on first attach. v11
   resolves the ambiguity: `EditorPresenter` seeds its cached `EditorViewState` from
   `transport.state()` in the constructor; listener callbacks update the cache;
   `attachSink` flushes the cache.

### Fixes Preserved From v10

1. **Composition sketch wires `setPresenter` before `attachSink`.** v9 called
   `attachSink` first, which triggered the presenter's initial `EditorViewState` push
   while `EditorView::m_presenter` was still `nullptr`. Nothing crashed (the sink is a
   pure render path), but the order relied on an unspoken rule. v10 swaps the last two
   lines so the view always has a presenter when it first sees state.
2. **Presenter tolerates a null sink.** Explicit rule: the presenter caches the most
   recent derived `EditorViewState` and its `onTransportStateChanged` path no-ops the
   sink push when no sink is attached. `attachSink` immediately flushes the cached
   state. This removes a timing dependency on "no transport events between ctor and
   `attachSink`."
3. **`TransportControls` state-propagation mechanism named.** `TransportControls`
   exposes a single `setState(const TransportControlsState&)` where
   `TransportControlsState` is a small struct (enabled flags + play/pause icon).
   `EditorView::setState` projects the relevant fields of `EditorViewState` into a
   `TransportControlsState` and forwards it. This replaces today's per-field setters.
4. **Failed-load test strengthens its assertions.** The v9 test asserted
   `play_pause_enabled == false` after a failed load, which was already false from the
   default initial state. v10 seeds a loaded state first, then runs a failed
   replacement load, and asserts (a) the error is surfaced and (b) the prior
   loaded-state view flags are restored on the next state event. A follow-up assertion
   covers `last_load_error` clearing on the next successful load.

### Fixes Preserved From v9

1. **Legacy `Engine` method deletion scoped to non-interface back doors.** Public
   `ITransport` overrides (`play`, `pause`, `stop`, `seek(core::PlaybackPosition)`,
   `state`, `loadAudioAsset`, listener registration) are retained. Only the legacy
   overloads (`loadFile(juce::File&)`, `seek(double)`, `isPlaying`,
   `getTransportPosition`) are removed.
2. **`MainWindow` member order is `engine, presenter, view`** so destruction runs
   `view -> presenter -> engine` and no raw pointer held by the view outlives its
   referent.

### Fixes Preserved From v8

1. **`EditorView` presenter-binding consistency.** Stored as
   `EditorPresenter* m_presenter{nullptr}`; `setPresenter(EditorPresenter&)` captures
   the address.
2. **`EditorView` inheritance is stated.** `juce::Component`, `juce::KeyListener`,
   `IEditorViewSink`.
3. **`TransportControls` has two buttons, not three.** Play/pause toggle + stop.
4. **Presenter pushes initial state on `attachSink`.** Via the constructor-seeded
   cache derived from current transport state.
5. **Concurrency contract phrasing.** No concurrent access from multiple threads;
   production runs both on the JUCE message thread.
6. **`MainWindow::~MainWindow` still needs `clearContentComponent()`.**
7. **Legacy dual-dispatch has an endpoint.** Step 7 deletes `Engine::Listener`.

### Intent Preserved From v7 and Earlier

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
- Thumbnail ownership transfers into the view as `std::unique_ptr`; the engine
  outlives the view; the composition root never holds a raw thumbnail pointer.
- Legacy `Engine` public methods not required by `ITransport` are removed in step 7.
- `EditorView` is the extracted form of today's `MainWindow::ContentComponent`.
- `EditorViewState.last_load_error` preserves the load-failure UX.
- Space-bar forwards to the presenter's play/pause intent.
- `juce::FileChooser` is owned by `EditorView`; `juce::File` → `core::AudioAsset`
  conversion happens at the dialog callback.

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

`audio::Thumbnail` is a translation adapter. Its abstract header uses
`core::AudioAsset` for source assignment and forward-declares the JUCE drawing types
needed by waveform rendering (`juce::Graphics` and `juce::Rectangle`), so
`waveform_display.cpp` does not pull Tracktion in. The concrete `TracktionThumbnail`
holds a `tracktion::Engine&` from `audio::Engine`, runs Tracktion's async
`SmartThumbnail` caching, and repaints a specific JUCE component bound at construction.

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

    // See "Concurrency Contract" below.
    [[nodiscard]] virtual TransportState state() const = 0;

    class Listener
    {
    public:
        virtual ~Listener() = default;

        // Fires on every underlying transport update (play/pause transitions, load,
        // seek, and every position tick). Consumers must diff the snapshot rather than
        // assume "event = transition." See "Event-Rate Semantics" below.
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

#### Concurrency Contract

- `ITransport::state()` and `Listener::onTransportStateChanged(...)` must not be
  invoked concurrently from multiple threads. In the production app, both run on the
  JUCE message thread, which satisfies this by construction. Single-threaded unit
  tests satisfy it trivially.
- Implementations may internally compose the snapshot from atomic/lock-free sources,
  but the public contract does not require cross-thread safety.
- A future renderer-thread or audio-thread consumer must either (a) be handed a
  copy marshalled across threads by the app, or (b) use a new port designed for
  cross-thread access. Introducing that port is explicitly out of scope for this plan.

The rationale: today `Engine::getTransportPosition()` is `noexcept` and atomic-backed,
but `TransportState` is a composite `{bool, bool, PlaybackPosition, PlaybackDuration}`,
and a composite snapshot cannot be read atomically without extra machinery. Keeping
the contract single-threaded avoids that machinery while matching every current caller.

#### Event-Rate Semantics

Today `audio::Engine::Listener::enginePlayingStateChanged(bool)` is pre-filtered: it
fires only on genuine play/not-playing transitions.
`onTransportStateChanged(TransportState)` replaces it and is **not** pre-filtered — it
fires on every underlying transport update, including every position tick. This is a
deliberate simplification of the event contract. The presenter diffs the snapshot and
only pushes a new `EditorViewState` when something visible changed, so the UI update
rate is bounded at the presenter layer, not the event layer.

**`rock_hero::ui::EditorPresenter`** — framework-free (no JUCE headers in the
implementation file). Owns an `audio::ITransport&`. No thumbnail — click-to-time math
uses `TransportState.length`.

- Intents: `onLoadAudioAssetRequested(const core::AudioAsset& asset)`,
  `onPlayPausePressed()`, `onStopPressed()`, `onWaveformClicked(double normalized_x)`.
- Constructor: reads `transport.state()` once and seeds the cached
  `EditorViewState` from that snapshot.
- View sink: `attachSink(IEditorViewSink&)`. Immediately pushes the cached
  `EditorViewState`. This keeps the view correct when composition order attaches the
  sink after a load has already happened (e.g. in future restore-session paths),
  because the cache was seeded from `transport.state()` during construction and is
  updated by every later transport event.
- Null-sink tolerance: the presenter always caches its most recent derived
  `EditorViewState`. `onTransportStateChanged` updates the cache and pushes to the
  sink if one is attached; otherwise it just updates the cache. `attachSink` reads the
  cache and pushes once. This removes any timing dependency on "no transport events
  fire between presenter construction and `attachSink`."
- Implements `audio::ITransport::Listener`; maps `TransportState` into
  `EditorViewState` and pushes it to the attached sink.
- Lifetime: the presenter calls `transport.addListener(*this)` in its constructor and
  `transport.removeListener(*this)` in its destructor. No separate RAII wrapper.

**`rock_hero::ui::EditorViewState`** — plain data struct describing what the view must
render (enabled flags, play/pause icon choice, cursor proportion, loaded-asset label,
optional load-error message). Does **not** carry the thumbnail handle — the view owns
its thumbnail independently.

```cpp
namespace rock_hero::ui
{
struct EditorViewState
{
    bool load_button_enabled{true};
    bool play_pause_enabled{false};
    bool stop_enabled{false};
    bool play_pause_shows_pause_icon{false};
    double cursor_proportion{0.0}; // 0..1, for the waveform cursor
    std::optional<std::string> loaded_asset_label;
    std::optional<std::string> last_load_error; // set on failed load, cleared on next success
};
} // namespace rock_hero::ui
```

**`rock_hero::ui::IEditorViewSink`** — single method, `setState(const EditorViewState&)`.

**`rock_hero::ui::TransportControlsState`** — projection of `EditorViewState` into the
state `TransportControls` needs. Lives next to `TransportControls` in `rock-hero-ui`.

```cpp
namespace rock_hero::ui
{
struct TransportControlsState
{
    bool play_pause_enabled{false};
    bool stop_enabled{false};
    bool play_pause_shows_pause_icon{false};
};
} // namespace rock_hero::ui
```

`TransportControls` exposes a single `setState(const TransportControlsState&)` method
that drives button enabledness and the play/pause icon. `EditorView::setState`
projects `EditorViewState` into `TransportControlsState` and forwards it. The per-field
setters on today's widget (`setFileLoaded`, `setPlaying`, `setTransportPosition`) are
retired.

**`rock_hero::ui::EditorView`** — replaces today's `MainWindow::ContentComponent`.
Inheritance:

```cpp
class EditorView : public juce::Component,
                   public juce::KeyListener,
                   public IEditorViewSink
{
public:
    EditorView();

    // Transfers ownership; the view keeps the thumbnail alive for its own lifetime.
    // Callers must ensure audio::Engine outlives this view.
    void attachThumbnail(std::unique_ptr<audio::Thumbnail> thumbnail);

    // Exposes the component that Tracktion's async proxy-ready callback must repaint.
    [[nodiscard]] juce::Component& waveformComponent();

    // Binds the presenter. The view stores the argument's address; the presenter must
    // outlive the view (enforced by member order in MainWindow).
    void setPresenter(EditorPresenter& presenter);

    // IEditorViewSink.
    void setState(const EditorViewState& state) override;

    // juce::Component.
    void resized() override;

    // juce::KeyListener.
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

private:
    EditorPresenter* m_presenter{nullptr};
    // ... load button, transport controls, waveform display, file chooser ...
};
```

Internally, `EditorView::attachThumbnail` forwards ownership to its `WaveformDisplay`
member, which stores it as `std::unique_ptr<audio::Thumbnail>` — matching how the widget
holds it today (waveform_display.h:77).

`EditorView` owns the `juce::FileChooser`, handles Space-bar, and converts
`juce::File` to `core::AudioAsset` at the native-dialog callback site. It does **not**
implement `ITransport::Listener` — only the presenter does. `EditorView::setState`
is a pure render sink: it projects the incoming `EditorViewState` to child widgets
(including the `TransportControlsState` projection) and must not dereference
`m_presenter`.

**Tests:** `libs/rock-hero-ui/tests/editor_presenter_tests.cpp` with `FakeTransport`
and `FakeEditorViewSink` in `libs/rock-hero-ui/tests/fakes/`. No JUCE init. No thumbnail
fake — the presenter never touches a thumbnail.

### Renamed or Relocated

- **`audio::Engine`** grows to implement `audio::ITransport`. Tracktion wiring stays
  internal. The engine populates `TransportState.length` from the loaded clip's duration
  (a new engine responsibility — today duration is only surfaced via the thumbnail).
- **`audio::Thumbnail::setFile(const juce::File&)`** is renamed to
  **`audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. The concrete
  Tracktion-backed implementation converts to `juce::File` internally. `Thumbnail` is
  still JUCE-aware because it draws into `juce::Graphics` — that remains fine under
  the dependency rule; it's the translation-adapter's job.
- **`Engine::createThumbnail(juce::Component& owner)`** keeps its shape. The `owner`
  parameter stays because Tracktion's async proxy-ready callback repaints it directly.
  **Rule:** this factory is called only from the app composition root. UI code must
  not call it.
- **`MainWindow::ContentComponent` (private nested struct)** is extracted into
  `libs/rock-hero-ui` as `rock_hero::ui::EditorView`. The nested struct is deleted.

### Deleted

- **`audio::Engine::Listener`** (the public nested interface). Replaced by
  `audio::ITransport::Listener`. Two external implementers today: `WaveformDisplay`
  (waveform_display.h) and `MainWindow::ContentComponent` (main_window.cpp:13). Both
  retire in the same step that deletes `Engine::Listener`.
- **`audio::Engine`'s legacy non-interface public methods.** After `Engine` implements
  `ITransport`, remove public methods that are not part of the new port surface
  (`loadFile(juce::File&)`, `seek(double)`, `isPlaying()`, `getTransportPosition()`)
  from `engine.h`. Keep the public `ITransport` overrides
  (`loadAudioAsset(const core::AudioAsset&)`, `play()`, `pause()`, `stop()`,
  `seek(core::PlaybackPosition)`, `state()`, `addListener(...)`, `removeListener(...)`)
  and concrete factory methods such as `createThumbnail(...)`. Callers outside
  `rock-hero-audio` should hold/use `audio::ITransport&` for transport behavior. This
  prevents UI code from drifting back to concrete-engine transport calls while keeping
  `Engine` a valid concrete implementation of `ITransport`.
- **`WaveformDisplay`'s `audio::Engine&` constructor parameter** and its
  `audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member. The widget
  keeps its existing `std::unique_ptr<audio::Thumbnail>` member but receives the
  thumbnail via `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` instead of
  constructing it internally. `on_seek` is exposed as
  `std::function<void(double)>` emitting a normalized x; the presenter converts to
  `core::PlaybackPosition`.
- **`TransportControls`' internal state machine and per-field setters.**
  `updateButtonStates`, `m_is_playing`, `m_file_loaded`, `m_transport_position`,
  `isFileLoaded()`, `setFileLoaded()`, `setPlaying()`, `setTransportPosition()`, and
  the internally-dispatching `onPlayPauseClicked()`. Replaced by
  `setState(TransportControlsState)` and two plain callbacks.
- **`MainWindow::ContentComponent`** (the private nested struct) — extracted into
  `ui::EditorView` (see *Renamed or Relocated*).
- **`audio::ScopedListener`** — if it has no remaining users after the `Engine::Listener`
  retirement, delete it. If it remains useful as an audio-library-internal RAII helper,
  retain it as an implementation detail.

### Composition After the Refactor

The thumbnail is created by the engine and its ownership is transferred into the view
in a single expression, so no raw thumbnail pointer ever lives as a composition-root
local. The view's presenter pointer is bound **before** the presenter's sink push, so
the first `setState` the view sees happens with `m_presenter` already bound:

```cpp
// apps/rock-hero-editor/main_window.cpp
auto engine = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(*engine);
auto view = std::make_unique<rock_hero::ui::EditorView>();

// Thumbnail ownership moves straight into the view.
view->attachThumbnail(engine->createThumbnail(view->waveformComponent()));

// Bind the presenter before attaching the sink, so the initial setState push
// happens with m_presenter already non-null.
view->setPresenter(*presenter);
presenter->attachSink(*view); // pushes initial EditorViewState immediately
```

In `MainWindow`, the members must be declared in the order `engine`, `presenter`,
`view` so destruction runs view (which destroys the thumbnail) -> presenter -> engine.
The engine outlives every thumbnail it created, and the presenter outlives the view
whose raw pointer references it.

`MainWindow::~MainWindow` continues to call `clearContentComponent()` before the
`std::unique_ptr<EditorView>` member is destroyed. JUCE's `DocumentWindow` holds a
non-owning pointer from `setContentNonOwned(view.get(), ...)`, and
`~ResizableWindow` would otherwise call `removeChildComponent` on a dangling pointer.
`MainWindow::~MainWindow` also calls `removeKeyListener(view.get())` before
`clearContentComponent()`, matching the current destructor.

The presenter sees an `audio::ITransport&` and nothing else from the audio library.
The view owns the thumbnail. Neither includes `<rock_hero/audio/engine.h>`; only the
app target does.

#### Space-Bar Hotkey

`MainWindow` continues to register a `juce::KeyListener` at the window level so Space
toggles playback regardless of which child owns focus. The key listener now forwards
to the presenter rather than to `TransportControls` directly:

```cpp
// Inside EditorView (inherits juce::KeyListener)
bool EditorView::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::spaceKey && m_presenter != nullptr)
    {
        m_presenter->onPlayPausePressed();
        return true;
    }
    return false;
}
```

The presenter already guards against pressing play with no file loaded, so no
duplicate `isFileLoaded()` check is needed at the key-listener site.

#### Load Flow

`EditorView` owns the `juce::FileChooser` and performs the `juce::File` →
`core::AudioAsset` conversion at the dialog callback:

```cpp
// Inside EditorView
void EditorView::onLoadClicked()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(/* ... */);
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile() || m_presenter == nullptr)
                return;
            m_presenter->onLoadAudioAssetRequested(
                rock_hero::core::AudioAsset{file.getFullPathName().toStdString()});
        });
}
```

The presenter calls `transport.loadAudioAsset(asset)`. On success, the next
`onTransportStateChanged` carries `file_loaded = true` and the presenter publishes a
fresh `EditorViewState` with `last_load_error` cleared and `loaded_asset_label` set.
On failure, the presenter publishes an `EditorViewState` whose `last_load_error`
contains a human-readable message; the view renders it (today's
`juce::NativeMessageBox` is one valid rendering — the view may choose another).

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
5. `EditorPresenter` registers itself as an `ITransport::Listener` in its constructor
   and unregisters in its destructor. The engine must outlive the presenter, which
   is already required by 1–3 above.
6. `EditorView::setPresenter(EditorPresenter& presenter)` stores `&presenter`; the
   presenter must outlive the view. `MainWindow` member order
   (engine -> presenter -> view) satisfies this: view is destroyed before presenter.
7. `EditorPresenter` tolerates a null sink. Its listener callback updates a cached
   `EditorViewState` and pushes to the sink only when one is attached. `attachSink`
   flushes the cache. The cache is seeded from `transport.state()` in the presenter
   constructor, so the view's first `setState` push reflects the current transport
   snapshot deterministically, not a transport event that may fire mid-construction.

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
| Cross-thread transport snapshot | A renderer-thread or audio-thread consumer needs `TransportState` (or a subset of it) outside the message thread. New port with an explicit threading contract. |

---

## Refactor Order

Each step is independently compilable; tests for step 4 do not wait on step 5.

1. **Add `core::PlaybackPosition`, `core::PlaybackDuration`, `core::AudioAsset`.
   Define `audio::TransportState`, `audio::ITransport`, `audio::ITransport::Listener`.
   Update `architecture.md`'s dependency-line wording in the same commit.** This is
   the step where `rock-hero-audio -> rock-hero-core` becomes true, driven by
   `ITransport` referencing `core::PlaybackPosition`, `core::PlaybackDuration`, and
   `core::AudioAsset`. The `ITransport` header documents the concurrency contract on
   `state()` and on `Listener::onTransportStateChanged(...)`.

2. **Make `audio::Engine` implement `audio::ITransport`.** Keep Tracktion wiring
   internal. Populate `TransportState.length` from the loaded clip (new engine
   responsibility — today duration is only surfaced via the thumbnail). During
   transition the engine dispatches to both `Engine::Listener` (legacy) and
   `ITransport::Listener` (new). Dual-dispatch ends in **step 7**, when
   `Engine::Listener` is deleted.

3. **Rename `audio::Thumbnail::setFile(const juce::File&)` to
   `audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. Update
   `TracktionThumbnail` and all call sites. Remove the now-unused `juce::File`
   forward declaration from `thumbnail.h` — after the rename, the `Thumbnail`
   interface no longer references `juce::File`. This step adds a second reason
   `rock-hero-audio` depends on `rock-hero-core` (alongside `ITransport` from step 1);
   the dependency-line correction in `architecture.md` already covers it, so no
   further doc edit is required here.

4. **Add `ui::EditorPresenter`, `ui::EditorViewState`, `ui::IEditorViewSink` with
   unit tests.** `FakeTransport` and `FakeEditorViewSink` live in the test target.
   The presenter registers/unregisters as a transport listener in its ctor/dtor, seeds
   its cached `EditorViewState` from `transport.state()` in the constructor, updates
   that cache from listener callbacks, tolerates a null sink, and `attachSink`
   immediately pushes the cached state. This lands the testing story before any JUCE
   widget changes.

5. **Refactor `WaveformDisplay`** to receive its `audio::Thumbnail` via
   `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` rather than constructing one
   from an `audio::Engine&`. Expose `on_seek` as `std::function<void(double)>`
   emitting a normalized x. Remove the engine listener and `audio::Engine::Listener`
   inheritance from this widget. (The second `Engine::Listener` implementer,
   `ContentComponent`, is retired in step 7.)

6. **Refactor `TransportControls`** to render from `TransportControlsState`. The widget
   has two buttons (play/pause toggle + stop) and exposes two callbacks —
   `on_play_pause_pressed` and `on_stop_pressed` — which `EditorView` forwards to
   `presenter.onPlayPausePressed()` and `presenter.onStopPressed()`. Add
   `setState(const TransportControlsState&)`. Remove the internal state machine
   (`updateButtonStates`, `m_is_playing`, `m_file_loaded`, `m_transport_position`,
   `isFileLoaded()`, `setFileLoaded()`, `setPlaying()`, `setTransportPosition()`, and
   the internally-dispatching `onPlayPauseClicked()`).

7. **Extract `ui::EditorView` from `MainWindow::ContentComponent` and rewire
   `main_window`.**
   - Move `ContentComponent`'s responsibilities into
     `libs/rock-hero-ui/include/rock_hero/ui/editor_view.h` +
     `libs/rock-hero-ui/src/editor_view.cpp` as `ui::EditorView`, inheriting
     `juce::Component`, `juce::KeyListener`, and `IEditorViewSink`. `EditorView`
     owns the load button, transport controls, waveform display, and
     `juce::FileChooser`; converts `juce::File` → `core::AudioAsset` at the
     native-dialog callback; forwards user intents (play/pause/stop/load/seek/Space)
     to the presenter via a stored `EditorPresenter*`. `EditorView::setState`
     projects `EditorViewState` into `TransportControlsState` and forwards it to the
     `TransportControls` child.
   - Rewire `MainWindow` to compose `Engine` → `ITransport&` → `EditorPresenter` →
     `EditorView`, with the thumbnail transferred into the view in one expression,
     then `setPresenter` called before `attachSink` (see *Composition After the
     Refactor*). Verify the `MainWindow` member order puts the engine before both
     presenter and view, and puts the presenter before the view. Preserve
     `MainWindow::~MainWindow`'s `removeKeyListener` + `clearContentComponent()`
     calls so JUCE's non-owning content pointer is nulled before the unique_ptr
     destroys the view.
   - **Delete `audio::Engine::Listener`** — ends the dual-dispatch introduced in
     step 2. Both external implementers (`WaveformDisplay` per step 5;
     `ContentComponent`, now retired) are gone.
   - **Delete the redundant legacy `Engine` public methods not required by
     `ITransport`** (`loadFile(juce::File&)`, `seek(double)`, `isPlaying`,
     `getTransportPosition`). Keep the public `ITransport` overrides (`play`, `pause`,
     `stop`, `seek(core::PlaybackPosition)`, `state`, `loadAudioAsset`, and listener
     registration) plus concrete factories such as `createThumbnail`. External
     transport callers route through `ITransport`.
   - Delete the private nested `MainWindow::ContentComponent` struct.
   - Delete `audio::ScopedListener` if no remaining users.

---

## Testing: One Worked Example

`TransportState` is authoritative for view state; intents never speculatively mutate
view state. The test target uses Catch2 v3 (matching the existing
`rock_hero_core_tests` target), so `Catch::Approx` is the floating-point helper.

```cpp
TEST_CASE("EditorPresenter: attachSink immediately pushes derived state")
{
    FakeTransport transport;
    // Seed state before constructing the presenter. The constructor reads
    // transport.state() and derives the initial cached EditorViewState from it.
    transport.set_state({.file_loaded = true,
                         .playing = false,
                         .position = {0.0},
                         .length = {10.0}});

    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    // Sink sees derived state without waiting for the next transport event.
    REQUIRE(sink.set_state_call_count == 1);
    REQUIRE(sink.last_state.play_pause_enabled == true);
}

TEST_CASE("EditorPresenter: events before attachSink update the cache, not the sink")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};

    // An event before any sink is attached must not crash and must not push.
    transport.simulateState({.file_loaded = true,
                             .playing = true,
                             .position = {1.0},
                             .length = {10.0}});
    REQUIRE(sink.set_state_call_count == 0);

    presenter.attachSink(sink);

    // attachSink flushes the cached derived state.
    REQUIRE(sink.set_state_call_count == 1);
    REQUIRE(sink.last_state.play_pause_shows_pause_icon == true);
}

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

    REQUIRE(transport.last_seek.seconds == Catch::Approx(3.0));
}

TEST_CASE("EditorPresenter: failed replacement load surfaces error and preserves prior flags")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    // Seed a loaded state so the "failed replacement" test exercises a non-trivial prior.
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});
    REQUIRE(sink.last_state.play_pause_enabled == true);
    REQUIRE_FALSE(sink.last_state.last_load_error.has_value());

    // The next load fails; transport state does not change.
    transport.next_load_result = false;
    presenter.onLoadAudioAssetRequested(
        rock_hero::core::AudioAsset{"/not/a/real/file.wav"});

    // Error surfaces; prior loaded flags remain true because the prior file is still loaded.
    REQUIRE(sink.last_state.last_load_error.has_value());
    REQUIRE(sink.last_state.play_pause_enabled == true);

    // A subsequent successful load clears the error.
    transport.next_load_result = true;
    presenter.onLoadAudioAssetRequested(
        rock_hero::core::AudioAsset{"/a/real/file.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {8.0}});

    REQUIRE_FALSE(sink.last_state.last_load_error.has_value());
}
```

`FakeTransport` exposes enough surface to drive these tests: `set_state(...)` seeds
the state read by `state()` without invoking listeners; `simulateState(...)` sets
state *and* invokes each registered listener; `play_call_count`, `last_seek`, and
`next_load_result` record/configure interactions. It is a hand-written test fake
living in `libs/rock-hero-ui/tests/fakes/`, not production code.

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
  `ISongRepository`, `IScoringInputStream`, `IWaveformDataSource`, or a cross-thread
  transport snapshot port.
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

- Concrete-engine references spreading across UI code. *Addressed: `ITransport`, and
  redundant legacy `Engine` transport back doors are removed so UI code has no
  concrete-engine transport path.*
- Workflow accreting in JUCE components. *Addressed: presenter owns workflow; Space-bar,
  load-failure surfacing, and seek math all live in the presenter, not the view.*
- `core` acquiring framework dependencies. *Addressed: direction tightened.*
- App targets accumulating business logic. *Addressed: `ContentComponent` is extracted
  into `ui::EditorView`; the app target's only remaining job is composition.*
- Framework types (`juce::File`) leaking into project boundaries. *Addressed:
  `core::AudioAsset`, `core::PlaybackPosition`, `core::PlaybackDuration`. Conversion
  happens once, at the view's native-dialog callback.*
- Conflating ports with translation adapters so test fakes are expected where none
  are practical. *Addressed: inventory separates the two.*
- Raw-pointer lifetime coupling across ownership boundaries. *Addressed: thumbnail
  ownership transferred into the view as `std::unique_ptr`, with written lifetime
  rules; presenter ↔ transport listener lifetime explicit; presenter pointer in the
  view bounded by corrected `MainWindow` member order; view's first `setState` runs
  after `setPresenter`.*
- Timing-fragile initialization (composition order implying "no events can fire
  before `attachSink`"). *Addressed: presenter caches state and tolerates a null sink;
  `attachSink` flushes the cache.*
- Ambiguous threading contracts that invite data races. *Addressed: `ITransport::state()`
  and `Listener::onTransportStateChanged(...)` forbid concurrent access, with the
  cross-thread case deferred behind a tripwire.*

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
- `testable-modular-architecture-refactor-plan-revised-v3.md` — v3, superseded.
- `testable-modular-architecture-refactor-plan-revised-v4.md` — v4, superseded.
- `testable-modular-architecture-refactor-plan-revised-v5.md` — v5, superseded.
- `testable-modular-architecture-refactor-plan-revised-v6.md` — v6, superseded.
- `testable-modular-architecture-refactor-plan-revised-v7.md` — v7, superseded.
- `testable-modular-architecture-refactor-plan-revised-v8.md` — v8, superseded.
- `testable-modular-architecture-refactor-plan-revised-v9.md` — v9, superseded.
- `testable-modular-architecture-refactor-plan-revised-v10.md` — v10, superseded by
  this v11.
- `testable-modular-architecture-refactor-plan-review-notes.md` — feedback applied.
- `testable-modular-architecture-refactor-plan-v3-review-notes.md` — feedback
  applied.
- `rockhero_modular_cmake_plan.md` — content folded into this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.
