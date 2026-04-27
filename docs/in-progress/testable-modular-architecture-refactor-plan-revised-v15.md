# Testable Modular Architecture Refactor Plan (Revised v15)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v14.md`. v15 keeps
the original goal of the refactor, but changes the editor shape around the decisions
from the walkthrough:

- content state moves into a pure `core::Session` with role-free tracks,
- transport state stays transport-only,
- `EditorPresenter` becomes `EditorController`,
- `IEditorViewSink` becomes `IEditorView`,
- the editor view state carries track waveform rows instead of one global loaded asset,
- thumbnail refresh follows track asset changes automatically,
- a `ui::Editor` wrapper owns the fully wired controller/view pair, and
- controller files stay in the `rock-hero-ui` module under a `controllers/` folder
  without adding a `rock_hero::ui::controllers` namespace.

The main correction from v14 is conceptual: a loaded audio asset is not transport
state. It belongs to session/content state. Transport remains about playback motion:
play, pause, stop, seek, position, and length.

### Fixes Introduced in v15

1. **Session/content state is split from transport state.** v14 put asset loading and
   current-asset identity on `ITransport`. v15 moves asset identity into `core::Session`
   and its track collection. `audio::ITransport` is narrowed to playback control and
   transport snapshots.
2. **Tracks are role-free for the first pass.** Stem support needs multiple tracks, but
   it does not require hard-coded roles such as `Drums`, `Bass`, or `Vocals`. Roles can
   be added later if concrete editor or playback behavior needs them.
3. **Undo/redo stays framework-neutral at the model boundary.** JUCE undo support may be
   useful later, but `core::Session` must not depend on JUCE. Future undo/redo can wrap
   pure session commands in an adapter or app-layer coordinator.
4. **Controller naming replaces presenter naming.** The class receives editor user
   intents, coordinates session and playback ports, and derives view state. In this
   codebase, `EditorController` reads more directly than `EditorPresenter`.
5. **The view state is multi-track shaped.** `EditorViewState` contains
   `TrackWaveformState` rows derived from `core::Session`. The current one-file editor
   is just the one-track case.
6. **The view/controller cycle is hidden inside `ui::Editor`.** The app target should
   not see half-wired `EditorView` or `EditorController` objects. `ui::Editor` owns both
   and performs the internal attach step.
7. **Thumbnail creation stays on the engine boundary for now.** A separate thumbnail
   factory is deferred. `audio::Engine::createThumbnail(...)` remains the concrete
   creation point until dynamic multi-track row creation proves a factory/cache is
   worth adding.
8. **Submodule folders do not create sub-namespaces.** Controller code lives under a
   physical `controllers/` folder inside `rock-hero-ui`, but public types remain in
   `rock_hero::ui`.

### Fixes Preserved From v14

1. **Load errors have an explicit lifecycle.** Failed loads surface a cached
   `last_load_error`. Ordinary transport ticks preserve the error; a successful load
   clears it; a future explicit dismiss intent may also clear it, but this plan does
   not add that intent.
2. **Error rendering is edge-triggered in the view.** If the view renders load errors
   through `juce::NativeMessageBox`, it must not reopen the same modal on every state
   push. `EditorView::setState` presents an error only when the error changes from
   empty to present or when the error string changes.
3. **No unload/clear-thumbnail flow is added.** This refactor does not add an explicit
   unload operation. Thumbnail source changes are driven by track asset changes.
4. **Goal alignment stays explicit.** The plan continues to target testable editor
   workflow, framework isolation, thin adapters, app-level composition, and incremental
   delivery.

---

## Terminology: Port vs. Translation Adapter

This distinction is load-bearing for the plan. Both hide framework details from
consumers, but they serve different roles.

**Port** - a narrow, project-owned interface at a boundary, designed so that:

- production code and tests can both implement it,
- multiple concrete implementations are plausible, and
- controllers or domain code can be unit-tested against hand-written fakes.

`audio::ITransport` is a port. It exposes transport controls and transport state. It
does not expose loaded asset identity.

`audio::IPlaybackContent` is also a port. It applies session track audio to the
playback engine without making `core::Session` or `EditorController` know Tracktion.
It exists because loading/applying audio content is not a transport operation.

**Translation adapter** - an abstract base class whose purpose is to keep framework
implementation types out of consuming headers, but which:

- has one realistic production implementation for now,
- is bound to framework-owned runtime state, and
- is not the primary unit-test seam.

`audio::Thumbnail` is a translation adapter. Its header accepts project-owned audio
asset references and forward-declares JUCE drawing types. The concrete Tracktion-backed
thumbnail owns the framework-specific proxy generation and drawing integration.

