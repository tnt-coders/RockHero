#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Open sets busy=OpeningProject with the default message before the worker's completion runs.
TEST_CASE("EditorController open begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::OpeningProject);
    CHECK(busy->message == "Opening project...");
    CHECK(busy->presentation == BusyPresentation::Animated);
    CHECK_FALSE(busy->progress.has_value());
    CHECK(busy->cancel_enabled == false);
}

// Open-time normalization validation reports the distinct audio-analysis busy phase.
TEST_CASE("EditorController open reports audio analysis state", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    DeferredEditorTaskRunner runner;
    int analysis_progress_call_count = 0;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = makeAnalyzingOpenFunction(
                std::filesystem::path{"source.wav"}, analysis_progress_call_count),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::AnalyzingBackingAudio);
    CHECK(busy->message == "Analyzing audio...");
    CHECK(analysis_progress_call_count == 1);
}

// Import sets busy=ImportingProject with the default message before the worker's completion runs.
TEST_CASE("EditorController import begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    controller.onImportRequested(std::filesystem::path{"first.psarc"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::ImportingProject);
    CHECK(busy->message == "Importing project...");
}

// Import-time normalization validation reports the distinct audio-analysis busy phase.
TEST_CASE("EditorController import reports audio analysis state", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    DeferredEditorTaskRunner runner;
    int analysis_progress_call_count = 0;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = makeAnalyzingImportFunction(
                std::filesystem::path{"source.wav"}, analysis_progress_call_count),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::AnalyzingBackingAudio);
    CHECK(busy->message == "Analyzing audio...");
    CHECK(analysis_progress_call_count == 1);
}

// Save sets busy=SavingProject before the deferred write completion restores normal state.
TEST_CASE("EditorController save begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveRequested();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::SavingProject);
    CHECK(busy->message == "Saving project...");
    CHECK(project_services.save_call_count == 1);
}

// Saving from an unsaved prompt clears the save overlay before the deferred open begins.
TEST_CASE("EditorController deferred save clears busy before open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset original_asset{
        .path = std::filesystem::path{"original.wav"}, .normalization = std::nullopt
    };
    const common::core::AudioAsset replacement_asset{
        .path = std::filesystem::path{"replacement.wav"}, .normalization = std::nullopt
    };
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(original_asset.path);
    controller.onOpenRequested(std::filesystem::path{"original.rhp"});
    runner.runPendingCompletions();
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();

    addKnownPlugin(controller);

    project_services.next_song = makeSong(replacement_asset.path);
    controller.onOpenRequested(std::filesystem::path{"replacement.rhp"});

    const EditorViewState* prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(
        prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::OpenProject}});

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);

    const EditorViewState* saving_state = stateOrNull(view.last_state);
    REQUIRE(saving_state != nullptr);
    const BusyViewState* saving_busy = busyOrNull(*saving_state);
    REQUIRE(saving_busy != nullptr);
    CHECK(saving_busy->operation == BusyOperation::SavingProject);
    CHECK(project_services.save_call_count == 1);

    runner.runPendingCompletions();

    CHECK(runner.pendingCount() == 1);
    REQUIRE(view.pushed_states.size() >= 2);
    const EditorViewState& saved_state = view.pushed_states[view.pushed_states.size() - 2];
    const EditorViewState& opening_state = view.pushed_states.back();

    CHECK_FALSE(saved_state.busy.has_value());
    CHECK(saved_state.project_loaded == true);
    CHECK(saved_state.arrangement.audio_asset == std::optional{original_asset});
    CHECK_FALSE(saved_state.unsaved_changes_prompt.has_value());

    const BusyViewState* opening_busy = busyOrNull(opening_state);
    REQUIRE(opening_busy != nullptr);
    CHECK(opening_busy->operation == BusyOperation::OpeningProject);
    CHECK(opening_state.arrangement.audio_asset == std::optional{original_asset});

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == true);
    CHECK(final_state->arrangement.audio_asset == std::optional{replacement_asset});
    CHECK(
        controller.currentProjectFile() == std::optional{std::filesystem::path{"replacement.rhp"}});
}

// Save As sets busy=SavingProjectAs before the deferred write completion commits the path.
TEST_CASE("EditorController save as begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveAsRequested(std::filesystem::path{"renamed.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::SavingProjectAs);
    CHECK(busy->message == "Saving project...");
    CHECK(project_services.save_as_call_count == 1);
}

// Publish sets busy=PublishingProject before the deferred package completion restores state.
TEST_CASE("EditorController publish begins busy with default message", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .publish_function = project_services.publishFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    const BusyViewState* busy = busyOrNull(*state);
    REQUIRE(busy != nullptr);
    CHECK(busy->operation == BusyOperation::PublishingProject);
    CHECK(busy->message == "Publishing project...");
    CHECK(project_services.publish_call_count == 1);
}

