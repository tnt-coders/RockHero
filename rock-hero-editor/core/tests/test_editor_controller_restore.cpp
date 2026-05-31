#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Startup restores persisted audio-device state through the audio-device boundary.
TEST_CASE("EditorController restores serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"serialized_audio_device_restore"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setAudioDeviceState("serialized-device-state").has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings),
        noopExitFunction()
    };

    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"serialized-device-state"});
    CHECK(settings.audioDeviceState() == std::optional<std::string>{"serialized-device-state"});
}

// Invalid serialized audio-device state is discarded so future launches do not retry it.
TEST_CASE(
    "EditorController clears invalid serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"invalid_serialized_audio_device_restore"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setAudioDeviceState("invalid-serialized-device-state").has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.next_restore_serialized_device_state_error =
        common::audio::AudioDeviceConfigurationError{
            common::audio::AudioDeviceConfigurationErrorCode::InvalidSerializedState,
            "Serialized audio-device state is not XML.",
        };

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings),
        noopExitFunction()
    };

    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"invalid-serialized-device-state"});
    CHECK_FALSE(settings.audioDeviceState().has_value());
}

// Device-change notifications persist the serialized audio-device state from the audio boundary.
TEST_CASE("EditorController persists serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"serialized_audio_device_persist"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.serialized_device_state = "updated-serialized-device-state";
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings),
        noopExitFunction()
    };

    audio_devices.notifyChanged();

    CHECK(audio_devices.serialized_device_state_call_count == 1);
    CHECK(
        settings.audioDeviceState() ==
        std::optional<std::string>{"updated-serialized-device-state"});
}

// Empty capture results clear audio-device state instead of preserving stale settings.
TEST_CASE(
    "EditorController clears serialized audio device state when capture is empty",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"empty_serialized_audio_device_persist"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setAudioDeviceState("old-serialized-device-state").has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.serialized_device_state = std::nullopt;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings),
        noopExitFunction()
    };

    audio_devices.notifyChanged();

    CHECK(audio_devices.serialized_device_state_call_count == 1);
    CHECK_FALSE(settings.audioDeviceState().has_value());
}

// Missing restore paths are cleared without asking project IO to open anything.
TEST_CASE("EditorController clears missing restore path", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"missing_restore_path"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 0);
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// Valid restore paths are opened and kept when the controller accepts the project.
TEST_CASE("EditorController restores valid last project", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"valid_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// Startup restore leaves the saved path intact until the async open completion resolves.
TEST_CASE("EditorController restore keeps path while open is pending", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"pending_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings, runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK(settings.interruptedRestoreProject() == std::optional{files.projectFile()});

    runner.runPendingCompletions();

    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// Exiting while startup restore is pending leaves a recovery marker for the next launch.
TEST_CASE(
    "EditorController exit during pending restore marks interrupted", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"exit_pending_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    int exit_call_count = 0;
    std::optional<std::filesystem::path> setting_seen_at_exit{};
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings, runner),
        [&exit_call_count, &setting_seen_at_exit, &settings] {
            setting_seen_at_exit = settings.lastOpenProject();
            ++exit_call_count;
        },
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();
    controller.onExitRequested();

    CHECK(exit_call_count == 1);
    CHECK(setting_seen_at_exit == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK(settings.interruptedRestoreProject() == std::optional{files.projectFile()});

    runner.runPendingCompletions();

    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK(settings.interruptedRestoreProject() == std::optional{files.projectFile()});
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// An interrupted restore marker pauses auto-open and asks the user whether to retry.
TEST_CASE("EditorController prompts after interrupted restore", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"interrupted_restore_prompt"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    REQUIRE(settings.setInterruptedRestoreProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 0);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(
        state->restore_interrupted_prompt ==
        std::optional{RestoreInterruptedPrompt{files.projectFile()}});
    CHECK_FALSE(state->busy.has_value());
}

// OK on the interrupted-restore prompt retries the same project and clears the marker on success.
TEST_CASE("EditorController retries interrupted restore prompt", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"interrupted_restore_retry"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    REQUIRE(settings.setInterruptedRestoreProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings, runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.restoreLastOpenProject();
    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onRestoreInterruptedDecision(RestoreInterruptedDecision::Retry);

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK(settings.interruptedRestoreProject() == std::optional{files.projectFile()});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    CHECK_FALSE(busy_state->restore_interrupted_prompt.has_value());
    CHECK(busy_state->busy.has_value());

    runner.runPendingCompletions();

    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// Cancel on the interrupted-restore prompt starts empty and suppresses future auto-open.
TEST_CASE("EditorController cancels interrupted restore prompt", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"interrupted_restore_cancel"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    REQUIRE(settings.setInterruptedRestoreProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.restoreLastOpenProject();
    controller.onRestoreInterruptedDecision(RestoreInterruptedDecision::Cancel);

    CHECK(project_services.open_call_count == 0);
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->restore_interrupted_prompt.has_value());
    CHECK_FALSE(state->busy.has_value());
}

// Missing interrupted-restore paths are removed from both recovery and auto-open state.
TEST_CASE("EditorController clears missing interrupted restore", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"missing_interrupted_restore"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    REQUIRE(settings.setInterruptedRestoreProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 0);
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->restore_interrupted_prompt.has_value());
}

// A stored project path rejected by open is removed from future startup restore state.
TEST_CASE("EditorController clears restore path when open fails", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"failed_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// Startup restore clears failed paths only from the async completion path, not scheduling.
TEST_CASE("EditorController restore clears path after async failure", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"async_failed_restore_path"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings, runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK(settings.interruptedRestoreProject() == std::optional{files.projectFile()});

    runner.runPendingCompletions();

    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
}

// A restore request fired while the controller already has dirty work routes through the same
// unsaved-changes gate as Open, instead of overwriting the in-progress project. Today this only
// matters as a guard for future call sites that invoke RestoreProject after startup (a
// reopen-last-session menu item, a crash-recovery flow, etc.); the startup path is unaffected
// because the controller has nothing loaded yet at that point.
TEST_CASE("EditorController restore prompts for unsaved changes", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"restore_prompts_unsaved"};
    files.createProjectFile();
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    const int open_call_count_before_restore = project_services.open_call_count;
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == open_call_count_before_restore);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::RestoreProject}});
    }
}

} // namespace rock_hero::editor::core
