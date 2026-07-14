#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Owns a build-local game audio-config file so a game-source restore test starts and ends clean.
class ScopedGameFile final
{
public:
    explicit ScopedGameFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        remove();
    }

    ~ScopedGameFile()
    {
        remove();
    }

    ScopedGameFile(const ScopedGameFile&) = delete;
    ScopedGameFile& operator=(const ScopedGameFile&) = delete;
    ScopedGameFile(ScopedGameFile&&) = delete;
    ScopedGameFile& operator=(ScopedGameFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    void remove() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    std::filesystem::path m_path;
};

// Persists a calibrated game route so the editor's config store arms and reads the game source.
void writeCalibratedGameRoute(const std::filesystem::path& game_file, const std::string& blob)
{
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    common::audio::AudioConfigStore game_store{
        game_file, common::audio::AudioConfigStore::Access::ReadWrite
    };
    REQUIRE(
        game_store
            .setActiveDeviceRoute(
                common::audio::ActiveDeviceRoute{.serialized_state = blob, .identity = identity})
            .has_value());
    REQUIRE(game_store
                .saveInputCalibration(
                    common::audio::InputCalibrationState{
                        .calibration_gain = common::audio::Gain{6.0},
                        .input_device_identity = identity,
                    })
                .has_value());
}

} // namespace

// Startup restores the persisted device route from the editor's own audio-config store.
TEST_CASE("EditorController restores serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"serialized_audio_device_restore"};
    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "serialized-device-state", .identity = std::nullopt
                    })
                .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;

    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings, store),
        noopExitFunction()
    };

    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"serialized-device-state"});
    REQUIRE(store.activeDeviceRoute().has_value());
    CHECK(store.activeDeviceRoute()->serialized_state == "serialized-device-state");
}

// Invalid serialized device state is discarded from the store so future launches do not retry it.
TEST_CASE(
    "EditorController clears invalid serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"invalid_serialized_audio_device_restore"};
    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(
        store
            .setActiveDeviceRoute(
                common::audio::ActiveDeviceRoute{
                    .serialized_state = "invalid-serialized-device-state", .identity = std::nullopt
                })
            .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.next_restore_serialized_device_state_error =
        common::audio::AudioDeviceConfigurationError{
            common::audio::AudioDeviceConfigurationErrorCode::InvalidSerializedState,
            "Serialized audio-device state is not XML.",
        };

    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings, store),
        noopExitFunction()
    };

    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"invalid-serialized-device-state"});
    CHECK_FALSE(store.activeDeviceRoute().has_value());
}

// Device-change notifications persist the blob paired with the resolved input identity to the store.
TEST_CASE("EditorController persists serialized audio device state", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"serialized_audio_device_persist"};
    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore store;
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.serialized_device_state = "updated-serialized-device-state";
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings, store),
        noopExitFunction()
    };

    audio_devices.notifyChanged();

    CHECK(audio_devices.serialized_device_state_call_count == 1);
    REQUIRE(store.activeDeviceRoute().has_value());
    CHECK(store.activeDeviceRoute()->serialized_state == "updated-serialized-device-state");
    CHECK(store.activeDeviceRoute()->identity == std::optional{makeInputDeviceIdentity()});
}

// Empty capture results clear the stored route instead of preserving stale device state.
TEST_CASE(
    "EditorController clears serialized audio device state when capture is empty",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"empty_serialized_audio_device_persist"};
    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "old-serialized-device-state", .identity = std::nullopt
                    })
                .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.serialized_device_state = std::nullopt;
    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        controllerServices(settings, store),
        noopExitFunction()
    };

    audio_devices.notifyChanged();

    CHECK(audio_devices.serialized_device_state_call_count == 1);
    CHECK_FALSE(store.activeDeviceRoute().has_value());
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
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK(project_services.last_open_file == std::optional{files.projectFile()});
    CHECK(controller.currentProjectFile() == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->project_loaded == true);
    CHECK(state->project_load_id == 1);
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
    FakeEditorView view;
    controller.attachView(view);

    controller.restoreLastOpenProject();

    CHECK(project_services.open_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->project_loaded);
    CHECK(state->project_load_id == 0);
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
    controller.onImportRequested(std::filesystem::path{"song.rock"});

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