// While busy, action routing disables ordinary commands and keeps Close available to supersede.
TEST_CASE("EditorController busy routing disables ordinary commands", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK(state->open_enabled == false);
    CHECK(state->import_enabled == false);
    CHECK(state->save_enabled == false);
    CHECK(state->save_as_enabled == false);
    CHECK(state->publish_enabled == false);
    CHECK(state->transport.play_pause_enabled == false);
    CHECK(state->transport.stop_enabled == false);
    CHECK(state->signal_chain.add_plugin_enabled == false);
    CHECK(state->signal_chain.remove_plugins_enabled == false);
    CHECK(state->plugin_browser.scan_enabled == false);
    CHECK(state->plugin_browser.add_enabled == false);
    CHECK(state->close_enabled == true);
}

// Verifies Close remains available through the same supersede policy when a project is loaded.
TEST_CASE(
    "EditorController busy keeps close enabled for a loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"loaded.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    runner.runPendingCompletions();

    const EditorViewState* loaded_state = stateOrNull(view.last_state);
    REQUIRE(loaded_state != nullptr);
    CHECK_FALSE(loaded_state->busy.has_value());
    CHECK(loaded_state->close_enabled == true);

    project_services.next_song = makeSong(std::filesystem::path{"second.wav"});
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());
    CHECK(busy_state->close_enabled == true);
}

// Direct controller calls still go through action routing, so busy state blocks mutations even
// when the JUCE overlay is bypassed.
TEST_CASE("EditorController busy routing blocks direct commands", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .publish_function = project_services.publishFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(std::filesystem::path{"loaded.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});
    runner.runPendingCompletions();
    // Drain the completed load here; this test's pending-count assertion is about the next open
    // request.
    runner.runPendingCompletions();
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();
    addKnownPlugin(controller);
    plugin_host.catalog_scan_call_count = 0;
    plugin_host.known_candidates_call_count = 0;
    plugin_host.add_call_count = 0;
    plugin_host.remove_call_count = 0;
    plugin_host.open_call_count = 0;

    transport.current_position = common::core::TimePosition{1.0};
    project_services.next_song = makeSong(std::filesystem::path{"pending.wav"});
    controller.onOpenRequested(std::filesystem::path{"pending.rhp"});
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);
    transport.stop_call_count = 0;
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->busy.has_value());

    controller.onOpenRequested(std::filesystem::path{"blocked.rhp"});
    controller.onImportRequested(std::filesystem::path{"blocked.psarc"});
    controller.onSaveRequested();
    controller.onSaveAsRequested(std::filesystem::path{"blocked.rhp"});
    controller.onPublishRequested(std::filesystem::path{"blocked.rock"});
    controller.onPlayPausePressed();
    controller.onStopPressed();
    controller.onWaveformClicked(0.5);
    controller.onPluginBrowserRequested();
    controller.onPluginCatalogScanRequested();
    controller.onAddPluginRequested("catalog-plugin-id");
    controller.onRemovePluginRequested("instance-id");
    controller.onOpenPluginRequested("instance-id");

    CHECK(runner.pendingCount() == 1);
    CHECK(project_services.open_call_count == 2);
    CHECK(project_services.import_call_count == 0);
    CHECK(project_services.save_call_count == 0);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.publish_call_count == 0);
    CHECK(transport.play_call_count == 0);
    CHECK(transport.stop_call_count == 0);
    CHECK(transport.seek_call_count == 1);
    CHECK(plugin_host.catalog_scan_call_count == 0);
    CHECK(plugin_host.known_candidates_call_count == 0);
    CHECK(plugin_host.add_call_count == 0);
    CHECK(plugin_host.remove_call_count == 0);
    CHECK(plugin_host.open_call_count == 0);
}

// Completion of a successful open clears busy and publishes the final committed state.
TEST_CASE("EditorController open completion clears busy and commits", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* loaded_state = stateOrNull(view.last_state);
    REQUIRE(loaded_state != nullptr);
    CHECK_FALSE(loaded_state->busy.has_value());
    CHECK(loaded_state->project_loaded == true);
    CHECK(view.shown_errors.empty());
}

// A failed open clears busy first, then reports the error through the existing one-shot path.
TEST_CASE(
    "EditorController failed open clears busy then reports error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_error_message = "missing package";
    controller.onOpenRequested(std::filesystem::path{"missing.rhp"});

    runner.runPendingCompletions();

    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->busy.has_value());
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not open: missing package");
}

