\page guide_patterns Design Patterns in This Codebase

*Applies to: Repo-wide.*

Every pattern below is deliberate, recurring, and has a canonical exemplar you can open and read.
For each one: what it is, the exemplar with real code, where else it recurs, and when your new
code should reach for it. The rules behind the patterns are owned by
\ref design_architectural_principles; this page shows their concrete shape.

# Structural patterns

## Ports and adapters {#patterns_ports_and_adapters}

A *port* is a small pure-virtual interface the project owns; the *adapter* implements it with a
framework. Ports live in `include/` (public); adapters and their private headers live in `src/`
(on no consumer's include path) — the include-path split is itself the isolation mechanism.

Exemplar: `ITransport`
(`rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h`) is a
Tracktion-free control boundary returning typed errors; `Engine` adapts it (and eleven other
ports) onto Tracktion:

```cpp
class Engine : public ITransport,
               public IPlaybackClock,
               public ISongAudio,
               public IPluginHost,
               public ILiveRig,
               // ...seven more ports
```

Tests implement the same port by hand — `FakeTransport` in `test_transport.cpp` re-implements the
*contract* (it normalizes loop endpoints and rejects too-short loops exactly like the engine), so
tests exercise real rules, not mock plumbing.

Recurring: every `i_*.h` under `rock-hero-common/audio/include/`, editor ports
(`i_editor_controller.h`, `i_editor_view.h`, `i_editor_settings.h`, `i_editor_task_runner.h`),
game ports (`i_album_art_generator.h`, `i_library_directory_lister.h`, `i_game_settings.h`).

Reach for it when: behavior needs a framework, hardware, the filesystem, or a clock — and a test
will ever want to substitute it. Do not mint a port nothing fakes (see \ref guide_add_port).

## Pimpl — two different motivations

The same `struct Impl; std::unique_ptr<Impl> m_impl;` shape appears for two distinct reasons, and
knowing which one you need matters:

- **Framework isolation** — `Engine` (`engine.h` public, `src/engine/engine_impl.h` private).
  The Impl header includes `<tracktion_engine/tracktion_engine.h>`; the public header includes
  nothing framework-shaped. Consumers never compile Tracktion. `HighwayRenderer` gives bgfx the
  identical treatment in `common/ui`.
- **Compile firewall + file splitting** — `EditorController`
  (`src/controller/editor_controller_impl.h`). Its own docstring says why: the Impl exists "so
  the controller's member function definitions can be distributed across per-feature translation
  units while the state stays declared exactly once."

Both Impl headers live under `src/`, so no consumer can even reach them.

## Multi-TU coordination object

One class, one private state declaration, member definitions filed by feature. The `Engine`
defines its members across per-port files (`engine_transport.cpp`, `engine_live_rig.cpp`, ...);
`EditorController` across per-feature `*_handlers.cpp` files. A member body simply lives in its
slice's file:

```cpp
// engine_transport.cpp — a member of Engine::Impl, declared once in engine_impl.h
TransportState Engine::Impl::currentTransportState() const noexcept
{
    return TransportState{.playing = m_edit->getTransport().isPlaying()};
}
```

Reach for it when a deliberately unified object grows past one file. The conditions (declaration
header stays logic-free, helpers stay in their slice, the object's own `.cpp` is assembly only)
are in \ref design_architectural_principles, "Multi-TU Coordination Objects".

## Single-authority helpers (choke points)

When one rule has several call sites, the rule becomes one named function and every consumer
routes through it. The codebase deliberately collapses duplicated derivations onto such choke
points, because the second hand-written copy of a formula is where drift starts — the 2026-07
consolidation pass fixed exactly that class of bug three times (a hand-copied lane-band formula
missing its min-height guard, a region-boundary resolver dropping the sub-beat offset, a
seconds round-trip overshooting a grid step).

Exemplars, each the *only* home of its rule:

- `toneRegionSpanSeconds(...)` (`editor/core/src/tone/tone_track_projection.h`) — the one
  region-span rule; the tone-track projection, cursor-follow, and the active-region window all
  resolve spans through it.
- `gridStepBeats` / `adjacentTempoGridPosition`
  (`editor/core/include/.../timeline/tempo_grid_geometry.h`) — the one keyboard grid-step
  primitive behind both the chart caret step and the automation-lane nudge.
- `chartPlacementAt(...)` (`editor/core/src/controller/editor_controller.cpp`) — the single
  chart placement seam: caret arm, Alt-insert, and the insert ghost share one snap + occupancy
  judgement, so the ghost can never preview a placement the click would refuse.
- `setSelection(...)` (`editor_controller_impl.h`) — the one non-chart selection-assignment
  seam, carrying the fret-entry invalidation invariant so no assignment can forget it.
- `valueBandFor`/`valueBandY` (`ui/src/tone/tone_automation_lanes_view.h`) — the lane
  value-band geometry authority that replaced five hand-copied formulas across paint, hit-test,
  and editor anchoring.

Reach for it the moment you re-derive something a second time: name the rule, give it one home
next to its data, and route the existing call site through it in the same change.

# Modeling patterns

## Sum type for stable cases, interface for growing cases

The codebase deliberately uses both, split by which axis grows (the expression problem):

- **`EditorAction`** — a `std::variant` of ~40 small structs (`editor_action.h`), because new
  *operations over* actions (availability, busy policy, deferral, replay) arrive more often than
  new actions. Dispatch is one `std::visit`; every policy is an exhaustive `switch` with no
  `default`, so an unhandled case fails to compile:

  ```cpp
  void EditorController::Impl::performAction(EditorAction::Action action)
  {
      std::visit(
          [this](auto&& a) { performActionImpl(std::forward<decltype(a)>(a)); },
          std::move(action));
  }
  ```

- **`IEdit`** (`editor_undo_history.h`) — a three-method interface (`undo`, `redo`, `label`),
  because new *kinds of edit* keep arriving while the operation surface never changes. A concrete
  edit is a memento pair:

  ```cpp
  struct [[nodiscard]] PluginStateEdit final : IEdit
  {
      std::string instance_id;
      common::audio::PluginInstanceState before_state;
      common::audio::PluginInstanceState after_state;
      // undo() restores before_state; redo() restores after_state
  };
  ```

When adding a closed family, name which axis grows and pick the matching side; never convert one
to the other for symmetry.

## Typed boundary errors {#patterns_typed_errors}

Recoverable failures cross project-owned APIs as a code + message pair, never as raw framework
text. The shape is always the same: `enum class XxxErrorCode : std::uint8_t` + a
`struct [[nodiscard]] XxxError { code; message; }` + a `defaultXxxErrorMessage(code)` switch in
the `.cpp`, returned through `std::expected<T, XxxError>`. Framework failures are translated at
the boundary:

```cpp
// project_io.cpp — a JUCE failure becomes a domain error the caller can branch on
if (project_document_file.failedToOpen())
    return std::unexpected{ProjectError{
        ProjectErrorCode::InvalidProjectDocument,
        "Could not open project.json: " +
            project_document_file.getStatus().getErrorMessage().toStdString()}};
```

Callers branch on `result.error().code`, never by parsing message text. Recurring: every
`*_error.h`/`*_error.cpp` pair across all three products (transport, live rig, plugin host,
project, song import, library index, gameplay session, ...).

## Snapshots and atomic publish

State that crosses a boundary as a *read* is a plain value struct captured at one instant:
`PluginChainSnapshot`, `PlaybackClockSnapshot`, `AudioMeterSnapshot`, `EditorUndoHistorySnapshot`
— paired with a `snapshot()` method on the owner. For the one truly cross-thread case, the
playback clock publishes integer atomics (`AtomicPlaybackClock`,
`src/clock/atomic_playback_clock.h`): the message thread stores nanoseconds and parts-per-million,
any thread reads, no locks. Reach for a snapshot whenever a consumer needs "the state now"
without holding a reference into live mutable state.

# UI patterns

These three form one pipeline — data in, intents out — described end-to-end in
\ref guide_action_anatomy and \ref guide_2d_views.

## Projection

A free function `makeXxxViewState(domain...) -> XxxViewState`: a pure, headless transform from
domain data to render-ready state, resolving musical positions through the tempo map *once* so
painting never does musical math.

```cpp
[[nodiscard]] ToneTrackViewState makeToneTrackViewState(
    const common::core::Arrangement& arrangement, const common::core::TempoMap& tempo_map,
    const std::string& active_region_id, const std::string& selected_region_id);
```

Recurring: `tone_track_projection`, `tone_automation_projection`,
`input_calibration_projection` (editor core), `library_entry_projection` (game core),
`highway_projection` and `tab_projection` (common core — each feeds its renderer in both
products; tab promoted by plan 30 Phase 1). Projections are where headless tests live; write one
for anything a view will draw.

## View-state push

View-state structs are dumb PODs with defaulted `operator==`
(`transport_view_state.h` is the smallest exemplar); the controller's `deriveViewState()`
composes the aggregate `EditorViewState`, and components consume their slice via
`setState(const core::XyzViewState&)`, repainting on change. No component computes policy.

## Listener / intent

Components declare a nested `class Listener` of pure-virtual intent methods, take a `Listener&`
in the constructor, and report gestures without knowing what they mean:

```cpp
class Listener
{
public:
    virtual void onPlayPausePressed() = 0;
    virtual void onStopPressed() = 0;
};
explicit TransportControls(Listener& listener);
```

Recurring in every editor view (`signal_chain_view.h`, `tab_view.h`, `tone_track_view.h`,
`arrangement_view.h`, ...). `EditorView` implements them all and forwards to the controller.

# Asynchrony and lifetime patterns

## Workflow objects with busy tokens

A *workflow* is a stateful, non-copyable orchestrator owned by the controller that sequences
multi-step async choreography and rejects stale completions via a monotonic token:

```cpp
[[nodiscard]] std::uint64_t begin(BusyOperation operation);
[[nodiscard]] bool isCurrentToken(std::uint64_t token) const noexcept;
```

Exemplar: `BusyOperationWorkflow` (`editor/core/src/busy/`). Siblings: `SignalChainWorkflow`,
`PluginCatalogWorkflow`, and `InputCalibrationWorkflow` in common/audio. If your feature has
"start, maybe supersede, complete later" shape, it wants a workflow — not ad-hoc flags.

## Liveness guards — three variants, by owner kind

Every deferred callback checks that its owner still exists. The variant depends on what the owner
is:

```cpp
// (a) Project-owned non-component (EditorController::Impl::safeCallback)
return [wrapped_callback = std::forward<Callback>(callback),
        alive = std::weak_ptr<bool>{m_alive}](auto&&... args) mutable {
    if (alive.expired())
        return;
    wrapped_callback(std::forward<decltype(args)>(args)...);
};
```

(b) The engine's `callAsync` continuations capture the same `std::shared_ptr<bool>` alive-flag
idiom (`engine_impl.h`). (c) JUCE components use `juce::Component::SafePointer` — centralized for
dialogs in `showThemedDialogModally` (`ui/src/shared/themed_message_box.h`), which delivers the
result only while the owner component is alive, so call sites don't each track a pointer.

## RAII scoped guards and scoped listeners

Paired begin/end operations are types, not call pairs: `ScopedPluginUndoCaptureDeferral`
(`engine_impl.h`) defers plugin undo capture for exactly one scope;
`ScopedListener<Broadcaster, Listener>` (`common/audio/.../shared/scoped_listener.h`) subscribes
in its constructor and unsubscribes in its destructor — declared *last* in the owner so it
detaches first. If you write a manual `removeListener` in a destructor, use `ScopedListener`
instead.

## Per-operation task-state structs

Multi-step async operations keep their state in one heap-owned struct shared by the worker and
completion lambdas (`OpenTaskState`, `ImportTaskState`, `ProjectWriteTaskState` at the tail of
`editor_controller_impl.h`; `LiveRigLoadOperation`, `ToneChainReplaceOperation` in the engine).
One struct per operation kind — never a pile of loose captured locals.

# Creation patterns

- **Validated static factory:** construction that can fail is a
  `static std::expected<T, Error> create(...)` with a private constructor —
  `GameResources::create`, `HighwayRenderer::create`, `LiveInputMonitor`. There is no
  half-constructed object to misuse.
- **Factory interface:** when UI must create backend-owned objects without naming the backend —
  `IThumbnailFactory::createThumbnail(juce::Component& owner)`, implemented by `Engine` so the
  Tracktion thumbnail never leaks through the API.
- **Null object:** a do-nothing implementation that keeps a code path uniform.
  `NullAlbumArtGenerator` *ships in production* as the default until real album art lands;
  `NullEditorSettings` serves tests. Prefer a null object over optional-dependency `if`s.

# The test-double taxonomy

Test doubles follow a naming contract — the prefix tells you the behavior before you open the
file:

| Prefix | Behavior | Exemplar |
|---|---|---|
| `Fake*` | Real, minimal in-memory contract behavior | `FakeTransport`, `FakeLiveRig` |
| `Recording*` | Captures/counts every call for assertion | `RecordingEditorController` |
| `Configurable*` | Next outcome set per-test via public fields | `ConfigurableSongAudio` |
| `Immediate*` | Runs deferred work synchronously, inline | `ImmediateEditorTaskRunner` |
| `Null*` | No-op; fills a required dependency slot | `NullEditorSettings` |
| `InMemory*` | A real store, minus the filesystem | `InMemoryAudioConfigStore` |

When you write a new double, pick the prefix that matches its behavior and put reusable ones in
the library's `tests/include/.../testing/` folder.