The plan treats ports and translation adapters asymmetrically. Ports carry the main
testability contract. Translation adapters earn their existence by keeping framework
headers out of unrelated translation units.

---

## Guiding Principle

> Pay for structure now where deferring it forces an invasive later refactor. Defer
> structure where adding it later is a local, non-disruptive change.

---

## Target Architecture

Dependency arrows point from depender to dependency:

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
- `rock-hero-ui` may depend on `rock-hero-core` and on Tracktion-free
  `rock-hero-audio` headers. It may not depend on `audio::Engine` or Tracktion types.
- Apps depend on concrete implementations and wire them together.

### Internal UI Layout

Use folders as conceptual submodules before introducing new CMake targets:

```text
libs/rock-hero-ui/
  include/rock_hero/ui/
    controllers/
      editor_controller.h
      editor_view_state.h
      i_editor_controller.h
      i_editor_view.h
    views/
      editor.h
      editor_view.h
    widgets/
      track_waveform_row.h
      transport_controls.h
      waveform_display.h
```

These folders are organizational only. They do not create nested public namespaces.
All public UI declarations remain in `rock_hero::ui`, for example:

```cpp
namespace rock_hero::ui
{
class EditorController;
struct EditorViewState;
class EditorView;
class Editor;
class TransportControls;
} // namespace rock_hero::ui
```

---

## The Editor Seam - What Gets Added, Renamed, and Deleted

### Added

**`rock_hero::core::AudioAsset`** - project-owned reference to a loadable audio asset.
Lives in `rock-hero-core/domain/`.

```cpp
namespace rock_hero::core
{
struct AudioAsset
{
    std::filesystem::path path;

    [[nodiscard]] bool operator==(const AudioAsset&) const = default;
};
} // namespace rock_hero::core
```

Today this is just a path. The type exists so later additions such as format hints,
asset IDs, or bundled-resource handles are non-breaking at call sites. `juce::File`
conversion happens once at the JUCE view/app edge.

**`rock_hero::core::PlaybackPosition`** and **`rock_hero::core::PlaybackDuration`** -
lightweight semantic time value types. Live in `rock-hero-core/timing/`.

```cpp
namespace rock_hero::core
{
struct PlaybackPosition
{
    double seconds{0.0};

    [[nodiscard]] bool operator==(const PlaybackPosition&) const = default;
};

struct PlaybackDuration
{
    double seconds{0.0};

    [[nodiscard]] bool operator==(const PlaybackDuration&) const = default;
};
} // namespace rock_hero::core
```

Both currently wrap `double`, but they represent different concepts. A position is a
point on the playback timeline. A duration is a span. Keeping them separate makes seek,
length, calibration, and hit-window APIs harder to misuse as timing behavior grows.
Keep them intentionally boring for now: no units framework and no clever operators.

**`rock_hero::core::TrackId`**, **`rock_hero::core::Track`**, and
**`rock_hero::core::Session`** - pure editor/session content model. Lives in
`rock-hero-core`.

```cpp
namespace rock_hero::core
{
struct TrackId
{
    std::uint64_t value{0};

    [[nodiscard]] bool operator==(const TrackId&) const = default;
};

struct Track
{
    TrackId id;
    std::string name;
    std::optional<AudioAsset> audio_asset;
    bool muted{false};
    double gain_db{0.0};

    [[nodiscard]] bool operator==(const Track&) const = default;
};

class Session
{
public:
    [[nodiscard]] std::span<const Track> tracks() const noexcept;
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;

    TrackId addTrack(std::string name,
                     std::optional<AudioAsset> asset = std::nullopt);
    [[nodiscard]] bool replaceTrackAsset(TrackId id, AudioAsset asset);

private:
    std::vector<Track> m_tracks;
    std::uint64_t m_next_track_id{1};
};
} // namespace rock_hero::core
```

Tracks are intentionally role-free in this revision. A session can contain one empty
track row before audio is loaded, one full-mix track after the first load, or multiple
stem tracks later. Role metadata can be added later if concrete behavior needs it.

`core::Session` is pure C++. It must not depend on JUCE, Tracktion, UI components, or
audio engine runtime state. Future undo/redo should be designed around pure session
commands first. JUCE's `UndoManager` may be used later by an adapter or app-layer
coordinator, but it must not become a dependency of `core::Session`.

**`rock_hero::audio::TransportState`** - audio-transport snapshot. Lives in
`rock-hero-audio/api/`.

