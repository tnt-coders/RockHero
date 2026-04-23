# Testable Modular Architecture Refactor Plan (Revised v19)

## Purpose of This Revision

Supersedes `testable-modular-architecture-refactor-plan-revised-v18.md`. v19 closes
three gaps in v18: `ThumbnailCreator` is now a top-level UI construction helper
instead of a nested `Editor` alias, the post-apply session commit is encoded as a
non-failing preconditioned API instead of a debug-only assert, and later transport
listener events are described without assuming a specific next tick or unchanged
transport snapshot. The shape from v18 stands.

### Fixes Introduced in v19

1. **`ThumbnailCreator` is a top-level UI helper.** v18 nested the alias under
   `Editor`, which would force `editor_view.h` to include `editor.h` just to name the
   constructor parameter while `editor.h` needs `editor_view.h` for its by-value view
   member. v19 moves the alias to namespace `rock_hero::ui`, outside `Editor`. The
   alias mentions `audio::Thumbnail`, but it belongs in UI because its purpose is
   wiring UI-owned rows without exposing concrete `audio::Engine` to UI code.
2. **The post-apply session commit cannot silently fail in release builds.** v18 used
   a debug assert around `Session::replaceTrackAsset(...)`. v19 adds a
   preconditioned, non-failing `Session::replaceExistingTrackAsset(...)` path for
   the controller after it has already validated the track id. Violating that
   precondition is a hard invariant failure in all builds, not a release-only ignored
   `false`.
3. **Later transport events are handled normally.** v18 implied the next natural
   transport listener invocation would carry the same state the controller had just
   derived. That is not guaranteed if playback is running or if no next tick occurs.
   v19 instead states that any later transport listener invocation is handled normally:
   the controller re-derives from current session and transport state and suppresses
   only genuinely unchanged `EditorViewState` pushes.

### Fixes Preserved From v18

1. **Thumbnail handoff inside `ui::Editor` is fully described.** `Editor` forwards
   the `ThumbnailCreator` into `EditorView`'s constructor. `EditorView` constructs
   its initial `TrackWaveformRow` and then calls the callback with the row component
   as `owner`, transferring the resulting `unique_ptr<audio::Thumbnail>` into the row
   via `TrackWaveformRow::setThumbnail(std::unique_ptr<audio::Thumbnail>)`. The
   callback runs once during view construction and is not retained.
2. **`applyTrackAudio(...)` suppresses all transport-listener events, not only
   length changes.** During `IPlaybackContent::applyTrackAudio(...)`, the engine must
   not invoke `ITransport::Listener::onTransportStateChanged(...)`, either
   synchronously or through a deferred post originating from work inside that call.
   The initiating controller picks up all resulting transport state changes via the
   post-apply `transport.state()` read.
3. **`ThumbnailCreator` is one-shot.** The `ThumbnailCreator` passed to `ui::Editor`
   is consumed once during construction, forwarded once to `EditorView`, and then
   discarded. Dynamic multi-row thumbnail creation is still deferred behind the
   `IThumbnailFactory` tripwire; the callback must not be stored as a member of
   `Editor`, `EditorView`, or `EditorController`.
4. **`IPlaybackContent` cross-links to the single-track-applied invariant.** The
   port comment cross-references the `TransportState.length` single-track-applied
   invariant so a reader does not need to reconcile "session is multi-track shaped"
   with "second track is unspecified" without context.

### Fixes Preserved From v17

1. **The controller validates track IDs before applying audio.** `EditorController`
   first verifies the track exists in `core::Session`. If it does not, the
   controller composes a load error and does not call
   `IPlaybackContent::applyTrackAudio(...)`. After successful apply, the session
   commit uses the preconditioned `replaceExistingTrackAsset(...)` path because the
   track was already validated.
2. **`ui::Editor` is fully wired at construction.** `ui::Editor` receives a
   thumbnail creation callback in its constructor, ensures the initial row
   thumbnail is in place before `EditorController::attachView(...)`, and exposes
   only the root component to the app. v18 fix #1 fills in the previously
   unspecified internal handoff.
3. **Content-apply transport-state changes do not emit transport events.** v17
   covered length only; v18 fix #2 broadens this to cover all transport-state
   changes caused by `applyTrackAudio(...)`.

