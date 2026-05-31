#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Persisted normalization metadata is forwarded to the audio backend through the loaded session.
TEST_CASE("EditorController forwards normalization to audio backend", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    FakeProjectServices project_services;
    common::core::Song song = makeSong(std::filesystem::path{"song.wav"});
    song.arrangements.front().audio_asset.normalization = makeCurrentNormalization();
    project_services.next_song = std::move(song);
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    REQUIRE(audio.last_active_audio_asset.has_value());
    REQUIRE(audio.last_active_audio_asset->normalization.has_value());
    CHECK(audio.last_active_audio_asset->normalization->gain_db == -4.0);
}

// A failed project-audio activation leaves the session unchanged and surfaces an error.
TEST_CASE("EditorController failed activation preserves session", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0)));
    audio.next_set_active_arrangement_result = false;
    project_services.next_song = makeSong(std::filesystem::path{"new.wav"});
    FakeEditorView view;
    controller.attachView(view);

    controller.onOpenRequested(std::filesystem::path{"new.rhp"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        common::core::AudioAsset{std::filesystem::path{"old.wav"}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == true);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: new.rhp: Configured active-arrangement failure");
}

// A successful open stores the selected audio without replaying a prior error.
TEST_CASE("EditorController successful open stores audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"first.wav"});
    audio.next_set_active_arrangement_result = false;
    controller.onOpenRequested(std::filesystem::path{"first.rhp"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: first.rhp: Configured active-arrangement failure");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{std::filesystem::path{"second.wav"}};
    project_services.next_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onOpenRequested(std::filesystem::path{"second.rhp"});

    const common::core::Session& session = controller.session();
    CHECK(audio.set_active_arrangement_call_count == 2);
    CHECK(audio.last_active_audio_asset == std::optional{replacement});
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"second.rhp"}});
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == replacement);
    CHECK(session.currentArrangement()->audio_duration == common::core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.save_enabled == true);
        CHECK(state.save_as_enabled == true);
        CHECK(state.publish_enabled == true);
        CHECK(state.suggested_publish_file == std::filesystem::path{"second.rock"});
        CHECK(state.close_enabled == true);
        CHECK(state.project_loaded == true);
        CHECK(state.save_requires_destination == false);
        CHECK_FALSE(state.unsaved_changes_prompt.has_value());
    }
    // Each open now produces two pushes: one when busy state begins, one when the task
    // completion commits or fails. The inline default task runner runs both synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 2);
    CHECK(view.shown_errors.size() == 1);
}

// Close stops playback, clears backend audio, and returns the view to an empty project state.
TEST_CASE("EditorController close clears loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onCloseRequested();

    CHECK(transport.stop_call_count == 1);
    CHECK(audio.clear_active_arrangement_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.save_enabled == false);
        CHECK(state.save_as_enabled == false);
        CHECK(state.publish_enabled == false);
        CHECK(state.suggested_publish_file.empty());
        CHECK(state.close_enabled == false);
        CHECK(state.project_loaded == false);
        CHECK(state.transport.play_pause_enabled == false);
        CHECK(state.visible_timeline == common::core::TimeRange{});
        CHECK_FALSE(state.arrangement.hasAudio());
    }
}

// Exiting persists the editor project path before requesting host shutdown.
TEST_CASE("EditorController persists project file on exit", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"persist_loaded_exit"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    std::optional<std::filesystem::path> setting_seen_at_exit{};
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        [&exit_call_count, &setting_seen_at_exit, &settings] {
            setting_seen_at_exit = settings.lastOpenProject();
            ++exit_call_count;
        },
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(files.projectFile());

    controller.onExitRequested();

    CHECK(exit_call_count == 1);
    CHECK(setting_seen_at_exit == std::optional{files.projectFile()});
    CHECK(settings.lastOpenProject() == std::optional{files.projectFile()});
    CHECK_FALSE(controller.currentProjectFile().has_value());
}

