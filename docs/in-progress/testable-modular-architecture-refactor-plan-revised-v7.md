# Testable Modular Architecture Refactor Plan (Revised v7)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v6.md`. v7 closes gaps found
in a full audit of v6 against the actual codebase. None of the architectural moves change;
the revisions are structural bookkeeping and contract clarifications.

### Fixes Introduced in v7

1. **`ContentComponent` → `ui::EditorView` extraction is explicit.** v6 referred to
   `EditorView` as if it already existed. Today the editor window's JUCE-side composer is
   `MainWindow::ContentComponent`, a private nested struct
   (`apps/rock-hero-editor/main_window.cpp`). Step 7 now spells out extracting it into
   `libs/rock-hero-ui` as `ui::EditorView`.
2. **`ContentComponent` is the second external user of `audio::Engine::Listener`.** v6's
   "Deleted" section named only `WaveformDisplay`. `ContentComponent` also implements
   `Engine::Listener` (`main_window.cpp:13`, :38, :44) and must be retired in the same step
   that deletes `Engine::Listener`.
3. **Disposition of legacy `Engine` methods.** After `Engine` implements `ITransport`,
   every non-`ITransport` transport method (`loadFile`, `play`, `pause`, `stop`, `seek`,
   `isPlaying`, `getTransportPosition`) is removed from the public header. Step 7 deletes
   them; no public back-doors remain.
4. **Thread/consistency contract of `ITransport::state()` is pinned.** `state()` is
   declared message-thread only. The listener callback also runs on the message thread.
   Any cross-thread consumer must copy through a JUCE message or an explicitly-threadsafe
   adapter — not added by this plan.
5. **Event-rate semantics change is acknowledged.** Today `enginePlayingStateChanged` is
   pre-filtered to transitions. `onTransportStateChanged(TransportState)` fires on every
   position tick. The presenter diffs the snapshot; downstream consumers must not assume
   "event = transition."
6. **`TransportState.length` population is an engine responsibility.** Step 2 explicitly
   requires the engine to populate `length` from the loaded clip (today the duration is
   only available via the thumbnail).
7. **Space-bar hotkey is routed through the presenter.** The window-level `KeyListener`
   now forwards to `presenter.onPlayPausePressed()` rather than calling a `TransportControls`
   method directly.
8. **`juce::FileChooser` ownership and the `juce::File` → `core::AudioAsset` conversion
   point are named.** `EditorView` owns the `FileChooser` and performs the conversion at
   the native-dialog callback; the presenter never sees `juce::File`.
9. **Load-failure UX is preserved.** `EditorViewState` gains an optional error-message
   field surfaced by the presenter when `ITransport::loadAudioAsset` returns false.
10. **Presenter ↔ transport listener lifetime is explicit.** The presenter registers in
    its constructor and unregisters in its destructor.
11. **Worked test uses Catch2 v3 syntax.** `Approx` becomes `Catch::Approx` (the v3
    spelling in the repo's existing test target).

### Intent Preserved From v6 and Earlier

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
- Thumbnail ownership is transferred into the view as `std::unique_ptr`; the engine
  outlives the view; the composition root never holds a raw thumbnail pointer.

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

    // Message-thread only. See "Thread / Consistency Contract" below.
    [[nodiscard]] virtual TransportState state() const = 0;

    class Listener
    {
    public:
        virtual ~Listener() = default;

        // Invoked on the message thread. Fires on every underlying transport update
        // (play/pause transitions, load, seek, and every position tick). Consumers must
        // diff the snapshot rather than assume "event = transition." See "Event-Rate
        // Semantics" below.
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

#### Thread / Consistency Contract

- `ITransport::state()` is callable only from the JUCE message thread. The same
  constraint applies to `Listener::onTransportStateChanged(...)`, which is invoked on the
  message thread.
- Implementations may internally compose the snapshot from atomic/lock-free sources, but
  the public contract does not require cross-thread safety.
- A later renderer-thread or audio-thread consumer must either (a) be handed a
  copy marshalled across threads by the app, or (b) use a new port designed for
  cross-thread access. Introducing that port is explicitly out of scope for this plan.

The rationale: today `Engine::getTransportPosition()` is `noexcept` and atomic-backed,
but `TransportState` is a composite `{bool, bool, PlaybackPosition, PlaybackDuration}`,
and a composite snapshot cannot be read atomically without extra machinery. Constraining
to the message thread keeps the contract small and matches every current caller.

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
- View sink: `attachSink(IEditorViewSink&)`.
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
    std::optional<std::string> last_load_error; // populated on failed load, cleared on next success
};
} // namespace rock_hero::ui
```

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

    // Binds the presenter so the view can emit intents and receive state.
    void setPresenter(EditorPresenter& presenter);
    // ...
};
} // namespace rock_hero::ui
```