### Fixes Preserved From v16

1. **`TransportState.length` scope is committed to single-track-applied
   semantics.** In this revision the editor applies audio to exactly one track at
   a time. `length` is the duration of that applied track. Multi-track-applied
   semantics are explicitly out of scope.
2. **The view sources the load-target track id from `EditorViewState`.** The view
   uses `EditorViewState.tracks.front().track_id`. `load_button_enabled` is
   `false` when `EditorViewState.tracks` is empty.
3. **Load-error message is composed by the controller.**
   `IPlaybackContent::applyTrackAudio(...)` returns only `bool`; the controller
   composes the `last_load_error` string from the failed asset request.
4. **Concurrency contract names `core::Session`.** `core::Session` is
   single-thread-owned by the message thread through the controller in
   production.
5. **Edge-triggered error display has a named storage mechanism.** `EditorView`
   stores `std::optional<std::string> m_last_presented_error` and presents
   `state.last_load_error` only when it differs.
6. **`TrackWaveformRow` owns its `audio::Thumbnail`.** Stored as
   `std::unique_ptr<audio::Thumbnail>`; `audio::Engine` must outlive `ui::Editor`
   (enforced by `MainWindow` member order).
7. **`TrackWaveformState::selected` is removed.** Was speculative.
8. **`Session::tracks()` reference-lifetime contract is documented.** Returned
   span is valid until the next mutating call on the session.

### Fixes Preserved From v15

1. **Session/content state is split from transport state.** Asset identity lives
   in `core::Session`/`Track`; `audio::ITransport` is narrowed to playback control
   and transport snapshots.
2. **Tracks are role-free for the first pass.**
3. **Undo/redo stays framework-neutral at the model boundary.** `core::Session`
   must not depend on JUCE.
4. **Controller naming replaces presenter naming** (`EditorController`,
   `IEditorView`, `attachView`).
5. **The view state is multi-track shaped.**
6. **The view/controller cycle is hidden inside `ui::Editor`.**
7. **Thumbnail creation stays on the engine boundary** via
   `audio::Engine::createThumbnail(...)` for now.
8. **Submodule folders do not create sub-namespaces.** All public UI declarations
   remain in `rock_hero::ui`.

### Fixes Preserved From v14

1. **Load errors have an explicit lifecycle.** Failed loads surface a cached
   `last_load_error`; ordinary transport ticks preserve it; a successful load
   clears it.
