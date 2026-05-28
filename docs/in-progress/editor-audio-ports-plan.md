# Editor Audio Ports Plan

## Purpose

Replace `dynamic_cast`-based capability discovery and constructor overload combinations with
explicit, scoped construction bundles.

The editor currently discovers `ILiveInput` and `IAudioMeterSource` by `dynamic_cast`-ing
`ITransport&` at three sites. `EditorController` has six public constructor overloads encoding
combinations of optional audio ports. `Editor` has six matching overloads plus
`IThumbnailFactory&`. The app composition root passes `*m_audio_engine` repeatedly for different
interface roles.

The target shape is:

- `EditorController::AudioPorts` groups every audio dependency used by the headless controller.
- `Editor::AudioPorts` groups every audio dependency used by the composed editor feature.
- The app composition root constructs `Editor::AudioPorts` once from the concrete engine.
- `Editor` maps its bundle into `EditorController::AudioPorts` and passes only view-relevant
  pointers to `EditorView`.

This keeps the available audio capabilities visible in the type system without introducing a
freestanding `EditorAudioPorts` type that could become a vague shared bucket.

## Problem

### `dynamic_cast` Capability Discovery

Three sites discover optional capabilities from `ITransport&` at runtime:

1. `editor_controller.cpp` line 250: `liveInputFrom()` discovers `ILiveInput*` from `ITransport&`
   so the controller can manage input calibration and monitoring.

2. `editor_view.cpp` line 703: `EditorView` discovers `ILiveInput*` from `ITransport&` so the
   calibration window can read meter levels.

3. `editor.cpp` line 20: `meterSourceFrom()` discovers `IAudioMeterSource*` from `ITransport&`
   so the view can display level meters.

These work because `Engine` implements all these interfaces, so the casts succeed in production.
But the dependency is invisible in constructor signatures. A reader cannot see that the view needs
live-input capability. Tests must remember to make fake transport types implement unrelated
interfaces for discovery to work.

### Constructor Overload Explosion

`EditorController` has six public constructors:

1. `(ITransport&, ISongAudio&, IAudioDeviceConfiguration&, Services)`
2. `(ITransport&, ISongAudio&, IAudioDeviceConfiguration&, IPluginHost&, Services)`
3. `(ITransport&, ISongAudio&, IAudioDeviceConfiguration&, IPluginHost&, ILiveRig&, Services)`
4. `(ITransport&, ISongAudio&, Services)`
5. `(ITransport&, ISongAudio&, IPluginHost&, Services)`
6. `(ITransport&, ISongAudio&, IPluginHost&, ILiveRig&, Services)`

`Editor` has six matching overloads plus `IThumbnailFactory&` in each. Every new optional port
would multiply overloads again.

All controller overloads feed into one private constructor that takes raw pointers:

```cpp
EditorController(
    ITransport&,
    ISongAudio&,
    IAudioDeviceConfiguration*,
    IPluginHost*,
    ILiveRig*,
    ILiveInput*,
    Services);
```

A named aggregate expresses this more clearly and makes every capability visible at the call site.

### Composition Root Verbosity

The app entry point currently passes the same engine object six times:

```cpp
auto editor = std::make_unique<Editor>(
    *m_audio_engine,  // ITransport
    *m_audio_engine,  // IAudio
    *m_audio_engine,  // IAudioDeviceConfiguration
    *m_audio_engine,  // IPluginHost
    *m_audio_engine,  // ILiveRig
    *m_audio_engine,  // IThumbnailFactory
    services);
```

With a nested `Editor::AudioPorts` bundle, app composition constructs one named dependency object:

```cpp
ui::Editor::AudioPorts audio_ports{
    .transport = *m_audio_engine,
    .song_audio = *m_audio_engine,
    .thumbnail_factory = *m_audio_engine,
    .audio_devices = m_audio_engine.get(),
    .plugin_host = m_audio_engine.get(),
    .live_rig = m_audio_engine.get(),
    .live_input = m_audio_engine.get(),
    .meter_source = m_audio_engine.get(),
};

auto editor = std::make_unique<ui::Editor>(audio_ports, services);
```

`Engine` implements each editor-facing port, but that fact remains confined to app composition.
Editor core and editor UI still depend on project-owned interfaces, not on the concrete Tracktion
adapter.

## Design

### `EditorController::AudioPorts`

Add a nested public construction bundle to `EditorController`:

```cpp
class EditorController final : public IEditorController
{
public:
    struct AudioPorts final
    {
        common::audio::ITransport& transport;
        common::audio::ISongAudio& audio;
        common::audio::IAudioDeviceConfiguration* audio_devices{};
        common::audio::IPluginHost* plugin_host{};
        common::audio::ILiveRig* live_rig{};
        common::audio::ILiveInput* live_input{};
    };

    explicit EditorController(
        AudioPorts audio_ports, Services services = defaultServices());
};
```