Internally, `EditorView::attachThumbnail` forwards ownership to its
`WaveformDisplay` member, which stores it as `std::unique_ptr<audio::Thumbnail>` —
matching how the widget holds it today (waveform_display.h:77).

**`rock_hero::ui::EditorView` role.** `EditorView` replaces today's
`MainWindow::ContentComponent`. It owns the load button, transport controls, waveform
display, and the `juce::FileChooser` used by the load flow. It implements
`IEditorViewSink`, handles Space-bar key input, and converts `juce::File` to
`core::AudioAsset` at the native-dialog callback site. It does **not** implement
`ITransport::Listener` — only the presenter does.

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
- **`audio::Engine`'s pre-`ITransport` public methods.** After `Engine` implements
  `ITransport`, the redundant non-port methods (`loadFile(juce::File&)`, `play()`,
  `pause()`, `stop()`, `seek(double)`, `isPlaying()`, `getTransportPosition()`) are
  removed from the public header. Callers go through `ITransport`. This is what
  prevents UI code from drifting back to `#include <rock_hero/audio/engine.h>`.
- **`WaveformDisplay`'s `audio::Engine&` constructor parameter** and its
  `audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member. The widget
  keeps its existing `std::unique_ptr<audio::Thumbnail>` member but receives the
  thumbnail via `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` instead of
  constructing it internally. `on_seek` is exposed as
  `std::function<void(double)>` emitting a normalized x; the presenter converts to
  `core::PlaybackPosition`.
- **`MainWindow::ContentComponent`** (the private nested struct) — extracted into
  `ui::EditorView` (see *Renamed or Relocated*).
- **`audio::ScopedListener`** — if it has no remaining users after the `Engine::Listener`
  retirement, delete it. If it remains useful as an audio-library-internal RAII helper,
  retain it as an implementation detail.

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

#### Space-Bar Hotkey

`MainWindow` continues to register a `juce::KeyListener` at the window level so Space
toggles playback regardless of which child owns focus. The key listener now forwards
to the presenter rather than to `TransportControls` directly:

```cpp
// Inside EditorView (implements juce::KeyListener)
bool keyPressed(const juce::KeyPress& key, juce::Component*) override
{
    if (key == juce::KeyPress::spaceKey)
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
void onLoadClicked()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(/* ... */);
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
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
   `core::AudioAsset`. The `ITransport` header documents the message-thread contract
   on `state()` and on `Listener::onTransportStateChanged(...)`.

2. **Make `audio::Engine` implement `audio::ITransport`.** Keep Tracktion wiring
   internal. Populate `TransportState.length` from the loaded clip (new engine
   responsibility — today duration is only surfaced via the thumbnail). During
   transition the engine dispatches to both `Engine::Listener` (legacy) and
   `ITransport::Listener` (new). Both paths run on the message thread.

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
   The presenter registers/unregisters as a transport listener in its ctor/dtor.
   This lands the testing story before any JUCE widget changes.

5. **Refactor `WaveformDisplay`** to receive its `audio::Thumbnail` via
   `attachThumbnail(std::unique_ptr<audio::Thumbnail>)` rather than constructing one
   from an `audio::Engine&`. Expose `on_seek` as `std::function<void(double)>`
   emitting a normalized x. Remove the engine listener and `audio::Engine::Listener`
   inheritance from this widget. (The second `Engine::Listener` implementer,
   `ContentComponent`, is retired in step 7.)

6. **Refactor `TransportControls`** to render from `EditorViewState`. Remove its
   internal state machine (`updateButtonStates`, cached `m_is_playing`,
   `m_file_loaded`, `m_transport_position`). The widget becomes a dumb renderer with
   three button callbacks that the view forwards to presenter intents.

7. **Extract `ui::EditorView` from `MainWindow::ContentComponent` and rewire
   `main_window`.**
   - Move `ContentComponent`'s responsibilities into
     `libs/rock-hero-ui/include/rock_hero/ui/editor_view.h` +
     `libs/rock-hero-ui/src/editor_view.cpp` as `ui::EditorView`. `EditorView` owns
     the load button, transport controls, waveform display, and `juce::FileChooser`;
     implements `IEditorViewSink` and `juce::KeyListener`; converts `juce::File` →
     `core::AudioAsset` at the native-dialog callback; forwards user intents
     (play/pause/stop/load/seek/Space) to the presenter.
   - Rewire `MainWindow` to compose `Engine` → `ITransport&` → `EditorPresenter` →
     `EditorView`, with the thumbnail transferred into the view in one expression
     (see *Composition After the Refactor*). Verify the `MainWindow` member order
     puts the engine before the view.
   - **Delete `audio::Engine::Listener`.** Both external implementers
     (`WaveformDisplay` per step 5; `ContentComponent`, now retired) are gone.
   - **Delete the redundant legacy `Engine` public methods**
     (`loadFile(juce::File&)`, `play`, `pause`, `stop`, `seek(double)`, `isPlaying`,
     `getTransportPosition`). All callers route through `ITransport`.
   - Delete the private nested `MainWindow::ContentComponent` struct.
   - Delete `audio::ScopedListener` if no remaining users.

---

## Testing: One Worked Example

`TransportState` is authoritative for view state; intents never speculatively mutate
view state. The test target uses Catch2 v3 (matching the existing
`rock_hero_core_tests` target), so `Catch::Approx` is the floating-point helper.

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

    REQUIRE(transport.last_seek.seconds == Catch::Approx(3.0));
}