// Save writes the currently loaded session song through the injected persistence seam.
TEST_CASE("EditorController save writes current session song", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"song.wav"}};
    project_services.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    transport.current_position = common::core::TimePosition{1.25};

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.last_save_audio_path == std::optional{audio_asset.path});
    CHECK(
        project_services.last_save_editor_state ==
        std::optional{ProjectEditorState{
            .cursor_position = common::core::TimePosition{1.25},
            .selected_arrangement = std::string{g_lead_arrangement_id},
        }});
    CHECK(view.shown_errors.empty());
}

// Save failures are surfaced without clearing the loaded session.
TEST_CASE("EditorController save failure surfaces an error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});
    project_services.next_save_error = std::string{"disk full"};

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not save: disk full");
}

// Save As failures clear busy before reporting the error and keep the loaded project.
TEST_CASE("EditorController save as failure clears busy first", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
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
    project_services.next_save_as_error = std::string{"disk full"};

    controller.onSaveAsRequested(std::filesystem::path{"renamed.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    CHECK(error_state->project_loaded == true);
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"song.rhp"}});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not save as: disk full");
}

// Publish writes a native song package copy without changing save-destination state.
TEST_CASE("EditorController publish writes package copy", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
            .publish_function = project_services.publishFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"song.wav"}};
    project_services.next_song = makeSong(audio_asset.path);
    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    CHECK(project_services.publish_call_count == 1);
    CHECK(project_services.save_as_call_count == 0);
    CHECK(project_services.last_publish_file == std::optional{std::filesystem::path{"song.rock"}});
    CHECK(project_services.last_publish_audio_path == std::optional{audio_asset.path});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.save_requires_destination == false);
    }
    CHECK(view.shown_errors.empty());
}

// Publish failures surface an error without closing or retargeting the current project.
TEST_CASE("EditorController publish failure surfaces an error", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
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
    project_services.next_publish_error = std::string{"disk full"};

    controller.onPublishRequested(std::filesystem::path{"song.rock"});

    CHECK(project_services.publish_call_count == 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.publish_enabled == true);
        CHECK(state.close_enabled == true);
    }
    REQUIRE(view.states_seen_at_errors.size() == 1);
    REQUIRE(view.states_seen_at_errors.back().has_value());
    const EditorViewState* error_state = stateOrNull(view.states_seen_at_errors.back());
    REQUIRE(error_state != nullptr);
    CHECK_FALSE(error_state->busy.has_value());
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not publish: disk full");
}

// A failed import leaves the current session unchanged and surfaces an error.
TEST_CASE("EditorController failed import preserves session", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"old.wav"},
        loadedTimelineRange(6.0)));
    project_services.open_call_count = 0;
    project_services.last_open_file.reset();
    FakeEditorView view;
    controller.attachView(view);

    controller.onImportRequested(std::filesystem::path{"broken.psarc"});

    const common::core::Session& session = controller.session();
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(
        session.currentArrangement()->audio_asset ==
        common::core::AudioAsset{std::filesystem::path{"old.wav"}});
    CHECK(session.timeline() == loadedTimelineRange(6.0));
    CHECK(project_services.import_call_count == 1);
    CHECK(project_services.open_call_count == 0);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == true);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(view.shown_errors.back() == "Could not import: Import failed");
}

// A successful import stores the imported audio without replaying a prior error.
TEST_CASE("EditorController successful import stores audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"first.ogg"});
    audio.next_set_active_arrangement_result = false;
    controller.onImportRequested(std::filesystem::path{"first.psarc"});
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load imported audio from: first.psarc: Configured active-arrangement failure");
    const int pushes_before_success = view.set_state_call_count;

    audio.next_set_active_arrangement_result = true;
    const common::core::AudioAsset replacement{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(replacement.path, loadedTimelineRange(4.0));
    controller.onImportRequested(std::filesystem::path{"second.psarc"});

    const common::core::Session& session = controller.session();
    CHECK(project_services.import_call_count == 2);
    CHECK(project_services.open_call_count == 0);
    CHECK(audio.set_active_arrangement_call_count == 2);
    CHECK(audio.last_active_audio_asset == std::optional{replacement});
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == replacement);
    CHECK(session.currentArrangement()->audio_duration == common::core::TimeDuration{4.0});
    CHECK(session.timeline() == loadedTimelineRange(4.0));
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.arrangement.audio_asset == std::optional{replacement});
        CHECK(state.save_enabled == true);
        CHECK(state.save_as_enabled == true);
        CHECK(state.publish_enabled == true);
        CHECK(state.close_enabled == true);
        CHECK(state.project_loaded == true);
        CHECK(state.save_requires_destination == true);
    }
    // Each import produces two pushes: one when busy state begins, one when the task completion
    // commits or fails. The inline default task runner runs both synchronously.
    CHECK(view.set_state_call_count == pushes_before_success + 2);
    CHECK(view.shown_errors.size() == 1);
}

