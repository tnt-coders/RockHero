# Editor Controller Service Contract Cleanup

Status: completed.

## Purpose

Clean up `EditorController` construction so each dependency bundle expresses one clear contract:

- required audio capabilities are grouped in `AudioPorts`;
- required long-lived non-audio services are grouped in `Services`;
- defaultable project IO hooks are grouped in `ProjectOperations`;
- host exit remains a single explicit `ExitFunction` argument instead of being hidden in a broad
  service or callback bundle;
- controller-facing settings persistence uses a narrow port instead of concrete JUCE-backed
  settings.

This is the follow-up to the editor audio-ports cleanup and the project-operation progress cleanup.
It applies the same contract discipline to settings, task execution, project IO overrides, and host
exit.

## Final Shape

`EditorController::AudioPorts` remains the required audio capability bundle:

```cpp
struct AudioPorts final
{
    common::audio::ITransport& transport;
    common::audio::ISongAudio& song_audio;
    common::audio::IAudioDeviceConfiguration& audio_devices;
    common::audio::IPluginHost& plugin_host;
    common::audio::ILiveRig& live_rig;
    common::audio::ILiveInput& live_input;
};
```

`EditorController::Services` now contains only required long-lived non-audio collaborators:

```cpp
struct Services final
{
    IEditorSettings& settings;
    IEditorTaskRunner& task_runner;
};
```

`EditorController::ProjectOperations` contains optional project workflow overrides:

```cpp
struct ProjectOperations final
{
    OpenFunction open_function{};
    ImportFunction import_function{};
    SaveFunction save_function{};
    SaveAsFunction save_as_function{};
    PublishFunction publish_function{};
};
```

The constructor keeps required dependencies first, then the single host-exit callback, then optional
project operation overrides:

```cpp
explicit EditorController(
    AudioPorts audio_ports,
    Services services,
    ExitFunction exit_function,
    ProjectOperations project_operations = {});
```

`Editor` mirrors this vocabulary with its own nested `AudioPorts`, `Services`,
`ProjectOperations`, and `ExitFunction` types. `EditorView` does not receive a `Services` bundle
because it only needs view-facing audio ports and controller intents.

## Implemented Changes

- Added `IEditorSettings` as the controller-facing persistence port.
- Made `EditorSettings` implement `IEditorSettings`.
- Converted required services from nullable pointers to references.
- Removed the controller-owned inline task-runner fallback.
- Split project IO function overrides out of `Services` and into `ProjectOperations`.
- Kept `ExitFunction` as its own constructor argument because there is currently only one host
  callback and a wrapper type would not simplify the design.
- Updated `Editor`, app composition, and tests to use the same contract vocabulary.

## Naming Rationale

- `AudioPorts`: required audio capability ports.
- `Services`: required long-lived non-audio collaborators.
- `ProjectOperations`: defaultable project workflow operation hooks.
- `ExitFunction`: a single host-exit callback, kept explicit until host callbacks multiply.
- `IEditorSettings`: stable persistence port for app-local editor state.

Avoid putting optional project IO hooks back into `Services`. Avoid introducing a host callback
bundle until there is more than one host-owned callback.

## Verification

- `rock_hero_editor_core_tests` passed.
- `rock_hero_editor_ui_tests` passed.
- Full debug CTest passed: 332/332 tests.