2. **Error rendering is edge-triggered in the view.** (Mechanism named per v16
   fix #6.)
3. **No unload/clear-thumbnail flow is added.**
4. **Goal alignment stays explicit.**

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
    // The returned span is valid until the next mutating call on this session
    // (addTrack, replaceTrackAsset, replaceExistingTrackAsset, ...). Callers
    // that need that view longer must copy out the values they need.
    [[nodiscard]] std::span<const Track> tracks() const noexcept;
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;

    TrackId addTrack(std::string name,
                     std::optional<AudioAsset> asset = std::nullopt);
    [[nodiscard]] bool replaceTrackAsset(TrackId id, AudioAsset asset);
    void replaceExistingTrackAsset(TrackId id, AudioAsset asset);

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

`replaceTrackAsset(...)` is the ordinary fallible mutation for callers that have not
already proved the track exists. `replaceExistingTrackAsset(...)` is for the
prevalidated controller path after a successful audio apply. Its precondition is that
`id` already exists in the session; violating that precondition is a hard invariant
failure in all builds. The controller uses this path so audio/session desync cannot be
silently ignored in release builds.

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

`length` is the duration of the currently applied audio. In this revision the editor applies
audio to exactly one track at a time, so `length` is the duration of that single
applied track. Multi-track-applied semantics (longest, sum, longest-non-muted, etc.)
are explicitly out of scope; they will be defined when stem playback lands.

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
stop, seek, non-content-apply transport-state changes, and position ticks.
Transport-state changes (length, position, playing flag) caused by
`IPlaybackContent::applyTrackAudio(...)` are intentionally excluded: the controller
that initiated the apply reads `transport.state()` after a successful apply and
pushes the resulting state itself. Consumers must diff derived state instead of
assuming "event equals transition."

**`rock_hero::audio::IPlaybackContent`** - content-loading/apply port for editor audio.

```cpp
namespace rock_hero::audio
{
class IPlaybackContent
{
public:
    virtual ~IPlaybackContent() = default;

    // Applies the asset to the named track. Updates internal transport state
    // (length, position, etc.) synchronously so that a subsequent
    // ITransport::state() call reflects the new content. During this call the
    // implementation must not invoke ITransport::Listener::onTransportStateChanged(...)
    // for any transport-state change caused by the apply, either synchronously
    // or through a deferred post originating from work performed within this
    // call. The controller that initiated the apply is responsible for a
    // post-apply derive from transport.state().
    //
    // In this revision the editor applies audio to exactly one track at a time
    // (see TransportState.length single-track-applied semantics). Implementations
    // may treat applying to a different track id across calls as unspecified
    // behavior; only the most recently applied track is required to behave
    // correctly. This restriction lifts when stem playback lands.
    virtual bool applyTrackAudio(core::TrackId track_id,
                                 const core::AudioAsset& asset) = 0;
};
} // namespace rock_hero::audio
```

This port is separate from `ITransport` because track audio identity is not transport
state. The controller uses it to ask the audio adapter to accept a candidate asset for a
track after first verifying that the track exists in `core::Session`. If the track is
missing, the controller does not call this port. If the port returns `false`, the
controller leaves `core::Session` unchanged and surfaces a load error. If it returns
`true`, the controller commits the already-validated session change through
`Session::replaceExistingTrackAsset(...)` and derives a new `EditorViewState`.

The first implementation maps the single editor track to today's backing-track
loading path. The shape still supports future stems because the operation is keyed by
`core::TrackId`.

#### Concurrency Contract

- `ITransport::state()`, `IPlaybackContent::applyTrackAudio(...)`, and
  `ITransport::Listener::onTransportStateChanged(...)` must not be invoked
  concurrently from multiple threads. In the production editor, these calls run on
  the JUCE message thread.
- `core::Session` is single-thread-owned. In production it is owned by the message
  thread through the controller. Future stem-loading or background-import paths must
  marshal mutations onto that thread or introduce a separate thread-safe
  coordinator.
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

`load_button_enabled` is `false` when `tracks` is empty. With the composition
root seeding one empty track row, the empty case does not arise during normal
startup, but the rule keeps the view honest if it ever does. Track-row-selection
state is intentionally not in this struct; it will land when concrete row-selection
behavior does.

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

The interface is deliberately narrow. It is not the full JUCE component surface; it
is only the state output path the controller needs.

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
without constructing the real controller and its session/audio dependencies. The
view sources the load-target `track_id` from
`EditorViewState.tracks.front().track_id`; with `tracks.empty()` it suppresses the
load button via `load_button_enabled`.

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
- `onLoadAudioAssetRequested(track_id, asset)` first verifies that `track_id` exists in
  `core::Session`. If not, it composes a load error, preserves session and audio state,
  and does not call `IPlaybackContent::applyTrackAudio(...)`. If the track exists, it
  calls `IPlaybackContent::applyTrackAudio(track_id, asset)`. On apply failure, it
  preserves the existing session track asset and pushes `last_load_error` composed by
  the controller (e.g., `"Failed to load audio asset: " + asset.path.string()`). On
  success, it commits the asset into `core::Session` via
  `session.replaceExistingTrackAsset(track_id, asset)`. The track was just validated
  and `core::Session` is single-thread-owned, so a missing track at this point is a
  violated invariant, not an ordinary recoverable failure. After the commit, the
  controller clears `last_load_error` and derives a new view state. Because
  `applyTrackAudio` updated transport state synchronously without firing transport
  listeners for any apply-caused change, the derive's `transport.state()` query
  returns the new content state.
- The controller composes the human-readable error string. The port returns only
  `bool`; widening it to carry a structured error is out of scope.

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

- Owns one `audio::Thumbnail` as `std::unique_ptr<audio::Thumbnail>`, set once via
  `setThumbnail(std::unique_ptr<audio::Thumbnail>)` shortly after construction.
- Receives one `TrackWaveformState`.
- Diffs the row's optional `AudioAsset` against the previously applied asset.
- Calls `audio::Thumbnail::setSource(asset)` when the asset changes to a present value.
- Draws its thumbnail and shared playhead cursor.
- Emits waveform click events to its parent through a local listener.

This is the mechanism that makes thumbnail refresh automatic: a session track asset
change changes `EditorViewState::tracks`, and applying that state to the row starts new
thumbnail generation. Lifetime: rows are destroyed during `ui::Editor` destruction;
`audio::Engine` must outlive `ui::Editor` (already guaranteed by `MainWindow` member
order).

**`rock_hero::ui::EditorView`** - JUCE component tree for the editor screen.

- Inherits `juce::Component`, `juce::KeyListener`, and `IEditorView`.
- Receives `IEditorController&` for user-intent output.
- Receives a `ThumbnailCreator` callback in its constructor and uses it exactly once:
  after constructing its initial `TrackWaveformRow`, the view invokes the callback
  with the row's component as `owner` and transfers the resulting
  `unique_ptr<audio::Thumbnail>` into the row via `TrackWaveformRow::setThumbnail(...)`.
  The callback is not stored.
- Owns the load button, `TransportControls`, waveform rows, and `juce::FileChooser`.
- Converts `juce::File` to `core::AudioAsset` at the file chooser callback.
- Renders `EditorViewState` by projecting transport state into
  `TransportControlsState` and track state into waveform rows.
- Sources the load-target `track_id` from `EditorViewState.tracks.front().track_id`
  and disables the load button when `tracks` is empty.
- Presents load errors on an edge: the view stores
  `std::optional<std::string> m_last_presented_error` and presents
  `state.last_load_error` only when it differs from `m_last_presented_error`.

**`rock_hero::ui::Editor`** - fully wired editor UI feature.

```cpp
namespace rock_hero::ui
{
using ThumbnailCreator =
    std::function<std::unique_ptr<audio::Thumbnail>(juce::Component& owner)>;

class Editor final
{
public:
    Editor(core::Session& session,
           audio::ITransport& transport,
           audio::IPlaybackContent& playback_content,
           ThumbnailCreator create_thumbnail);

    [[nodiscard]] juce::Component& component() noexcept;

private:
    EditorController m_controller;
    EditorView m_view;
};
} // namespace rock_hero::ui
```

`ThumbnailCreator` is intentionally a `rock_hero::ui` alias even though it returns an
`audio::Thumbnail`. The thumbnail backend is strongly coupled to `rock-hero-audio`,
but the construction decision exists to give UI-owned row components the correct
owner component without exposing concrete `audio::Engine` to `EditorView`.

`Editor` owns the controller and view. Its constructor:

1. Constructs `m_controller(session, transport, playback_content)`.
2. Constructs `m_view(m_controller, std::move(create_thumbnail))`. The view
   immediately constructs its initial `TrackWaveformRow` and uses the callback to
   create and attach the row's thumbnail before the view constructor returns.
3. Calls `m_controller.attachView(m_view)`.

By the time `Editor`'s constructor returns, the controller is attached, the row is
in the view tree, and the row holds its thumbnail. App code does not manually
sequence `setController`, `attachView`, or thumbnail attachment calls. The
`ThumbnailCreator` is consumed during view construction and is not retained by
either `Editor` or `EditorView`.

For the current one-row case the composition root supplies concrete thumbnail creation
as a lambda that closes over `audio::Engine::createThumbnail(...)`. The state model is
multi-track shaped so expanding to more rows does not rewrite the controller/session
contract; the dynamic thumbnail side will land with the `IThumbnailFactory` port.

### Renamed or Relocated

- `EditorPresenter` -> `EditorController`.
- `IEditorViewSink` -> `IEditorView`.
- `FakeEditorViewSink` -> `FakeEditorView`.
- `attachSink(...)` -> `attachView(...)`.
- Nested `Editor::ThumbnailCreator` is avoided; `ThumbnailCreator` is a top-level
  `rock_hero::ui` alias.
- Controller/state/interface files live under `rock-hero-ui/controllers/` but stay in
  namespace `rock_hero::ui`.
- `EditorViewState.loaded_asset` becomes `EditorViewState.tracks`.
- The old one-waveform display becomes a one-row case of the track waveform view.

### Deleted

- Asset loading and current-asset identity from `ITransport`.
- `ITransport::currentAsset()`.
- `ITransport::loadAudioAsset(...)`.
- `IEditorViewSink` terminology.
- `TrackWaveformState::selected` (was speculative; reintroduce when row-selection
  behavior lands).
- Any public `std::function` callback members in `TransportControls` sketches.
- Direct `audio::Engine` dependency from UI widgets and views.
- Legacy `Engine` public methods not required by the new ports after migration is
  complete.
- `Editor::initialRowComponent()` and `Editor::attachInitialThumbnail(...)` (v16
  artifacts; superseded by constructor-provided `ThumbnailCreator` plus internal
  view handoff).

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
        , m_editor(m_session,
                   m_audio_engine,
                   m_audio_engine,
                   [this](juce::Component& owner) {
                       return m_audio_engine.createThumbnail(owner);
                   })
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

`createInitialEditorSession()` seeds the editor with one empty track row.
`audio::Engine` implements both `audio::ITransport` and `audio::IPlaybackContent`.
`MainWindow` owns concrete objects and supplies concrete thumbnail creation through a
constructor callback, but it does not contain editor workflow or half-wired editor
assembly.

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
2. `EditorView` converts `juce::File` to `core::AudioAsset` and reads the target
   `TrackId` from `EditorViewState.tracks.front().track_id`.
3. `EditorView` calls
   `IEditorController::onLoadAudioAssetRequested(track_id, asset)`.
4. `EditorController` verifies `track_id` exists in `core::Session`. If not, it
   composes a load error, preserves all state, and pushes a view state. Stop.
5. `EditorController` calls `IPlaybackContent::applyTrackAudio(track_id, asset)`.
   The engine updates internal transport state (length, position, etc.) synchronously
   but does not invoke any transport listeners during this call.
6. **On failure**, the controller preserves `core::Session`, sets `last_load_error`
   to a controller-composed message, and pushes a view state.
7. **On success**, the controller calls
   `session.replaceExistingTrackAsset(track_id, asset)`, clears `last_load_error`,
   and derives a new view state. The derive reads `transport.state()`, which now
   reflects the new content because the engine updated it during step 5.
8. `EditorView::setState(...)` applies the row state.
9. `TrackWaveformRow` sees the asset changed to a present value and calls
   `Thumbnail::setSource(asset)`.
10. Any later transport listener invocation is handled normally. The controller
    re-derives from current session and transport state, then suppresses the push if
    the derived `EditorViewState` is unchanged.

This keeps failed replacement loads from corrupting the session, makes successful
asset updates automatically refresh the waveform thumbnail, and avoids transient
inconsistent view states caused by mid-load listener emission.

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
7. `TrackWaveformRow` owns its `audio::Thumbnail` as `std::unique_ptr`. The row is
   destroyed during `ui::Editor` destruction; `audio::Engine` must outlive
   `ui::Editor` (enforced by `MainWindow` member order).
8. `core::Session::tracks()` returns a span valid only until the next mutating call
   on the session. Callers that need to outlive that window must copy out the values.
9. The top-level `ThumbnailCreator` callback is consumed during view construction
   and is not retained by `Editor` or `EditorView`. Dynamic thumbnail creation for
   rows added at runtime is deferred behind the `IThumbnailFactory` tripwire.

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
| `IClock` | Core code references wall-clock time. Prefer injected transport position. |
| `ISongRepository` | Persistence policy varies or needs fakes in tests. |
| `IScoringInputStream` | Gameplay consumes player onsets separately from the raw detector. |
| `IWaveformDataSource` | UI needs waveform rendering outside JUCE or without `juce::Graphics`. |
| `IThumbnailFactory` | Dynamic row creation needs thumbnail creation or caching. |
| Cross-thread snapshot | Renderer/audio-thread consumer needs off-thread state. |

---

## Refactor Order

1. **Add core value types and session model.**
   - Add `core::AudioAsset`, `PlaybackPosition`, `PlaybackDuration`, `TrackId`,
     `Track`, and `Session`.
   - Add core tests for adding tracks, preserving order, replacing a track asset,
     replacing a prevalidated existing track asset, and rejecting missing track IDs.
   - Keep `Session` role-free and framework-free.

2. **Add focused audio ports.**
   - Add `audio::TransportState`, `audio::ITransport`, and `audio::IPlaybackContent`
     under Tracktion-free audio headers.
   - Do not add asset identity to `TransportState`.
   - Do not put load/apply methods on `ITransport`.
   - Document the all-state listener-suppression rule on
     `IPlaybackContent::applyTrackAudio(...)`.

3. **Adapt `audio::Engine` to the ports.**
   - Implement `ITransport` with play, pause, stop, seek, state, and listener methods.
   - Implement `IPlaybackContent::applyTrackAudio(...)` using the existing single-file
     load path as the first one-track implementation.
   - Update internal transport state (length, position, etc.) synchronously during
     apply.
   - Do not emit transport listener events for any state change caused by
     `applyTrackAudio(...)`; the initiating controller reads `transport.state()`
     after successful apply. A re-entrancy guard inside the engine is the simplest
     mechanism.
   - Keep legacy methods temporarily where needed for independent compilation.

4. **Update thumbnail source API.**
   - Replace `Thumbnail::setFile(const juce::File&)` with
     `Thumbnail::setSource(const core::AudioAsset&)`.
   - Convert `core::AudioAsset` to framework file objects inside the concrete thumbnail
     adapter.

5. **Refactor waveform UI toward rows.**
   - Introduce `TrackWaveformRow` or equivalent row-shaped component.
   - Give each row a `std::unique_ptr<audio::Thumbnail>` member set via
     `setThumbnail(std::unique_ptr<audio::Thumbnail>)` and a
     `setState(const TrackWaveformState&)` path.
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
   - Controller composes the human-readable `last_load_error` string from the
     failed asset request.
   - Controller validates `track_id` exists before calling
     `IPlaybackContent::applyTrackAudio(...)`.
   - Controller uses `session.replaceExistingTrackAsset(...)` after a successful
     apply.
   - Add headless controller tests with `FakeTransport`, `FakePlaybackContent`, and
     `FakeEditorView`.
   - Cover initial state push, duplicate suppression, play/pause, stop, seek, failed
     load preservation, invalid-track load rejection, successful load commit,
     track-row derivation, and the rule that `applyTrackAudio` does not fire
     transport listeners for any apply-caused change.

8. **Refactor `TransportControls`.**
   - Replace internal business rules with `TransportControlsState`.
   - Use a nested `Listener` interface and required listener reference.
   - Keep the widget concrete inside `EditorView`.

9. **Extract `EditorView`.**
   - Move `MainWindow::ContentComponent` responsibilities into `ui::EditorView`.
   - `EditorView` owns JUCE widgets, file chooser, transport controls, and waveform
     rows.
   - `EditorView` implements `IEditorView` and consumes `IEditorController&`.
   - Add the top-level `ThumbnailCreator` alias in a view/composition header, not in
     the controller contracts, because it names `juce::Component` and
     `audio::Thumbnail`.
   - `EditorView` receives a `ThumbnailCreator` in its constructor; after constructing
     the initial `TrackWaveformRow`, it invokes the callback with the row component as
     `owner` and transfers the resulting `unique_ptr<audio::Thumbnail>` into the row.
   - View sources the load-target `track_id` from `EditorViewState.tracks.front()`
     and disables the load button when `tracks` is empty.
   - Implement edge-triggered error display via `m_last_presented_error`.

10. **Add `ui::Editor`.**
    - Own `EditorController` and `EditorView`.
    - Accept a thumbnail creation callback in the constructor and forward it to
      `EditorView`.
    - Call `m_controller.attachView(m_view)` after `m_view` construction completes
      (which is after the row's thumbnail has been attached).
    - Expose only `component()` to the app.

11. **Rewire `MainWindow`.**
    - Make `MainWindow` own `audio::Engine`, `core::Session`, and `ui::Editor`.
    - Pass a lambda that delegates to `audio::Engine::createThumbnail(...)` into the
      `ui::Editor` constructor.
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

TEST_CASE("EditorController: applyTrackAudio state changes do not fire transport listeners")
{
    rock_hero::core::Session session;
    const auto track_id = session.addTrack("Full Mix");

    FakeTransport transport;
    FakePlaybackContent playback_content;
    // Configure the fake to update both length and position synchronously during
    // apply, mirroring the engine's load behavior. The fake must not invoke any
    // listener inside on_apply, matching the port contract.
    playback_content.on_apply = [&](rock_hero::core::TrackId, const auto&) {
        transport.set_state_silently(
            {.playing = false, .position = {0.0}, .length = {15.0}});
        return true;
    };

    FakeEditorView view;
    rock_hero::ui::EditorController controller{session, transport, playback_content};
    controller.attachView(view);

    const auto pushes_before = view.set_state_call_count;

    controller.onLoadAudioAssetRequested(
        track_id, rock_hero::core::AudioAsset{"/songs/new.wav"});

    // Exactly one push from the controller's post-apply derive.
    REQUIRE(view.set_state_call_count == pushes_before + 1);
    REQUIRE(view.last_state.tracks.front().audio_asset.has_value());
}

TEST_CASE("EditorController: invalid load track id does not apply audio")
{
    rock_hero::core::Session session;
    const auto valid_track_id = session.addTrack("Full Mix");
    const rock_hero::core::TrackId missing_track_id{valid_track_id.value + 1};

    FakeTransport transport;
    FakePlaybackContent playback_content;
    FakeEditorView view;

    rock_hero::ui::EditorController controller{session, transport, playback_content};
    controller.attachView(view);

    controller.onLoadAudioAssetRequested(
        missing_track_id, rock_hero::core::AudioAsset{"/songs/new.wav"});

    REQUIRE(playback_content.apply_call_count == 0);
    REQUIRE_FALSE(session.findTrack(valid_track_id)->audio_asset.has_value());
    REQUIRE(view.last_state.last_load_error.has_value());
}

TEST_CASE("EditorController: failed load preserves session and reports a composed error")
{
    rock_hero::core::Session session;
    const auto track_id = session.addTrack(
        "Full Mix", rock_hero::core::AudioAsset{"/songs/good.wav"});

    FakeTransport transport;
    FakePlaybackContent playback_content;
    playback_content.next_apply_result = false;

    FakeEditorView view;
    rock_hero::ui::EditorController controller{session, transport, playback_content};
    controller.attachView(view);

    controller.onLoadAudioAssetRequested(
        track_id, rock_hero::core::AudioAsset{"/missing.wav"});

    REQUIRE(view.last_state.tracks.front().audio_asset->path == "/songs/good.wav");
    REQUIRE(view.last_state.last_load_error.has_value());
    REQUIRE(view.last_state.last_load_error->find("/missing.wav") != std::string::npos);
}
```

Additional controller tests:

- events before `attachView` update the cache but do not push to a view,
- identical derived state does not push twice,
- play intent calls `play()` when session has a track with an asset and transport is
  stopped,
- play intent calls `pause()` when transport is playing,
- play intent is ignored when the session has no track with an asset,
- waveform click at `0.25` seeks to 25% of transport length,
- a later transport tick preserves the load error,
- successful load commits the track asset and clears the error,
- track asset changes appear as changed `TrackWaveformState` rows,
- invalid load track IDs do not call `IPlaybackContent::applyTrackAudio(...)`.

### View And Widget Tests

Keep these narrow:

- `TransportControls::setState(...)` updates enabledness and play/pause icon state,
- transport button clicks call `TransportControls::Listener`,
- `EditorView::setState(...)` projects to child controls,
- applying the same `last_load_error` twice does not present it twice (verifies the
  `m_last_presented_error` edge mechanism),
- `EditorView` disables the load button when `EditorViewState.tracks` is empty,
- `EditorView` sources `track_id` from `EditorViewState.tracks.front()` when calling
  `IEditorController::onLoadAudioAssetRequested(...)`,
- `EditorView` invokes the constructor-supplied `ThumbnailCreator` exactly once and
  installs the result on the initial row before its constructor returns,
- `TrackWaveformRow::setState(...)` calls `Thumbnail::setSource(...)` when the asset
  changes and does not call it again for the same asset.

### Audio Adapter Tests

Keep these focused and treat Tracktion/JUCE setup cost honestly:

- `Engine` implements `ITransport` state/listener behavior,
- `Engine::applyTrackAudio(...)` preserves existing content on failed replacement load,
- `Engine::applyTrackAudio(...)` does not invoke transport listeners for any
  apply-caused state change (length, position, or playing flag),
- `Engine::applyTrackAudio(...)` updates `transport.state()` synchronously so a
  post-apply read reflects the new content,
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
- define multi-track-applied `TransportState.length` semantics (longest, sum, etc.),
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
- add row-selection state to `TrackWaveformState` or `EditorViewState`,
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
  invalid-track rejection, cursor derivation, track-row state, and the
  content-apply listener rule are handled by `EditorController` and tested with
  fakes. These tests do not require JUCE initialization, Tracktion runtime state, an
  audio device, or a message loop.
- **Framework isolation:** `rock-hero-ui` consumes `audio::ITransport`,
  `audio::IPlaybackContent`, and `audio::Thumbnail` headers, not `audio::Engine` or
  Tracktion types. `juce::File` conversion is isolated to the view's native-dialog
  callback.
- **Thin audio adapter:** `rock-hero-audio` owns Tracktion-backed transport,
  content-apply behavior, thumbnail/proxy generation, and framework translation. It
  does not own editor workflow or view-state policy.
- **Pure session model:** `core::Session` owns track content state without JUCE,
  Tracktion, or UI dependencies. It is the natural future home for pure undoable
  editor commands. The single-thread-owned rule is documented in the concurrency
  contract.
- **Presentation-focused UI:** `EditorView` renders `EditorViewState`, owns JUCE
  widgets, emits user intents, sources track ids from view state, performs
  edge-triggered error presentation, and consumes the one-shot `ThumbnailCreator`
  during construction. It does not decide transport policy or seek math.
- **Small app composition root:** `MainWindow` creates concrete engine/session/editor
  objects and relies on `ui::Editor` to keep the editor feature fully wired. It
  supplies concrete thumbnail creation through a constructor callback instead of
  manually attaching thumbnails after construction.
- **Incremental delivery:** the refactor order keeps old paths alive until the new
  session/controller/view path is wired.
- **Deferred complexity stays deferred:** stem roles, multi-track-applied length
  semantics, undo/redo, plugin-host, waveform-data-source, cross-thread transport,
  scoring, thumbnail factory, and simulation ports remain out of scope until their
  first real consumers exist.

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
- Audio/session desync from stale track IDs. Addressed by validating the track exists
  before calling `IPlaybackContent::applyTrackAudio(...)` and by using the
  preconditioned `Session::replaceExistingTrackAsset(...)` commit path after a
  successful apply.
- Transient inconsistent view state during loads. Addressed by excluding all
  apply-caused transport-state changes from transport listener events and by using
  the controller's post-apply derive against `transport.state()`.
- Half-wired editor thumbnail setup. Addressed by constructor-provided thumbnail
  creation forwarded into `EditorView` and consumed during view construction.
- Premature CMake churn. Addressed by submodule folders plus tripwires.

The moves that do not make growth painful, even if deferred:

- choosing future CMake target names,
- adding track roles after a concrete need appears,
- integrating JUCE undo infrastructure around pure commands,
- introducing a thumbnail factory/cache when dynamic track rows require it,
- defining multi-track-applied `length` semantics when stems land,
- converting thumbnail drawing into pure waveform data when a non-JUCE renderer needs
  it.

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
- `testable-modular-architecture-refactor-plan-revised-v15.md` - v15, superseded.
- `testable-modular-architecture-refactor-plan-revised-v16.md` - v16, superseded.
- `testable-modular-architecture-refactor-plan-revised-v17.md` - v17, superseded.
- `testable-modular-architecture-refactor-plan-revised-v18.md` - v18, superseded by
  this v19.
- `testable-modular-architecture-refactor-plan-v15-concerns.md` - decisions applied.
- review note files whose feedback has been applied.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing.
