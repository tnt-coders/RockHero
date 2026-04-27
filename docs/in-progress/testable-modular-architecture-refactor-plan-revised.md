# Testable Modular Architecture Refactor Plan (Revised)

## Purpose of This Revision

This plan supersedes `testable-modular-architecture-refactor-plan.md`. The original had
the right direction but needed sharpening. The user's stated goal is to avoid painful
refactors as the project grows, so the revisions below lean toward *structural commitments
made now* — but only where deferring the commitment would create real migration pain
later. Where a commitment can be deferred without penalty (e.g., naming future CMake
targets, pre-declaring interfaces for subsystems that don't exist yet), this plan defers.

### Concerns That Motivated This Revision

1. **Two overlapping plans.** `rockhero_modular_cmake_plan.md` and the original refactor
   plan restate each other. Two in-progress docs drift. This plan is the single source of
   truth for the refactor; the two earlier docs are superseded.

2. **Speculative port list.** The original enumerated `ITransport`, `IPlaybackSession`,
   `IWaveformSource`, `IWaveformSourceFactory`, `IPlaybackStateListener`, `IToneEngine`,
   and `IPitchDetector` before most of the corresponding subsystems exist. Interfaces
   designed ahead of their implementations encode the wrong contract against later
   reality. This plan commits only to interfaces for subsystems that exist today, and
   specifies **tripwires** — observable conditions that trigger introducing the next
   interface when its subsystem arrives.

3. **Refactor order started at low leverage.** Extracting pure calculations (click-to-
   time math, cursor proportion) as step 1 is weak — those calculations are one line
   each. The leverage move is introducing the port and the presenter; the pure math
   falls out naturally from the presenter. This plan reorders: seam first, pure
   extractions as a consequence.

4. **The migration did not name what gets deleted or renamed.** The critical question
   about the seam is: what happens to `audio::Engine::Listener`, `audio::ScopedListener`,
   and the UI's direct dependency on `audio::Engine`? This plan commits to specific
   moves so the migration is reviewable.

5. **Speculative CMake target names.** The original listed future targets
   (`rock_hero_audio_api`, `rock_hero_audio_tracktion`, ...) before the pressure to split
   existed, anchoring names against unknown requirements. This plan replaces the name
   list with promotion **tripwires** — observable conditions that justify splitting,
   regardless of what the resulting target ends up being called.

6. **No worked test example.** Abstract test pyramids do not demonstrate payoff. This
   plan includes one concrete presenter test sketch so the target shape is legible.

7. **Doc updates listed as a separate phase.** `architecture.md` and
   `architectural-principles.md` updates should land in the same commits as the code
   changes that invalidate their current wording. This plan pins the one contradiction
   that must be fixed during Phase 1 (the "neither library depends on the other" line)
   and defers the rest until the code actually invalidates them.

---

## Guiding Principle

> **Pay for structure now where deferring it forces an invasive later refactor. Defer
> structure where adding it later is a local, non-disruptive change.**

Presenters, the audio port boundary, and dependency direction are in the first category.
They shape the foundation; bolting them on later is expensive because every subsequent
widget, gameplay subsystem, and adapter accretes against the current shape. Speculative
per-subsystem interfaces (pitch detection, plugin hosting, scoring input streams) are in
the second category — they can be introduced alongside their first implementation without
disturbing any other file in the project, because the surrounding structure already
expects ports.

This is the scalability stance. Commit early to the *dependency skeleton*. Add
interfaces and subtargets when their subsystems show up.

---

## Target Architecture (Condensed)

Three library domains stay. Dependency direction tightens:

```text
rock-hero-core       (no JUCE, no Tracktion, no threading, no real clocks)
  ↑
rock-hero-audio      (depends on core; Tracktion lives here, isolated)
  ↑
rock-hero-ui         (depends on core and on audio ports — not the concrete engine)
  ↑
apps (editor, game)  (compose concrete implementations)
```

Internal layout inside each library uses folders as conceptual submodules. CMake targets
stay at three (`rock_hero_core`, `rock_hero_audio`, `rock_hero_ui`) until a promotion
tripwire fires (see *CMake Evolution*).

Key inversions vs. today:

- `rock-hero-ui` must not know about `audio::Engine` the type. It depends on ports.
- `rock-hero-audio` may depend on `rock-hero-core` (today it does not, because core has
  nothing the audio library needs; this plan allows it, and the editor seam will start
  using it for shared types like `TransportState`).

---

## The Editor Seam — What Gets Added, Renamed, and Deleted

This is the most specific section because the migration lives or dies in these details.

### Added

**`rock_hero::audio::ITransport`** — narrow port for the editor's transport needs.

```cpp
struct TransportState
{
    bool file_loaded{false};
    bool playing{false};
    double position_seconds{0.0};
    double length_seconds{0.0};
};

class ITransport
{
public:
    virtual ~ITransport() = default;

    virtual bool loadFile(const juce::File& file) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(double seconds) = 0;

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
```

One state snapshot, one event. No granular "position changed" vs. "play state changed"
split until a presenter actually needs it — the snapshot is cheap, it prevents event-
ordering bugs, and splitting it later is local.

Whether `TransportState` lives in `rock-hero-audio` or `rock-hero-core` is a minor
judgment call. Put it in `rock-hero-core/timing/` — it's pure data and core will grow to
reference transport position from gameplay code anyway.

**`rock_hero::audio::IWaveformSource`** — abstracts the thumbnail for UI consumption.
Returns sample data and length. Draws nothing; the UI draws, the audio adapter samples.

**`rock_hero::ui::EditorPresenter`** — framework-free (no JUCE headers in the
implementation file either). Owns `ITransport&` and an optional `IWaveformSource*`.
Exposes:

- Intent methods: `onLoadFileRequested(const juce::File&)`, `onPlayPausePressed()`,
  `onStopPressed()`, `onWaveformClicked(double normalized_x)`.
- A view sink attachment point (push model): `attachSink(IEditorViewSink&)`.
- Implements `ITransport::Listener`; maps transport state into an `EditorViewState`
  and pushes it to the sink.

**`rock_hero::ui::EditorViewState`** — plain data struct representing what the view
needs to render: button enabled flags, current play/pause icon, cursor proportion,
loaded-file label, waveform source handle.

**`rock_hero::ui::IEditorViewSink`** — one method, `setState(const EditorViewState&)`.
The JUCE view implements it.

**Tests:** `libs/rock-hero-ui/tests/editor_presenter_tests.cpp` (no JUCE init). Test
fakes live in `libs/rock-hero-ui/tests/fakes/`:
`FakeTransport`, `FakeWaveformSource`, `FakeEditorViewSink`.

### Renamed or Relocated

**`audio::Engine`** stays as the concrete Tracktion-backed facade and grows to
implement `ITransport`. New UI code depends on `ITransport`; the legacy
`Engine::Listener` path is deleted by end of this refactor.

**`audio::Thumbnail`** stays but implements `IWaveformSource`. The name stays because
"thumbnail" is JUCE's own term for this concept and renaming for its own sake adds
noise.

### Deleted

**`audio::Engine::Listener`** (the public nested interface used by `WaveformDisplay`).
Replaced by `ITransport::Listener`. The engine may keep a *private* internal bridge
between Tracktion callbacks and `ITransport::Listener` dispatch, but no UI code inherits
from an engine-specific listener class.

**`WaveformDisplay`'s `audio::Engine&` constructor parameter** and its
`audio::ScopedListener<audio::Engine, audio::Engine::Listener>` member. The widget
becomes passive: it receives an `EditorViewState` (or a narrower slice) from the
presenter and exposes `on_seek` as `std::function<void(double)>` taking a normalized x.
The presenter converts normalized x to seconds — click-to-time math moves out of the
widget into the presenter, where it's testable.

**`audio::ScopedListener`** — if the only external user is removed, delete it. If it
remains useful as an audio-library internal RAII helper, retain it as an implementation
detail.

### Composition After the Refactor

The editor's `main_window` looks like this:

```cpp
// apps/rock-hero-editor/main_window.cpp
auto engine = std::make_unique<rock_hero::audio::Engine>(/* ... */);
auto waveform_source = engine->makeWaveformSource();
auto presenter = std::make_unique<rock_hero::ui::EditorPresenter>(
    *engine, waveform_source.get());
auto view = std::make_unique<rock_hero::ui::EditorView>(*presenter);
presenter->attachSink(*view);
```

The presenter sees an `ITransport&` and an `IWaveformSource*`. The view sees an
`EditorPresenter&` and nothing from the audio library.

---

## Ports: Committed Now vs. Deferred

### Introduced now (the editor path needs them)

- `rock_hero::audio::ITransport`
- `rock_hero::audio::IWaveformSource`

### Deferred — introduce when the subsystem lands

Each has a **tripwire**: the condition that justifies introducing the interface.

| Interface | Tripwire |
|---|---|
| `IToneEngine` | The first concrete effects/tone chain exists and a second consumer (editor preview + game) needs to drive it. |
| `IPitchDetector` | The first pitch/onset detector implementation exists and core gameplay code needs to consume detections. |
| `IPluginHost` | Plugin scanning/loading code exists and is invoked from more than one place. |
| `IClock` | Core code references real wall-clock time. The architectural principles doc explicitly says to inject transport position, not wall clock, so this interface may never be needed. |
| `ISongRepository` | Persistence policy needs to vary (filesystem, bundled resources, network) or needs fakes in tests. |
| `IScoringInputStream` | Gameplay subsystem exists and needs to consume a stream of player onsets separately from the raw detector. |

The tripwires matter because an interface introduced before its implementation exists
encodes a guessed contract. The pressure to refactor later comes from an interface that
doesn't match reality, not from one that doesn't exist.

**This does not delay scalability.** By the time any of these subsystems arrives, the
surrounding structure (ports at boundaries, presenters owning workflow, apps as
composition roots) already expects a port. Adding the interface is a local PR, not a
migration.

---

## Refactor Order (Revised)

Each step is independently compilable. Tests for step 4 do not wait on step 5.

1. **Define `ITransport`, `TransportState`, `ITransport::Listener`.** `TransportState`
   lives in `rock-hero-core/timing/`. `ITransport` lives in `rock-hero-audio/api/`.
   Compile-only step.

2. **Make `audio::Engine` implement `ITransport`.** Tracktion wiring unchanged
   internally. The existing `Engine::Listener` is still present; the engine now
   dispatches to both old and new listener types during the transition.

3. **Define `IWaveformSource`. Make `audio::Thumbnail` implement it.** Compile-only
   shape for now.

4. **Add `EditorPresenter`, `EditorViewState`, `IEditorViewSink` with unit tests.**
   `FakeTransport`, `FakeWaveformSource`, `FakeEditorViewSink` live in the test target.
   This lands the testing story before any JUCE widget changes.

5. **Refactor `WaveformDisplay`** to take `IWaveformSource*` and an `on_seek`
   callback emitting normalized x. Remove its engine listener. Remove `audio::Engine::
   Listener` inheritance from this component.

6. **Refactor `TransportControls`** to render from a view-state struct. Its public API
   already suits this; the change is removing the owner-side state machine
   (`updateButtonStates`, cached `m_is_playing`, `m_file_loaded`,
   `m_transport_position`) and letting the presenter push state.

7. **Rewire `main_window`** to compose `Engine` → `ITransport&` → `EditorPresenter` →
   `EditorView`. Delete `audio::Engine::Listener` — no remaining references. Delete
   `audio::ScopedListener` if unused.

8. **Update `architecture.md`** in the same commit as step 7. Specifically, replace
   the "neither library depends on the other" line with the tightened rule: core is the
   pure foundation; audio and ui may depend on it; ui may not depend on concrete audio
   implementations. No other doc edits in this phase.

---

## Testing: One Worked Example

In `libs/rock-hero-ui/tests/editor_presenter_tests.cpp`:

```cpp
TEST_CASE("EditorPresenter: play intent starts transport when file is loaded")
{
    FakeTransport transport;
    FakeEditorViewSink sink;
    rock_hero::ui::EditorPresenter presenter{transport, nullptr};
    presenter.attachSink(sink);

    transport.simulateState({.file_loaded = true,
                             .playing = false,
                             .position_seconds = 0.0,
                             .length_seconds = 10.0});

    presenter.onPlayPausePressed();

    REQUIRE(transport.play_call_count == 1);
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
                             .position_seconds = 0.0,
                             .length_seconds = 12.0});

    presenter.onWaveformClicked(0.25);

    REQUIRE(transport.last_seek_seconds == Approx(3.0));
}
```

No JUCE init. No Tracktion runtime. No audio device. These three tests cover the rules
currently encoded across `TransportControls::updateButtonStates()`, `WaveformDisplay::
mouseDown()`, and the glue in `ContentComponent`/`main_window`. After the refactor the
rules are observable from outside, and the game code can consume the same presenter
patterns when it needs similar transport semantics.

---

## CMake Evolution: Tripwires, Not Target Names

Keep `rock_hero_core`, `rock_hero_audio`, `rock_hero_ui`. Promote a submodule to its
own target when **any one** of these tripwires fires:

1. **Independent test surface needed** — the submodule has enough tests that mixing
   them into the parent library's test target hurts build times or confuses triage.
2. **Different linkage requirements** — e.g., the presenter submodule must not link
   JUCE because a headless tool or server-side target consumes it.
3. **Alternative implementation swap** — e.g., Tracktion-backed audio vs. raw-JUCE
   audio chosen at link time.
4. **API stability contract** — another repository or a long-lived external consumer
   needs a narrower versioned surface than the full library.
5. **Build-time compartmentalization** — editing the submodule forces rebuilding
   unrelated code in the same library, and the wall-clock cost is material.

Without one of these, a folder is enough. This plan does not pre-commit target names;
the names follow the split.

Based on the pressures this plan creates, the **likely first split** is whatever
houses the presenters, because they'll want to build without JUCE (tripwire #2). The
plan does not commit to a name or a date.

---

## Explicit Anti-Scope

This plan does **not**:

- Introduce `IPitchDetector`, `IToneEngine`, `IPluginHost`, `IClock`,
  `ISongRepository`, or `IScoringInputStream`.
- Add a fourth top-level library or an `application` domain.
- Move gameplay, scoring, or calibration logic into dedicated modules. That belongs to
  a later plan once the editor seam is proven and gameplay subsystems start
  accumulating.
- Split CMake targets.
- Rewrite `architecture.md` beyond the one-line dependency correction in step 8.
- Add snapshot, approval, or end-to-end tests.
- Introduce a replayable simulation harness. That is a first-class objective in the
  architectural principles doc, but it belongs in the gameplay-layer plan, not this
  one.

None of these are blocked by this plan. They become *easier* because the dependency
direction and presenter pattern are in place.

---

## Why This Is the Scalable Choice

The user's concern is that later growth will force a painful refactor. The moves that
make growth painful are:

- Concrete-engine references spreading across UI code. *Addressed: `ITransport` port.*
- Workflow accreting in JUCE components. *Addressed: presenter owns workflow.*
- `core` acquiring framework dependencies. *Addressed: dependency direction tightened.*
- App targets accumulating business logic. *Addressed: composition only.*
- Time and threading reaching into domain code. *Reinforced by principle; specific
  enforcement comes as gameplay lands and the replayable simulation plan follows.*

The moves that *don't* make growth painful, even if deferred:

- Choosing precise names for future CMake targets.
- Pre-declaring interfaces for subsystems that don't exist yet.
- Extracting one-line pure calculations into standalone types.

This plan commits to the first list and defers the second. When the pitch detector
arrives, its interface is added in the same PR as its first implementation, against a
presenter/gameplay layer already shaped to consume ports. That's the shape of a
codebase that scales without traumatic refactors — one where the *skeleton* is right
and the *organs* grow into it, rather than one where every speculative interface is
drawn before anatomy is known.

---

## Supersession

Once this plan is agreed, remove from `docs/in-progress/`:

- `rockhero_modular_cmake_plan.md` — content folded into this plan.
- `testable-modular-architecture-refactor-plan.md` — superseded by this plan.

`rockhero_architecture_analysis.md` and `testable-architecture-recommendations.md` are
diagnostic artifacts; archive or delete per preference. They are not load-bearing for
the refactor described here.
