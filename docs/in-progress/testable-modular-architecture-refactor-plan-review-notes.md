# Review Notes for Revised Testable Architecture Plan

## Summary

`testable-modular-architecture-refactor-plan-revised.md` is stronger than the original plan. It
is more concrete, less speculative, and gives the editor refactor a reviewable migration path.
The best changes are:

- Using tripwires instead of pre-naming future CMake targets.
- Committing to the editor seam before more UI code grows around `audio::Engine`.
- Naming the public `audio::Engine::Listener` and `WaveformDisplay(audio::Engine&)` paths that
  should go away.
- Adding a worked presenter-test example.
- Deferring ports for subsystems that do not exist yet.

The concerns below are not objections to the revised direction. They are issues to resolve before
using the revised plan as the implementation guide.

## 1. `juce::File` Leaks Into the Presenter and Port Boundary

The revised plan calls `EditorPresenter` framework-free, but the proposed API includes:

```cpp
virtual bool loadFile(const juce::File& file) = 0;
void onLoadFileRequested(const juce::File&);
```

This does not make presenter tests impossible. `juce::File` is a relatively lightweight JUCE type,
and tests can usually construct it without a GUI message loop.

The problem is boundary quality:

- The presenter is no longer truly framework-free.
- Presenter tests must include/link JUCE core types.
- Headless tools, CLI importers, package validators, or future non-JUCE callers must manufacture
  `juce::File` to call a Rock Hero API.
- A project-owned port starts expressing JUCE's abstraction instead of Rock Hero's abstraction.

Recommended revision:

- Keep `juce::File` at the JUCE view/app edge.
- Convert it before calling the presenter.
- Use a project-owned type such as `core::AudioAssetRef`, `core::FilePath`, or possibly
  `std::filesystem::path` in presenter and audio-port contracts.

Example:

```cpp
namespace rock_hero::core
{
struct AudioAssetRef
{
    std::filesystem::path path;
};
}

class ITransport
{
public:
    virtual bool loadAudioAsset(const core::AudioAssetRef& asset) = 0;
};
```

Short-term, `juce::File` in the concrete `audio::Engine` adapter remains acceptable. Avoid putting
it in `EditorPresenter` and preferably avoid it in `ITransport`.

## 2. `TransportState` Should Not All Live in `core/timing`

The revised plan places this whole struct in `rock-hero-core/timing/`:

```cpp
struct TransportState
{
    bool file_loaded{false};
    bool playing{false};
    double position_seconds{0.0};
    double length_seconds{0.0};
};
```

`position_seconds` and `length_seconds` are timing values, but `file_loaded` and `playing` are
playback/session state. Putting the full snapshot in `core/timing/` blurs the core boundary.

Recommended revision:

- Put reusable time value types in core, e.g. `core::PlaybackPosition` and
  `core::PlaybackDuration`.
- Put the transport snapshot itself in `rock-hero-audio/api/`, because it describes audio
  transport state.

Example:

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
}

namespace rock_hero::audio
{
struct TransportState
{
    bool file_loaded{false};
    bool playing{false};
    core::PlaybackPosition position;
    core::PlaybackDuration length;
};
}
```

## 3. `IWaveformSource` Is Probably Not a Compile-Only Change

The revised plan says:

- `IWaveformSource` returns sample data and length.
- It draws nothing.
- `audio::Thumbnail` stays but implements `IWaveformSource`.

Current `audio::Thumbnail` is drawing-oriented:

```cpp
virtual void drawChannels(
    juce::Graphics& g, juce::Rectangle<int> bounds, float vertical_zoom) = 0;