TEST_CASE("EditorPresenter: failed load surfaces an error in view state")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport};
    presenter.attachSink(sink);

    transport.next_load_result = false;
    presenter.onLoadAudioAssetRequested(
        rock_hero::core::AudioAsset{"/not/a/real/file.wav"});

    REQUIRE(sink.last_state.last_load_error.has_value());
    REQUIRE(sink.last_state.play_pause_enabled == false);
}
```

`FakeTransport` exposes enough surface to drive these tests: a `simulateState(...)`
helper that invokes each registered listener, `play_call_count`, `last_seek`, and
`next_load_result` to control the return value of `loadAudioAsset(...)`. It is a
hand-written test fake living in `libs/rock-hero-ui/tests/fakes/`, not production code.

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
  the redundant legacy `Engine` public methods are removed so UI code has no back-door.*
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
  rules; presenter ↔ transport listener lifetime is explicit.*
- Ambiguous threading contracts that invite data races. *Addressed: `ITransport::state()`
  and `Listener::onTransportStateChanged(...)` are message-thread only, with the
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
- `testable-modular-architecture-refactor-plan-revised-v6.md` — v6, superseded by
  this v7.
- `testable-modular-architecture-refactor-plan-review-notes.md` — feedback applied.
- `testable-modular-architecture-refactor-plan-v3-review-notes.md` — feedback
  applied.
- `rockhero_modular_cmake_plan.md` — content folded into this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.