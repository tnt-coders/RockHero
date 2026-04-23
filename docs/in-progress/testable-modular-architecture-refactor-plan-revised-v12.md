# Testable Modular Architecture Refactor Plan (Revised v12)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v11.md`. v12 closes three
contract gaps that v11 left implicit: the waveform source update path after a successful
load, the cursor update API after the `Engine::Listener` inheritance is removed from
`WaveformDisplay`, and the requirement that `Engine::loadAudioAsset(...)` publish a
state snapshot on success even when play state and position are unchanged. It also
tightens several smaller contract points. No architectural moves change.

### Fixes Introduced in v12

1. **Asset identity is a separate query on `ITransport`, not a field of `TransportState`.**
   v11 left the presenter unable to derive a waveform source or a loaded-asset label
   after a restore-session construction, because `TransportState` (a tick-rate snapshot
   constructed on every position update) carried no asset identity and copying an
   `AudioAsset` by value into it would heap-allocate the `std::filesystem::path` on
   every tick. v12 adds
   `[[nodiscard]] virtual const std::optional<core::AudioAsset>& currentAsset() const = 0;`
   to `ITransport`. The presenter queries it during every derive step. Returning by
   const reference is tick-free regardless of call rate; `AudioAsset` can grow without
   affecting tick cost.

2. **`EditorViewState` carries `std::optional<core::AudioAsset> loaded_asset`.** The
   presenter populates it from `currentAsset()` during derive. `EditorView::setState`
   compares it against the last state it rendered and, on change, forwards the asset
   to `WaveformDisplay::setThumbnailSource(const core::AudioAsset&)`, which in turn
   calls `audio::Thumbnail::setSource(asset)`. This replaces today's path where
   `MainWindow::ContentComponent` called `m_waveform_display.setAudioFile(file)`
   directly after a successful `Engine::loadFile`.

3. **`WaveformDisplay::setCursorProportion(double)` replaces the removed engine
   listener.** v11 put `cursor_proportion` in `EditorViewState` and removed
   `WaveformDisplay`'s `Engine::Listener` inheritance, but did not specify how the
   cursor value reached the widget after the refactor. `EditorView::setState` now
   forwards `state.cursor_proportion` to
   `WaveformDisplay::setCursorProportion(double)`, which stores and repaints. This
   closes the loop.

4. **`Engine::loadAudioAsset(...)` must publish a fresh `TransportState` on success.**
   Today `loadFile` changes `file_loaded` (and may change `length`) while leaving
   `playing` and `position` unchanged. Tracktion's existing position and play-state
   callbacks will not fire for a pure `file_loaded`-flag transition, so the engine
   must explicitly publish a new snapshot to listeners after a successful load.
   `loadAudioAsset(...)` is also the point where `currentAsset()` begins returning the
   new asset; the snapshot must not fire until that ordering is in place (new
   `currentAsset()` visible before listeners observe the new `TransportState`).

5. **Presenter re-derives on every event and diffs derived state before pushing.**
   v11 stated this in prose but did not test it. v12 adds a test that asserts
   firing an identical `TransportState` twice produces at most one `setState` call,
   and a test that an asset change produces a push with `loaded_asset` updated.
   "Identical" for this purpose means the derived `EditorViewState` is equal, not
   just the `TransportState`.

6. **Presenter constructor order is pinned: read state, then register listener.** v11
   left the order between `transport.state()` and `transport.addListener(*this)`
   unspecified. v12 pins read-then-register. The single-threaded contract makes this
   safe (no listener callback can race the constructor), and the order is simpler to
   reason about: the cache starts from one well-defined snapshot, after which every
   listener callback produces a monotonic update to it.

7. **`Engine`'s `Listener&`-taking `ITransport` overrides coexist with its legacy
   `Listener*`-taking methods during steps 2–6.** v11's port uses `addListener(Listener&)`
   and `removeListener(Listener&)`; today's `Engine` uses pointer overloads for
   `Engine::Listener`. During the dual-dispatch window the engine exposes both
   shapes; step 7 deletes the pointer overloads with `Engine::Listener`.