// While the game source is active the editor's config store is a read-only view of the game's file,
// so a device-change persist fails at that view instead of mutating the editor's own store route.
TEST_CASE(
    "EditorController persist while sourcing the game leaves the own route intact",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"game_source_persist_no_own_write"};
    const ScopedGameFile game_file{"game_source_persist.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    const common::audio::ActiveDeviceRoute editor_route{
        .serialized_state = "own-device-state", .identity = std::nullopt
    };
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());

    EditorAudioConfigStore editor_store{own_store, game_file.path()};
    REQUIRE(editor_store.gameSourceAvailable());
    editor_store.useGameSource(true);

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.serialized_device_state = "changed-device-state";
    audio_devices.current_input_identity = makeInputDeviceIdentity();

    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = defaultLiveInputMonitor(),
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };

    audio_devices.notifyChanged();

    // The persist attempt hit the read-only game view; the editor's own route is unchanged.
    CHECK(audio_devices.serialized_device_state_call_count == 1);
    REQUIRE(own_store.activeDeviceRoute().has_value());
    CHECK(own_store.activeDeviceRoute() == editor_route);
}

// A failed startup restore normally clears the stored route, but while sourcing the game that clear
// targets the read-only game view and fails, so the editor's own store route is left untouched.
TEST_CASE(
    "EditorController restore-failure clear while sourcing the game leaves the own route intact",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"game_source_clear_no_own_write"};
    const ScopedGameFile game_file{"game_source_clear.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    const common::audio::ActiveDeviceRoute editor_route{
        .serialized_state = "own-device-state", .identity = std::nullopt
    };
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());

    EditorAudioConfigStore editor_store{own_store, game_file.path()};
    REQUIRE(editor_store.gameSourceAvailable());
    editor_store.useGameSource(true);

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.next_restore_serialized_device_state_error =
        common::audio::AudioDeviceConfigurationError{
            common::audio::AudioDeviceConfigurationErrorCode::InvalidSerializedState,
            "Serialized audio-device state is not XML.",
        };

    const EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = defaultLiveInputMonitor(),
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };

    // Startup restore read the game route, failed to apply it, and tried to clear it; the clear hit
    // the read-only game view and failed, so the editor's own route is unchanged.
    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    REQUIRE(own_store.activeDeviceRoute().has_value());
    CHECK(own_store.activeDeviceRoute() == editor_route);
}

namespace
{

// Reports whether any state pushed to the view carried the audio-device open overlay.
[[nodiscard]] bool sawAudioDeviceOpenOverlay(const FakeEditorView& view)
{
    for (const EditorViewState& state : view.pushed_states)
    {
        if (state.busy.has_value() && state.busy->operation == BusyOperation::OpeningAudioDevice)
        {
            return true;
        }
    }

    return false;
}

} // namespace

// When the game route resolves to the device that is already open, the toggle applies the route
// inline: the guarded restore no-ops, so no "Opening audio device..." overlay is shown.
TEST_CASE(
    "EditorController game-audio toggle applies instantly when the device is unchanged",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"game_toggle_instant"};
    const ScopedGameFile game_file{"game_toggle_instant.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "own-device-state", .identity = std::nullopt
                    })
                .has_value());

    EditorAudioConfigStore editor_store{own_store, game_file.path()};
    REQUIRE(editor_store.gameSourceAvailable());

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    // The live device already matches the target route.
    audio_devices.device_state_matches_active = true;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, editor_store};

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = monitor,
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    // Ignore the startup restore; assert only the toggle's behavior.
    audio_devices.restore_serialized_device_state_call_count = 0;
    const int paint_callbacks_before = view.busy_overlay_paint_callback_count;

    int applying_call_count = 0;
    controller.onUseGameAudioSettingsChangeRequested(
        true, [&applying_call_count](bool) { ++applying_call_count; });

    // The toggle consulted the skip-if-unchanged predicate against the now-active game route.
    CHECK(audio_devices.device_state_matches_active_call_count >= 1);
    // An instant flip never hides the settings dialog.
    CHECK(applying_call_count == 0);
    CHECK(
        audio_devices.last_device_state_match_query ==
        std::optional<std::string>{"game-device-state"});
    // The guarded restore still ran (idempotent no-op), and the monitor source flip is not gated.
    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"game-device-state"});
    // No genuine re-open, so no busy overlay paint fence was scheduled.
    CHECK(view.busy_overlay_paint_callback_count == paint_callbacks_before);
    CHECK_FALSE(sawAudioDeviceOpenOverlay(view));
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
}