// Imported content requires Save As before direct Save can write to a destination.
TEST_CASE("EditorController import requires Save As destination", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});
    transport.current_position = common::core::TimePosition{2.5};
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& imported_state = view.last_state.value();
        CHECK(imported_state.save_requires_destination == true);
        CHECK(imported_state.publish_enabled == true);
        CHECK(imported_state.suggested_publish_file.empty());
        CHECK(imported_state.close_enabled == true);
        CHECK(imported_state.project_loaded == true);
    }

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 0);

    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    CHECK(project_services.last_save_as_file == std::optional{std::filesystem::path{"saved.rhp"}});
    CHECK(project_services.last_save_as_audio_path == std::optional{audio_asset.path});
    CHECK(controller.currentProjectFile() == std::optional{std::filesystem::path{"saved.rhp"}});
    CHECK(
        project_services.last_save_as_editor_state ==
        std::optional{ProjectEditorState{
            .cursor_position = common::core::TimePosition{2.5},
            .selected_arrangement = std::string{g_lead_arrangement_id},
        }});
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& saved_state = view.last_state.value();
        CHECK(saved_state.save_requires_destination == false);
        CHECK(saved_state.suggested_publish_file == std::filesystem::path{"saved.rock"});
        CHECK_FALSE(saved_state.unsaved_changes_prompt.has_value());
    }

    controller.onSaveRequested();

    CHECK(project_services.save_call_count == 1);
}

// Unsaved imported content prompts before close and Cancel leaves the project loaded.
TEST_CASE("EditorController prompts before closing unsaved import", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    controller.onCloseRequested();

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});
    }
    CHECK(audio.clear_active_arrangement_call_count == 0);

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Cancel);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& cancel_state = view.last_state.value();
        CHECK_FALSE(cancel_state.unsaved_changes_prompt.has_value());
        CHECK(cancel_state.publish_enabled == true);
        CHECK(cancel_state.close_enabled == true);
        CHECK(cancel_state.project_loaded == true);
        CHECK(cancel_state.save_requires_destination == true);
    }
    CHECK(audio.clear_active_arrangement_call_count == 0);
}

// Discarding a dirty saved project before import still makes the imported project displace it.
TEST_CASE(
    "EditorController discard import reopens dirty displaced project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.plugins.clear();
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const std::filesystem::path existing_project{"existing.rhp"};
    const common::core::AudioAsset original_asset{std::filesystem::path{"original.wav"}};
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    project_services.next_song = makeSong(original_asset.path);
    controller.onOpenRequested(existing_project);
    REQUIRE(controller.currentProjectFile() == std::optional{existing_project});
    controller.onInputCalibrationRequested();
    const auto calibrated = controller.onInputCalibrationManuallySet(0.0);
    REQUIRE(calibrated.has_value());
    controller.onInputCalibrationDismissed();

    addKnownPlugin(controller);

    const common::core::AudioAsset imported_asset{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(imported_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    const EditorViewState* import_prompt_state = stateOrNull(view.last_state);
    REQUIRE(import_prompt_state != nullptr);
    CHECK(
        import_prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::ImportSong}});

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(project_services.import_call_count == 1);
    CHECK_FALSE(controller.currentProjectFile().has_value());
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->audio_asset == imported_asset);

    project_services.next_song = makeSong(original_asset.path);
    controller.onCloseRequested();

    const EditorViewState* close_prompt_state = stateOrNull(view.last_state);
    REQUIRE(close_prompt_state != nullptr);
    CHECK(
        close_prompt_state->unsaved_changes_prompt ==
        std::optional{UnsavedChangesPrompt{EditorActionId::CloseProject}});

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(project_services.open_call_count == 2);
    CHECK(project_services.last_open_file == std::optional{existing_project});
    CHECK(controller.currentProjectFile() == std::optional{existing_project});
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->audio_asset == original_asset);
}