8. **Stop-enable rule moves into the presenter, explicitly.** Today `TransportControls`
   gates Stop on "playing OR position > 0". v11 retires that internal state machine
   but does not state where the rule goes. v12: the presenter derives
   `stop_enabled = state.file_loaded && (state.playing || state.position.seconds > 0.0)`
   and places it in `EditorViewState.stop_enabled`, which `EditorView::setState`
   projects into `TransportControlsState.stop_enabled`.

### Fix Preserved From v11

1. **Presenter cache seeding is explicit.** `EditorPresenter` seeds its cached
   `EditorViewState` from `transport.state()` (and, per v12, `transport.currentAsset()`)
   in the constructor; listener callbacks update the cache; `attachSink` flushes the
   cache.

### Fixes Preserved From v10

1. **Composition sketch wires `setPresenter` before `attachSink`** so the view has a
   presenter when it first sees state.
2. **Presenter tolerates a null sink.** The presenter caches the most recent derived
   `EditorViewState` and its `onTransportStateChanged` path no-ops the sink push when
   no sink is attached. `attachSink` immediately flushes the cached state.
3. **`TransportControls` state-propagation mechanism named.** `TransportControls`
   exposes a single `setState(const TransportControlsState&)`.
4. **Failed-load test strengthens its assertions.** The test seeds a loaded state
   first, runs a failed replacement load, and asserts (a) the error is surfaced and
   (b) the prior loaded-state view flags are preserved. A follow-up assertion covers
   `last_load_error` clearing on the next successful load.

### Fixes Preserved From v9

1. **Legacy `Engine` method deletion scoped to non-interface back doors.** Public
   `ITransport` overrides are retained. Only the legacy overloads (`loadFile(juce::File&)`,
   `seek(double)`, `isPlaying`, `getTransportPosition`) are removed.
2. **`MainWindow` member order is `engine, presenter, view`** so destruction runs
   `view -> presenter -> engine`.

### Fixes Preserved From v8

1. **`EditorView` presenter-binding consistency.** Stored as
   `EditorPresenter* m_presenter{nullptr}`; `setPresenter(EditorPresenter&)` captures
   the address.
2. **`EditorView` inheritance is stated.** `juce::Component`, `juce::KeyListener`,
   `IEditorViewSink`.
3. **`TransportControls` has two buttons, not three.** Play/pause toggle + stop.
4. **Presenter pushes initial state on `attachSink`.** Via the constructor-seeded
   cache derived from current transport state and current asset.
5. **Concurrency contract phrasing.** No concurrent access from multiple threads;
   production runs both on the JUCE message thread.
6. **`MainWindow::~MainWindow` still needs `clearContentComponent()`.**
7. **Legacy dual-dispatch has an endpoint.** Step 7 deletes `Engine::Listener`.

### Intent Preserved From v7 and Earlier

- Port vs. translation-adapter distinction.
- `ITransport` is a genuine port with a single coarse state event.
- `audio::Thumbnail` is a translation adapter, routed from engine to the view without
  passing through the presenter.
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

`audio::ITransport` is a port. Its contract (play/pause/stop/seek + state snapshot +
current-asset query) is small, independent of any specific framework, and naturally
fakeable.

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
depend on it. The primary unit-test seam for the editor flow is `ITransport`.

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
`rock-hero-audio/api/`. Does **not** carry asset identity; see `ITransport::currentAsset()`
below.

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

`TransportState` is a tick-rate snapshot value type. It is constructed and copied on
every transport position update. Asset identity is deliberately not a member: copying
a `core::AudioAsset` by value would heap-allocate its `std::filesystem::path` on every
tick, and `AudioAsset` is designed to grow (format hints, bundled-resource handles,
asset IDs). Asset identity is queried separately via `ITransport::currentAsset()` so
the tick-rate snapshot remains allocation-free.

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

    // Returns a reference to the transport's stored optional asset. Null-empty when
    // no asset is loaded. The reference is valid until the next mutating call on the
    // transport (load/play/pause/stop/seek). See "Concurrency Contract" below.
    [[nodiscard]] virtual const std::optional<core::AudioAsset>& currentAsset() const = 0;

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