```cpp
namespace rock_hero::audio
{
struct TransportState
{
    bool playing{false};
    core::PlaybackPosition position;
    core::PlaybackDuration length;

    [[nodiscard]] bool operator==(const TransportState&) const = default;
};
} // namespace rock_hero::audio
```

`TransportState` is a tick-rate snapshot. It does not carry asset identity, track lists,
or load errors. Those are content/session or presentation concepts, not transport
concepts.

**`rock_hero::audio::ITransport`** - transport-only playback port.

```cpp
namespace rock_hero::audio
{
class ITransport
{
public:
    virtual ~ITransport() = default;

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

One coarse event carries a full transport snapshot. It fires on play/pause transitions,
stop, seek, length changes, and position ticks. Consumers must diff derived state
instead of assuming "event equals transition."

**`rock_hero::audio::IPlaybackContent`** - content-loading/apply port for editor audio.

```cpp
namespace rock_hero::audio
{
class IPlaybackContent
{
public:
    virtual ~IPlaybackContent() = default;

    virtual bool applyTrackAudio(core::TrackId track_id,
                                 const core::AudioAsset& asset) = 0;
};
} // namespace rock_hero::audio
```

This port is separate from `ITransport` because track audio identity is not transport
state. The controller uses it to ask the audio adapter to accept a candidate asset for a
track. If it returns `false`, the controller leaves `core::Session` unchanged and
surfaces a load error. If it returns `true`, the controller commits the session change
and derives a new `EditorViewState`.

The first implementation can map the single editor track to today's backing-track
loading path. The shape still supports future stems because the operation is keyed by
`core::TrackId`.

#### Concurrency Contract

- `ITransport::state()`, `IPlaybackContent::applyTrackAudio(...)`, and
  `ITransport::Listener::onTransportStateChanged(...)` must not be invoked concurrently
  from multiple threads. In the production editor, these calls run on the JUCE message
  thread.
- Implementations may internally read atomic/lock-free audio state to build snapshots,
  but the public contract is single-threaded.
- A future renderer-thread or audio-thread consumer must use a separate port with an
  explicit cross-thread contract. That is out of scope for this plan.

**`rock_hero::ui::EditorViewState`** - view-facing rendering snapshot. Lives in the
`rock-hero-ui/controllers/` folder but remains in namespace `rock_hero::ui`.

```cpp
namespace rock_hero::ui
{
struct TrackWaveformState
{
    core::TrackId track_id;
    std::string display_name;
    std::optional<core::AudioAsset> audio_asset;
    bool selected{false};

    [[nodiscard]] bool operator==(const TrackWaveformState&) const = default;
};

struct EditorViewState
{
    bool load_button_enabled{true};
    bool play_pause_enabled{false};
    bool stop_enabled{false};
    bool play_pause_shows_pause_icon{false};
    double cursor_proportion{0.0};
    std::vector<TrackWaveformState> tracks;
    std::optional<std::string> last_load_error;

    [[nodiscard]] bool operator==(const EditorViewState&) const = default;
};
} // namespace rock_hero::ui
```

`EditorViewState` is not the document model. `core::Session` is the source of truth.
`EditorViewState` is a flattened projection for the current editor screen. Fields such
as `stop_enabled`, `cursor_proportion`, and `last_load_error` belong here because they
are view-facing derivations or presentation state.

A `TrackWaveformState` with no `audio_asset` represents an empty track row. The view
may render the row label and empty waveform area, but it must not call
`Thumbnail::setSource(...)` until an asset is present.

**`rock_hero::ui::IEditorView`** - output interface from controller to view.

```cpp
namespace rock_hero::ui
{
class IEditorView
{
public:
    virtual ~IEditorView() = default;

    virtual void setState(const EditorViewState& state) = 0;
};
} // namespace rock_hero::ui
```

This replaces `IEditorViewSink`. The interface is deliberately narrow. It is not the
full JUCE component surface; it is only the state output path the controller needs.

**`rock_hero::ui::IEditorController`** - input interface from view to controller.

```cpp
namespace rock_hero::ui
{
class IEditorController
{
public:
    virtual ~IEditorController() = default;