// Choosing Save for an unsaved import asks for a destination, saves, and then closes.
TEST_CASE("EditorController saves prompted import before close", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset audio_asset{std::filesystem::path{"imported.ogg"}};
    project_services.next_import_song = makeSong(audio_asset.path);
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    controller.onCloseRequested();
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Save);

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.save_as_prompt ==
            std::optional{SaveAsPrompt{EditorActionId::CloseProject}});
    }

    controller.onSaveAsRequested(std::filesystem::path{"saved.rhp"});

    CHECK(project_services.save_as_call_count == 1);
    CHECK(project_services.last_save_as_audio_path == std::optional{audio_asset.path});
    CHECK(audio.clear_active_arrangement_call_count == 1);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& close_state = view.last_state.value();
        CHECK(close_state.publish_enabled == false);
        CHECK(close_state.close_enabled == false);
        CHECK(close_state.project_loaded == false);
        CHECK_FALSE(close_state.arrangement.hasAudio());
    }
}

// Discarding unsaved import changes lets the pending exit request reach the host callback.
TEST_CASE("EditorController prompts before exit with unsaved import", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"discard_unsaved_import_exit"};
    EditorSettings settings{files.settingsFile()};
    REQUIRE(settings.setLastOpenProject(files.projectFile()).has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    int exit_call_count = 0;
    EditorController controller{
        audioPorts(transport, audio),
        controllerServices(settings),
        [&exit_call_count] { ++exit_call_count; },
        EditorController::ProjectOperations{
            .import_function = project_services.importFunction(),
            .save_function = project_services.saveFunction(),
            .save_as_function = project_services.saveAsFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    project_services.next_import_song = makeSong(std::filesystem::path{"imported.ogg"});
    controller.onImportRequested(std::filesystem::path{"song.psarc"});

    controller.onExitRequested();

    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& prompt_state = view.last_state.value();
        CHECK(
            prompt_state.unsaved_changes_prompt ==
            std::optional{UnsavedChangesPrompt{EditorActionId::ExitApplication}});
    }
    CHECK(exit_call_count == 0);

    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    CHECK(audio.clear_active_arrangement_call_count >= 1);
    CHECK(exit_call_count == 1);
    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// Project packages do not carry editor selection state, so the controller opens index zero.
TEST_CASE("EditorController defaults open to first arrangement", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };

    const common::core::AudioAsset lead_asset{std::filesystem::path{"lead.wav"}};
    const common::core::AudioAsset bass_asset{std::filesystem::path{"bass.wav"}};
    project_services.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(audio.last_active_audio_asset == std::optional{lead_asset});
    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK(controller.session().currentArrangement()->part == common::core::Part::Lead);
    CHECK(controller.session().currentArrangement()->audio_asset == lead_asset);
}

// Opening a project validates every arrangement before the selected arrangement is loaded.
TEST_CASE("EditorController rejects invalid project arrangement audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);

    const common::core::AudioAsset lead_asset{std::filesystem::path{"lead.wav"}};
    const common::core::AudioAsset bass_asset{std::filesystem::path{"bass.wav"}};
    audio.failed_prepare_audio_path = bass_asset.path;
    project_services.next_song = makeTwoArrangementSong(lead_asset.path, bass_asset.path);

    controller.onOpenRequested(std::filesystem::path{"song.rhp"});

    CHECK(audio.prepared_audio_asset_count == 2);
    CHECK(audio.set_active_arrangement_call_count == 0);
    CHECK(controller.session().currentArrangement() == nullptr);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        const EditorViewState& state = view.last_state.value();
        CHECK(state.project_loaded == false);
    }
    REQUIRE(view.shown_errors.size() == 1);
    CHECK(
        view.shown_errors.back() ==
        "Could not load audio from: song.rhp: Configured song preparation failure for: bass.wav");
}

} // namespace rock_hero::editor::core