One coarse event carrying a full `TransportState` snapshot. `currentAsset()` is a
pull query rather than a pushed field so the tick-rate event path stays
allocation-free. When an asset changes, implementations must update `currentAsset()`
before publishing the follow-up `TransportState` so that listeners observe the new
asset on their first re-derive.

#### Concurrency Contract

- `ITransport::state()`, `ITransport::currentAsset()`, and
  `Listener::onTransportStateChanged(...)` must not be invoked concurrently from
  multiple threads. In the production app, all three run on the JUCE message thread,
  which satisfies this by construction. Single-threaded unit tests satisfy it trivially.
- Implementations may internally compose the snapshot from atomic/lock-free sources,
  but the public contract does not require cross-thread safety.
- The reference returned by `currentAsset()` is valid only until the next mutating
  call on the transport. Callers that need to outlive that window must copy the
  `AudioAsset` value out of the optional.
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
deliberate simplification of the event contract. The presenter diffs the **derived**
`EditorViewState` (not the raw `TransportState`) and only pushes a new state to the
sink when the derivation produces something different from the cache. The UI update
rate is bounded at the presenter layer, not the event layer.

In addition, `Engine::loadAudioAsset(...)` must publish a fresh `TransportState`
snapshot on successful load even when `playing` and `position` are unchanged. Today's
Tracktion callbacks fire on play/pause and on position changes, neither of which is
guaranteed on a pure `file_loaded`-flag transition. Without this requirement, a load
could complete while the presenter never sees `file_loaded = true`. Implementations
must also make the new `currentAsset()` visible before firing the state event so that
listeners re-deriving in response see the new asset.

**`rock_hero::ui::EditorPresenter`** — framework-free (no JUCE headers in the
implementation file). Owns an `audio::ITransport&`. No thumbnail — click-to-time math
uses `TransportState.length`.

- Intents: `onLoadAudioAssetRequested(const core::AudioAsset& asset)`,
  `onPlayPausePressed()`, `onStopPressed()`, `onWaveformClicked(double normalized_x)`.
- Constructor order: (1) read `transport.state()` and `transport.currentAsset()` once;
  (2) derive and seed the cached `EditorViewState`; (3) call
  `transport.addListener(*this)`. Safe under the single-threaded contract and simpler
  to reason about than the inverse order: the cache starts from one well-defined
  snapshot, after which every listener callback produces a monotonic update.
- View sink: `attachSink(IEditorViewSink&)`. Immediately pushes the cached
  `EditorViewState`.
- Null-sink tolerance: `onTransportStateChanged` re-derives from
  `transport.state()` + `transport.currentAsset()`, updates the cache, diffs against
  the previous cache, and pushes to the sink only if the derivation differs *and* a
  sink is attached. `attachSink` always pushes the current cache once.
- Stop-enable rule lives here: the presenter derives
  `stop_enabled = state.file_loaded && (state.playing || state.position.seconds > 0.0)`.
- Load-failure handling: on a `false` return from `transport.loadAudioAsset(...)` the
  presenter synthesizes a new `EditorViewState` with `last_load_error` set, preserving
  the cache's prior flags (including `loaded_asset` and the loaded-label), updates the
  cache, and pushes. No transport event is involved.
- Load-success handling: on a `true` return the presenter waits for the engine's
  follow-up `TransportState` event; the derive step reads the new
  `currentAsset()` and clears `last_load_error`.
- Implements `audio::ITransport::Listener`; maps (`TransportState` + `currentAsset()`)
  into `EditorViewState` and pushes it to the attached sink.
- Lifetime: the presenter calls `transport.addListener(*this)` in its constructor and
  `transport.removeListener(*this)` in its destructor. No separate RAII wrapper.