    virtual void onLoadAudioAssetRequested(core::TrackId track_id,
                                           const core::AudioAsset& asset) = 0;
    virtual void onPlayPausePressed() = 0;
    virtual void onStopPressed() = 0;
    virtual void onWaveformClicked(double normalized_x) = 0;
};
} // namespace rock_hero::ui
```

`EditorView` consumes this interface so view-wiring tests can use a fake controller
without constructing the real controller and its session/audio dependencies.

**`rock_hero::ui::EditorController`** - framework-free editor workflow coordinator.

- Lives under `libs/rock-hero-ui/.../controllers/`.
- Uses namespace `rock_hero::ui`, not `rock_hero::ui::controllers`.
- Implements `IEditorController` and `audio::ITransport::Listener`.
- Owns references to `core::Session`, `audio::ITransport`, and
  `audio::IPlaybackContent`.
- Does not include JUCE headers.
- Receives view intents and coordinates session/content/transport behavior.
- Derives `EditorViewState` from `core::Session` and `audio::TransportState`.
- Pushes derived state through `IEditorView`.

Key behavior:

- Constructor reads `transport.state()`, derives the initial cached `EditorViewState`,
  then registers as a transport listener.
- `attachView(IEditorView&)` attaches the output view and immediately pushes the cached
  state.
- Transport events re-derive from the current `Session` and `TransportState`; duplicate
  derived states are not pushed.
- `onPlayPausePressed()` calls `pause()` when transport is playing and `play()` when at
  least one track has an audio asset and transport is not playing.
- `onStopPressed()` calls `stop()` when `stop_enabled` is true.
- `onWaveformClicked(normalized_x)` clamps the value and seeks to
  `normalized_x * transport.length`.
- `onLoadAudioAssetRequested(track_id, asset)` first calls
  `IPlaybackContent::applyTrackAudio(track_id, asset)`. On failure, it preserves the
  existing session track asset and pushes `last_load_error`. On success, it commits the
  asset into `core::Session`, clears `last_load_error`, and pushes a new view state.
  The current editor can seed a single empty track row during composition so this
  operation always has a stable `TrackId`.

**`rock_hero::ui::TransportControlsState`** - state needed by the transport control
widget.

```cpp
namespace rock_hero::ui
{
struct TransportControlsState
{
    bool play_pause_enabled{false};
    bool stop_enabled{false};
    bool play_pause_shows_pause_icon{false};

    [[nodiscard]] bool operator==(const TransportControlsState&) const = default;
};
} // namespace rock_hero::ui
```

`TransportControls` exposes `setState(const TransportControlsState&)` and a nested
listener interface:

```cpp
namespace rock_hero::ui
{
class TransportControls final : public juce::Component
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;

        virtual void onPlayPausePressed() = 0;
        virtual void onStopPressed() = 0;
    };

    explicit TransportControls(Listener& listener);

    void setState(const TransportControlsState& state);

private:
    Listener& m_listener;
};
} // namespace rock_hero::ui
```

Do not add `ITransportControls` in this plan. `TransportControls` is an internal child
widget owned by `EditorView`, not an architectural boundary. It has a local listener
only to report button events to its parent.

**`rock_hero::ui::TrackWaveformRow`** - JUCE row component for one track waveform.

- Owns or references one `audio::Thumbnail`.
- Receives one `TrackWaveformState`.
- Diffs the row's optional `AudioAsset` against the previously applied asset.
- Calls `audio::Thumbnail::setSource(asset)` when the asset changes to a present value.
- Draws its thumbnail and shared playhead cursor.
- Emits waveform click events to its parent through a local listener.

This is the mechanism that makes thumbnail refresh automatic: a session track asset
change changes `EditorViewState::tracks`, and applying that state to the row starts new
thumbnail generation.

**`rock_hero::ui::EditorView`** - JUCE component tree for the editor screen.

- Inherits `juce::Component`, `juce::KeyListener`, and `IEditorView`.
- Receives `IEditorController&` for user-intent output.
- Owns the load button, `TransportControls`, waveform rows, and `juce::FileChooser`.
- Converts `juce::File` to `core::AudioAsset` at the file chooser callback.
- Renders `EditorViewState` by projecting transport state into
  `TransportControlsState` and track state into waveform rows.
- Presents load errors on an edge, not on every repeated state push.

**`rock_hero::ui::Editor`** - fully wired editor UI feature.

```cpp
namespace rock_hero::ui
{
class Editor final
{
public:
    Editor(core::Session& session,
           audio::ITransport& transport,
           audio::IPlaybackContent& playback_content);