```

Changing from "draw into a JUCE graphics context" to "return waveform data for UI rendering" is a
real contract change, not just a compile-only shape. It may be the right long-term direction, but
the plan should not understate the migration.

Recommended revision, choose one:

- Short-term pragmatic path: keep a UI-facing waveform drawing adapter and only remove direct
  `audio::Engine` coupling.
- Cleaner long-term path: introduce a separate `IWaveformDataSource` that returns peak buckets or
  sample summaries, and do not describe current `Thumbnail` as simply implementing it.

The long-term data-source design is more testable, but it needs a small design pass: bucket shape,
channel handling, duration mapping, proxy-generation state, and repaint/notification semantics.

## 4. View/Audio Dependency Story Needs One Clear Rule

The revised plan says:

- The view sees `EditorPresenter&` and nothing from the audio library.
- `EditorViewState` may contain a waveform source handle.
- `WaveformDisplay` takes `IWaveformSource*`.

Those statements conflict. Pick one rule.

Option A: UI widgets may depend on narrow audio ports.

- `WaveformDisplay` can take `audio::IWaveformSource*`.
- The line "the view sees nothing from the audio library" should be softened.
- This is pragmatic and probably acceptable for the waveform component.

Option B: Only presenters depend on audio ports.

- `WaveformDisplay` receives UI-owned render data only.
- The presenter or a view model converts audio waveform data into a presentation model.
- This is stricter but may add unnecessary copying or complexity early.

Recommendation:

- Use Option A for now.
- State clearly that JUCE widgets may depend on narrow, Tracktion-free audio ports when the
  displayed data is inherently audio-backed.
- Continue forbidding widgets from depending on `audio::Engine` or Tracktion implementation types.

## 5. Worked Presenter Test Should Avoid Optimistic UI Assumptions

The revised test sketch does this:

```cpp
presenter.onPlayPausePressed();

REQUIRE(transport.play_call_count == 1);
REQUIRE(sink.last_state.play_pause_shows_pause_icon == true);
```

That assumes either:

- the presenter updates the view optimistically before the transport reports `playing = true`, or
- the fake transport synchronously emits a new state during `play()`.

The plan also says transport state is pushed through `ITransport::Listener`, so the test should
make the source of truth explicit.

Recommended test shape:

```cpp
presenter.onPlayPausePressed();
REQUIRE(transport.play_call_count == 1);

transport.simulateState({.file_loaded = true,
                         .playing = true,
                         .position_seconds = 0.0,
                         .length_seconds = 10.0});

REQUIRE(sink.last_state.play_pause_shows_pause_icon == true);
```

This keeps transport state authoritative and avoids baking an optimistic-update policy into the
architecture accidentally.

## 6. `architecture.md` Update Should Happen With the First Dependency Change

The revised plan says the `architecture.md` dependency correction happens in step 8, after the
editor rewiring.

If step 1 places `TransportState` in core and `ITransport` in audio, then `rock-hero-audio`
already depends on `rock-hero-core`. That immediately invalidates the current architecture line
that says neither library depends on the other.

Recommended revision:

- Update `architecture.md` in the same commit as the first code change that makes
  `audio -> core` true.
- If `TransportState` stays in audio and only uses primitive doubles, the doc update can wait until
  the first actual `audio -> core` dependency.

Either way, tie the doc edit to the first dependency change, not to the final UI rewiring.

## 7. Dependency Diagram Is Easy to Misread

The vertical arrow diagram can be read in either direction depending on the reader:

```text
rock-hero-core
  ↑
rock-hero-audio
  ↑
rock-hero-ui
  ↑
apps
```

Recommended replacement:

```text
apps -> rock-hero-ui -> audio ports -> rock-hero-core
apps -> rock-hero-audio concrete implementation
rock-hero-audio -> rock-hero-core
```

Then explicitly state:

- `core` depends on no project-specific module.
- `audio` may depend on `core`.
- `ui` may depend on `core` and Tracktion-free audio ports.
- apps depend on concrete implementations and wire them together.

## Recommended Next Revision

Keep the revised plan as the base, but patch these points:

1. Replace `juce::File` in presenter/port contracts with a project-owned asset/path type.
2. Move the full `TransportState` snapshot to audio API, while keeping reusable time value types
   in core.
3. Decide whether `IWaveformSource` is a drawing adapter or a waveform-data source; do not treat
   a data-source conversion as compile-only.
4. Explicitly allow JUCE widgets to depend on narrow audio ports, or remove audio handles from
   view state. Prefer allowing narrow ports for now.
5. Rewrite the worked test to make transport state updates explicit.
6. Move the architecture-doc dependency update to the first commit that creates `audio -> core`.
7. Replace the vertical dependency diagram with explicit dependency arrows.

After those changes, the revised plan is stronger than the original and should be a good
implementation guide.