**`rock_hero::ui::EditorViewState`** — plain data struct describing what the view must
render. Does **not** carry the thumbnail handle — the view owns its thumbnail
independently. It does carry `loaded_asset` so the view can drive thumbnail source
updates.

```cpp
namespace rock_hero::ui
{
struct EditorViewState
{
    bool load_button_enabled{true};
    bool play_pause_enabled{false};
    bool stop_enabled{false};
    bool play_pause_shows_pause_icon{false};
    double cursor_proportion{0.0};
    std::optional<core::AudioAsset> loaded_asset;
    std::optional<std::string> loaded_asset_label;
    std::optional<std::string> last_load_error;
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
    // Retains the last rendered state so setState can diff and avoid redundant
    // downstream calls (in particular, avoid repainting the thumbnail source when
    // the asset has not changed).
    EditorViewState m_last_rendered_state{};
    // ... load button, transport controls, waveform display, file chooser ...
};
```

`EditorView::setState` is a pure render sink. It:

1. Projects the `TransportControlsState` fields from the incoming state and calls
   `m_transport_controls.setState(controls_state)`.
2. Calls `m_waveform_display.setCursorProportion(state.cursor_proportion)`.
3. If `state.loaded_asset != m_last_rendered_state.loaded_asset`, calls
   `m_waveform_display.setThumbnailSource(*state.loaded_asset)` when the new asset is
   present, or `m_waveform_display.clearThumbnailSource()` when it is absent. This
   is the replacement for today's `MainWindow::ContentComponent` calling
   `m_waveform_display.setAudioFile(file)` after a successful load.
4. Renders `state.loaded_asset_label` and `state.last_load_error` (today's
   `juce::NativeMessageBox` is one valid rendering of the error — the view may choose
   another).
5. Stores the incoming state as `m_last_rendered_state`.

`EditorView::setState` must not dereference `m_presenter`.

Internally, `EditorView::attachThumbnail` forwards ownership to its `WaveformDisplay`
member, which stores it as `std::unique_ptr<audio::Thumbnail>` — matching how the widget
holds it today (waveform_display.h:77). `WaveformDisplay` gains:

- `setCursorProportion(double)` — stores and repaints.
- `setThumbnailSource(const core::AudioAsset&)` — forwards to
  `m_thumbnail->setSource(asset)`.
- `clearThumbnailSource()` — symmetric, for the empty case.

`EditorView` owns the `juce::FileChooser`, handles Space-bar, and converts
`juce::File` to `core::AudioAsset` at the native-dialog callback site. It does **not**
implement `ITransport::Listener` — only the presenter does.

**Tests:** `libs/rock-hero-ui/tests/editor_presenter_tests.cpp` with `FakeTransport`
and `FakeEditorViewSink` in `libs/rock-hero-ui/tests/fakes/`. No JUCE init. No thumbnail
fake — the presenter never touches a thumbnail.

### Renamed or Relocated

- **`audio::Engine`** grows to implement `audio::ITransport`. Tracktion wiring stays
  internal. The engine populates `TransportState.length` from the loaded clip's duration
  (a new engine responsibility — today duration is only surfaced via the thumbnail).
  It also owns the backing `std::optional<core::AudioAsset>` returned by
  `currentAsset()`.
- **`audio::Thumbnail::setFile(const juce::File&)`** is renamed to
  **`audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. The concrete
  Tracktion-backed implementation converts to `juce::File` internally. `Thumbnail` is
  still JUCE-aware because it draws into `juce::Graphics`.
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
  from `engine.h`. Keep the public `ITransport` overrides and concrete factory methods
  such as `createThumbnail(...)`.
- **`WaveformDisplay`'s `audio::Engine&` constructor parameter** and its
  `audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member. The widget
  keeps its existing `std::unique_ptr<audio::Thumbnail>` member but receives the
  thumbnail via `attachThumbnail(std::unique_ptr<audio::Thumbnail>)`. `on_seek` is
  exposed as `std::function<void(double)>` emitting a normalized x; the presenter
  converts to `core::PlaybackPosition`. Cursor updates arrive through
  `setCursorProportion(double)` instead of the removed engine listener.