    [[nodiscard]] juce::Component& component() noexcept;

private:
    EditorController m_controller;
    EditorView m_view;
};
} // namespace rock_hero::ui
```

`Editor` owns the controller and view, wires `m_controller.attachView(m_view)` in its
constructor, and exposes a fully ready root component. App code should not manually
sequence `setController` and `attachView` calls.

The exact thumbnail handoff remains app/composition responsibility in this revision.
The app creates Tracktion-backed thumbnails through `audio::Engine::createThumbnail(...)`
and passes or attaches them to the view/rows. A separate thumbnail factory/cache is
deferred until dynamic row creation makes it necessary. The first implementation may
wire one empty/default row and one thumbnail through composition; the state model is
multi-track shaped so expanding to more rows does not rewrite the controller/session
contract.

### Renamed or Relocated

- `EditorPresenter` -> `EditorController`.
- `IEditorViewSink` -> `IEditorView`.
- `FakeEditorViewSink` -> `FakeEditorView`.
- `attachSink(...)` -> `attachView(...)`.
- Controller/state/interface files live under `rock-hero-ui/controllers/` but stay in
  namespace `rock_hero::ui`.
- `EditorViewState.loaded_asset` becomes `EditorViewState.tracks`.
- The old one-waveform display becomes a one-row case of the track waveform view.

### Deleted

- Asset loading and current-asset identity from `ITransport`.
- `ITransport::currentAsset()`.
- `ITransport::loadAudioAsset(...)`.
- `IEditorViewSink` terminology.
- Any public `std::function` callback members in `TransportControls` sketches.
- Direct `audio::Engine` dependency from UI widgets and views.
- Legacy `Engine` public methods not required by the new ports after migration is
  complete.

---

## Composition After the Refactor

The app target remains the composition root for concrete implementations:

```cpp
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : m_audio_engine()
        , m_session(createInitialEditorSession())
        , m_editor(m_session, m_audio_engine, m_audio_engine)
    {
        setContentNonOwned(&m_editor.component(), true);
    }