// Close during a busy open supersedes the in-flight operation through action routing. The
// worker's deferred completion sees a generation mismatch and the loaded song is never committed.
TEST_CASE("EditorController close during busy supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"superseded.wav"});
    controller.onOpenRequested(std::filesystem::path{"superseded.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onCloseRequested();
    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK(audio.set_active_arrangement_call_count == 0);
    CHECK(view.shown_errors.empty());
}

// Exit during a busy open follows the same supersede path through closeProject() and triggers
// the composition host's exit callback.
TEST_CASE("EditorController exit during busy supersedes open", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    int exit_call_count = 0;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        [&exit_call_count]() { ++exit_call_count; },
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"never_committed.wav"});
    controller.onOpenRequested(std::filesystem::path{"never_committed.rhp"});
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onExitRequested();
    CHECK(exit_call_count == 1);

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK(audio.set_active_arrangement_call_count == 0);
}

// Close during a busy save invalidates the deferred completion so it cannot restore the project.
TEST_CASE("EditorController close during busy save supersedes write", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    runner.runPendingCompletions();

    controller.onSaveRequested();
    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    controller.onCloseRequested();
    const EditorViewState* closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK_FALSE(closed_state->busy.has_value());
    CHECK(closed_state->project_loaded == false);

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == false);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    CHECK(transport.stop_call_count == 1);
    CHECK(view.shown_errors.empty());
}

// Exiting during a busy save still remembers the current project path before closing the session.
TEST_CASE("EditorController exit during busy save persists file", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"busy_save_exit"};
    EditorSettings settings{files.settingsFile()};
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
            .save_function = project_services.saveFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(files.projectFile());
    runner.runPendingCompletions();

    controller.onSaveRequested();
    controller.onExitRequested();

    CHECK(exit_call_count == 1);
    CHECK(setting_seen_at_exit == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());

    runner.runPendingCompletions();

    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
}

// A stale completion (busy token no longer matches) does not finish busy state: if another
// operation is busy when the stale completion fires, the live busy state is preserved.
TEST_CASE(
    "EditorController stale completion preserves live busy state", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});

    // Supersede the first open by closing, then start a second open. The first open's pending
    // completion is now stale. The second open's busy state must survive the stale completion.
    controller.onCloseRequested();
    project_services.next_song = makeSong(std::filesystem::path{"second.wav"});
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const EditorViewState* busy_state = stateOrNull(view.last_state);
    REQUIRE(busy_state != nullptr);
    REQUIRE(busy_state->busy.has_value());

    runner.runPendingCompletions();

    const EditorViewState* final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    // The second open's completion fires after the first (stale) one. The first's discard does
    // not clear busy; the second's successful commit does. project_loaded should reflect the
    // second song only.
    CHECK_FALSE(final_state->busy.has_value());
    CHECK(final_state->project_loaded == true);
    CHECK(audio.set_active_arrangement_call_count == 1);
    const common::core::AudioAsset* active_asset =
        audio.last_active_audio_asset.has_value() ? &*audio.last_active_audio_asset : nullptr;
    REQUIRE(active_asset != nullptr);
    CHECK(active_asset->path == std::filesystem::path{"second.wav"});
}

// Stop on the message thread is not required by the task runner contract, but the controller
// must still call ISongAudio::prepareSong() during the message-thread commit stage rather than the
// worker. The deferred runner exposes this: prepareSong is not called until completion runs.
TEST_CASE(
    "EditorController prepareSong runs on message-thread completion stage",
    "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    DeferredEditorTaskRunner runner;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(runner),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    // After submit, work has run (open_function called) but completion is deferred.
    CHECK(project_services.open_call_count == 1);
    CHECK(audio.prepare_song_call_count == 0);

    runner.runPendingCompletions();

    CHECK(audio.prepare_song_call_count == 1);
}

// Audio-device open is scheduled behind the busy overlay paint fence so the blocking
// presentation paints once before juce::AudioDeviceManager occupies the message thread.
TEST_CASE(
    "EditorController schedules audio device open via paint fence", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), defaultControllerServices(), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    int audio_device_change_call_count = 0;
    controller.onAudioDeviceChangeRequested(
        [&audio_device_change_call_count] { audio_device_change_call_count += 1; });

    const BusyViewState* audio_device_busy = nullptr;
    for (const EditorViewState& pushed_state : view.pushed_states)
    {
        if (pushed_state.busy.has_value())
        {
            audio_device_busy = &*pushed_state.busy;
        }
    }

    REQUIRE(audio_device_busy != nullptr);
    CHECK(audio_device_busy->operation == BusyOperation::OpeningAudioDevice);
    CHECK(audio_device_busy->message == "Opening audio device...");
    CHECK(audio_device_busy->presentation == BusyPresentation::Blocking);
    CHECK(view.busy_overlay_paint_callback_count == 1);
    CHECK(audio_device_change_call_count == 1);
    const EditorViewState* state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->busy.has_value());
}

} // namespace rock_hero::editor::core