`ITransport&` and `ISongAudio&` are required because the controller cannot function without them.
The remaining ports are optional capabilities. Tests construct the bundle with only the optional
ports needed by the behavior under test.

Do not add `EditorController::AudioPorts::from(Engine&)`. The controller library should not expose
or include the concrete `Engine` type for this composition helper.

### `Editor::AudioPorts`

Add a separate nested public construction bundle to `Editor`:

```cpp
class Editor final
{
public:
    struct AudioPorts final
    {
        common::audio::ITransport& transport;
        common::audio::ISongAudio& audio;
        common::audio::IThumbnailFactory& thumbnail_factory;

        common::audio::IAudioDeviceConfiguration* audio_devices{};
        common::audio::IPluginHost* plugin_host{};
        common::audio::ILiveRig* live_rig{};
        common::audio::ILiveInput* live_input{};
        const common::audio::IAudioMeterSource* meter_source{};
    };

    explicit Editor(
        AudioPorts audio_ports, core::EditorController::Services services = {});
};
```

`Editor::AudioPorts` intentionally differs from `EditorController::AudioPorts`:

- `thumbnail_factory` is required by the composed editor feature and view, not by the controller.
- `meter_source` is view-only and read-only, so it is `const IAudioMeterSource*`.
- `plugin_host` and `live_rig` are passed into the controller only; they should not be exposed to
  `EditorView`.

Keeping the bundles nested avoids creating a global `EditorAudioPorts` type with unclear
ownership. These are construction contracts for two specific classes, like
`EditorController::Services`.

### `EditorView` Parameters

Do not pass the full `Editor::AudioPorts` bundle to `EditorView`.

`EditorView` should receive only what it uses:

```cpp
EditorView(
    core::IEditorController& controller,
    const common::audio::ITransport& transport,
    common::audio::IThumbnailFactory& thumbnail_factory,
    common::audio::IAudioDeviceConfiguration* audio_devices = nullptr,
    const common::audio::IAudioMeterSource* audio_meters = nullptr,
    const common::audio::ILiveInput* live_input = nullptr);
```

This removes `EditorView`'s `dynamic_cast` discovery while keeping plugin-host and live-rig
capabilities out of the UI view surface.

### Header Dependencies

No new standalone public header is needed.

- `EditorController::AudioPorts` lives in `editor_controller.h`.
- `Editor::AudioPorts` lives in `editor.h`.
- Both headers can forward-declare the audio interface types used by reference or pointer.

Do not include `engine.h` from either editor header. App composition may include `engine.h` because
it constructs the concrete adapter.

### Constructor Ambiguity Guard

Both audio-port bundles and `Services` are aggregate-like construction types. Avoid ambiguous or
opaque calls such as:

```cpp
EditorController controller{transport, audio, {}};
```

Tests and call sites should spell the intended aggregate type:

```cpp
EditorController controller{
    EditorController::AudioPorts{.transport = transport, .song_audio = audio},
};

EditorController controller{
    EditorController::AudioPorts{.transport = transport, .song_audio = audio},
    EditorController::Services{.open_function = project_services.openFunction()},
};
```

The production `Editor` constructor should be `explicit` and should take only
`Editor::AudioPorts` plus optional services, so app composition remains unambiguous.

## Steps

### 0. Rename `IAudio` to `ISongAudio`

Rename `IAudio` to `ISongAudio` across the codebase before introducing the `AudioPorts` bundles.
This gives the port a name that communicates its role — song-level audio preparation and arrangement
activation — and distinguishes it from the other audio-related ports it will sit alongside in the
bundles.

- Rename the header from `i_audio.h` to `i_song_audio.h`.
- Rename the class from `IAudio` to `ISongAudio`.
- Update the `\file` and `\brief` Doxygen commands.
- Update all includes and references across `common/audio`, `editor/core`, `editor/ui`,
  `editor/app`, and tests.
- Update the `Engine` class declaration (it implements `ISongAudio`).

### 1. Add `EditorController::AudioPorts`

In `editor_controller.h`:

- Add forward declarations for every interface used by the bundle.
- Add `struct AudioPorts final` near `Services`.
- Replace the six public constructor declarations with one public constructor:

```cpp
explicit EditorController(
    AudioPorts audio_ports, Services services = defaultServices());
```

- Remove the private constructor that accepts six separate audio pointers.

In `editor_controller.cpp`:

- Remove `liveInputFrom()`.
- Initialize `Impl` from `audio_ports.transport`, `audio_ports.song_audio`, and the optional pointer
  fields.