private:
    static core::Session createInitialEditorSession();

    audio::Engine m_audio_engine;
    core::Session m_session;
    ui::Editor m_editor;
};
```

The sketch assumes `createInitialEditorSession()` seeds the current editor with one
empty track row, and that `audio::Engine` implements both `audio::ITransport` and
`audio::IPlaybackContent`. `MainWindow` owns concrete objects only. It does not contain
editor workflow.

Member order matters. C++ destroys members in reverse declaration order:

```text
m_editor -> m_session -> m_audio_engine
```

This keeps the view/controller and thumbnail owners dead before the concrete audio
engine is destroyed. Preserve `MainWindow::~MainWindow` cleanup such as
`clearContentComponent()` if JUCE ownership requires it.

### Space-Bar Hotkey

The space bar is a view-level input. `EditorView` handles the key event and calls
`IEditorController::onPlayPausePressed()`. The controller decides whether that means
play, pause, or no-op.

### Load Flow

1. User chooses an audio file in `EditorView`.
2. `EditorView` converts `juce::File` to `core::AudioAsset`.
3. `EditorView` calls
   `IEditorController::onLoadAudioAssetRequested(track_id, asset)`.
4. `EditorController` calls
   `IPlaybackContent::applyTrackAudio(track_id, asset)`.
5. On failure, the controller preserves `core::Session`, sets `last_load_error`, and
   pushes a view state.
6. On success, the controller updates `core::Session`, clears `last_load_error`, and
   pushes a view state with an updated track row.
7. `EditorView::setState(...)` applies the row state.
8. `TrackWaveformRow` sees the asset changed to a present value and calls
   `Thumbnail::setSource(asset)`.

This keeps failed replacement loads from corrupting the session while making successful
asset updates automatically refresh the waveform thumbnail.

### Lifetime Rules

1. `audio::Engine` must outlive every `audio::Thumbnail` created from it.
2. `ui::Editor` must outlive its `EditorController` and `EditorView` members by owning
   them directly.
3. `EditorController` registers as an `ITransport::Listener` in its constructor and
   unregisters in its destructor.
4. `EditorController::attachView(...)` does not transfer ownership. It binds the output
   state channel and pushes the current cached state.
5. `EditorView` owns JUCE child widgets and waveform rows.
6. `TransportControls` receives a required `TransportControls::Listener&` because it is
   an internal child-to-parent relationship inside `EditorView`.

---

## Ports vs. Translation Adapters: Current Inventory

### Ports

- `rock_hero::audio::ITransport` - transport control/state boundary.
- `rock_hero::audio::IPlaybackContent` - session track audio apply/load boundary.
- `rock_hero::ui::IEditorView` - controller-to-view state output boundary.
- `rock_hero::ui::IEditorController` - view-to-controller intent boundary.

### Translation Adapters

- `rock_hero::audio::Thumbnail` - hides Tracktion thumbnail/proxy implementation from
  UI rows while still drawing through JUCE graphics primitives.

### Deferred Ports

| Port | Tripwire |
|---|---|
| `IToneEngine` | First concrete effects/tone chain exists and a second consumer needs it. |
| `IPitchDetector` | First pitch/onset detector exists and gameplay needs to consume detections. |
| `IPluginHost` | Plugin scanning/loading exists and is invoked from more than one place. |
| `IClock` | Core code references wall-clock time. Likely avoidable; inject transport position instead. |
| `ISongRepository` | Persistence policy varies or needs fakes in tests. |
| `IScoringInputStream` | Gameplay consumes player onsets separately from the raw detector. |
| `IWaveformDataSource` | UI needs waveform rendering outside JUCE or without `juce::Graphics`. |
| `IThumbnailFactory` | Dynamic multi-track row creation needs view-owned thumbnail creation or caching. |
| Cross-thread transport snapshot | A renderer/audio-thread consumer needs transport state outside the message thread. |

---

## Refactor Order

1. **Add core value types and session model.**
   - Add `core::AudioAsset`, `PlaybackPosition`, `PlaybackDuration`, `TrackId`,
     `Track`, and `Session`.
   - Add core tests for adding tracks, preserving order, replacing a track asset, and
     rejecting missing track IDs.
   - Keep `Session` role-free and framework-free.

2. **Add focused audio ports.**
   - Add `audio::TransportState`, `audio::ITransport`, and `audio::IPlaybackContent`
     under Tracktion-free audio headers.
   - Do not add asset identity to `TransportState`.
   - Do not put load/apply methods on `ITransport`.

3. **Adapt `audio::Engine` to the ports.**
   - Implement `ITransport` with play, pause, stop, seek, state, and listener methods.
   - Implement `IPlaybackContent::applyTrackAudio(...)` using the existing single-file
     load path as the first one-track implementation.
   - Keep legacy methods temporarily where needed for independent compilation.

4. **Update thumbnail source API.**
   - Replace `Thumbnail::setFile(const juce::File&)` with
     `Thumbnail::setSource(const core::AudioAsset&)`.
   - Convert `core::AudioAsset` to framework file objects inside the concrete thumbnail
     adapter.

5. **Refactor waveform UI toward rows.**
   - Introduce `TrackWaveformRow` or equivalent row-shaped component.
   - Give each row a thumbnail adapter and a `setState(const TrackWaveformState&)`
     path.
   - Diff optional `AudioAsset` in the row and call `Thumbnail::setSource(...)` only
     when it changes to a present value.
   - Remove direct engine listener behavior from waveform UI.

6. **Add controller contracts and state.**
   - Add `IEditorView`, `IEditorController`, `TrackWaveformState`, and
     `EditorViewState` under `rock-hero-ui/controllers/`.
   - Keep declarations in namespace `rock_hero::ui`.

7. **Add `EditorController` and tests.**
   - Implement `EditorController` against `core::Session`, `audio::ITransport`,
     `audio::IPlaybackContent`, and `IEditorView`.
   - Add headless controller tests with `FakeTransport`, `FakePlaybackContent`, and
     `FakeEditorView`.
   - Cover initial state push, duplicate suppression, play/pause, stop, seek, failed
     load preservation, successful load commit, and track-row derivation.

8. **Refactor `TransportControls`.**
   - Replace internal business rules with `TransportControlsState`.
   - Use a nested `Listener` interface and required listener reference.
   - Keep the widget concrete inside `EditorView`.

9. **Extract `EditorView`.**
   - Move `MainWindow::ContentComponent` responsibilities into `ui::EditorView`.
   - `EditorView` owns JUCE widgets, file chooser, transport controls, and waveform
     rows.
   - `EditorView` implements `IEditorView` and consumes `IEditorController&`.
   - Add narrow UI wiring checks where useful, especially edge-triggered error display.

10. **Add `ui::Editor`.**
    - Own `EditorController` and `EditorView`.
    - Wire `m_controller.attachView(m_view)` inside the constructor.
    - Expose a root JUCE component to the app.

11. **Rewire `MainWindow`.**
    - Make `MainWindow` own `audio::Engine`, `core::Session`, and `ui::Editor`.
    - Keep concrete thumbnail creation at the engine/composition boundary.
    - Preserve lifetime-safe member order.

12. **Delete legacy paths.**
    - Remove old direct `audio::Engine` calls from UI code.
    - Remove legacy `Engine` methods that are not part of the new ports.
    - Remove old `Engine::Listener` UI coupling once no external UI implementers remain.
    - Delete the old nested `MainWindow::ContentComponent`.

---

## Testing Strategy

### Core Session Tests

These live under `libs/rock-hero-core/tests/`:

- adding a track creates a stable nonzero `TrackId`,
- adding an empty track is valid for the initial no-file editor state,
- tracks preserve insertion order,
- replacing a track asset updates only that track,
- replacing a missing track asset fails cleanly,
- track roles are not required for one-track or multi-track sessions.

### Controller Tests

These live under `libs/rock-hero-ui/tests/` and do not initialize JUCE:

```cpp
TEST_CASE("EditorController: attachView immediately pushes derived state")
{
    rock_hero::core::Session session;
    const auto track_id = session.addTrack(
        "Full Mix", rock_hero::core::AudioAsset{"/songs/intro.wav"});

    FakeTransport transport;
    transport.set_state({.playing = false, .position = {0.0}, .length = {10.0}});

    FakePlaybackContent playback_content;
    FakeEditorView view;

    rock_hero::ui::EditorController controller{session, transport, playback_content};
    controller.attachView(view);

    REQUIRE(view.set_state_call_count == 1);
    REQUIRE(view.last_state.play_pause_enabled == true);
    REQUIRE(view.last_state.tracks.size() == 1);
    REQUIRE(view.last_state.tracks.front().track_id == track_id);
}
```

Additional controller tests:

- events before `attachView` update the cache but do not push to a view,
- identical derived state does not push twice,
- play intent calls `play()` when session has a track with an asset and transport is stopped,
- play intent calls `pause()` when transport is playing,
- play intent is ignored when the session has no track with an asset,
- waveform click at `0.25` seeks to 25% of transport length,
- failed replacement load preserves the prior session track asset and surfaces an error,
- a later transport tick preserves the load error,
- successful load commits the track asset and clears the error,
- track asset changes appear as changed `TrackWaveformState` rows.

### View And Widget Tests

Keep these narrow:

- `TransportControls::setState(...)` updates enabledness and play/pause icon state,
- transport button clicks call `TransportControls::Listener`,
- `EditorView::setState(...)` projects to child controls,
- applying the same `last_load_error` twice does not present it twice,
- `TrackWaveformRow::setState(...)` calls `Thumbnail::setSource(...)` when the asset
  changes and does not call it again for the same asset.

### Audio Adapter Tests

Keep these focused and treat Tracktion/JUCE setup cost honestly:

- `Engine` implements `ITransport` state/listener behavior,
- `Engine::applyTrackAudio(...)` preserves existing content on failed replacement load,
- `TracktionThumbnail::setSource(...)` accepts `core::AudioAsset`.

If these require heavier Tracktion setup, they are adapter/integration tests, not the
main proof of editor workflow.

---

## CMake Evolution: Tripwires, Not Target Names

Keep `rock_hero_core`, `rock_hero_audio`, and `rock_hero_ui` as the CMake targets for
this refactor. Use folders as conceptual submodules first. Promote a submodule to its
own target only when one of these tripwires fires:

1. **Independent test surface needed** - enough tests in the submodule that mixing them
   into the parent library's test target hurts build times or triage.
2. **Different linkage requirements** - for example, controller code must be consumed
   by a headless tool that must not link JUCE.
3. **Alternative implementation swap** - for example, Tracktion-backed vs. raw-JUCE
   audio selected at link time.
4. **API stability contract** - an external consumer needs a narrower versioned surface
   than the full library.
5. **Build-time compartmentalization** - editing the submodule forces rebuilding
   unrelated code in the same library with material wall-clock cost.

No target names are pre-committed. If the `rock-hero-ui/controllers/` submodule later
needs a non-JUCE target, the split should happen in that future PR with the concrete
pressure documented.

---

## Explicit Anti-Scope

This plan does not:

- implement full multi-stem playback,
- add fixed track roles,
- add full undo/redo,
- put JUCE into `core::Session`,
- introduce `ITransportControls`,
- introduce `IThumbnailFactory` or a thumbnail cache,
- convert `audio::Thumbnail` into a pure-data waveform source,
- add `IPitchDetector`, `IToneEngine`, `IPluginHost`, `IClock`, `ISongRepository`,
  `IScoringInputStream`, or a cross-thread transport port,
- add a fourth top-level library or an application domain,
- split CMake targets,
- add snapshot, approval, or end-to-end tests, or
- introduce a replayable simulation harness.

None of these are blocked by this plan. The point is to make them easier by putting
session state, controller behavior, framework adapters, and app composition in the
right places now.

---

## Goal Alignment Checkpoint

This plan still targets the original reason for the refactor: make the editor path a
testable, modular slice of the architecture without prematurely splitting targets or
inventing ports for subsystems that do not exist yet.

- **Automated-testable workflow:** load, play/pause, stop, seek, failed-load handling,
  cursor derivation, and track-row state are handled by `EditorController` and tested
  with fakes. These tests do not require JUCE initialization, Tracktion runtime state,
  an audio device, or a message loop.
- **Framework isolation:** `rock-hero-ui` consumes `audio::ITransport`,
  `audio::IPlaybackContent`, and `audio::Thumbnail` headers, not `audio::Engine` or
  Tracktion types. `juce::File` conversion is isolated to the view's native-dialog
  callback.
- **Thin audio adapter:** `rock-hero-audio` owns Tracktion-backed transport,
  content-apply behavior, thumbnail/proxy generation, and framework translation. It
  does not own editor workflow or view-state policy.
- **Pure session model:** `core::Session` owns track content state without JUCE,
  Tracktion, or UI dependencies. It is the natural future home for pure undoable
  editor commands.
- **Presentation-focused UI:** `EditorView` renders `EditorViewState`, owns JUCE
  widgets, emits user intents, and performs edge-triggered presentation effects. It
  does not decide transport policy or seek math.
- **Small app composition root:** `MainWindow` creates concrete engine/session/editor
  objects and relies on `ui::Editor` to keep the editor feature fully wired.
- **Incremental delivery:** the refactor order keeps old paths alive until the new
  session/controller/view path is wired.
- **Deferred complexity stays deferred:** stem roles, undo/redo, plugin-host,
  waveform-data-source, cross-thread transport, scoring, and simulation ports remain
  out of scope until their first real consumers exist.

---

## Why This Is The Scalable Choice

The moves that make later growth painful:

- Concrete-engine references spreading across UI code. Addressed by ports and by
  removing `audio::Engine` from UI widgets/views.
- Workflow accreting in JUCE components. Addressed by `EditorController`.
- Assets masquerading as transport state. Addressed by `core::Session` and
  track-based view state.
- `core` acquiring framework dependencies. Addressed by keeping `Session`, `Track`,
  and future commands pure.
- App targets accumulating business logic. Addressed by `ui::Editor`.
- Framework types leaking into project boundaries. Addressed by `core::AudioAsset`,
  `PlaybackPosition`, and `PlaybackDuration`.
- Conflating ports with translation adapters. Addressed by the explicit inventory.
- Half-wired controller/view objects leaking into the app. Addressed by `ui::Editor`.
- Silent waveform staleness after asset replacement. Addressed by row-level asset
  diffing and automatic `Thumbnail::setSource(...)`.
- Premature CMake churn. Addressed by submodule folders plus tripwires.

The moves that do not make growth painful, even if deferred:

- choosing future CMake target names,
- adding track roles after a concrete need appears,
- integrating JUCE undo infrastructure around pure commands,
- introducing a thumbnail factory/cache when dynamic track rows require it,
- converting thumbnail drawing into pure waveform data when a non-JUCE renderer needs it.

---

## Supersession

Once this plan is agreed, remove from `docs/in-progress/` or archive:

- `testable-modular-architecture-refactor-plan.md` - original, superseded.
- `testable-modular-architecture-refactor-plan-revised.md` - v1, superseded.
- `testable-modular-architecture-refactor-plan-revised-v2.md` - v2, superseded.
- `testable-modular-architecture-refactor-plan-revised-v3.md` - v3, superseded.
- `testable-modular-architecture-refactor-plan-revised-v4.md` - v4, superseded.
- `testable-modular-architecture-refactor-plan-revised-v5.md` - v5, superseded.
- `testable-modular-architecture-refactor-plan-revised-v6.md` - v6, superseded.
- `testable-modular-architecture-refactor-plan-revised-v7.md` - v7, superseded.
- `testable-modular-architecture-refactor-plan-revised-v8.md` - v8, superseded.
- `testable-modular-architecture-refactor-plan-revised-v9.md` - v9, superseded.
- `testable-modular-architecture-refactor-plan-revised-v10.md` - v10, superseded.
- `testable-modular-architecture-refactor-plan-revised-v11.md` - v11, superseded.
- `testable-modular-architecture-refactor-plan-revised-v12.md` - v12, superseded.
- `testable-modular-architecture-refactor-plan-revised-v13.md` - v13, superseded.
- `testable-modular-architecture-refactor-plan-revised-v14.md` - v14, superseded.
- `testable-modular-architecture-refactor-plan-v15-concerns.md` - decisions applied.
- review note files whose feedback has been applied.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.