- **`TransportControls`' internal state machine and per-field setters.**
  `updateButtonStates`, `m_is_playing`, `m_file_loaded`, `m_transport_position`,
  `isFileLoaded()`, `setFileLoaded()`, `setPlaying()`, `setTransportPosition()`, and
  the internally-dispatching `onPlayPauseClicked()`. Replaced by
  `setState(TransportControlsState)` and two plain callbacks.
- **`MainWindow::ContentComponent`** (the private nested struct) — extracted into
  `ui::EditorView`.
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
toggles playback regardless of which child owns focus. The key listener forwards to
the presenter:

```cpp
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

The presenter calls `transport.loadAudioAsset(asset)`.

- **On success:** the engine updates `currentAsset()` to hold the new asset and then
  publishes a fresh `TransportState` with `file_loaded = true` and the new `length`.
  The presenter's listener callback re-derives: reads the new `state()` and new
  `currentAsset()`, produces an `EditorViewState` with `loaded_asset` set,
  `loaded_asset_label` set, and `last_load_error` cleared, and pushes.
  `EditorView::setState` detects the asset change against `m_last_rendered_state` and
  calls `m_waveform_display.setThumbnailSource(*state.loaded_asset)`.
- **On failure:** the presenter synthesizes an `EditorViewState` whose
  `last_load_error` contains a human-readable message and whose other fields mirror
  the cache's prior values (including `loaded_asset`, so a prior load remains visible).
  The view renders the error.

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
   flushes the cache. The cache is seeded from `transport.state()` and
   `transport.currentAsset()` in the presenter constructor (read-then-register
   listener), so the view's first `setState` push reflects the current transport
   snapshot deterministically.
8. The reference returned by `ITransport::currentAsset()` is valid only until the
   next mutating call on the transport. The presenter copies the value out into its
   cached `EditorViewState.loaded_asset` during derive; it does not cache the
   reference itself.

---

## Ports vs. Translation Adapters: Current Inventory

### Ports (genuine boundaries, fakeable, primary test seams)

- `rock_hero::audio::ITransport`

### Translation adapters (Tracktion-free headers, not primary test seams)

- `rock_hero::audio::Thumbnail` — hides Tracktion from the UI translation unit.
  Bound to an `audio::Engine` and a `juce::Component` at construction.

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
   `state()`, `currentAsset()`, and `Listener::onTransportStateChanged(...)`, and the
   lifetime contract on the reference returned from `currentAsset()`.

2. **Make `audio::Engine` implement `audio::ITransport`.** Keep Tracktion wiring
   internal. Populate `TransportState.length` from the loaded clip. Maintain a
   `std::optional<core::AudioAsset>` data member whose const reference is returned
   from `currentAsset()`; update it at the start of `loadAudioAsset(...)` and clear
   it on failure or on explicit unload. On a successful `loadAudioAsset(...)`, publish
   a fresh `TransportState` snapshot to listeners even when `playing` and `position`
   are unchanged, and do so *after* the asset member is updated so listeners observe
   the new `currentAsset()` on their first re-derive. During transition the engine
   dispatches to both `Engine::Listener` (legacy, pointer registration) and
   `ITransport::Listener` (new, reference registration). Dual-dispatch ends in
   **step 7**, when `Engine::Listener` is deleted.

3. **Rename `audio::Thumbnail::setFile(const juce::File&)` to
   `audio::Thumbnail::setSource(const core::AudioAsset& asset)`**. Update
   `TracktionThumbnail` and all call sites. Remove the now-unused `juce::File`
   forward declaration from `thumbnail.h`. This step adds a second reason
   `rock-hero-audio` depends on `rock-hero-core`; the dependency-line correction in
   `architecture.md` already covers it.

4. **Add `ui::EditorPresenter`, `ui::EditorViewState`, `ui::IEditorViewSink` with
   unit tests.** `FakeTransport` and `FakeEditorViewSink` live in the test target.
   The presenter registers/unregisters as a transport listener in its ctor/dtor,
   reads `transport.state()` and `transport.currentAsset()` first and then registers
   the listener, seeds its cached `EditorViewState` from the derive step, updates
   that cache from listener callbacks, diffs the derived state before pushing to the
   sink, tolerates a null sink, and flushes the cache on `attachSink`. This lands
   the testing story before any JUCE widget changes.

5. **Refactor `WaveformDisplay`** to receive its `audio::Thumbnail` via
   `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` rather than constructing one
   from an `audio::Engine&`. Add `setCursorProportion(double)`,
   `setThumbnailSource(const core::AudioAsset&)`, and `clearThumbnailSource()`.
   Expose `on_seek` as `std::function<void(double)>` emitting a normalized x.
   Remove the engine listener and `audio::Engine::Listener` inheritance from this
   widget. (The second `Engine::Listener` implementer, `ContentComponent`, is retired
   in step 7.)

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
     projects `EditorViewState` into `TransportControlsState` and forwards it;
     calls `WaveformDisplay::setCursorProportion(state.cursor_proportion)`; and, on
     `loaded_asset` change against `m_last_rendered_state`, calls
     `WaveformDisplay::setThumbnailSource(...)` or `::clearThumbnailSource()`.
   - Rewire `MainWindow` to compose `Engine` → `ITransport&` → `EditorPresenter` →
     `EditorView`, with the thumbnail transferred into the view in one expression,
     then `setPresenter` called before `attachSink`. Verify the `MainWindow` member
     order puts the engine before both presenter and view, and puts the presenter
     before the view. Preserve `MainWindow::~MainWindow`'s `removeKeyListener` +
     `clearContentComponent()` calls.
   - **Delete `audio::Engine::Listener`** — ends the dual-dispatch introduced in
     step 2. Both external implementers (`WaveformDisplay` per step 5;
     `ContentComponent`, now retired) are gone.
   - **Delete the redundant legacy `Engine` public methods not required by
     `ITransport`** (`loadFile(juce::File&)`, `seek(double)`, `isPlaying`,
     `getTransportPosition`). Keep the public `ITransport` overrides plus concrete
     factories such as `createThumbnail`.
   - Delete the private nested `MainWindow::ContentComponent` struct.
   - Delete `audio::ScopedListener` if no remaining users.

---

## Testing: One Worked Example

`TransportState` + `currentAsset()` are authoritative for view state; intents never
speculatively mutate view state. The test target uses Catch2 v3 (matching the existing
`rock_hero_core_tests` target), so `Catch::Approx` is the floating-point helper.

```cpp
TEST_CASE("EditorPresenter: attachSink immediately pushes derived state")
{
    FakeTransport transport;
    transport.set_state({.file_loaded = true,
                         .playing = false,
                         .position = {0.0},
                         .length = {10.0}});
    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/intro.wav"});

    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    REQUIRE(sink.set_state_call_count == 1);
    REQUIRE(sink.last_state.play_pause_enabled == true);
    REQUIRE(sink.last_state.loaded_asset.has_value());
    REQUIRE(sink.last_state.loaded_asset->path == "/songs/intro.wav");
}