### 2. Add `Editor::AudioPorts`

In `editor.h`:

- Forward-declare `IThumbnailFactory` and `IAudioMeterSource` along with the other audio ports.
- Add `struct AudioPorts final`.
- Replace the six public constructor declarations with one public constructor:

```cpp
explicit Editor(
    AudioPorts audio_ports, core::EditorController::Services services = {});
```

In `editor.cpp`:

- Remove `meterSourceFrom()`.
- Construct the controller with:

```cpp
core::EditorController::AudioPorts controller_audio_ports{
    .transport = audio_ports.transport,
    .song_audio = audio_ports.song_audio,
    .audio_devices = audio_ports.audio_devices,
    .plugin_host = audio_ports.plugin_host,
    .live_rig = audio_ports.live_rig,
    .live_input = audio_ports.live_input,
};
```

- Construct `EditorView` with `audio_ports.transport`, `audio_ports.thumbnail_factory`,
  `audio_ports.audio_devices`, `audio_ports.meter_source`, and `audio_ports.live_input`.

### 3. Update `EditorView`

In `editor_view.h` and `editor_view.cpp`:

- Add `const common::audio::ILiveInput* live_input = nullptr` to the constructor.
- Initialize `m_live_input` directly from that parameter.
- Remove the `dynamic_cast<const ILiveInput*>(&transport)` discovery.

The view should not receive `IPluginHost*` or `ILiveRig*`.

### 4. Update App Composition

In `rock-hero-editor/app/main.cpp`, replace repeated constructor arguments with one named bundle:

```cpp
rock_hero::editor::ui::Editor::AudioPorts audio_ports{
    .transport = *m_audio_engine,
    .song_audio = *m_audio_engine,
    .thumbnail_factory = *m_audio_engine,
    .audio_devices = m_audio_engine.get(),
    .plugin_host = m_audio_engine.get(),
    .live_rig = m_audio_engine.get(),
    .live_input = m_audio_engine.get(),
    .meter_source = m_audio_engine.get(),
};

auto editor = std::make_unique<rock_hero::editor::ui::Editor>(
    audio_ports,
    rock_hero::editor::core::EditorController::Services{
        .exit_function = &juce::JUCEApplicationBase::quit,
        .settings = m_editor_settings.get(),
        .task_runner = m_editor_task_runner.get(),
    });
```

Implicit conversion from `Engine*` to each optional interface pointer is acceptable here because
the app composition root owns the concrete adapter and knows which interfaces it implements.

### 5. Update Tests

Tests that construct `EditorController` should switch to named aggregate initialization.

Before:

```cpp
EditorController controller{transport, audio, audio_devices, plugin_host, live_rig, services};
```

After:

```cpp
EditorController controller{
    EditorController::AudioPorts{
        .transport = transport,
        .song_audio = audio,
        .audio_devices = &audio_devices,
        .plugin_host = &plugin_host,
        .live_rig = &live_rig,
        .live_input = &transport,
    },
    services,
};
```

Tests that need only required transport/audio dependencies should still use the bundle explicitly:

```cpp
EditorController controller{
    EditorController::AudioPorts{.transport = transport, .song_audio = audio},
};
```

Tests that construct `Editor` should use `Editor::AudioPorts`. UI tests that need meter display or
calibration behavior should pass `meter_source` and `live_input` directly rather than relying on
fake transport inheritance.

### 6. Remove Dead Code

- Delete `liveInputFrom()` in `editor_controller.cpp`.
- Delete `meterSourceFrom()` in `editor.cpp`.
- Remove any includes that existed only to support the old overload parameter lists.
- Keep interface includes where definitions call methods or need complete types.

## Test Impact

This should be a behavior-preserving refactor.

Expected test updates:

- `test_editor_controller.cpp` will change many constructor call sites but should become clearer
  because optional capabilities are named.
- `test_editor.cpp` will construct `Editor::AudioPorts`.
- `test_editor_view.cpp` should pass `live_input` and meter source explicitly for tests that need
  those capabilities.

No test should need a fake transport to implement `ILiveInput` only so discovery succeeds. If a
test needs live input, it should pass that capability explicitly.

## Exit Criteria

- No `dynamic_cast` capability discovery in `editor_controller.cpp`, `editor.cpp`, or
  `editor_view.cpp`.
- `EditorController` has one public constructor that takes `EditorController::AudioPorts`.
- `Editor` has one public constructor that takes `Editor::AudioPorts`.
- `EditorView` receives only the optional ports it directly uses.
- `meter_source` is typed as `const IAudioMeterSource*`.
- Neither editor public header mentions concrete `Engine`.
- App composition constructs `Editor::AudioPorts` once.
- All existing tests pass with no behavior changes.