// When the game route resolves to a different device, the toggle re-opens the device behind the
// busy overlay so the blocking juce::AudioDeviceManager work paints "Opening audio device..." first.
TEST_CASE(
    "EditorController game-audio toggle re-opens behind the busy overlay when the device changes",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"game_toggle_reopen"};
    const ScopedGameFile game_file{"game_toggle_reopen.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "own-device-state", .identity = std::nullopt
                    })
                .has_value());

    EditorAudioConfigStore editor_store{own_store, game_file.path()};
    REQUIRE(editor_store.gameSourceAvailable());

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    // The live device differs from the target route, so a genuine re-open is required.
    audio_devices.device_state_matches_active = false;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, editor_store};

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = monitor,
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    audio_devices.restore_serialized_device_state_call_count = 0;
    const int paint_callbacks_before = view.busy_overlay_paint_callback_count;

    std::vector<bool> applying_calls;
    controller.onUseGameAudioSettingsChangeRequested(
        true, [&applying_calls](bool applying) { applying_calls.push_back(applying); });

    CHECK(audio_devices.device_state_matches_active_call_count >= 1);
    // The dialog was hidden for the re-open and reshown once the busy overlay cleared.
    CHECK(applying_calls == std::vector<bool>{true, false});
    // The re-open ran behind exactly one busy overlay paint fence.
    CHECK(view.busy_overlay_paint_callback_count == paint_callbacks_before + 1);
    CHECK(sawAudioDeviceOpenOverlay(view));
    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"game-device-state"});
    // The overlay clears once the re-open completes.
    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
}

// With no applying presentation (the cancel-time toggle restore), a required re-open runs inline on
// the calling path instead of entering the busy workflow, so the cancel's own staged-device rollback
// cannot supersede its token and drop the re-open.
TEST_CASE(
    "EditorController game-audio toggle re-opens inline without an applying presentation",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"game_toggle_inline"};
    const ScopedGameFile game_file{"game_toggle_inline.settings"};
    writeCalibratedGameRoute(game_file.path(), "game-device-state");

    EditorSettings settings{files.settingsFile()};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "own-device-state", .identity = std::nullopt
                    })
                .has_value());

    EditorAudioConfigStore editor_store{own_store, game_file.path()};
    REQUIRE(editor_store.gameSourceAvailable());

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    // The live device differs from the target route, so a genuine re-open is required.
    audio_devices.device_state_matches_active = false;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, editor_store};

    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        EditorController::Services{
            .settings = settings,
            .task_runner = immediateTaskRunner(),
            .message_thread_scheduler = immediateMessageThreadScheduler(),
            .audio_config_store = editor_store,
            .live_input_monitor = monitor,
            .editor_audio_config_store = &editor_store,
        },
        noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    audio_devices.restore_serialized_device_state_call_count = 0;
    const int paint_callbacks_before = view.busy_overlay_paint_callback_count;

    controller.onUseGameAudioSettingsChangeRequested(true, {});

    // The re-open ran synchronously with no busy presentation at all.
    CHECK(audio_devices.restore_serialized_device_state_call_count == 1);
    CHECK(
        audio_devices.last_restored_serialized_device_state ==
        std::optional<std::string>{"game-device-state"});
    CHECK(view.busy_overlay_paint_callback_count == paint_callbacks_before);
    CHECK_FALSE(sawAudioDeviceOpenOverlay(view));
}

} // namespace rock_hero::editor::core