TEST_CASE("EditorPresenter: events before attachSink update the cache, not the sink")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/intro.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = true,
                             .position = {1.0},
                             .length = {10.0}});
    REQUIRE(sink.set_state_call_count == 0);

    presenter.attachSink(sink);

    REQUIRE(sink.set_state_call_count == 1);
    REQUIRE(sink.last_state.play_pause_shows_pause_icon == true);
    REQUIRE(sink.last_state.loaded_asset.has_value());
}

TEST_CASE("EditorPresenter: identical derived state does not push to sink again")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/intro.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});

    const auto count_after_first_event = sink.set_state_call_count;

    // Replay the same state. Nothing derived changes.
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});

    REQUIRE(sink.set_state_call_count == count_after_first_event);
}

TEST_CASE("EditorPresenter: asset change triggers a push with new loaded_asset")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/a.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});
    REQUIRE(sink.last_state.loaded_asset->path == "/songs/a.wav");

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/b.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {12.0}});

    REQUIRE(sink.last_state.loaded_asset->path == "/songs/b.wav");
}

TEST_CASE("EditorPresenter: play intent calls transport when a file is loaded")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/intro.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 1);
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

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/intro.wav"});
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

    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/good.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {10.0}});
    REQUIRE(sink.last_state.play_pause_enabled == true);
    REQUIRE(sink.last_state.loaded_asset->path == "/songs/good.wav");
    REQUIRE_FALSE(sink.last_state.last_load_error.has_value());

    transport.next_load_result = false;
    presenter.onLoadAudioAssetRequested(
        rock_hero::core::AudioAsset{"/not/a/real/file.wav"});

    // Error surfaces; prior flags and asset remain because transport state and
    // currentAsset() did not change.
    REQUIRE(sink.last_state.last_load_error.has_value());
    REQUIRE(sink.last_state.play_pause_enabled == true);
    REQUIRE(sink.last_state.loaded_asset->path == "/songs/good.wav");

    // A subsequent successful load clears the error.
    transport.next_load_result = true;
    presenter.onLoadAudioAssetRequested(
        rock_hero::core::AudioAsset{"/songs/new.wav"});
    transport.set_current_asset(rock_hero::core::AudioAsset{"/songs/new.wav"});
    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position = {0.0},
                             .length = {8.0}});

    REQUIRE_FALSE(sink.last_state.last_load_error.has_value());
    REQUIRE(sink.last_state.loaded_asset->path == "/songs/new.wav");
}
```

`FakeTransport` exposes enough surface to drive these tests:

- `set_state(...)` / `set_current_asset(...)` seed the values read by `state()` and
  `currentAsset()` without invoking listeners.
- `simulateState(...)` sets state *and* invokes each registered listener. It does not
  touch the current asset, so asset changes are staged via `set_current_asset(...)`
  before calling `simulateState(...)`.
- `play_call_count`, `last_seek`, and `next_load_result` record/configure interactions.

It is a hand-written test fake living in `libs/rock-hero-ui/tests/fakes/`, not production
code. No JUCE init. No Tracktion runtime. No thumbnail fake — the presenter never
references a thumbnail. Length comes from the transport-state event; asset identity
comes from `currentAsset()`.

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
  redundant legacy `Engine` transport back doors are removed.*
- Workflow accreting in JUCE components. *Addressed: presenter owns workflow;
  Space-bar, load-failure surfacing, stop-enable rule, and seek math all live in the
  presenter, not the view.*
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
  after `setPresenter`; `currentAsset()` reference lifetime spelled out.*
- Timing-fragile initialization (composition order implying "no events can fire
  before `attachSink`"). *Addressed: presenter caches state and tolerates a null sink;
  `attachSink` flushes the cache; constructor order is pinned.*
- Tick-rate allocation via asset identity baked into `TransportState`. *Addressed:
  asset identity is a separate pull query on `ITransport`, returned by const
  reference, so `AudioAsset` can grow without affecting the tick-rate snapshot.*
- Silent "load succeeded but the UI never knew" failures. *Addressed: engine must
  publish a `TransportState` on successful load even when play/position are
  unchanged.*
- Ambiguous threading contracts that invite data races. *Addressed:
  `ITransport::state()`, `currentAsset()`, and
  `Listener::onTransportStateChanged(...)` forbid concurrent access, with the
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
- `testable-modular-architecture-refactor-plan-revised-v10.md` — v10, superseded.
- `testable-modular-architecture-refactor-plan-revised-v11.md` — v11, superseded by
  this v12.
- `testable-modular-architecture-refactor-plan-review-notes.md` — feedback applied.
- `testable-modular-architecture-refactor-plan-v3-review-notes.md` — feedback
  applied.
- `testable-modular-architecture-refactor-plan-revised-v11-review-notes.md` —
  feedback applied.
- `rockhero_modular_cmake_plan.md` — content folded into this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.